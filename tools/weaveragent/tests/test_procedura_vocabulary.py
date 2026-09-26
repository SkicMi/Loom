import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from procedura_vocabulary import alias_inventory, load_vocabulary, resolve_terms
from evaluate_procedura_vocabulary import has_fact


class ProceduraVocabularyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = load_vocabulary()

    def test_every_alias_has_its_own_exact_concept_mapping(self):
        for concept_id, language, alias in alias_inventory(self.data):
            with self.subTest(concept=concept_id, language=language, alias=alias):
                self.assertEqual(resolve_terms(alias, self.data), [concept_id])

    def test_every_vocabulary_item_has_bilingual_behavioral_tests(self):
        self.assertEqual(self.data["version"], 3)
        self.assertTrue(self.data["entries"])
        seen = set()
        case_ids = set()
        for entry in self.data["entries"]:
            with self.subTest(concept=entry["id"]):
                self.assertNotIn(entry["id"], seen)
                seen.add(entry["id"])
                self.assertGreaterEqual(len(entry["test_cases"]), 2)
                self.assertEqual({case["language"] for case in entry["test_cases"]}, {"hr", "en"})
                for case in entry["test_cases"]:
                    self.assertNotIn(case["id"], case_ids)
                    case_ids.add(case["id"])
                    self.assertTrue(case["must_include_any"])
                    self.assertIsInstance(case.get("must_not_include_any", []), list)
                    self.assertTrue(case["prompt"].strip())
        self.assertEqual(len(seen), len(self.data["entries"]))

    def test_longest_compound_term_wins_over_its_shorter_words(self):
        self.assertEqual(resolve_terms("closed curve and rectangle profile", self.data),
                         ["closed_path", "rectangle_profile"])
        self.assertEqual(resolve_terms("zatvorena krivulja i pravokutni profil", self.data),
                         ["closed_path", "rectangle_profile"])

    def test_reserved_capabilities_are_not_mislabelled_as_implemented(self):
        by_id = {entry["id"]: entry for entry in self.data["entries"]}
        for concept_id in ("street_network", "building_generator", "rope_generator",
                           "chain_generator", "smooth_spline"):
            with self.subTest(concept=concept_id):
                self.assertEqual(by_id[concept_id]["status"], "reserved_not_exposed")
                self.assertTrue(all(case["mode"] == "abstain" for case in by_id[concept_id]["test_cases"]))

    def test_new_procedural_nodes_have_bilingual_cases_and_engine_sources(self):
        by_id = {entry["id"]: entry for entry in self.data["entries"]}
        for concept_id in ("grid", "grid_point_height", "grid_to_mesh", "interior_blockout"):
            with self.subTest(concept=concept_id):
                entry = by_id[concept_id]
                self.assertEqual(entry["status"], "implemented")
                self.assertIn("engine/src/Engine/WeaverProcedura", entry["source"])
                self.assertEqual({case["language"] for case in entry["test_cases"]}, {"hr", "en"})

    def test_short_rubric_terms_require_word_boundaries(self):
        self.assertFalse(has_fact("The engine does not use the seed yet.", ["ne"]))
        self.assertTrue(has_fact("Sjeme ne utječe na geometriju.", ["ne"]))
        self.assertFalse(has_fact("Width and height are measured in meters.", ["m"]))
        self.assertTrue(has_fact("Both are measured in metrima.", ["metri", "metara", "meter"]))
        self.assertTrue(has_fact("zgrade s katovima", ["katovima", "floor"]))
        self.assertTrue(has_fact("it generates a plane", ["generates a plane"]))

    def test_road_is_preview_only_and_seed_is_not_claimed_as_active(self):
        by_id = {entry["id"]: entry for entry in self.data["entries"]}
        self.assertEqual(by_id["road_preview"]["status"], "composed_preview_only")
        self.assertEqual(by_id["seed"]["status"], "stored_not_evaluated")


if __name__ == "__main__":
    unittest.main()
