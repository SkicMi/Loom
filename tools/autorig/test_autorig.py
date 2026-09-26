"""Run with .venv/bin/python -m unittest discover -s tools/autorig -v."""
from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
import unittest

from run import configure_model_configs, inspect_input, require_file, run_step
from validate import validate


class InputTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="loom autorig '")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.work = self.directory / "work"
        self.work.mkdir()

    def test_skin_config_enables_flash_attention(self) -> None:
        import yaml
        model_config = self.work / "configs/model"
        model_config.mkdir(parents=True)
        (model_config / "unirig_ar_350m_1024_81920_float32.yaml").write_text("llm: {}\n")
        skin_config = model_config / "unirig_skin.yaml"
        skin_config.write_text("mesh_encoder: {}\n")
        configure_model_configs(self.work)
        skin = yaml.safe_load(skin_config.read_text())
        self.assertIs(skin["mesh_encoder"]["enable_flash"], True)

    def glb(self, document: dict) -> Path:
        data = json.dumps(document).encode()
        data += b" " * (-len(data) % 4)
        path = self.directory / "model.glb"
        path.write_bytes(struct.pack("<5I", 0x46546C67, 2, 20 + len(data), len(data), 0x4E4F534A) + data)
        return path

    def test_accepts_unrigged_glb_header(self) -> None:
        self.assertEqual(inspect_input(self.glb({"meshes": [{}]}))["meshes"], [{}])

    def test_rejects_existing_rig(self) -> None:
        with self.assertRaisesRegex(ValueError, "already contains a rig"):
            inspect_input(self.glb({"meshes": [{}], "skins": [{"joints": [0]}]}))

    def test_rejects_external_resources(self) -> None:
        with self.assertRaisesRegex(ValueError, "External resources"):
            inspect_input(self.glb({"meshes": [{}], "images": [{"uri": "../texture.png"}]}))

    def test_rejects_no_mesh(self) -> None:
        with self.assertRaisesRegex(ValueError, "no mesh"):
            inspect_input(self.glb({}))

    def test_rejects_truncated_file(self) -> None:
        path = self.glb({"meshes": [{}]})
        path.write_bytes(path.read_bytes()[:-4])
        with self.assertRaisesRegex(ValueError, "length"):
            inspect_input(path)

    def test_rejects_invalid_header(self) -> None:
        path = self.glb({"meshes": [{}]})
        path.write_bytes(b"BAD!" + path.read_bytes()[4:])
        with self.assertRaisesRegex(ValueError, "GLB v2"):
            inspect_input(path)

    def test_subprocess_preserves_shell_characters(self) -> None:
        # A real subprocess, not a mocked subprocess.run assertion.
        text = "spaces ' ; $(touch SHOULD_NOT_EXIST)"
        run_step("argv safety", ["-c", "import sys,pathlib;pathlib.Path('args.json').write_text(sys.argv[1])", text], self.work)
        self.assertEqual((self.work / "args.json").read_text(), text)
        self.assertFalse((self.work / "SHOULD_NOT_EXIST").exists())

    def test_nonzero_process_exit_propagates(self) -> None:
        import subprocess
        with self.assertRaises(subprocess.CalledProcessError):
            run_step("intentional failure", ["-c", "print('intentional failure trace', flush=True); raise SystemExit(17)"], self.work)
        self.assertIn("intentional failure trace", (self.directory / "autorig.log").read_text())

    def test_missing_and_empty_output_rejected(self) -> None:
        path = self.directory / "result.glb"
        with self.assertRaises(RuntimeError):
            require_file(path)
        path.touch()
        with self.assertRaises(RuntimeError):
            require_file(path)


class DeformationTests(unittest.TestCase):
    def fixture(self, directory: Path, bind_to_root: bool = False, rig: bool = True) -> Path:
        import bpy
        for obj in list(bpy.data.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        data = bpy.data.meshes.new("ribbon")
        data.from_pydata([(-0.2, 0, 1.3), (0.2, 0, 1.3), (0, 0, 1.7),
                          (-0.2, 0, 2.3), (0.2, 0, 2.3), (0, 0, 2.7)], [], [(0, 1, 2), (3, 4, 5)])
        mesh = bpy.data.objects.new("ribbon", data)
        bpy.context.collection.objects.link(mesh)
        if rig:
            arm = bpy.data.objects.new("rig", bpy.data.armatures.new("rig"))
            bpy.context.collection.objects.link(arm)
            bpy.context.view_layer.objects.active = arm
            arm.select_set(True)
            bpy.ops.object.mode_set(mode="EDIT")
            parent = None
            for index in range(3):
                bone = arm.data.edit_bones.new(f"bone{index}")
                bone.head = (0, 0, index)
                bone.tail = (0, 0, index + 1)
                bone.parent = parent
                parent = bone
            bpy.ops.object.mode_set(mode="OBJECT")
            for index in range(3):
                mesh.vertex_groups.new(name=f"bone{index}")
            mesh.vertex_groups[0 if bind_to_root else 1].add([0, 1, 2], 1.0, "REPLACE")
            mesh.vertex_groups[0 if bind_to_root else 2].add([3, 4, 5], 1.0, "REPLACE")
            mesh.modifiers.new("armature", "ARMATURE").object = arm
            mesh.parent = arm
        path = directory / "fixture.glb"
        bpy.ops.export_scene.gltf(filepath=str(path), export_animations=False)
        return path

    def test_real_export_import_skin_deforms_and_preview_is_static(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = self.fixture(root)
            result = validate(path, root / "preview.glb")
            self.assertEqual(result["vertices"], 6)
            self.assertEqual(result["bones"], 3)
            self.assertEqual(len(result["deforming_non_root_bones"]), 2)
            self.assertEqual(result["deforming_non_root_bones"][1]["moved_vertices"], 3)
            self.assertGreater(result["deforming_non_root_bones"][1]["max_displacement"], 0.1)
            self.assertNotIn("skins", inspect_input(root / "preview.glb"))

    def test_skeleton_with_only_root_weighting_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self.fixture(Path(directory), bind_to_root=True)
            with self.assertRaisesRegex(ValueError, "non-root bones"):
                validate(path)

    def test_static_model_fails_validation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self.fixture(Path(directory), rig=False)
            with self.assertRaisesRegex(ValueError, "one armature"):
                validate(path)


if __name__ == "__main__":
    unittest.main()


class HandRigTests(unittest.TestCase):
    """hand_rig.py in Blender: +X bends every finger into the palm and keeps it in its own plane."""

    def build(self, pose: str):
        """Pelvis + one arm per side with five fingers; UniRig-like random rolls on every bone."""
        import bpy
        import random
        from mathutils import Vector, Matrix
        from math import radians
        for obj in list(bpy.data.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        arm = bpy.data.objects.new("rig", bpy.data.armatures.new("rig"))
        bpy.context.collection.objects.link(arm)
        bpy.context.view_layer.objects.active = arm
        arm.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        bones = arm.data.edit_bones
        rng = random.Random(7)
        pelvis = bones.new("pelvis")
        pelvis.head, pelvis.tail = (0, 0, 1.0), (0, 0, 1.1)
        for side, sign in (("l", 1.0), ("r", -1.0)):
            # T-pose: arm along +-X, palm down. A-pose: arm rotated 45 degrees down, palm toward the thigh
            tilt = Matrix.Rotation(radians(-45.0 * sign), 3, "Y") if pose == "A" else Matrix.Identity(3)
            shoulder = Vector((0.2 * sign, 0, 1.45))
            def at(x, y, z=0.0):
                return shoulder + tilt @ Vector((x * sign, y, z))
            hand = bones.new(f"hand_{side}")
            hand.head, hand.tail = at(0.55, 0), at(0.62, 0)
            hand.parent = pelvis
            spread = {"index": 0.03, "middle": 0.01, "ring": -0.01, "pinky": -0.03}
            for finger, y in spread.items():
                meta = bones.new(f"{finger}_metacarpal_{side}")
                meta.head, meta.tail = at(0.56, y * 0.6), at(0.63, y)
                meta.parent = hand
                parent = meta
                for i in (1, 2, 3):
                    bone = bones.new(f"{finger}_0{i}_{side}")
                    bone.head, bone.tail = at(0.63 + 0.03 * (i - 1), y), at(0.66 + 0.03 * (i - 1), y)
                    bone.parent = parent
                    parent = bone
            parent = hand
            for i in (1, 2, 3):
                bone = bones.new(f"thumb_0{i}_{side}")
                bone.head, bone.tail = at(0.57 + 0.025 * i, 0.04 + 0.01 * i, -0.01), at(0.595 + 0.025 * i, 0.05 + 0.01 * i, -0.01)
                bone.parent = parent
                parent = bone
        for bone in bones:
            bone.roll = radians(rng.uniform(-180.0, 180.0))
        return arm

    def check(self, pose: str) -> None:
        import bpy
        import hand_rig
        arm = self.build(pose)
        bpy.ops.object.mode_set(mode="EDIT")
        report = hand_rig.orient_hands(arm)
        bpy.ops.object.mode_set(mode="OBJECT")
        self.assertEqual(report, {"l": 19, "r": 19})      #4 prsta x (metakarpal + 3) + palac 3
        curls = hand_rig.curl_report(arm)
        self.assertEqual(len(curls), 10)
        for name, value in curls.items():
            self.assertGreater(value["palm"], 0.99, f"{pose}-pose {name} does not curl into the palm: {value}")
            self.assertLess(value["lateral"], 0.05, f"{pose}-pose {name} bends sideways: {value}")

    def test_t_pose_fingers_curl_into_palm(self) -> None:
        self.check("T")

    def test_a_pose_fingers_curl_into_palm(self) -> None:
        self.check("A")
