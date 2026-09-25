#!/usr/bin/env bash
set -euo pipefail
base_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
upstream_dir="$base_dir/vendor/GR00T-WholeBodyControl"
if ! command -v curl >/dev/null 2>&1; then
    echo 'Install curl, then run this setup again; it fetches and validates NVIDIA model assets.' >&2
    exit 1
fi
if [[ ! -d "$upstream_dir/.git" ]]; then
    mkdir -p "$(dirname "$upstream_dir")"
    GIT_LFS_SKIP_SMUDGE=1 git clone --depth 1 --filter=blob:none --sparse \
        https://github.com/NVlabs/GR00T-WholeBodyControl.git "$upstream_dir"
    git -C "$upstream_dir" sparse-checkout set motionbricks
fi
python3 "$base_dir/fetch_assets.py" --repo "$upstream_dir"
python3 -m venv "$base_dir/.venv"
"$base_dir/.venv/bin/python" -m pip install --upgrade pip
"$base_dir/.venv/bin/python" -m pip install -e "$upstream_dir/motionbricks"
echo "MotionBricks ready at $upstream_dir/motionbricks"
