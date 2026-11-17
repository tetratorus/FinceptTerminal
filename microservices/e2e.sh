#!/usr/bin/env bash
# End-to-end check of the services architecture:
#   1. build + start the stack   2. HTTP contract tests   3. the desktop app's
#   real PythonRunner against the stack   4. same with one service stopped.
# Usage: microservices/e2e.sh [path/to/FinceptTerminal]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
app="${1:-$here/../fincept-qt/build/linux-release/FinceptTerminal}"
export FINCEPT_SERVICES_URL="${FINCEPT_SERVICES_URL:-http://localhost:${FINCEPT_GATEWAY_PORT:-8080}}"
compose=(docker compose -f "$here/docker-compose.yml")

wait_healthy() {
  for svc in market-data analytics connectors; do
    for _ in $(seq 90); do
      curl -fsS "$FINCEPT_SERVICES_URL/services/$svc/healthz" >/dev/null 2>&1 && break
      sleep 1
    done
    curl -fsS "$FINCEPT_SERVICES_URL/services/$svc/healthz" >/dev/null
  done
}

echo "== stack"; "${compose[@]}" up -d --build; wait_healthy
echo "== contract tests"
python3 -m pytest -q "$here/tests"
echo "== desktop client against the stack"
QT_QPA_PLATFORM=offscreen "$app" --selftest-services
echo "== desktop client with analytics stopped"
"${compose[@]}" stop analytics
trap '"${compose[@]}" start analytics >/dev/null' EXIT
QT_QPA_PLATFORM=offscreen FINCEPT_SELFTEST_DOWN_SERVICE=analytics "$app" --selftest-services
echo "== all passed"
