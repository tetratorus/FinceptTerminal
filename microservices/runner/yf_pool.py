"""Warm pool of `yfinance_data.py --daemon` workers.

Each worker imports yfinance/pandas once and then answers framed requests
(4-byte big-endian length + JSON), the same protocol the desktop app's
PythonWorker speaks. This removes the ~3 s import cost from every quote call.
"""
import asyncio
import json
import os
import sys


def cli_to_daemon(args):
    """Translate yfinance_data.py CLI args to (action, payload); None if the daemon can't serve it.

    Mirrors route_yfinance_to_daemon() in fincept-qt/src/python/PythonRunner.cpp.
    """
    if not args:
        return None
    action, rest = args[0], args[1:]

    def arg(i, default=None):
        return rest[i] if i < len(rest) else default

    if action in ("quote", "info", "financials", "company_profile", "financial_ratios"):
        return (action, {"symbol": rest[0]}) if rest else None
    if action == "news":
        return (action, {"symbol": rest[0], "count": int(arg(1, 20))}) if rest else None
    if action == "search":
        return (action, {"query": rest[0], "limit": int(arg(1, 50))}) if rest else None
    if action == "historical_period":
        return (action, {"symbol": rest[0], "period": arg(1, "6mo"), "interval": arg(2, "1d")}) if rest else None
    if action == "historical":
        if len(rest) < 3:
            return None
        return action, {"symbol": rest[0], "start_date": rest[1], "end_date": rest[2], "interval": arg(3, "1d")}
    if action in ("multiple_ratios", "multiple_profiles"):
        return (action, {"symbols": [s.strip() for s in rest[0].split(",") if s.strip()]}) if rest else None
    if action in ("batch_quotes", "batch_sparklines"):
        return (action, {"symbols": list(rest)}) if rest else None
    if action == "batch_all":
        if not rest or rest[0].startswith("@"):
            return None
        try:
            payload = json.loads(rest[0])
        except ValueError:
            return None
        return (action, payload) if isinstance(payload, dict) else None
    return None


class _Worker:
    def __init__(self, scripts_dir, env):
        self.scripts_dir = scripts_dir
        self.env = env
        self.proc = None
        self.next_id = 0

    async def start(self):
        self.proc = await asyncio.create_subprocess_exec(
            sys.executable, os.path.join(self.scripts_dir, "yfinance_data.py"), "--daemon",
            cwd=self.scripts_dir, env=self.env, stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL, limit=64 * 1024 * 1024)
        ready = await self._read_frame()
        if not ready or not ready.get("ready"):
            raise RuntimeError("yfinance daemon did not report ready")

    async def _read_frame(self):
        header = await self.proc.stdout.readexactly(4)
        n = int.from_bytes(header, "big")
        return json.loads((await self.proc.stdout.readexactly(n)).decode("utf-8"))

    async def call(self, action, payload):
        self.next_id += 1
        req = json.dumps({"id": self.next_id, "action": action, "payload": payload}).encode("utf-8")
        self.proc.stdin.write(len(req).to_bytes(4, "big") + req)
        await self.proc.stdin.drain()
        return await self._read_frame()

    def alive(self):
        return self.proc is not None and self.proc.returncode is None

    def kill(self):
        if self.alive():
            self.proc.kill()


class YfPool:
    def __init__(self, scripts_dir, env, size):
        self.scripts_dir = scripts_dir
        self.env = env
        self.size = size
        self.idle = asyncio.Queue()

    async def start(self):
        workers = [_Worker(self.scripts_dir, self.env) for _ in range(self.size)]
        await asyncio.gather(*(w.start() for w in workers))
        for w in workers:
            self.idle.put_nowait(w)

    async def call(self, action, payload, timeout_s):
        """Returns (ok, result_or_error)."""
        w = await self.idle.get()
        try:
            if not w.alive():
                w = _Worker(self.scripts_dir, self.env)
                await w.start()
            resp = await asyncio.wait_for(w.call(action, payload), timeout_s)
            return bool(resp.get("ok")), resp.get("result") if resp.get("ok") else resp.get("error", "daemon error")
        except (asyncio.TimeoutError, asyncio.IncompleteReadError, ConnectionError, ValueError, RuntimeError) as e:
            # The worker's stream is now out of sync or dead: replace it.
            w.kill()
            w = _Worker(self.scripts_dir, self.env)
            try:
                await w.start()
            except Exception:  # keep the pool size; the next call retries the start
                pass
            msg = "timed out" if isinstance(e, asyncio.TimeoutError) else f"worker failed: {e!r}"
            return False, f"yfinance daemon {msg}"
        finally:
            self.idle.put_nowait(w)

    def stop(self):
        while not self.idle.empty():
            self.idle.get_nowait().kill()
