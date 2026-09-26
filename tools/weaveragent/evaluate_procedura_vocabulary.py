#!/usr/bin/env python3
"""Evaluate every bilingual WeaverProcedura vocabulary test against the local Loom API."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import statistics
import time
import unicodedata
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from model_runtime import DEFAULT_ADAPTER
from procedura_vocabulary import DEFAULT_VOCABULARY, load_vocabulary


def normalize(text: str) -> str:
    decomposed = unicodedata.normalize("NFKD", text.casefold())
    plain = "".join(ch for ch in decomposed if not unicodedata.combining(ch))
    return " ".join("".join(ch if ch.isalnum() else " " for ch in plain).split())


def sha256_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def has_fact(text: str, alternatives: list[str]) -> bool:
    normalized = normalize(text)
    for value in alternatives:
        phrase = normalize(value)
        if not phrase:
            continue
        if len(phrase) <= 3:
            if re.search(r"(?<!\w)" + re.escape(phrase) + r"(?!\w)", normalized, re.UNICODE):
                return True
        elif phrase in normalized:
            return True
    return False


def post_turn(base_url: str, prompt: str, timeout: float) -> dict:
    body = json.dumps({"message": prompt}, ensure_ascii=False).encode("utf-8")
    request = Request(base_url.rstrip("/") + "/v1/agent/turn", data=body,
                      headers={"Content-Type": "application/json"}, method="POST")
    with urlopen(request, timeout=timeout) as response:
        payload = json.loads(response.read())
    result = payload.get("result")
    if not isinstance(result, dict) or not isinstance(result.get("reply"), str) or not isinstance(result.get("actions"), list):
        raise ValueError("API returned an invalid Loom turn")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8765")
    parser.add_argument("--vocabulary", type=Path, default=DEFAULT_VOCABULARY)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[2] / ".cache/weaverprocedura/benchmarks/procedura-vocabulary.json")
    parser.add_argument("--limit", type=int, default=0, help="Run only the first N cases, for a short smoke check")
    args = parser.parse_args()

    started_at = datetime.now(timezone.utc)
    vocabulary = load_vocabulary(args.vocabulary)
    full_cases = [(entry, case) for entry in vocabulary["entries"] for case in entry["test_cases"]]
    test_payload = [{"concept_id": entry["id"], "cases": entry["test_cases"]} for entry in vocabulary["entries"]]
    test_cases_sha256 = hashlib.sha256(json.dumps(test_payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
    cases = list(full_cases)
    if args.limit:
        cases = cases[:args.limit]
    try:
        with urlopen(args.base_url.rstrip("/") + "/health", timeout=5) as response:
            health = json.loads(response.read())
    except (HTTPError, URLError, TimeoutError) as exc:
        raise SystemExit(f"Loom Agent API is unavailable at {args.base_url}: {exc}")
    if not health.get("model_loaded"):
        raise SystemExit("Loom Agent API health check says the model is not loaded")

    adapter_meta = {}
    metrics = DEFAULT_ADAPTER / "training_metrics.json"
    adapter_config = DEFAULT_ADAPTER / "adapter_config.json"
    adapter_weights = DEFAULT_ADAPTER / "adapter_model.safetensors"
    index_manifest_path = Path(__file__).resolve().parents[2] / ".cache/weaverprocedura/project-index.manifest.json"
    index_manifest = json.loads(index_manifest_path.read_text(encoding="utf-8")) if index_manifest_path.is_file() else {}
    if metrics.is_file():
        adapter_meta = json.loads(metrics.read_text(encoding="utf-8"))
    adapter_config_sha256 = hashlib.sha256(adapter_config.read_bytes()).hexdigest() if adapter_config.is_file() else None
    rows = []
    timings = []
    for entry, case in cases:
        started = time.perf_counter()
        try:
            turn = post_turn(args.base_url, case["prompt"], args.timeout)
            elapsed = time.perf_counter() - started
            timings.append(elapsed)
            reply = turn["reply"]
            facts = [has_fact(reply, group) for group in case["must_include_any"]]
            contradictions = [has_fact(reply, group) for group in case.get("must_not_include_any", [])]
            no_action = len(turn["actions"]) == 0
            passed = all(facts) and not any(contradictions) and no_action
            rows.append({
                "concept_id": entry["id"], "test_id": case["id"], "language": case["language"],
                "mode": case["mode"], "prompt": case["prompt"], "reply": reply,
                "fact_groups_passed": facts, "fact_group_count": len(facts),
                "forbidden_claims_detected": contradictions,
                "forbidden_claim_group_count": len(contradictions),
                "actions_empty": no_action, "passed": passed, "seconds": round(elapsed, 3)
            })
        except (HTTPError, URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
            elapsed = time.perf_counter() - started
            timings.append(elapsed)
            rows.append({
                "concept_id": entry["id"], "test_id": case["id"], "language": case["language"],
                "mode": case["mode"], "prompt": case["prompt"],
                "error": f"{type(exc).__name__}: {exc}", "fact_groups_passed": [],
                "fact_group_count": len(case["must_include_any"]),
                "forbidden_claims_detected": [], "forbidden_claim_group_count": len(case.get("must_not_include_any", [])),
                "actions_empty": False,
                "passed": False, "seconds": round(elapsed, 3)
            })

    fact_total = sum(row["fact_group_count"] for row in rows)
    fact_passed = sum(sum(row["fact_groups_passed"]) for row in rows)
    by_concept = {}
    for entry in vocabulary["entries"]:
        concept_rows = [row for row in rows if row["concept_id"] == entry["id"]]
        if not concept_rows:
            continue
        language_results = {}
        for language in ("hr", "en"):
            selected = [row for row in concept_rows if row["language"] == language]
            language_results[language] = bool(selected) and all(row["passed"] for row in selected)
        by_concept[entry["id"]] = {
            "status": entry["status"],
            "passed": sum(row["passed"] for row in concept_rows),
            "total": len(concept_rows),
            "languages": language_results
        }

    report = {
        "benchmark": "weaverprocedura-vocabulary",
        "version": vocabulary["version"],
        "started_at_utc": started_at.isoformat(),
        "completed_at_utc": datetime.now(timezone.utc).isoformat(),
        "base_url": args.base_url,
        "api_health": health,
        "adapter": {
            "path": str(DEFAULT_ADAPTER),
            "base_model_id": adapter_meta.get("base_model_id"),
            "base_model_revision": adapter_meta.get("base_model_revision"),
            "dataset_version": adapter_meta.get("dataset_manifest", {}).get("dataset_version"),
            "adapter_config_sha256": adapter_config_sha256,
            "adapter_weights_sha256": sha256_file(adapter_weights),
            "training_metrics_sha256": sha256_file(metrics),
            "system_prompt_sha256": sha256_file(Path(__file__).parent / "data/system_prompt.txt"),
            "model_runtime_sha256": sha256_file(Path(__file__).with_name("model_runtime.py")),
            "agent_protocol_sha256": sha256_file(Path(__file__).with_name("agent_protocol.py")),
            "tool_schema_sha256": sha256_file(Path(__file__).parent / "data/tools.json"),
            "benchmark_runner_sha256": sha256_file(Path(__file__))
        },
        "vocabulary_sha256": hashlib.sha256(args.vocabulary.read_bytes()).hexdigest(),
        "test_cases_sha256": test_cases_sha256,
        "full_suite_cases": len(full_cases),
        "retrieval_index": {key: index_manifest.get(key) for key in ("source_files", "chunks", "source_sha256", "git_revision")},
        "cases": len(rows),
        "case_passes": sum(row["passed"] for row in rows),
        "case_accuracy": sum(row["passed"] for row in rows) / len(rows) if rows else 0.0,
        "fact_groups_passed": fact_passed,
        "fact_groups_total": fact_total,
        "fact_coverage": fact_passed / fact_total if fact_total else 0.0,
        "forbidden_claim_groups": sum(row["forbidden_claim_group_count"] for row in rows),
        "forbidden_claim_hits": sum(sum(row["forbidden_claims_detected"]) for row in rows),
        "unexpected_actions": sum(not row["actions_empty"] for row in rows),
        "mean_latency_seconds": statistics.mean(timings) if timings else None,
        "p95_latency_seconds": sorted(timings)[int(0.95 * (len(timings) - 1))] if timings else None,
        "by_concept": by_concept,
        "items": rows
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items()
                      if key not in {"items", "by_concept"}}, ensure_ascii=False, indent=2))
    print(json.dumps(by_concept, ensure_ascii=False, indent=2))
    print(f"Full report: {args.output}")
    return 0 if rows and all(row["passed"] for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
