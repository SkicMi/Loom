#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
VENV="$SCRIPT_DIR/.venv-clean"
KIMODO_COMMIT="58e781898b3d7e328a676a75d3e338c45dce3ad9"

cd "$REPO_ROOT"
if ! command -v uv >/dev/null 2>&1; then
    echo "Missing uv; install Astral uv, then rerun this script." >&2
    exit 1
fi

uv venv --python 3.12 "$VENV"
uv pip install --python "$VENV/bin/python" \
    "git+https://github.com/nv-tlabs/kimodo.git@$KIMODO_COMMIT"

if ! "$VENV/bin/hf" auth whoami >/dev/null 2>&1; then
    echo "Hugging Face login is missing. Run: $VENV/bin/hf auth login" >&2
    exit 1
fi

echo "WeaverMotion runner ready: $VENV/bin/kimodo_gen"
echo "First generation downloads the Kimodo and gated LLM2Vec/Llama weights."
