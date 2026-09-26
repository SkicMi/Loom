#!/usr/bin/env python3
"""Hand rig for the Manny-profile Auto Rig: clean, consistent finger orientation.

WHY. UniRig predicts finger joint *positions*, but its bone orientations (roll) change from joint to
joint (index_01 Z ~ world up, index_02 tilted 20-30 degrees), and the fitted Manny metacarpals point
anywhere. Bending a finger about its own axis then twists it sideways, a grip pose made on one rig
lands differently on another, and retargeting to UE Manny needs a manual retarget pose per finger.

CONVENTION (Blender/glTF bone frame, same on both hands):
  Y  along the bone, toward the next joint (the last bone continues the finger by 80 %)
  Z  toward the palm: +rotation about X curls the finger into the palm
  X  = Y x Z, the flexion axis - identical for every joint of one finger, so a finger bends in one
     plane like a real one (UE Manny: every index joint shares one flexion axis too)
The palm side is decided from the body (the palm faces the pelvis side in T- and A-pose), so it
works for any character without asking. The thumb curls toward the base of the little finger.

    blender --background --python hand_rig.py -- report rigged.glb     measure a rig (no changes)
"""
from __future__ import annotations

import sys

FINGERS = ("thumb", "index", "middle", "ring", "pinky")


def chain(bones, finger: str, side: str):
    """Bones of one finger from the hand outward (metacarpal first when the rig has one)."""
    names = ([f"{finger}_metacarpal_{side}"] if finger != "thumb" else []) + [f"{finger}_0{i}_{side}" for i in (1, 2, 3)]
    return [bones[name] for name in names if name in bones]


def _vec(v):
    from mathutils import Vector
    return Vector(v)


def palm_frame(bones, side: str, pelvis):
    """(palm normal, across index->pinky, forward hand->middle) from joint heads, or None."""
    need = [f"hand_{side}", f"index_01_{side}", f"pinky_01_{side}", f"middle_01_{side}"]
    if any(name not in bones for name in need):
        return None
    hand = _vec(bones[f"hand_{side}"].head)
    index = _vec(bones[f"index_01_{side}"].head)
    pinky = _vec(bones[f"pinky_01_{side}"].head)
    middle = _vec(bones[f"middle_01_{side}"].head)
    forward = (middle - hand).normalized()
    across = pinky - index
    across = (across - forward * across.dot(forward)).normalized()
    normal = forward.cross(across).normalized()
    # T-pose: palm down, A-pose: palm toward the thigh - both face the pelvis side of the hand
    if normal.dot(_vec(pelvis) - hand) < 0.0:
        normal = -normal
    return normal, across, forward


def orient_hands(arm) -> dict:
    """Edit mode on `arm`: tails along the finger, roll so Z faces the palm. Returns a small report."""
    import bpy
    bones = arm.data.edit_bones
    if "pelvis" not in bones:
        raise ValueError("Hand rig needs a pelvis bone to find the palm side")
    pelvis = _vec(bones["pelvis"].head)
    report = {}
    for side in ("l", "r"):
        frame = palm_frame(bones, side, pelvis)
        if frame is None:
            continue
        normal, across, forward = frame
        hand = bones[f"hand_{side}"]
        hand.tail = _vec(bones[f"middle_01_{side}"].head)
        hand.align_roll(normal)
        oriented = 0
        for finger in FINGERS:
            links = chain(bones, finger, side)
            if not links:
                continue
            heads = [_vec(bone.head) for bone in links]
            # the palm side of this finger: the thumb curls toward the little finger's base
            if finger == "thumb":
                toward = (_vec(bones[f"pinky_01_{side}"].head) - heads[0]).normalized()
                up = (toward * 0.6 + normal * 0.4).normalized()
            else:
                up = normal
            for i, bone in enumerate(links):
                if i + 1 < len(links):
                    tail = heads[i + 1]
                else:
                    step = heads[i] - heads[i - 1] if i > 0 else forward * 0.03
                    tail = heads[i] + step * 0.8
                if (tail - heads[i]).length < 1e-5:
                    tail = heads[i] + forward * 0.01
                bone.tail = tail
                bone.align_roll(up)
                oriented += 1
        report[side] = oriented
    return report


def curl_report(arm, angle_degrees: float = 15.0) -> dict:
    """Pose mode: bend each finger about its joints' local X and measure where the tip goes.

    palm:    cosine between the tip's motion and the palm normal (> 0: into the palm)
    lateral: sideways share of the tip's motion (0: the finger stays in its own plane)
    """
    import bpy
    from math import radians
    from mathutils import Quaternion
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    pose = arm.pose.bones
    rest = arm.data.bones
    mw = arm.matrix_world
    result = {}
    pelvis = rest["pelvis"].head_local if "pelvis" in rest else None
    for side in ("l", "r"):
        frame = palm_frame({b.name: _RestHead(b) for b in rest}, side, pelvis) if pelvis is not None else None
        if frame is None:
            continue
        normal, _, _ = frame
        for finger in FINGERS:
            names = [b.name for b in chain(rest, finger, side)]
            joints = [n for n in names if "metacarpal" not in n]
            if not joints:
                continue
            tip_bone = pose[joints[-1]]
            for n in names:
                pose[n].rotation_mode = "QUATERNION"
                pose[n].rotation_quaternion = Quaternion((1.0, 0.0, 0.0, 0.0))
            bpy.context.view_layer.update()
            before = mw @ tip_bone.tail
            for n in joints:
                pose[n].rotation_quaternion = Quaternion((1.0, 0.0, 0.0), radians(angle_degrees))
            bpy.context.view_layer.update()
            after = mw @ tip_bone.tail
            for n in joints:
                pose[n].rotation_quaternion = Quaternion((1.0, 0.0, 0.0, 0.0))
            # Only the part of the tip's motion across the finger: a small bend moves the tip
            # sideways off the finger's line - into the palm on a good rig
            direction = (mw @ tip_bone.tail) - (mw @ pose[joints[0]].head)
            direction = direction.normalized() if direction.length > 1e-9 else direction
            motion = after - before
            motion = motion - direction * motion.dot(direction)
            if motion.length < 1e-9:
                continue
            target = mw.to_3x3() @ normal
            if finger == "thumb":
                toward = (rest[f"pinky_01_{side}"].head_local - rest[joints[0]].head_local).normalized()
                target = mw.to_3x3() @ (toward * 0.6 + normal * 0.4)
            target = (target - direction * target.dot(direction)).normalized()
            plane = direction.cross(target).normalized()
            result[f"{finger}_{side}"] = {
                "palm": motion.normalized().dot(target),
                "lateral": abs(motion.normalized().dot(plane)),
            }
    bpy.context.view_layer.update()
    bpy.ops.object.mode_set(mode="OBJECT")
    return result


class _RestHead:
    """Rest bone with an edit-bone-like `.head` (armature space) for palm_frame."""
    def __init__(self, bone):
        self.head = bone.head_local


def _report_file(path: str) -> int:
    import bpy
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=path)
    arms = [obj for obj in bpy.data.objects if obj.type == "ARMATURE"]
    if len(arms) != 1:
        raise SystemExit("expected one armature")
    report = curl_report(arms[0])
    worst_palm = min((v["palm"] for v in report.values()), default=0.0)
    worst_lateral = max((v["lateral"] for v in report.values()), default=1.0)
    for name, v in sorted(report.items()):
        print(f"HAND {name:10s} palm {v['palm']:+.3f} lateral {v['lateral']:.3f}")
    print(f"HAND worst palm {worst_palm:+.3f} worst lateral {worst_lateral:.3f} fingers {len(report)}")
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    if len(argv) == 2 and argv[0] == "report":
        sys.exit(_report_file(argv[1]))
    raise SystemExit("Usage: blender --background --python hand_rig.py -- report rigged.glb")
