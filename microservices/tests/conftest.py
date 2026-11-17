import os
import sys

import pytest
import requests

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "runner"))

GATEWAY = os.environ.get("FINCEPT_SERVICES_URL", "http://localhost:8080").rstrip("/")


@pytest.fixture(scope="session")
def gw():
    try:
        requests.get(GATEWAY + "/healthz", timeout=5).raise_for_status()
    except requests.RequestException as e:
        pytest.skip(f"gateway not running at {GATEWAY}: {e}")
    return GATEWAY


def run(gw, script, args=None, **body):
    body["args"] = args or []
    return requests.post(f"{gw}/v1/scripts/{script}", json=body, timeout=180)
