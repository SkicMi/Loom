#!/usr/bin/env python3
"""Run NVIDIA MotionBricks as a path-driven live pose source for Loom.

The controller uses tiny atomic files so Loom can keep its render loop responsive while
MotionBricks owns CUDA inference in this process. pose.bvh contains the initial and latest
sample for Loom's live retargeter; the final BVH contains every recorded sample.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import sys
import time
import traceback


def atomic_text(path: Path, text: str) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    os.replace(temporary, path)


def read_control(path: Path, previous: dict) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else previous
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return previous


def control_for_agent(torch, styles, control: dict, context_qpos, agent, demo, clip_holder):
    style = str(control.get("style", "walk"))
    if style == "run":
        style = "walk"  # the preview release has no separate G1 run primitive
    if style not in styles:
        style = "walk"

    speed = max(0.0, min(4.0, float(control.get("speed", 0.0))))
    if speed <= 1e-4 and style == "walk":
        style = "idle"
    mode = styles.index(style)
    dx = float(control.get("direction_x", 0.0))
    dz = float(control.get("direction_z", 0.0))
    heading = float(control.get("heading", 0.0))
    direction_length = math.hypot(dx, dz)
    if direction_length > 1e-5:
        dx /= direction_length
        dz /= direction_length
    else:
        dx, dz = math.sin(heading), math.cos(heading)

    # MotionBricks' planner uses [Z, X, 0] for world movement and facing vectors.
    movement = torch.tensor([[dz, dx, 0.0]], dtype=torch.float32)
    facing = torch.tensor([[math.cos(heading), math.sin(heading), 0.0]], dtype=torch.float32)
    allowed = clip_holder.CLIPS[style].get("allowed_pred_num_tokens")
    controls = {
        "context_mujoco_qpos": context_qpos,
        "movement_direction": movement,
        "facing_direction": facing,
        "mode": torch.tensor([[mode]], dtype=torch.int64),
        "allowed_pred_num_tokens": torch.tensor([allowed], dtype=torch.int64) if allowed else
            torch.ones((1, 11), dtype=torch.int64),
        "random_seed": torch.tensor([int(control.get("seed", 42))], dtype=torch.int64),
    }
    if speed > 0.0:
        controls["target_vel"] = torch.tensor([speed], dtype=torch.float32)
    return controls


def write_pose_pair(write_bvh, skeleton, converter, baseline_qpos, current_qpos, path: Path) -> None:
    import numpy as np
    import torch
    qpos = torch.as_tensor(np.stack([baseline_qpos, current_qpos]), dtype=torch.float32,
                           device="cuda")[None]
    positions, rotations = converter.convert_mujoco_qpos_to_motion_transforms(qpos)
    write_bvh(path, skeleton, positions[0].detach().cpu().numpy(),
              rotations[0].detach().cpu().numpy())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    session = args.session.resolve()
    session.mkdir(parents=True, exist_ok=True)
    control_path = session / "control.json"
    pose_path = session / "pose.bvh"
    sequence_path = session / "pose.seq"
    status_path = session / "status.txt"
    try:
        upstream = args.upstream.resolve()
        if not (upstream / "motionbricks" / "motion_backbone" / "demo" / "utils.py").is_file():
            raise RuntimeError(f"MotionBricks source not found at {upstream}")
        if not (upstream / "out" / "G1-clip.ckpt").is_file():
            raise RuntimeError("G1 checkpoints are missing; run tools/motionbricks/setup.sh")
        sys.path.insert(0, str(upstream))
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        os.chdir(upstream)
        import numpy as np
        import torch
        from generate import write_bvh
        from motionbricks.motion_backbone.demo.clips import clip_holder_G1
        from motionbricks.motion_backbone.demo.utils import navigation_demo

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
        baseline_qpos = agent.get_next_frame().copy()
        current_qpos = baseline_qpos.copy()
        write_pose_pair(write_bvh, agent._motion_rep.skeleton, agent._converter,
                        baseline_qpos, current_qpos, pose_path.with_suffix(".initial"))
        os.replace(pose_path.with_suffix(".initial"), pose_path)
        atomic_text(sequence_path, "0\n")
        atomic_text(status_path, "READY\n")

        recorded_qpos = [baseline_qpos]
        sequence = 0
        previous = {"style": "idle", "speed": 0.0, "direction_x": 0.0,
                    "direction_z": 0.0, "heading": 0.0, "seed": args.seed}
        period = 1.0 / float(agent._motion_rep.fps)
        next_tick = time.monotonic()
        with torch.inference_mode():
            while True:
                control = read_control(control_path, previous)
                previous = control
                if control.get("stop", False):
                    break

                current_qpos = agent.get_next_frame().copy()
                demo.mj_data.qpos[:] = current_qpos
                if control.get("record", True):
                    recorded_qpos.append(current_qpos)
                write_pose_pair(write_bvh, agent._motion_rep.skeleton, agent._converter,
                                baseline_qpos, current_qpos, pose_path.with_suffix(".next"))
                os.replace(pose_path.with_suffix(".next"), pose_path)
                sequence += 1
                atomic_text(sequence_path, f"{sequence}\n")
                atomic_text(status_path, f"READY\nframes={len(recorded_qpos)}\n")

                context = agent.get_context_mujoco_qpos()
                signals = control_for_agent(torch, styles, control, context, agent, demo, clip_holder_G1)
                agent.generate_new_frames(signals, demo.controller.get_controller_dt())

                next_tick += period
                delay = next_tick - time.monotonic()
                if delay > 0.0:
                    time.sleep(delay)
                else:
                    # Avoid a backlog after a slow CUDA frame; the live controller always uses the latest route.
                    next_tick = time.monotonic()

        if len(recorded_qpos) < 2:
            recorded_qpos.append(current_qpos.copy())
        qpos_tensor = torch.as_tensor(np.stack(recorded_qpos), dtype=torch.float32,
                                      device="cuda")[None]
        positions, rotations = agent._converter.convert_mujoco_qpos_to_motion_transforms(qpos_tensor)
        output = args.output.resolve()
        write_bvh(output, agent._motion_rep.skeleton,
                  positions[0].detach().cpu().numpy(), rotations[0].detach().cpu().numpy())
        atomic_text(status_path, f"DONE\nframes={len(recorded_qpos)}\noutput={output}\n")
        return 0
    except BaseException as exc:
        message = f"ERROR: {type(exc).__name__}: {exc}\n"
        try:
            atomic_text(status_path, message + traceback.format_exc())
        except OSError:
            pass
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
