#!/usr/bin/env python3
"""Build a local lexical index over Loom's tracked source and documentation."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / ".cache" / "weaverprocedura" / "project-index.jsonl"
EXTENSIONS = {".md", ".txt", ".h", ".hpp", ".cpp", ".cc", ".c", ".cmake", ".py", ".json", ".toml", ".yaml", ".yml"}
EXCLUDE_PARTS = {".git", ".cache", "build", "cmake-build-debug", "cmake-build-release", ".venv", "vendor", "node_modules"}
MAX_BYTES = 750_000
CHUNK_LINES = 72
OVERLAP_LINES = 12
WORD = re.compile(r"[\w:./+-]{2,}", re.UNICODE)


def tracked_files() -> list[Path]:
    output = subprocess.check_output(["git", "ls-files", "-co", "--exclude-standard"], cwd=ROOT, text=True)
    files = []
    for raw in output.splitlines():
        path = Path(raw)
        if path.suffix.lower() not in EXTENSIONS:
            continue
        if any(part in EXCLUDE_PARTS for part in path.parts):
            continue
        if path.parts[:2] == ("tools", "weaveragent") and path.name not in {"README.md"}:
            continue
        absolute = ROOT / path
        try:
            if absolute.is_file() and absolute.stat().st_size <= MAX_BYTES:
                files.append(path)
        except OSError:
            pass
    return sorted(set(files))


def chunks_for(path: Path, text: str) -> list[dict]:
    lines = text.splitlines()
    if not lines:
        return []
    chunks = []
    start = 0
    while start < len(lines):
        end = min(len(lines), start + CHUNK_LINES)
        block = "\n".join(lines[start:end]).strip()
        if len(block) >= 80:
            chunks.append({"path": path.as_posix(), "start_line": start + 1,
                           "end_line": end, "text": block})
        if end == len(lines):
            break
        start = end - OVERLAP_LINES
    return chunks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    corpus = []
    files = tracked_files()
    source_digests = []
    for path in files:
        try:
            text = (ROOT / path).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
        source_digests.append(f"{path.as_posix()}:{digest}")
        corpus.extend(chunks_for(path, text))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as stream:
        for record in corpus:
            stream.write(json.dumps(record, ensure_ascii=False) + "\n")
    manifest = {"source_files": len(files), "chunks": len(corpus), "index": str(args.output),
                "source_sha256": hashlib.sha256("\n".join(source_digests).encode("utf-8")).hexdigest(),
                "git_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()}
    manifest_path = args.output.with_suffix(".manifest.json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
