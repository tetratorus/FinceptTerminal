"""Fincept script service.

One container image per service; every one runs this app. A service only
answers for the scripts that routes.json assigns to it, so the gateway is the
only place that knows the full map.

POST /v1/scripts/<path>   run one script: {"args": [...], "stdin": "...", "env": {...}, "timeout_ms": N}
POST /v1/code             run a notebook code cell (services with "code": true)
GET  /healthz             liveness + what this service owns
"""
import asyncio
import json
import os
import re
import sys
import tempfile
import time
import uuid

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from routes import load_routes, owner_of
from yf_pool import YfPool, cli_to_daemon

SERVICE = os.environ["FINCEPT_SERVICE"]
SCRIPTS_DIR = os.environ.get("FINCEPT_SCRIPTS_DIR", "/app/scripts")
DATA_DIR = os.environ.get("FINCEPT_DATA_DIR", "/data")
MAX_CONCURRENT = int(os.environ.get("FINCEPT_MAX_CONCURRENT", "8"))
DEFAULT_TIMEOUT_MS = 300_000
MAX_TIMEOUT_MS = 3_600_000
YF_WORKERS = int(os.environ.get("FINCEPT_YF_WORKERS", "0"))

ROUTES = load_routes()
ME = next(s for s in ROUTES["services"] if s["name"] == SERVICE)

app = FastAPI(title=f"fincept-{SERVICE}")
slots = asyncio.Semaphore(MAX_CONCURRENT)
yf_pool = None
stats = {"requests": 0, "failures": 0, "pool_hits": 0}


def base_env():
    env = {k: v for k, v in os.environ.items() if not k.startswith("FINCEPT_")}
    env.update({
        "PYTHONIOENCODING": "utf-8",
        "PYTHONDONTWRITEBYTECODE": "1",
        "PYTHONUNBUFFERED": "1",
        "PYTHONPATH": SCRIPTS_DIR,
        "FINCEPT_DATA_DIR": DATA_DIR,
        "FINAGENT_DATA_DIR": os.path.join(DATA_DIR, "finagent"),
    })
    return env


CLIENT_ENV_NAME = re.compile(r"[A-Z][A-Z0-9_]{1,63}")
# Names a client must never set: they would change how Python or the loader behaves.
CLIENT_ENV_DENY = re.compile(r"(PYTHON|LD_|FINCEPT_|FINAGENT_|SSL_|REQUESTS_CA|CURL_CA|HTTPS?_PROXY|NO_PROXY).*|PATH|HOME|USER|SHELL|TMPDIR")


def request_env(extra):
    env = base_env()
    # Credentials the desktop keeps in SecureStorage (API keys) arrive here, one name per key.
    for k, v in (extra or {}).items():
        if (isinstance(k, str) and isinstance(v, str) and CLIENT_ENV_NAME.fullmatch(k)
                and not CLIENT_ENV_DENY.fullmatch(k)):
            env[k] = v
    return env


def reply(body, status=200):
    body.setdefault("service", SERVICE)
    return JSONResponse(body, status_code=status, headers={"X-Fincept-Service": SERVICE})


def module_form(script):
    """`python -m pkg.mod` when every parent dir is a package (relative imports), else the file path."""
    parts = script.split("/")
    if len(parts) > 1:
        d = SCRIPTS_DIR
        for p in parts[:-1]:
            d = os.path.join(d, p)
            if not os.path.exists(os.path.join(d, "__init__.py")):
                return [os.path.join(SCRIPTS_DIR, script)]
        return ["-m", script[:-3].replace("/", ".")]
    return [os.path.join(SCRIPTS_DIR, script)]


async def run_process(argv, stdin, env, timeout_ms):
    start = time.monotonic()
    async with slots:
        proc = await asyncio.create_subprocess_exec(
            sys.executable, *argv, cwd=SCRIPTS_DIR, env=env,
            stdin=asyncio.subprocess.PIPE if stdin else asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        timed_out = False
        try:
            out, err = await asyncio.wait_for(
                proc.communicate(stdin.encode("utf-8") if stdin else None), timeout_ms / 1000 if timeout_ms > 0 else None)
        except asyncio.TimeoutError:
            timed_out = True
            proc.kill()
            out, err = await proc.communicate()
    return {
        "service": SERVICE,
        "exit_code": -1 if timed_out else proc.returncode,
        "stdout": out.decode("utf-8", "replace"),
        "stderr": err.decode("utf-8", "replace"),
        "timed_out": timed_out,
        "duration_ms": int((time.monotonic() - start) * 1000),
    }


@app.on_event("startup")
async def startup():
    global yf_pool
    if YF_WORKERS > 0 and owner_of("yfinance_data.py", ROUTES) == SERVICE:
        yf_pool = YfPool(SCRIPTS_DIR, base_env(), YF_WORKERS)
        await yf_pool.start()


@app.on_event("shutdown")
async def shutdown():
    if yf_pool:
        yf_pool.stop()


@app.get("/healthz")
async def healthz():
    return reply({"service": SERVICE, "ok": True, "description": ME.get("description", ""),
                  "scripts": ME["scripts"], "code": bool(ME.get("code")),
                  "yf_workers": YF_WORKERS if yf_pool else 0, "stats": stats})


@app.post("/v1/scripts/{script:path}")
async def run_script(script: str, request: Request):
    stats["requests"] += 1
    if owner_of(script, ROUTES) != SERVICE:
        return reply({"error": f"{SERVICE} does not own {script}"}, 404)
    if not os.path.isfile(os.path.join(SCRIPTS_DIR, script)):
        return reply({"error": f"Script not found: {script}"}, 404)
    body = await request.json()
    args = [str(a) for a in body.get("args", [])]
    stdin = body.get("stdin") or ""
    timeout_ms = int(body.get("timeout_ms") or DEFAULT_TIMEOUT_MS)
    timeout_ms = min(timeout_ms, MAX_TIMEOUT_MS) if timeout_ms > 0 else MAX_TIMEOUT_MS
    env = request_env(body.get("env"))

    if yf_pool and script == "yfinance_data.py" and not stdin:
        routed = cli_to_daemon(args)
        if routed:
            stats["pool_hits"] += 1
            start = time.monotonic()
            ok, result = await yf_pool.call(routed[0], routed[1], timeout_ms / 1000)
            res = {"service": SERVICE, "exit_code": 0 if ok else 1,
                   "stdout": json.dumps(result, default=str) if ok else "",
                   "stderr": "" if ok else str(result), "timed_out": False,
                   "duration_ms": int((time.monotonic() - start) * 1000), "worker": "pool"}
            if not ok:
                stats["failures"] += 1
            return reply(res)

    spilled = []
    if body.get("spill"):
        # The script reads "@<file>" arguments (see script_supports_arg_spill in PythonRunner.cpp);
        # use it for arguments too large for one argv entry on Linux.
        for i, a in enumerate(args):
            if len(a) > 100_000:
                path = os.path.join(tempfile.gettempdir(), f"fincept_arg_{uuid.uuid4().hex}.json")
                with open(os.open(path, os.O_WRONLY | os.O_CREAT, 0o600), "w", encoding="utf-8") as f:
                    f.write(a)
                args[i] = "@" + path
                spilled.append(path)
    try:
        res = await run_process(module_form(script) + args, stdin, env, timeout_ms)
    finally:
        for p in spilled:
            if os.path.exists(p):
                os.remove(p)
    if res["exit_code"] != 0:
        stats["failures"] += 1
    return reply(res)


@app.post("/v1/code")
async def run_code(request: Request):
    stats["requests"] += 1
    if not ME.get("code"):
        return reply({"error": f"{SERVICE} does not run code cells"}, 404)
    body = await request.json()
    fd, path = tempfile.mkstemp(prefix="fincept_cell_", suffix=".py")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(body.get("code", ""))
    try:
        res = await run_process([path], "", request_env(body.get("env")), int(body.get("timeout_ms") or MAX_TIMEOUT_MS))
    finally:
        os.remove(path)
    return reply(res)
