"""Contract tests against the running stack (docker compose up)."""
import json
import subprocess

import pytest
import requests

from conftest import run

COMPOSE = ["docker", "compose", "-f", __file__.rsplit("/tests/", 1)[0] + "/docker-compose.yml"]


def test_gateway_health(gw):
    assert requests.get(gw + "/healthz").json() == {"ok": True, "service": "gateway"}


@pytest.mark.parametrize("svc", ["market-data", "analytics", "connectors"])
def test_service_health(gw, svc):
    r = requests.get(f"{gw}/services/{svc}/healthz")
    assert r.status_code == 200
    assert r.json()["service"] == svc and r.json()["ok"]


def test_routes_published(gw):
    names = [s["name"] for s in requests.get(gw + "/routes.json").json()["services"]]
    assert names == ["market-data", "analytics", "connectors"]


def test_quote_served_by_warm_pool(gw):
    r = run(gw, "yfinance_data.py", ["quote", "AAPL"])
    body = r.json()
    assert r.status_code == 200 and body["service"] == "market-data" and body["worker"] == "pool"
    assert json.loads(body["stdout"])["price"] > 0


def test_warm_pool_is_fast(gw):
    run(gw, "yfinance_data.py", ["quote", "MSFT"])
    body = run(gw, "yfinance_data.py", ["quote", "MSFT"]).json()
    assert body["duration_ms"] < 2500, body["duration_ms"]


def test_analytics_script(gw):
    body = run(gw, "Analytics/technical_analysis/momentum_indicators.py",
               ["AAPL", "rsi", '{"timeframe":"3mo"}']).json()
    assert body["service"] == "analytics" and body["exit_code"] == 0
    assert len(json.loads(body["stdout"])["values"]) > 20


def test_connector_script(gw):
    body = run(gw, "ecb_data.py", ["currency_rates"]).json()
    assert body["service"] == "connectors" and body["exit_code"] == 0
    assert "USD" in json.loads(body["stdout"])["data"]


def test_code_cell(gw):
    r = requests.post(gw + "/v1/code", json={"code": "print(6*7)"})
    assert r.json()["service"] == "analytics" and r.json()["stdout"].strip() == "42"


def test_code_cell_timeout(gw):
    body = requests.post(gw + "/v1/code", json={"code": "import time; time.sleep(10)", "timeout_ms": 1000}).json()
    assert body["timed_out"] and body["duration_ms"] < 5000


def test_client_env_filtered(gw):
    code = "import os; print(os.environ.get('FOO_API_KEY'), os.environ.get('PYTHONPATH'), os.environ.get('LD_PRELOAD'))"
    body = requests.post(gw + "/v1/code", json={"code": code, "env": {
        "FOO_API_KEY": "abc", "PYTHONPATH": "/evil", "LD_PRELOAD": "/evil.so", "lower": "x"}}).json()
    assert body["stdout"].split() == ["abc", "/app/scripts", "None"]


@pytest.mark.parametrize("script", ["spreadsheet.py", "agents/finagent_core/main.py"])
def test_unrouted_scripts_bounce_to_desktop(gw, script):
    r = run(gw, script)
    assert r.status_code == 404 and r.headers.get("X-Fincept-Unrouted") == "1"


def test_unknown_script_is_404_not_unrouted(gw):
    r = run(gw, "no_such_script_xyz.py")
    assert r.status_code == 404 and r.json()["service"] == "connectors"
    assert "X-Fincept-Unrouted" not in r.headers


def test_traversal_rejected(gw):
    r = requests.post(gw + "/v1/scripts/..%2F..%2Fetc%2Fpasswd", json={})
    assert r.status_code in (400, 404)
    assert "root:" not in r.text


def test_script_failure_reported_not_raised(gw):
    body = run(gw, "ecb_data.py", ["no_such_command"]).json()
    assert body["service"] == "connectors"
    assert body["exit_code"] != 0 or '"error"' in body["stdout"] or '"success": false' in body["stdout"]


def test_one_service_down_others_keep_working(gw):
    subprocess.run(COMPOSE + ["stop", "analytics"], check=True, capture_output=True)
    try:
        r = run(gw, "Analytics/technical_analysis/momentum_indicators.py", ["AAPL", "rsi", "{}"])
        assert r.status_code == 503 and r.json() == {"error": "service unavailable", "service": "analytics"}
        assert run(gw, "yfinance_data.py", ["quote", "AAPL"]).json()["exit_code"] == 0
        assert run(gw, "ecb_data.py", ["currency_rates"]).json()["exit_code"] == 0
    finally:
        subprocess.run(COMPOSE + ["start", "analytics"], check=True, capture_output=True)
    for _ in range(60):
        try:
            if requests.get(gw + "/services/analytics/healthz", timeout=2).status_code == 200:
                break
        except requests.RequestException:
            pass
        import time
        time.sleep(1)
    assert requests.post(gw + "/v1/code", json={"code": "print(1)"}).json()["stdout"].strip() == "1"
