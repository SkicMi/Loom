"""Independently reimport the exported GLB and test actual armature deformation with Blender."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def validate(path: Path, preview: Path | None = None) -> dict:
    import bpy
    import numpy as np

    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    # Disable Blender's Icosphere bone-display helper (it is not source geometry).
    options = {"disable_bone_shape": True} if "disable_bone_shape" in bpy.ops.import_scene.gltf.get_rna_type().properties else {}
    bpy.ops.import_scene.gltf(filepath=str(path), **options)
    arms = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
    # Blender 4.0 cannot disable custom bone shapes. Exclude only actual shape references.
    helpers = {bone.custom_shape for arm in arms for bone in arm.pose.bones if bone.custom_shape}
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and obj not in helpers]
    if len(arms) != 1 or not meshes:
        raise ValueError("Expected one armature and at least one mesh")
    arm = arms[0]
    if len(arm.data.bones) < 3:
        raise ValueError("Too few bones for a usable character rig")
    names = {bone.name for bone in arm.data.bones}
    total = 0
    for mesh in meshes:
        if not any(mod.type == "ARMATURE" and mod.object == arm for mod in mesh.modifiers):
            raise ValueError(f"Mesh {mesh.name} is not bound to the armature")
        groups = {group.index for group in mesh.vertex_groups if group.name in names}
        for vertex in mesh.data.vertices:
            weights = [group.weight for group in vertex.groups if group.group in groups]
            if not weights or any(not math.isfinite(w) or w < 0 for w in weights):
                raise ValueError(f"Invalid or missing skin weights on vertex {vertex.index}")
            if abs(sum(weights) - 1.0) > 0.01:
                raise ValueError(f"Skin weights are not normalized on vertex {vertex.index}")
            total += 1

    def coordinates() -> np.ndarray:
        bpy.context.view_layer.update()
        graph = bpy.context.evaluated_depsgraph_get()
        result = []
        for mesh in meshes:
            evaluated = mesh.evaluated_get(graph)
            data = evaluated.to_mesh()
            try:
                result.extend(tuple(evaluated.matrix_world @ vertex.co) for vertex in data.vertices)
            finally:
                evaluated.to_mesh_clear()
        values = np.asarray(result)
        if not np.isfinite(values).all():
            raise ValueError("Deformation generated non-finite vertices")
        return values

    rest = coordinates()
    span = float(np.ptp(rest, axis=0).max())
    if span <= 1e-8:
        raise ValueError("Degenerate mesh bounds")
    # Each non-root bone is tested separately; global movement of a static object cannot pass.
    bends = []
    for bone in arm.pose.bones:
        if bone.parent is None:
            continue
        bone.rotation_mode = "XYZ"
        bone.rotation_euler.x = math.radians(25)
        posed = coordinates()
        displacement = np.linalg.norm(posed - rest, axis=1)
        count = int((displacement > span * 1e-5).sum())
        if count:
            bends.append({"bone": bone.name, "moved_vertices": count,
                          "max_displacement": float(displacement.max())})
        bone.rotation_euler.x = 0
    if len(bends) < 2:
        raise ValueError("Fewer than two non-root bones deform the mesh")
    if preview:
        # Offline pose snapshot; this is NOT an animation or a claim of Loom runtime skinning.
        for item in bends[::2]:
            arm.pose.bones[item["bone"]].rotation_euler.x = math.radians(25)
        bpy.context.view_layer.update()
        for mesh in meshes:
            bpy.context.view_layer.objects.active = mesh
            for modifier in list(mesh.modifiers):
                if modifier.type == "ARMATURE":
                    bpy.ops.object.modifier_apply(modifier=modifier.name)
        bpy.ops.object.select_all(action="DESELECT")
        for mesh in meshes:
            mesh.select_set(True)
        bpy.ops.export_scene.gltf(filepath=str(preview), use_selection=True,
                                  export_animations=False, export_skins=False)
    return {"vertices": total, "bones": len(arm.data.bones), "deforming_non_root_bones": bends,
            "finite": True, "normalized_weights": True,
            "note": "Structural/deformation checks only; anatomical quality requires visual review."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--preview", type=Path)
    import sys
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else None)
    report = validate(args.model, args.preview)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report), flush=True)
