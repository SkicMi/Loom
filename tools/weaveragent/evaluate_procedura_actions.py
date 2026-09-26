#!/usr/bin/env python3
"""Measure the local agent's supported and abstaining Procedura tool calls."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from model_runtime import DEFAULT_ADAPTER

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CASES = Path(__file__).parent / "data/benchmarks/procedura-action-cases-v1.jsonl"


def file_sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def expected_subset(actual: object, expected: object) -> bool:
    if isinstance(expected, dict):
        return isinstance(actual, dict) and all(
            key in actual and expected_subset(actual[key], value) for key, value in expected.items())
    if isinstance(expected, list):
        return isinstance(actual, list) and len(actual) == len(expected) and all(
            expected_subset(a, e) for a, e in zip(actual, expected))
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        return isinstance(actual, (int, float)) and not isinstance(actual, bool) and math.isclose(
            float(actual), float(expected), rel_tol=1e-6, abs_tol=1e-6)
    return actual == expected


def request_turn(base_url: str, prompt: str, timeout: float) -> dict:
    scene_context = {"schema": "loom.scene-context", "version": 1, "frame": 1,
                     "selected_path": None, "entities": [], "truncated": False}
    body = json.dumps({"message": prompt, "scene_context": scene_context}, ensure_ascii=False).encode("utf-8")
    request = Request(base_url.rstrip("/") + "/v1/agent/turn", data=body,
                      headers={"Content-Type": "application/json"}, method="POST")
    with urlopen(request, timeout=timeout) as response:
        return json.loads(response.read())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8765")
    parser.add_argument("--cases", type=Path, default=DEFAULT_CASES)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--output", type=Path,
                        default=ROOT / ".cache/weaverprocedura/benchmarks/procedura-actions-v1.json")
    args = parser.parse_args()

    try:
        with urlopen(args.base_url.rstrip("/") + "/health", timeout=5) as response:
            health = json.loads(response.read())
    except (HTTPError, URLError, TimeoutError, json.JSONDecodeError) as exc:
        raise SystemExit(f"Loom Agent API is unavailable at {args.base_url}: {exc}")
    if not health.get("model_loaded"):
        raise SystemExit("Loom Agent API health check says the model is not loaded")

    cases = [json.loads(line) for line in args.cases.read_text(encoding="utf-8").splitlines() if line.strip()]
    if args.limit:
        cases = cases[:args.limit]
    started_at = datetime.now(timezone.utc)
    rows = []
    durations = []
    for case in cases:
        started = time.perf_counter()
        try:
            payload = request_turn(args.base_url, case["prompt"], args.timeout)
            result = payload.get("result", {})
            diagnostics = payload.get("diagnostics", {})
            actions = result.get("actions")
            protocol_valid = isinstance(actions, list) and isinstance(result.get("reply"), str)
            expected = case["expected"]
            if expected.get("abstain"):
                matched = protocol_valid and not actions
                argument_match = matched
            else:
                matched = protocol_valid and len(actions) == 1 and actions[0].get("tool") == expected["tool"]
                argument_match = bool(matched and expected_subset(
                    actions[0].get("arguments"), expected.get("arguments", {})))
            unexpected = protocol_valid and ((expected.get("abstain") and bool(actions)) or
                (not expected.get("abstain") and any(item.get("tool") != expected["tool"] for item in actions)))
            recovered = bool(diagnostics.get("actions_recovered_by_host_guard", False))
            model_match = argument_match and not recovered if not expected.get("abstain") else (
                protocol_valid and diagnostics.get("model_action_count") == 0)
            elapsed = time.perf_counter() - started
            durations.append(elapsed)
            rows.append({"id": case["id"], "language": case["language"], "prompt": case["prompt"],
                         "reply": result.get("reply"), "expected": expected,
                         "actual": actions if protocol_valid else None, "protocol_valid": protocol_valid,
                         "tool_match": matched, "argument_match": argument_match,
                         "model_action_count": diagnostics.get("model_action_count"),
                         "actions_recovered_by_host_guard": recovered,
                         "model_tool_match": model_match,
                         "unexpected_action": unexpected, "passed": argument_match and not unexpected,
                         "seconds": round(elapsed, 3)})
        except (HTTPError, URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
            elapsed = time.perf_counter() - started
            durations.append(elapsed)
            rows.append({"id": case["id"], "language": case["language"], "prompt": case["prompt"],
                         "expected": case["expected"], "error": f"{type(exc).__name__}: {exc}",
                         "protocol_valid": False, "tool_match": False, "argument_match": False,
                         "model_action_count": None, "actions_recovered_by_host_guard": False,
                         "model_tool_match": False,
                         "unexpected_action": True, "passed": False, "seconds": round(elapsed, 3)})

    manifest_path = ROOT / ".cache/weaverprocedura/project-index.manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else {}
    adapter_config_path = DEFAULT_ADAPTER / "adapter_config.json"
    adapter_config = json.loads(adapter_config_path.read_text(encoding="utf-8")) if adapter_config_path.is_file() else {}
    passed = sum(bool(row["passed"]) for row in rows)
    report = {
        "benchmark": "weaverprocedura-agent-actions",
        "version": 1,
        "started_at_utc": started_at.isoformat(),
        "completed_at_utc": datetime.now(timezone.utc).isoformat(),
        "adapter": {"path": str(DEFAULT_ADAPTER),
                    "base_model_name_or_path": adapter_config.get("base_model_name_or_path"),
                    "adapter_config_sha256": file_sha256(DEFAULT_ADAPTER / "adapter_config.json"),
                    "adapter_weights_sha256": file_sha256(DEFAULT_ADAPTER / "adapter_model.safetensors"),
                    "training_metrics_sha256": file_sha256(DEFAULT_ADAPTER / "training_metrics.json"),
                    "tool_schema_sha256": file_sha256(ROOT / "tools/weaveragent/data/tools.json"),
                    "system_prompt_sha256": file_sha256(ROOT / "tools/weaveragent/data/system_prompt.txt"),
                    "model_runtime_sha256": file_sha256(ROOT / "tools/weaveragent/model_runtime.py"),
                    "agent_protocol_sha256": file_sha256(ROOT / "tools/weaveragent/agent_protocol.py"),
                    "benchmark_runner_sha256": file_sha256(Path(__file__).resolve())},
        "retrieval_index": {"chunks": health.get("index_chunks"),
                            "source_sha256": manifest.get("source_sha256")},
        "case_file_sha256": file_sha256(args.cases),
        "cases": len(rows), "passes": passed,
        "action_accuracy": passed / len(rows) if rows else 0.0,
        "raw_model_action_accuracy": sum(bool(row["model_tool_match"]) for row in rows) / len(rows) if rows else 0.0,
        "valid_protocol": sum(bool(row["protocol_valid"]) for row in rows),
        "host_recovered_cases": sum(bool(row["actions_recovered_by_host_guard"]) for row in rows),
        "unexpected_actions": sum(bool(row["unexpected_action"]) for row in rows),
        "mean_latency_seconds": statistics.mean(durations) if durations else None,
        # Nearest-rank percentile includes the slowest sample for small suites.
        "p95_latency_seconds": sorted(durations)[math.ceil(0.95 * len(durations)) - 1] if durations else None,
        "items": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "items"}, indent=2))
    print(f"Full report: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
