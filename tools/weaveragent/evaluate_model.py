#!/usr/bin/env python3
"""Held-out protocol accuracy and generation latency for the trained adapter."""
from __future__ import annotations

import argparse
import json
import statistics
import time
from pathlib import Path

from agent_protocol import parse_turn
from model_runtime import DEFAULT_ADAPTER, DEFAULT_BASE, LocalWeaverModel, ProjectIndex


def comparable(turn: dict) -> list[dict]:
    return [{"tool": item["tool"], "arguments": item["arguments"]} for item in turn["actions"]]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-model", type=Path, default=DEFAULT_BASE)
    parser.add_argument("--adapter", type=Path, default=DEFAULT_ADAPTER)
    parser.add_argument("--eval", type=Path, default=Path(__file__).parent / "data/v1/eval.jsonl")
    parser.add_argument("--bf16", action="store_true")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()
    model = LocalWeaverModel(args.base_model, args.adapter, inference_4bit=not args.bf16)
    index = ProjectIndex()
    records = [json.loads(line) for line in args.eval.read_text(encoding="utf-8").splitlines() if line.strip()]
    if args.limit:
        records = records[:args.limit]
    exact_action = 0
    valid_json = 0
    timings = []
    rows = []
    for example in records:
        user = next(message["content"] for message in example["messages"] if message["role"] == "user")
        expected = parse_turn(example["messages"][-1]["content"])
        started = time.perf_counter()
        try:
            actual = model.generate([{"role": "user", "content": user}], index)["result"]
            elapsed = time.perf_counter() - started
            valid_json += 1
            matched = comparable(actual) == comparable(expected)
            exact_action += int(matched)
            rows.append({"prompt": user, "reply": actual["reply"], "expected": comparable(expected),
                         "actual": comparable(actual), "valid_json": True,
                         "action_match": matched, "seconds": round(elapsed, 3)})
            timings.append(elapsed)
        except Exception as exc:
            elapsed = time.perf_counter() - started
            rows.append({"prompt": user, "error": f"{type(exc).__name__}: {exc}", "valid_json": False,
                         "seconds": round(elapsed, 3)})
            timings.append(elapsed)
    report = {"examples": len(records), "valid_protocol": valid_json,
              "exact_action_matches": exact_action,
              "action_accuracy": exact_action / len(records) if records else 0.0,
              "mean_seconds": statistics.mean(timings) if timings else None,
              "p95_seconds": sorted(timings)[int(0.95 * (len(timings) - 1))] if timings else None,
              "items": rows}
    output = args.adapter / "evaluation.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "items"}, indent=2))
    print(f"Full report: {output}")
    return 0 if valid_json == len(records) else 1


if __name__ == "__main__":
    raise SystemExit(main())
