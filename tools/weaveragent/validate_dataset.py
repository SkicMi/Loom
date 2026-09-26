#!/usr/bin/env python3
"""Validate chat SFT files, deduplicate prompts, check tool protocol, and report lengths."""
from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path

from agent_protocol import ProtocolError, parse_turn


def read(path: Path) -> list[dict]:
    rows = []
    with path.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            try:
                row = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            rows.append(row)
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=Path(__file__).parent / "data/v1/train.jsonl")
    parser.add_argument("--eval", type=Path, default=Path(__file__).parent / "data/v1/eval.jsonl")
    parser.add_argument("--tokenizer", type=Path, default=None, help="Optional local Hugging Face tokenizer path")
    args = parser.parse_args()
    try:
        train, evaluation = read(args.input), read(args.eval)
        tokenizer = None
        if args.tokenizer:
            from transformers import AutoTokenizer
            tokenizer = AutoTokenizer.from_pretrained(str(args.tokenizer), local_files_only=True)
        seen: set[str] = set()
        lengths: list[int] = []
        action_counts: dict[str, dict[str, int]] = {"train": {}, "eval": {}}
        for dataset_name, rows in (("train", train), ("eval", evaluation)):
            if not rows:
                raise ValueError(f"{dataset_name} dataset is empty")
            for idx, row in enumerate(rows):
                messages = row.get("messages")
                if not isinstance(messages, list) or len(messages) < 3:
                    raise ValueError(f"{dataset_name}[{idx}] requires system/user/assistant messages")
                roles = [message.get("role") for message in messages]
                if roles[0] != "system" or roles[-2:] != ["user", "assistant"]:
                    raise ValueError(f"{dataset_name}[{idx}] has invalid role order")
                if any(not isinstance(message.get("content"), str) or not message["content"].strip()
                       for message in messages):
                    raise ValueError(f"{dataset_name}[{idx}] has an empty message")
                assistant = messages[-1]["content"]
                if not assistant.lstrip().startswith("{"):
                    raise ValueError(f"{dataset_name}[{idx}] assistant target is not JSON")
                parsed = parse_turn(assistant)
                for tool_action in parsed["actions"]:
                    name = tool_action["tool"]
                    action_counts[dataset_name][name] = action_counts[dataset_name].get(name, 0) + 1
                prompt = " ".join(" ".join(m["content"] for m in messages if m["role"] == "user").casefold().split())
                if dataset_name == "train":
                    if prompt in seen:
                        raise ValueError(f"duplicate training prompt at index {idx}")
                    seen.add(prompt)
                    if tokenizer:
                        rendered = tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=False)
                        lengths.append(len(tokenizer.encode(rendered, add_special_tokens=False)))
        report = {"train_examples": len(train), "eval_examples": len(evaluation),
                  "action_counts_train": action_counts["train"],
                  "action_counts_eval": action_counts["eval"]}
        if lengths:
            report["tokens"] = {"mean": round(statistics.mean(lengths), 1),
                                "p95": sorted(lengths)[int(0.95 * (len(lengths) - 1))],
                                "max": max(lengths), "over_1024": sum(n > 1024 for n in lengths)}
        print(json.dumps(report, indent=2))
        return 0
    except (OSError, ValueError, ProtocolError, ImportError) as exc:
        print(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
