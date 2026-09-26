#!/usr/bin/env python3
"""Convert a verified UniRig-52 GLB to the UE5 Manny bone hierarchy.

The 52 inferred joint locations and skin groups are kept; Manny's extra spine,
twist, metacarpal and IK bones are fitted to those locations. No mesh vertices
or existing vertex weights are changed.
"""
from __future__ import annotations

import json
from pathlib import Path
import re
import sys


UNIRIG_NAMES = [
    "pelvis", "spine_01", "spine_02", "spine_05", "neck_01", "head",
    "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
    "thumb_01_l", "thumb_02_l", "thumb_03_l",
    "index_01_l", "index_02_l", "index_03_l",
    "middle_01_l", "middle_02_l", "middle_03_l",
    "ring_01_l", "ring_02_l", "ring_03_l",
    "pinky_01_l", "pinky_02_l", "pinky_03_l",
    "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
    "thumb_01_r", "thumb_02_r", "thumb_03_r",
    "index_01_r", "index_02_r", "index_03_r",
    "middle_01_r", "middle_02_r", "middle_03_r",
    "ring_01_r", "ring_02_r", "ring_03_r",
    "pinky_01_r", "pinky_02_r", "pinky_03_r",
    "thigh_l", "calf_l", "foot_l", "ball_l",
    "thigh_r", "calf_r", "foot_r", "ball_r",
]
assert len(UNIRIG_NAMES) == 52 and len(set(UNIRIG_NAMES)) == 52


def _mesh_components(mesh):
    adjacency = [[] for _ in mesh.vertices]
    for edge in mesh.edges:
        a, b = edge.vertices
        adjacency[a].append(b)
        adjacency[b].append(a)
    visited = bytearray(len(mesh.vertices))
    components = []
    for start in range(len(mesh.vertices)):
        if visited[start]:
            continue
        visited[start] = 1
        stack = [start]
        component = []
        while stack:
            vertex = stack.pop()
            component.append(vertex)
            for neighbor in adjacency[vertex]:
                if not visited[neighbor]:
                    visited[neighbor] = 1
                    stack.append(neighbor)
        components.append(component)
    return components


def _rigidify_fragmented_mesh(meshes):
    """Keep disconnected hard-surface pieces from blending across distant joints."""
    reports = []
    for mesh in meshes:
        components = _mesh_components(mesh.data)
        vertex_count = len(mesh.data.vertices)
        if vertex_count == 0 or not components:
            continue

        max_piece = max(map(len, components))
        small_piece_limit = max(64, int(vertex_count * 0.03))
        small_piece_vertices = sum(len(piece) for piece in components if len(piece) <= small_piece_limit)
        fragmented_ratio = small_piece_vertices / vertex_count
        # Ordinary continuous characters keep UniRig's smooth skin weights. Only opt into
        # per-piece rigid weights when the mesh is clearly a collection of many small parts.
        if (len(components) < max(40, vertex_count // 100) or
                max_piece > vertex_count * 0.05 or fragmented_ratio < 0.65):
            continue

        group_by_index = {group.index: group for group in mesh.vertex_groups}
        bone_groups = {
            group.index: group for group in mesh.vertex_groups
            if re.fullmatch(r"bone_\d+", group.name)
        }
        if not bone_groups:
            continue

        assignments = []
        for component in components:
            totals = {}
            for vertex_index in component:
                for membership in mesh.data.vertices[vertex_index].groups:
                    group = bone_groups.get(membership.group)
                    if group and membership.weight > 0.0:
                        totals[group.index] = totals.get(group.index, 0.0) + membership.weight
            if totals:
                winner = max(totals, key=totals.get)
                assignments.append((component, winner))

        old_members = {index: [] for index in bone_groups}
        for vertex in mesh.data.vertices:
            for membership in vertex.groups:
                if membership.group in bone_groups:
                    old_members[membership.group].append(vertex.index)
        for index, vertices in old_members.items():
            if vertices:
                group_by_index[index].remove(vertices)
        for component, winner in assignments:
            bone_groups[winner].add(component, 1.0, "REPLACE")

        reports.append({"mesh": mesh.name, "components": len(components),
                        "rigid_components": len(assignments), "vertices": vertex_count,
                        "fragmented_ratio": round(fragmented_ratio, 4)})
    return reports


def convert(source: Path, output: Path, template_path: Path) -> None:
    import bpy
    import numpy as np
    from mathutils import Vector

    template_data = json.loads(template_path.read_text())
    template = {item["name"]: item for item in template_data["bones"]}
    if len(template) != 88 or not set(UNIRIG_NAMES).issubset(template):
        raise ValueError("Manny reference skeleton is incomplete")
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(source))
    arms = [obj for obj in bpy.data.objects if obj.type == "ARMATURE"]
    if len(arms) != 1:
        raise ValueError("Expected exactly one UniRig armature")
    arm = arms[0]
    original = {bone.name for bone in arm.data.bones}
    if original != {f"bone_{i}" for i in range(52)}:
        raise ValueError("Input must have the verified UniRig-52 skeleton")
    helpers = {bone.custom_shape for bone in arm.pose.bones if bone.custom_shape}
    meshes = [obj for obj in bpy.data.objects if obj.type == "MESH" and obj not in helpers]
    if not meshes:
        raise ValueError("UniRig GLB has no skinned mesh")
    for mesh in meshes:
        if not any(mod.type == "ARMATURE" and mod.object == arm for mod in mesh.modifiers):
            raise ValueError(f"Mesh {mesh.name} is not bound to UniRig armature")

    rigid_reports = _rigidify_fragmented_mesh(meshes)
    if rigid_reports:
        for report in rigid_reports:
            print("Auto Rig: rigidized disconnected parts "
                  f"({report['components']} islands, {report['vertices']} vertices, "
                  f"{report['fragmented_ratio']:.0%} in small pieces) on {report['mesh']}", flush=True)

    for mesh in meshes:
        for group in mesh.vertex_groups:
            match = re.fullmatch(r"bone_(\d+)", group.name)
            if match:
                index = int(match.group(1))
                if index >= 52:
                    raise ValueError("Unexpected vertex group")
                group.name = UNIRIG_NAMES[index]

    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bones = arm.data.edit_bones
    anchors = {}
    for index, name in enumerate(UNIRIG_NAMES):
        bone = bones[f"bone_{index}"]
        anchors[name] = np.array(bone.head, dtype=float)
        bone.name = name

    # Manny FBX rest landmarks are in centimetres, UniRig GLB landmarks in metres.
    # Align the pelvis and feet, then apply local residuals from nearby inferred joints.
    reference = lambda name: np.asarray(template[name]["head"], dtype=float) * 0.01
    source_height = np.linalg.norm(anchors["pelvis"] -
                                   0.5 * (anchors["foot_l"] + anchors["foot_r"]))
    reference_height = np.linalg.norm(reference("pelvis") -
                                      0.5 * (reference("foot_l") + reference("foot_r")))
    if source_height <= 1e-5 or reference_height <= 1e-5:
        raise ValueError("Degenerate inferred/template leg length")
    scale = source_height / reference_height
    shift = anchors["pelvis"] - scale * reference("pelvis")
    residuals = {name: anchors[name] - (scale * reference(name) + shift)
                 for name in UNIRIG_NAMES}

    def fitted(point, target_name):
        source_point = np.asarray(point, dtype=float) * 0.01
        side = target_name[-2:] if target_name.endswith(("_l", "_r")) else ""
        candidates = [name for name in UNIRIG_NAMES if not side or name.endswith(side)]
        # The nearest inferred bones carry the local limb/pose correction.
        nearest = sorted(candidates, key=lambda name: np.linalg.norm(reference(name) - source_point))[:4]
        weights = np.array([1.0 / (0.02 + np.linalg.norm(reference(name) - source_point))**2
                            for name in nearest])
        weights /= weights.sum()
        correction = sum(weight * residuals[name] for weight, name in zip(weights, nearest))
        return Vector(scale * source_point + shift + correction)

    def metacarpal(point, name):
        # A metacarpal lies on the line from the wrist to the base of its own finger, as far along as
        # in Manny. Residuals from the nearest joints fanned them across each other when the rest
        # fingers are spread (HumanoidMascott: pinky metacarpal over the ring finger)
        side = name[-2:]
        finger = name.split("_")[0]
        hand, base = reference("hand" + side), reference(f"{finger}_01{side}")
        span = base - hand
        t = float(np.dot(np.asarray(point, dtype=float) * 0.01 - hand, span) / np.dot(span, span))
        return Vector(anchors["hand" + side] + t * (anchors[f"{finger}_01{side}"] - anchors["hand" + side]))

    for item in template_data["bones"]:
        name = item["name"]
        if name in anchors:
            continue
        bone = bones.new(name)
        place = metacarpal if "_metacarpal_" in name else fitted
        bone.head = place(item["head"], name)
        bone.tail = place(item["tail"], name)
        if (bone.tail - bone.head).length < 0.001:
            bone.tail = bone.head + Vector((0.0, 0.0, 0.01))

    # Blender keeps every bone's world rest transform when changing an edit-bone parent.
    for item in template_data["bones"]:
        bone = bones[item["name"]]
        bone.use_connect = False
        bone.parent = bones[item["parent"]] if item["parent"] else None
        if item["name"] not in anchors and item["name"].startswith(("ik_", "interaction", "center_of_mass")):
            bone.use_deform = False
    # Hand rig: tails along the fingers and one flexion axis per finger, +X curls into the palm
    # (hand_rig.py). Positions and weights stay as UniRig predicted them
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from hand_rig import orient_hands
    oriented = orient_hands(arm)
    print(f"Manny Auto Rig: hand rig oriented {oriented}", flush=True)
    bpy.ops.object.mode_set(mode="OBJECT")
    if {bone.name for bone in arm.data.bones} != set(template):
        raise ValueError("Manny skeleton conversion lost bones")

    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    for mesh in meshes:
        mesh.select_set(True)
    bpy.context.view_layer.objects.active = arm
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(output), export_format="GLB", use_selection=True,
                              export_animations=False, export_skins=True)
    print(f"Manny Auto Rig: {len(arm.data.bones)} bones, {len(meshes)} skinned meshes, "
          f"{sum(report['rigid_components'] for report in rigid_reports)} rigid pieces -> {output}", flush=True)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    if len(argv) != 3:
        raise SystemExit("Usage: blender --python manny_rig.py -- unirig.glb manny.glb manny_template.json")
    convert(Path(argv[0]).resolve(), Path(argv[1]).resolve(), Path(argv[2]).resolve())
