#!/usr/bin/env python3
"""Start the local Weaver API on demand and relay one JSON conversation."""
from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path
from urllib.error import URLError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[2]
BASE_URL = "http://127.0.0.1:8765"


def get_health() -> dict | None:
    try:
        with urlopen(BASE_URL + "/health", timeout=2) as response:
            return json.loads(response.read())
    except (URLError, TimeoutError, json.JSONDecodeError):
        return None


def ensure_server() -> None:
    health = get_health()
    if health and health.get("model_loaded"):
        return
    cache = ROOT / ".cache/weaverprocedura"
    cache.mkdir(parents=True, exist_ok=True)
    log_path = cache / "agent-api.log"
    log = log_path.open("ab", buffering=0)
    process = subprocess.Popen(
        [sys.executable, str(ROOT / "tools/weaveragent/serve.py"), "--quiet"],
        cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        health = get_health()
        if health and health.get("model_loaded"):
            return
        if process.poll() is not None:
            tail = log_path.read_text(encoding="utf-8", errors="replace")[-1800:]
            raise RuntimeError("Local Weaver API failed to start: " + tail)
        time.sleep(0.5)
    raise RuntimeError("Local Weaver model did not finish loading within 180 seconds.")


def main() -> int:
    try:
        request = json.load(sys.stdin)
        ensure_server()
        body = json.dumps(request, ensure_ascii=False).encode("utf-8")
        http_request = Request(BASE_URL + "/v1/agent/turn", data=body,
                              headers={"Content-Type": "application/json"}, method="POST")
        with urlopen(http_request, timeout=300) as response:
            result = json.loads(response.read())
        sys.stdout.write(json.dumps(result, ensure_ascii=False) + "\n")
        return 0
    except Exception as exc:
        sys.stdout.write(json.dumps({"error": f"{type(exc).__name__}: {exc}"}, ensure_ascii=False) + "\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
