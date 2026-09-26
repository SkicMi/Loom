#!/usr/bin/env python3
"""kimodo_range.py bez kartice: ogranicenja se ucitaju Kimodovim citacem, spoj drzi izvor izvan raspona.

Ulaz su dvije stvarne varijante istog generiranja iz repozitorija (WeaverMotion/motion_1790364325023).
Sto se brani:
  - fullbody ogranicenja: Kimodo ih procita i FK njegovih poza pogodi izvor (inace bi model vodio
    prema krivim pozama, a nista ne bi puklo)
  - spoj: izvan raspona IZVOR do zadnjeg bita, u sredini raspona generirano, a spoj izvora sa samim
    sobom daje isti BVH kao Kimodov izvoznik (editor ga cita kao svaki drugi take)
  - raspon preko cijelog takea se odbija - to je obicno generiranje
"""
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "weavermotion"))
import kimodo_range  # noqa: E402

TAKE = ROOT / "WeaverMotion" / "motion_1790364325023"
SOURCE = TAKE / "motion_1790364325023_02.npz"
OTHER = TAKE / "motion_1790364325023_00.npz"


class KimodoRangeTests(unittest.TestCase):
    def test_constraints_load_in_kimodo_and_match_source(self):
        from kimodo.constraints import load_constraints_lst
        from kimodo.skeleton import SOMASkeleton30, SOMASkeleton77

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "c.json"
            kept = kimodo_range.write_constraints(SOURCE, 40, 79, out)
            source = np.load(SOURCE)
            keep = kimodo_range._outside(source["local_rot_mats"].shape[0], 40, 79)
            self.assertEqual(kept, len(keep))
            full = load_constraints_lst(str(out), SOMASkeleton77())[0]
            error = np.abs(full.global_joints_positions.numpy() - source["posed_joints"][keep]).max()
            self.assertLess(error, 1e-4, f"FK ogranicenja prema izvoru {error:.2e} m")
            self.assertEqual(tuple(full.frame_indices.tolist()), tuple(keep))
            # Model SOMA RP radi u 30 zglobova; pretvorba 77 -> 30 mora proci
            self.assertEqual(load_constraints_lst(str(out), SOMASkeleton30())[0].global_joints_positions.shape[1], 30)

    def test_whole_take_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                kimodo_range.write_constraints(SOURCE, 0, 119, Path(tmp) / "c.json")

    def test_splice_keeps_source_outside_and_generated_inside(self):
        source, other = np.load(SOURCE), np.load(OTHER)
        motion = kimodo_range.splice(SOURCE, OTHER, 40, 79, 4)
        outside = list(range(0, 40)) + list(range(80, 120))
        self.assertLess(np.abs(motion["local_rot_mats"][outside] - source["local_rot_mats"][outside]).max(), 1e-5)
        self.assertLess(np.abs(motion["root_positions"][outside] - source["root_positions"][outside]).max(), 1e-6)
        # Pretapanje na rubu raste s razmakom (do trecine raspona = 13 kadrova), pa je cisto generirano
        # tek od 53 do 66
        self.assertLess(np.abs(motion["local_rot_mats"][55:65] - other["local_rot_mats"][55:65]).max(), 1e-5)

    def test_large_gap_at_edge_is_blended_without_a_jump(self):
        # Druga varijanta je na rubovima raspona daleko od izvora (kao cucanj iza backflipa). Fiksno
        # pretapanje od 4 kadra je ondje skakalo do 0.42 m po kadru; sirina po razmaku mora skok
        # smanjiti barem za trecinu (izmjereno 0.419 -> 0.263 m)
        def edges(motion):
            steps = np.linalg.norm(np.diff(motion["posed_joints"], axis=0), axis=-1).max(axis=1)
            return max(steps[36:44].max(), steps[76:84].max())
        fixed = edges(kimodo_range.splice(SOURCE, OTHER, 40, 79, 4, adaptive=False))
        adaptive = edges(kimodo_range.splice(SOURCE, OTHER, 40, 79, 4))
        self.assertLess(adaptive, fixed * (2.0 / 3.0), f"rub {adaptive:.3f} m, fiksno {fixed:.3f} m")

    def test_splice_with_itself_writes_the_same_bvh_as_kimodo(self):
        with tempfile.TemporaryDirectory() as tmp:
            motion = kimodo_range.splice(SOURCE, SOURCE, 40, 79, 4)
            bvh = Path(tmp) / "self.bvh"
            kimodo_range.write_spliced(motion, Path(tmp) / "self.npz", bvh)
            ours = bvh.read_text().splitlines()
            theirs = SOURCE.with_suffix(".bvh").read_text().splitlines()
            head = ours.index("MOTION")
            self.assertEqual(ours[:head], theirs[:theirs.index("MOTION")])
            a = np.array([[float(v) for v in line.split()] for line in ours[head + 3:]])
            b = np.array([[float(v) for v in line.split()] for line in theirs[head + 3:]])
            self.assertEqual(a.shape, b.shape)
            self.assertLess(np.abs(a - b).max(), 1e-3)


if __name__ == "__main__":
    unittest.main()
