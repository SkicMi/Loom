#!/usr/bin/env bash
set -euo pipefail
AUTORIG_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
UNIRIG_COMMIT=6793c6640ff01c8fb389f3993434124bb43d2933
VENV="$AUTORIG_DIR/.venv"
VENDOR="$AUTORIG_DIR/vendor/UniRig"
command -v uv >/dev/null || { echo 'Astral uv is required.' >&2; exit 1; }
if [[ ! -x "$VENV/bin/python" ]]; then uv venv --python 3.11 "$VENV"; fi
uv pip install --python "$VENV/bin/python" torch==2.7.1 torchvision==0.22.1 --index-url https://download.pytorch.org/whl/cu128
uv pip install --python "$VENV/bin/python" -r "$AUTORIG_DIR/requirements.txt"
uv pip install --python "$VENV/bin/python" torch-scatter==2.1.2+pt27cu128 torch-cluster==1.6.3+pt27cu128 \
    --find-links https://data.pyg.org/whl/torch-2.7.0+cu128.html
if ! "$VENV/bin/python" -c 'import flash_attn' >/dev/null 2>&1; then
    uv pip install --python "$VENV/bin/python" \
        'https://github.com/Dao-AILab/flash-attention/releases/download/v2.7.4.post1/flash_attn-2.7.4.post1%2Bcu12torch2.7cxx11abiTRUE-cp311-cp311-linux_x86_64.whl'
fi
if [[ ! -d "$VENDOR" ]]; then
    mkdir -p "$AUTORIG_DIR/vendor"
    git clone https://github.com/VAST-AI-Research/UniRig.git "$VENDOR"
    git -C "$VENDOR" checkout --detach "$UNIRIG_COMMIT"
fi
[[ "$(git -C "$VENDOR" rev-parse HEAD)" == "$UNIRIG_COMMIT" ]] || {
    echo 'Unexpected UniRig checkout; preserve it and resolve the version explicitly.' >&2; exit 1;
}
echo 'Auto-rig dependencies installed. Run a real model before considering the backend verified.'
