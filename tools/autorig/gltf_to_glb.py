#!/usr/bin/env python3
"""Pack a glTF file and its referenced resources into a self-contained GLB."""
from __future__ import annotations

from pathlib import Path
import sys

import bpy


def main() -> None:
    args = sys.argv[sys.argv.index("--") + 1:]
    if len(args) != 2:
        raise SystemExit("Usage: blender --background --python gltf_to_glb.py -- input.gltf output.glb")
    source, output = (Path(value).expanduser().resolve() for value in args)
    if source.suffix.lower() != ".gltf" or not source.is_file():
        raise SystemExit("Expected an existing .gltf source.")
    output.parent.mkdir(parents=True, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(source))
    if not any(obj.type == "MESH" for obj in bpy.context.scene.objects):
        raise RuntimeError("The glTF file contains no mesh objects.")
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(
        filepath=str(output),
        export_format="GLB",
        export_animations=False,
    )
    if not output.is_file() or output.stat().st_size == 0:
        raise RuntimeError("Blender did not write a GLB file.")


if __name__ == "__main__":
    main()
