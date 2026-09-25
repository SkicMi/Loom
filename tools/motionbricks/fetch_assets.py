#!/usr/bin/env python3
"""Fetch and SHA-256 verify the G1 MotionBricks checkpoints and MuJoCo meshes."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

HF_REVISION = "d8306fc4607259fda0220ce9d57bc423ad8cef20"
CHECKPOINTS = (
    "motionbricks/out/G1-clip.ckpt",
    "motionbricks/out/motionbricks_pose/version_1/checkpoints/model-step=2000000.ckpt",
    "motionbricks/out/motionbricks_vqvae/version_1/checkpoints/model-step=2000000.ckpt",
    "motionbricks/out/motionbricks_root/version_1/checkpoints/model-step=2000000.ckpt",
)
POINTER = b"version https://git-lfs.github.com/spec/v1"

def metadata(path: Path):
    with path.open("rb") as source:
        header = source.read(512)
    if not header.startswith(POINTER):
        return None
    text = header.decode("ascii")
    sha = re.search(r"^oid sha256:([0-9a-f]{64})$", text, re.M)
    size = re.search(r"^size ([0-9]+)$", text, re.M)
    if not sha or not size:
        raise RuntimeError(f"Malformed LFS pointer: {path}")
    return sha.group(1), int(size.group(1))

def digest(path: Path):
    h = hashlib.sha256()
    total = 0
    with path.open("rb") as source:
        for block in iter(lambda: source.read(8 * 1024 * 1024), b""):
            h.update(block)
            total += len(block)
    return h.hexdigest(), total

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True, help="Sparse NVIDIA GR00T-WholeBodyControl checkout")
    repo = parser.parse_args().repo.resolve()
    motion = repo / "motionbricks"
    if not (motion / "motionbricks/motion_backbone/demo/utils.py").is_file():
        raise SystemExit(f"MotionBricks source not found: {motion}")
    commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    targets = [repo / rel for rel in CHECKPOINTS]
    targets.extend((motion / "assets/skeletons/g1").rglob("*.STL"))
    pending = []
    for target in targets:
        if not target.is_file():
            continue
        expected = metadata(target)
        if expected is None:
            print(f"already present: {target.relative_to(repo)}", flush=True)
            continue
        sha, size = expected
        rel = target.relative_to(repo).as_posix()
        partial = target.with_name(target.name + ".partial")
        if rel.startswith("motionbricks/out/"):
            mirror_path = rel.removeprefix("motionbricks/")
            url = f"https://huggingface.co/suvadityamuk/motionbricks-ckpts/resolve/{HF_REVISION}/{mirror_path}"
        else:
            url = f"https://media.githubusercontent.com/media/NVlabs/GR00T-WholeBodyControl/{commit}/{rel}"
        pending.append((target, partial, sha, size, url, rel))
    if not pending:
        print("all required assets already present", flush=True)
        return 0
    for target, partial, sha, size, url, rel in pending:
        target.parent.mkdir(parents=True, exist_ok=True)
        print(f"fetching {rel} ({size} bytes)", flush=True)
        subprocess.run([
            "curl", "-fL", "--retry", "3", "--connect-timeout", "20",
            "--continue-at", "-", "--output", str(partial), url,
        ], check=True)
        actual_sha, actual_size = digest(partial)
        if actual_size != size or actual_sha != sha:
            raise RuntimeError(
                f"SHA-256 verification failed for {rel}: "
                f"{actual_size}/{size} bytes, {actual_sha}/{sha}"
            )
        os.replace(partial, target)
        print(f"verified {rel} sha256={actual_sha}", flush=True)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
