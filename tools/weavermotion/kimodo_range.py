#!/usr/bin/env python3
"""Ponovno generiranje DIJELA takea: sve prije i poslije raspona ostaje, Kimodo radi samo sredinu.

    kimodo_range.py constraints IZVOR.npz PRVI ZADNJI izlaz.constraints.json
    kimodo_range.py splice IZVOR.npz GENERIRANO(.npz | mapa/) PRVI ZADNJI [--blend 4]

PRVI i ZADNJI su kadrovi takea (30 Hz, od nule), ukljucivo: to je ono sto se generira iznova.

ZASTO OVAKO. Kimodo nema "inpainting" kao naredbu, ali ima ogranicenje "fullbody": cijela poza u
zadanim kadrovima. Kadrovi izvan raspona postanu takve poze iz izvornog NPZ-a (lokalne rotacije
77 zglobova i polozaj korijena - isti podaci iz kojih Kimodo pise BVH), pa model mora proci kroz
njih i slobodan je samo unutra.

Ogranicenja nisu tvrda - vodjenje difuzije i Kimodovo naknadno cistenje ih priblize, ne zakucaju.
Zato "splice": izvan raspona se uzme IZVOR tocno (bit po bit), unutra generirano, a na unutarnjim
rubovima se kroz --blend kadrova pretopi (slerp rotacija, lerp korijena), da nema skoka tamo gdje
generirano nije pogodilo izvor do milimetra. BVH se pise Kimodovim izvoznikom, istim putem kao
`kimodo_gen --bvh --bvh_standard_tpose`, pa ga editor cita kao svaki drugi take.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np


def _outside(frames: int, first: int, last: int) -> list[int]:
    return [f for f in range(frames) if f < first or f > last]


def write_constraints(source: Path, first: int, last: int, out: Path) -> int:
    import torch
    from kimodo.geometry import matrix_to_axis_angle

    d = np.load(source)
    frames = d["local_rot_mats"].shape[0]
    if not (0 <= first <= last < frames):
        raise SystemExit(f"raspon {first}-{last} je izvan takea (0-{frames - 1})")
    keep = _outside(frames, first, last)
    if not keep:
        raise SystemExit("raspon pokriva cijeli take - to je obicno generiranje, ne dio")
    local = torch.from_numpy(d["local_rot_mats"][keep]).float()
    axis_angle = matrix_to_axis_angle(local).numpy()
    constraint = {
        "type": "fullbody",
        "frame_indices": keep,
        "local_joints_rot": axis_angle.tolist(),
        "root_positions": d["root_positions"][keep].tolist(),
        "smooth_root_2d": d["smooth_root_pos"][keep][:, [0, 2]].tolist(),
    }
    out.write_text(json.dumps([constraint]))
    return len(keep)


def _quaternions(mats: np.ndarray) -> np.ndarray:
    from scipy.spatial.transform import Rotation

    shape = mats.shape[:-2]
    return Rotation.from_matrix(mats.reshape(-1, 3, 3)).as_quat().reshape(*shape, 4)


def _matrices(quats: np.ndarray) -> np.ndarray:
    from scipy.spatial.transform import Rotation

    shape = quats.shape[:-1]
    return Rotation.from_quat(quats.reshape(-1, 4)).as_matrix().reshape(*shape, 3, 3)


def _slerp(a: np.ndarray, b: np.ndarray, t: float) -> np.ndarray:
    dot = np.sum(a * b, axis=-1, keepdims=True)
    b = np.where(dot < 0.0, -b, b)
    dot = np.abs(dot)
    out = np.empty_like(a)
    close = (dot > 0.9995)[..., 0]
    out[close] = a[close] + (b[close] - a[close]) * t
    theta = np.arccos(np.clip(dot[~close], -1.0, 1.0))
    sin = np.sin(theta)
    out[~close] = (np.sin((1.0 - t) * theta) / sin) * a[~close] + (np.sin(t * theta) / sin) * b[~close]
    return out / np.linalg.norm(out, axis=-1, keepdims=True)


def splice(source: Path, generated: Path, first: int, last: int, blend: int) -> dict[str, np.ndarray]:
    """Izvor izvan [first, last], generirano unutra; pretapanje kroz `blend` kadrova unutar raspona."""
    import torch
    from kimodo.skeleton import SOMASkeleton77

    src = np.load(source)
    gen = np.load(generated)
    frames = min(src["local_rot_mats"].shape[0], gen["local_rot_mats"].shape[0])
    q_src = _quaternions(src["local_rot_mats"][:frames])
    q_gen = _quaternions(gen["local_rot_mats"][:frames])
    root_src = src["root_positions"][:frames]
    root_gen = gen["root_positions"][:frames]
    q = q_src.copy()
    root = root_src.copy()
    for f in range(max(0, first), min(frames, last + 1)):
        # Tezina generiranog: 0 na rubu raspona, 1 nakon `blend` kadrova unutra (smoothstep)
        edge = min(f - first + 1, last - f + 1)
        w = 1.0 if blend <= 0 else min(1.0, edge / float(blend + 1))
        w = w * w * (3.0 - 2.0 * w)
        q[f] = _slerp(q_src[f], q_gen[f], w)
        root[f] = root_src[f] + (root_gen[f] - root_src[f]) * w
    local = torch.from_numpy(_matrices(q)).float()
    skeleton = SOMASkeleton77()
    global_rots, posed, _ = skeleton.fk(local, torch.from_numpy(root).float())
    out = {k: gen[k][:frames].copy() for k in gen.files}
    out["local_rot_mats"] = local.numpy()
    out["global_rot_mats"] = global_rots.numpy()
    out["posed_joints"] = posed.numpy()
    out["root_positions"] = root.astype(np.float32)
    # Zagladjeni korijen i dodiri: izvan raspona od izvora, unutra od generiranog
    for key in ("smooth_root_pos", "foot_contacts", "global_root_heading"):
        if key in src.files and key in gen.files:
            merged = src[key][:frames].copy()
            merged[first:last + 1] = gen[key][first:last + 1]
            out[key] = merged
    return out


def write_spliced(motion: dict[str, np.ndarray], npz: Path, bvh: Path) -> None:
    import torch
    from kimodo.exports.bvh import save_motion_bvh
    from kimodo.skeleton import SOMASkeleton77

    np.savez(npz, **motion)
    skeleton = SOMASkeleton77()
    save_motion_bvh(bvh, torch.from_numpy(motion["local_rot_mats"]).float(),
                    torch.from_numpy(motion["root_positions"]).float(),
                    skeleton=skeleton, fps=30.0, standard_tpose=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    c = sub.add_parser("constraints")
    c.add_argument("source", type=Path)
    c.add_argument("first", type=int)
    c.add_argument("last", type=int)
    c.add_argument("out", type=Path)
    s = sub.add_parser("splice")
    s.add_argument("source", type=Path)
    s.add_argument("generated", type=Path, help="NPZ ili mapa s varijantama *_NN.npz")
    s.add_argument("first", type=int)
    s.add_argument("last", type=int)
    s.add_argument("--blend", type=int, default=4)
    args = parser.parse_args(argv)

    if args.command == "constraints":
        kept = write_constraints(args.source, args.first, args.last, args.out)
        print(f"ogranicenja: {kept} kadrova izvora drzi se, {args.first}-{args.last} se generira iznova -> {args.out}")
        return 0

    targets = sorted(args.generated.glob("*.npz")) if args.generated.is_dir() else [args.generated]
    if not targets:
        raise SystemExit(f"nema generiranih NPZ-ova u {args.generated}")
    for npz in targets:
        # Generirani BVH se prepise spojenim: editor uvozi upravo te datoteke
        motion = splice(args.source, npz, args.first, args.last, args.blend)
        write_spliced(motion, npz, npz.with_suffix(".bvh"))
        print(f"spojeno: {npz.with_suffix('.bvh').name} (izvor izvan {args.first}-{args.last}, pretapanje {args.blend} kadra)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
