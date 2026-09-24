"""Validate the exact root-constraint JSON written by Loom with Kimodo's public loader."""

from pathlib import Path
import sys

import torch
from kimodo.constraints import Root2DConstraintSet, load_constraints_lst
from kimodo.skeleton import SOMASkeleton30


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_weavermotion_constraints.py <loom-fixture.json>")

    fixture = Path(sys.argv[1])
    if not fixture.is_file():
        raise SystemExit(f"Loom did not write the expected Kimodo constraints fixture: {fixture}")

    constraints = load_constraints_lst(str(fixture), SOMASkeleton30())
    assert len(constraints) == 1, f"expected one constraint set, got {len(constraints)}"
    root = constraints[0]
    assert isinstance(root, Root2DConstraintSet), f"expected Root2DConstraintSet, got {type(root).__name__}"
    assert root.frame_indices.tolist() == [0, 30], root.frame_indices.tolist()
    assert torch.allclose(
        root.smooth_root_2d,
        torch.tensor([[0.0, 0.0], [1.25, -0.5]]),
        atol=1e-6,
    ), root.smooth_root_2d
    assert root.global_root_heading is not None, "heading constraints were omitted"
    assert torch.allclose(
        root.global_root_heading,
        torch.tensor([[1.0, 0.0], [0.0, 1.0]]),
        atol=1e-5,
    ), root.global_root_heading

    fixture.unlink()
    print("Kimodo load_constraints_lst accepted Loom's root2d path and heading JSON.")
    return 0


