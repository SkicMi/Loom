#!/usr/bin/env python3
"""Generate a MotionBricks G1 locomotion clip and export a Loom-readable Y-up BVH.

Uses NVIDIA's official MotionBricks inference code and released G1 checkpoints.
The BVH contains humanoid control joints for Loom's retargeter; G1 has no fingers.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True, help="GR00T-WholeBodyControl/motionbricks")
    parser.add_argument("--output", type=Path, required=True, help="Output BVH file")
    parser.add_argument("--duration", type=float, default=4.0, help="Seconds at 30 fps")
    parser.add_argument("--style", default="walk", help="Released G1 clip style")
    parser.add_argument("--constraints", type=Path, help="Optional Loom root2d constraints JSON")
    parser.add_argument("--seed", type=int, default=42)
    return parser.parse_args()


# Human control bones; skipped G1 hinge joints are folded into their descendants'
# world rotations. The root/leg/arm result can be retargeted onto UE Mannequin.
HUMAN_JOINTS = [
    ("Hips", "pelvis_skel", None),
    ("Spine1", "waist_yaw_skel", "Hips"),
    ("Spine2", "waist_roll_skel", "Spine1"),
    ("Chest", "waist_pitch_skel", "Spine2"),
    ("LeftShoulder", "left_shoulder_pitch_skel", "Chest"),
    ("LeftArm", "left_shoulder_yaw_skel", "LeftShoulder"),
    ("LeftForeArm", "left_elbow_skel", "LeftArm"),
    ("LeftHand", "left_wrist_yaw_skel", "LeftForeArm"),
    ("RightShoulder", "right_shoulder_pitch_skel", "Chest"),
    ("RightArm", "right_shoulder_yaw_skel", "RightShoulder"),
    ("RightForeArm", "right_elbow_skel", "RightArm"),
    ("RightHand", "right_wrist_yaw_skel", "RightForeArm"),
    ("LeftLeg", "left_hip_yaw_skel", "Hips"),
    ("LeftShin", "left_knee_skel", "LeftLeg"),
    ("LeftFoot", "left_ankle_roll_skel", "LeftShin"),
    ("LeftToeBase", "left_toe_base", "LeftFoot"),
    ("RightLeg", "right_hip_yaw_skel", "Hips"),
    ("RightShin", "right_knee_skel", "RightLeg"),
    ("RightFoot", "right_ankle_roll_skel", "RightShin"),
    ("RightToeBase", "right_toe_base", "RightFoot"),
]


def read_path(path: Path | None):
    if path is None:
        return None
    with path.open(encoding="utf-8") as stream:
        constraints = json.load(stream)
    root = next((item for item in constraints if item.get("type") == "root2d"), None)
    if root is None:
        raise ValueError("Constraints JSON has no root2d track")
    frames = root["frame_indices"]
    positions = root["smooth_root_2d"]
    if len(frames) < 2 or len(frames) != len(positions):
        raise ValueError("Root path needs at least two timed XZ points")
    if any(b <= a for a, b in zip(frames, frames[1:])):
        raise ValueError("Root path times must increase")
    return frames, positions


def path_velocity(path, frame: int) -> tuple[float, float, float]:
    if path is None:
        return 0.0, 1.0, 1.0
    import numpy as np
    frames, positions = path
    sample = lambda f, axis: float(np.interp(f, frames, [point[axis] for point in positions]))
    horizon = 15
    dx = sample(frame + horizon, 0) - sample(frame, 0)
    dz = sample(frame + horizon, 1) - sample(frame, 1)
    distance = math.hypot(dx, dz)
    if distance < 1e-5:
        return 0.0, 1.0, 0.0
    return dx / distance, dz / distance, distance * 30.0 / horizon


def write_bvh(output: Path, skeleton, positions, rotations, fps: float = 30.0) -> None:
    import numpy as np
    from scipy.spatial.transform import Rotation

    names = skeleton.bone_order_names
    indices = {name: names.index(source) for name, source, _ in HUMAN_JOINTS}
    parent = {name: parent_name for name, _, parent_name in HUMAN_JOINTS}
    children: dict[str, list[str]] = {name: [] for name in indices}
    for name, _, parent_name in HUMAN_JOINTS:
        if parent_name is not None:
            children[parent_name].append(name)
    neutral = skeleton.neutral_joints.detach().cpu().numpy()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="ascii") as stream:
        stream.write("HIERARCHY\n")

        def joint(name: str, depth: int) -> None:
            prefix = "  " * depth
            stream.write(f"{prefix}{'ROOT' if depth == 0 else 'JOINT'} {name}\n{prefix}{{\n")
            idx = indices[name]
            p = parent[name]
            offset = neutral[idx] if p is None else neutral[idx] - neutral[indices[p]]
            stream.write(f"{prefix}  OFFSET {offset[0]*100:.6f} {offset[1]*100:.6f} {offset[2]*100:.6f}\n")
            channels = "6 Xposition Yposition Zposition Zrotation Xrotation Yrotation" if p is None else "3 Zrotation Xrotation Yrotation"
            stream.write(f"{prefix}  CHANNELS {channels}\n")
            if children[name]:
                for child in children[name]:
                    joint(child, depth + 1)
            else:
                stream.write(f"{prefix}  End Site\n{prefix}  {{\n{prefix}    OFFSET 0 0 1\n{prefix}  }}\n")
            stream.write(f"{prefix}}}\n")

        joint("Hips", 0)
        stream.write(f"MOTION\nFrames: {len(positions)}\nFrame Time: {1/fps:.9f}\n")
        traversal: list[str] = []
        def visit(name: str) -> None:
            traversal.append(name)
            for child in children[name]:
                visit(child)
        visit("Hips")
        for frame in range(len(positions)):
            values: list[float] = []
            for name in traversal:
                idx = indices[name]
                p = parent[name]
                if p is None:
                    values.extend((positions[frame, idx] * 100.0).tolist())
                    local = rotations[frame, idx]
                else:
                    local = rotations[frame, indices[p]].T @ rotations[frame, idx]
                angles = Rotation.from_matrix(local).as_euler("ZXY", degrees=True)
                values.extend(float(v) for v in angles)
            if not np.isfinite(values).all():
                raise ValueError(f"MotionBricks produced non-finite values at frame {frame}")
            stream.write(" ".join(f"{value:.6f}" for value in values) + "\n")


def main() -> int:
    args = parse_args()
    upstream = args.upstream.resolve()
    if not (upstream / "motionbricks" / "motion_backbone" / "demo" / "utils.py").is_file():
        raise SystemExit(f"MotionBricks source not found at {upstream}")
    if not (upstream / "out" / "G1-clip.ckpt").is_file():
        raise SystemExit("MotionBricks G1 checkpoints are missing; run tools/motionbricks/setup.sh")
    if not math.isfinite(args.duration) or not 1.0 <= args.duration <= 30.0:
        raise SystemExit("Duration must be 1–30 seconds")
    path = read_path(args.constraints)
    sys.path.insert(0, str(upstream))
    os.chdir(upstream)  # NVIDIA configs and checkpoint paths are relative to this root.
    import numpy as np
    import torch
    from motionbricks.motion_backbone.demo.clips import clip_holder_G1
    from motionbricks.motion_backbone.demo.utils import navigation_demo

    if args.style not in clip_holder_G1.CLIPS:
        raise SystemExit(f"Unknown MotionBricks style {args.style!r}")
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    config = argparse.Namespace(controller="wasd", clips="G1", planner="default", EXP="default",
        random_seed=args.seed, result_dir=str(upstream / "out"), data_root=str(upstream / "datasets"),
        explicit_dataset_folder=None, reprocess_clips=0, use_qpos=1,
        source_root_realignment=1, target_root_realignment=1, force_canonicalization=1,
        skip_ending_target_cond=0, pre_filter_qpos=1, random_speed_scale=0,
        speed_scale=[0.8, 1.2], return_model_configs=True, return_dataloader=False)
    demo = navigation_demo(config)
    agent = demo.full_agent
    agent.reset()
    styles = list(clip_holder_G1.CLIPS)
    mode = styles.index(args.style)
    allowed = clip_holder_G1.CLIPS[args.style].get("allowed_pred_num_tokens")
    frames = []
    with torch.no_grad():
        for index in range(round(args.duration * 30)):
            qpos = agent.get_next_frame()
            frames.append(qpos.copy())
            demo.mj_data.qpos[:] = qpos
            dx, dz, speed = path_velocity(path, index)
            direction = torch.tensor([[dz, dx, 0.0]], dtype=torch.float32)
            controls = {
                "context_mujoco_qpos": agent.get_context_mujoco_qpos(),
                "movement_direction": direction,
                "facing_direction": direction,
                "mode": torch.tensor([[mode]], dtype=torch.int64),
                "allowed_pred_num_tokens": torch.tensor([allowed], dtype=torch.int64) if allowed else
                    torch.ones((1, 11), dtype=torch.int64),
                "random_seed": torch.tensor([args.seed], dtype=torch.int64),
            }
            if path is not None and speed > 0.0:
                controls["target_vel"] = torch.tensor([speed], dtype=torch.float32)
            agent.generate_new_frames(controls, demo.controller.get_controller_dt())
        qpos_tensor = torch.as_tensor(np.stack(frames), dtype=torch.float32, device="cuda")[None]
        positions, rotations = agent._converter.convert_mujoco_qpos_to_motion_transforms(qpos_tensor)
    write_bvh(args.output, agent._motion_rep.skeleton,
              positions[0].detach().cpu().numpy(), rotations[0].detach().cpu().numpy())
    print(f"MotionBricks G1: wrote {len(frames)} frames to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
