"""Script ownership rules shared by every service and the gateway generator."""
import json
import os
import re

ROUTES_FILE = os.environ.get(
    "FINCEPT_ROUTES", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "routes.json"))


def load_routes(path=ROUTES_FILE):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def owner_of(script, routes):
    """Name of the service that owns `script`, or None if it stays in the desktop app."""
    if ".." in script.split("/") or script.startswith("/"):
        return None
    for pat in routes.get("local_only", []):
        if re.fullmatch(pat, script):
            return None
    for svc in routes["services"]:
        for pat in svc["scripts"]:
            if re.fullmatch(pat, script):
                return svc["name"]
    return None
