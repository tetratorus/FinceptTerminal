# Fincept Terminal services

Optional deployment mode that moves the desktop app's Python back end out of
the app and into separate network services. The Qt app stays the UI; when
`FINCEPT_SERVICES_URL` is set, `PythonRunner` sends script calls to the
gateway over HTTP instead of starting local Python processes.

```
FinceptTerminal (Qt) ──HTTP──▶ gateway (nginx :8080)
                                 ├─▶ market-data   yfinance, screener, peers, CBOE  (warm yfinance worker pool)
                                 ├─▶ analytics     scripts/Analytics/**, notebook code cells
                                 └─▶ connectors    every other top-level data script (ECB, FRED, IMF, …)
                               local Python (in the app): agents, AI quant lab, local_only scripts
```

## Run

```bash
docker compose -f microservices/docker-compose.yml up -d --build
FINCEPT_SERVICES_URL=http://localhost:8080 fincept-qt/build/linux-release/FinceptTerminal
```

In services mode the app skips the first-run local Python install. Scripts no
service owns get `404` + `X-Fincept-Unrouted: 1` from the gateway, and the app
runs them in local Python from then on (this needs the normal local Python
setup).

## Routing

[`routes.json`](routes.json) is the single source of truth: regexes over the
script path relative to `fincept-qt/scripts`, first matching service wins,
`local_only` always stays in the app. After editing it run
`python3 microservices/gateway/gen_nginx.py` to regenerate the gateway config.

## API (every service)

| Method | Path | Body | Reply |
|---|---|---|---|
| `POST` | `/v1/scripts/<path>` | `{"args": [...], "stdin": "", "env": {}, "timeout_ms": N, "spill": bool}` | `{"service", "exit_code", "stdout", "stderr", "timed_out", "duration_ms"}` |
| `POST` | `/v1/code` | `{"code": "...", "timeout_ms": N}` | same (analytics only) |
| `POST` | `/v1/yfinance/<action>` | daemon payload, e.g. `{"symbols": ["AAPL"]}` | `{ok, result \| error}` (market-data; used by the desktop's `PythonWorker` for dashboard and Markets refreshes) |
| `GET` | `/services/<name>/healthz` | | service status, owned patterns, counters |

A stopped or crashed service returns `503 {"error": "service unavailable", "service": "<name>"}` from the gateway; the other services keep answering.

`env` carries the API keys the desktop keeps in SecureStorage. The app only
sends them to an `https://` or loopback gateway, and services drop names that
would change Python or the loader (`PYTHON*`, `LD_*`, `PATH`, proxies, CA
bundles, …).

## Tests

```bash
microservices/e2e.sh   # stack up → contract tests → app client → app client with analytics stopped
```

- `tests/` — pytest: routing rules and the HTTP contract against the running stack.
- `FinceptTerminal --selftest-services` — the app's real `PythonRunner` against
  `FINCEPT_SERVICES_URL`; `FINCEPT_SELFTEST_DOWN_SERVICE=<name>` checks that a
  stopped service fails cleanly while the rest still work.
