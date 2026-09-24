#!/usr/bin/env python3
"""Offline tests for Loom's thin Kimodo CLI adapter; no model/GPU is loaded."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "weavermotion"))
import kimodo_cli


class FakeKimodo:
    def __call__(self, prompt, **kwargs):
        return prompt, kwargs


class KimodoCliAdapterTests(unittest.TestCase):
    def test_custom_parameters_are_removed_before_official_cli_parse(self):
        options, forwarded = kimodo_cli.parse_adapter_args([
            "walk forward", "--model", "Kimodo-SOMA-RP-v1.1",
            "--first_heading_angle", "1.2", "--root_margin", "0.13",
        ])
        self.assertEqual(options.first_heading_angle, 1.2)
        self.assertEqual(options.root_margin, 0.13)
        self.assertEqual(forwarded, ["walk forward", "--model", "Kimodo-SOMA-RP-v1.1"])

    def test_api_values_are_injected_and_explicit_call_values_win(self):
        original = kimodo_cli.install_generation_defaults(FakeKimodo, 1.2, 0.13)
        try:
            _, defaults = FakeKimodo()("walk")
            self.assertEqual(defaults["first_heading_angle"], 1.2)
            self.assertEqual(defaults["root_margin"], 0.13)
            _, explicit = FakeKimodo()("walk", first_heading_angle=-0.5, root_margin=0.2)
            self.assertEqual(explicit["first_heading_angle"], -0.5)
            self.assertEqual(explicit["root_margin"], 0.2)
        finally:
            FakeKimodo.__call__ = original


if __name__ == "__main__":
    unittest.main()
