#!/usr/bin/env python3
"""Run every frozen Loom agent action prompt against the live local API."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import re
import statistics
import time
import unicodedata
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from agent_protocol import (ACTION_SPECS, PROTOCOL, guard_turn_for_request, parse_turn,
                            references_selected_entity)
from model_runtime import DEFAULT_ADAPTER

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
DEFAULT_DATASETS = (
    HERE / "data/v1/eval.jsonl",
    HERE / "data/v1/eval_extended.jsonl",
    HERE / "data/v2/eval.jsonl",
)
PROCEDURA_CASES = HERE / "data/benchmarks/procedura-action-cases-v1.jsonl"
CHAT_CASES = HERE / "data/benchmarks/agent-chat-cases-v1.jsonl"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def normalize(text: str) -> str:
    decomposed = unicodedata.normalize("NFKD", text.casefold())
    plain = "".join(char for char in decomposed if not unicodedata.combining(char))
    return " ".join("".join(char if char.isalnum() else " " for char in plain).split())


def subset(actual: object, expected: object) -> bool:
    if isinstance(expected, dict):
        return isinstance(actual, dict) and all(
            key in actual and subset(actual[key], value) for key, value in expected.items())
    if isinstance(expected, list):
        return isinstance(actual, list) and len(actual) == len(expected) and all(
            subset(a, e) for a, e in zip(actual, expected))
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        return isinstance(actual, (int, float)) and not isinstance(actual, bool) and math.isclose(
            float(actual), float(expected), rel_tol=1e-6, abs_tol=1e-6)
    return actual == expected


def load_cases() -> tuple[list[dict], dict[str, str]]:
    cases: list[dict] = []
    hashes: dict[str, str] = {}
    for path in DEFAULT_DATASETS:
        hashes[str(path.relative_to(ROOT))] = sha256(path)
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            record = json.loads(line)
            user = next(item["content"] for item in record["messages"] if item["role"] == "user")
            expected = parse_turn(record["messages"][-1]["content"])
            cases.append({"id": f"{path.stem}:{number}", "source": str(path.relative_to(ROOT)),
                          "prompt": user, "expected_actions": expected["actions"]})

    for path in (PROCEDURA_CASES, CHAT_CASES):
        hashes[str(path.relative_to(ROOT))] = sha256(path)
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            record = json.loads(line)
            if path == PROCEDURA_CASES:
                expected = record["expected"]
                actions = ([] if expected.get("abstain") else [{
                    "tool": expected["tool"], "arguments": expected.get("arguments", {}),
                    "confirmation_required": bool(ACTION_SPECS[expected["tool"]].get("confirmation", False)),
                }])
            else:
                actions = [{**item, "confirmation_required": bool(
                    ACTION_SPECS[item["tool"]].get("confirmation", False))}
                    for item in record["expected_actions"]]
            cases.append({"id": record["id"], "source": str(path.relative_to(ROOT)),
                          "prompt": record["prompt"], "expected_actions": actions,
                          "reply_must_include": record.get("reply_must_include", []),
                          "reply_must_not_include": record.get("reply_must_not_include", [])})
    return cases, hashes


def scene_fixture(prompt: str | None = None) -> dict:
    paths = (
        "/Scene/Hero", "/Scene/Robot", "/Scene/Block", "/Scene/Props/Box_1",
        "/Environment/WallOld", "/World/Proxy_2", "/Props/Cube_1", "/Props/OldSign",
        "/World/Group_A", "/World/Car", "/Environment/Set", "/Scene/Camera",
        "/Environment", "/Props", "/Scene/Camera_Block", "/Scene/Old", "/Scene/Proxy",
        "/Floor_B", "/Ground", "/Crate_01", "/StageFloor", "/LightBlock", "/Podium", "/ShadowFloor",
    )
    entities = []
    for path in paths:
        parent = path.rsplit("/", 1)[0] or ""
        entities.append({"path": path, "name": path.rsplit("/", 1)[-1],
                         "type": "group" if path in {"/Environment", "/Props"} else "cube",
                         "parent_path": parent, "visible": True,
                         "world_position": [0, 0, 0], "local_position": [0, 0, 0],
                         "local_scale": [1, 1, 1], "local_rotation_xyzw": [0, 0, 0, 1]})
    selected_path = "/Scene/Hero"
    if prompt is None:
        return {"schema": "loom.scene-context", "version": 1, "frame": 24,
                "selected_path": selected_path, "entities": entities, "truncated": False}

    paths = {path.rstrip(".,;:!?").casefold() for path in re.findall(
        r"(?<![\w])(/[\w.-]+(?:/[\w.-]+)*)", prompt)}
    listing_request = bool(re.search(
        r"\b(list|show\s+me|entities|hierarchy|contents|children|objekt\w*|objekat\w*|objekte|objekata)\b",
        prompt, re.IGNORECASE))
    mutating_request = bool(re.search(
        r"\b(add|create|make|spawn|place|put|move|translate|rotate|scale|rename|delete|remove|erase|"
        r"hide|napravi|stvori|dodaj|pomakni|premjesti|rotiraj|skaliraj|preimenuj|obriši|obrisi|ukloni)\b",
        prompt, re.IGNORECASE))
    listing = listing_request and not mutating_request
    wanted = [item for item in entities if item["path"].casefold() in paths]
    if references_selected_entity(prompt):
        selected = next(item for item in entities if item["path"] == selected_path)
        if selected not in wanted:
            wanted.insert(0, selected)
    if listing:
        wanted = []
    compact = []
    for item in wanted[:8]:
        keys = ("path", "name", "type", "parent_path", "visible")
        entity = {key: item[key] for key in keys}
        if item["path"] == selected_path:
            for key in ("world_position", "local_position", "local_scale", "local_rotation_xyzw"):
                entity[key] = item[key]
        compact.append(entity)
    return {"schema": "loom.scene-context", "version": 1, "frame": 24,
            "selected_path": selected_path, "entities": compact,
            "truncated": len(compact) < len(entities)}


def post_turn(base_url: str, prompt: str, timeout: float) -> dict:
    body = json.dumps({"message": prompt, "scene_context": scene_fixture(prompt)}, ensure_ascii=False).encode("utf-8")
    request = Request(base_url.rstrip("/") + "/v1/agent/turn", data=body,
                      headers={"Content-Type": "application/json"}, method="POST")
    with urlopen(request, timeout=timeout) as response:
        return json.loads(response.read())


def actions_match(actual: object, expected: list[dict],
                  scene_context: dict | None = None) -> bool:
    if not isinstance(actual, list) or len(actual) != len(expected):
        return False
    for actual_item, expected_item in zip(actual, expected):
        if not isinstance(actual_item, dict):
            return False
        for key in ("tool", "confirmation_required"):
            if key in expected_item and actual_item.get(key) != expected_item[key]:
                return False
        actual_arguments = actual_item.get("arguments")
        expected_arguments = expected_item.get("arguments", {})
        if (isinstance(actual_arguments, dict) and isinstance(expected_arguments, dict) and
                expected_arguments.get("target") == "selected" and
                actual_arguments.get("target") == (scene_context or {}).get("selected_path")):
            actual_arguments = {**actual_arguments, "target": "selected"}
        if not subset(actual_arguments, expected_arguments):
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8765")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--limit", type=int, default=0, help="Run only the first N prompts")
    parser.add_argument("--apply-current-host-guard", action="store_true",
                        help="Apply the checked-out request guard after the live API reply")
    parser.add_argument("--output", type=Path,
                        default=ROOT / ".cache/weaverprocedura/benchmarks/agent-comprehensive-v1-compact.json")
    args = parser.parse_args()

    try:
        with urlopen(args.base_url.rstrip("/") + "/health", timeout=5) as response:
            health = json.loads(response.read())
    except (HTTPError, URLError, TimeoutError, json.JSONDecodeError) as exc:
        raise SystemExit(f"Loom Agent API is unavailable at {args.base_url}: {exc}")
    if not health.get("model_loaded"):
        raise SystemExit("Loom Agent API health check says the model is not loaded")

    cases, source_hashes = load_cases()
    if args.limit:
        cases = cases[:args.limit]
    rows = []
    timings = []
    started_at = datetime.now(timezone.utc)
    for case in cases:
        started = time.perf_counter()
        try:
            payload = post_turn(args.base_url, case["prompt"], args.timeout)
            api_result = copy.deepcopy(payload.get("result", {}))
            api_actions = copy.deepcopy(api_result.get("actions"))
            result = copy.deepcopy(api_result)
            request_scene = scene_fixture(case["prompt"])
            if args.apply_current_host_guard:
                result = guard_turn_for_request(
                    case["prompt"], api_result, context=case["prompt"],
                    scene_context=request_scene)
            actual = result.get("actions")
            reply = result.get("reply", "")
            action_ok = actions_match(actual, case["expected_actions"], request_scene)
            api_action_ok = actions_match(api_actions, case["expected_actions"], request_scene)
            normalized_reply = normalize(reply) if isinstance(reply, str) else ""
            required = [any(normalize(option) in normalized_reply for option in alternatives)
                        for alternatives in case.get("reply_must_include", [])]
            forbidden = [normalize(phrase) in normalized_reply
                         for phrase in case.get("reply_must_not_include", [])]
            reply_ok = isinstance(reply, str) and all(required) and not any(forbidden)
            elapsed = time.perf_counter() - started
            timings.append(elapsed)
            rows.append({"id": case["id"], "source": case["source"], "prompt": case["prompt"],
                         "expected_actions": case["expected_actions"], "api_actions": api_actions,
                         "actual_actions": actual, "api_action_match": api_action_ok,
                         "host_guard_recovered": not bool(api_actions) and bool(actual),
                         "host_guard_changed_result": result != api_result,
                         "reply": reply, "action_match": action_ok, "reply_facts": required,
                         "reply_forbidden_hits": forbidden,
                         "protocol_valid": isinstance(actual, list) and isinstance(reply, str),
                         "response_source": payload.get("diagnostics", {}).get("response_source", "model"),
                         "passed": action_ok and reply_ok,
                         "seconds": round(elapsed, 3)})
        except (HTTPError, URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
            elapsed = time.perf_counter() - started
            timings.append(elapsed)
            rows.append({"id": case["id"], "source": case["source"], "prompt": case["prompt"],
                         "expected_actions": case["expected_actions"], "actual_actions": None,
                         "error": f"{type(exc).__name__}: {exc}", "action_match": False,
                         "reply_facts": [], "reply_forbidden_hits": [], "protocol_valid": False,
                         "response_source": "error", "passed": False, "seconds": round(elapsed, 3)})

    by_source = {}
    for source in sorted({row["source"] for row in rows}):
        source_rows = [row for row in rows if row["source"] == source]
        by_source[source] = {"passed": sum(bool(row["passed"]) for row in source_rows),
                             "cases": len(source_rows)}
    by_tool = {}
    for spec in PROTOCOL["actions"]:
        tool = spec["name"]
        selected = [row for row in rows if any(action.get("tool") == tool
                    for action in row["expected_actions"])]
        if selected:
            by_tool[tool] = {"passed": sum(bool(row["passed"]) for row in selected),
                             "cases": len(selected)}

    report = {
        "benchmark": "weaver-agent-comprehensive",
        "version": 1,
        "started_at_utc": started_at.isoformat(),
        "completed_at_utc": datetime.now(timezone.utc).isoformat(),
        "api_health": health,
        "adapter": str(DEFAULT_ADAPTER),
        "system_prompt_sha256": sha256(HERE / "data/system_prompt.txt"),
        "tool_schema_sha256": sha256(HERE / "data/tools.json"),
        "model_runtime_sha256": sha256(HERE / "model_runtime.py"),
        "agent_protocol_sha256": sha256(HERE / "agent_protocol.py"),
        "source_hashes": source_hashes,
        "cases": len(rows),
        "passes": sum(bool(row["passed"]) for row in rows),
        "accuracy": sum(bool(row["passed"]) for row in rows) / len(rows) if rows else 0.0,
        "api_action_passes": sum(bool(row.get("api_action_match")) for row in rows),
        "current_host_guard_recoveries": sum(bool(row.get("host_guard_recovered")) for row in rows),
        "current_host_guard_changed_results": sum(bool(row.get("host_guard_changed_result")) for row in rows),
        "local_guard_applied_after_api": args.apply_current_host_guard,
        "valid_protocol": sum(bool(row["protocol_valid"]) for row in rows),
        "unexpected_actions": sum(bool(row["protocol_valid"]) and not row["expected_actions"] and
                                   bool(row.get("actual_actions")) for row in rows),
        "mean_latency_seconds": statistics.mean(timings) if timings else None,
        "p95_latency_seconds_nearest_rank": sorted(timings)[math.ceil(0.95 * len(timings)) - 1] if timings else None,
        "by_source": by_source,
        "by_tool": by_tool,
        "items": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "items"},
                     ensure_ascii=False, indent=2))
    print(f"Full report: {args.output}")
    return 0 if rows and all(row["passed"] for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
