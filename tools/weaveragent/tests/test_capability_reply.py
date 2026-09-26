import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_protocol import PROTOCOL
from model_runtime import CAPABILITY_TOOLS, capability_reply, compact_scene_context_for_query, sanitize_scene_context


class CapabilityReplyTest(unittest.TestCase):
    def test_identity_and_capability_prompts_get_a_grounded_answer(self):
        for prompt in (
            "Hello, who are you and what can you do?",
            "Who is Weaver?",
            "What can Loom do?",
            "How can you help me?",
        ):
            with self.subTest(prompt=prompt):
                reply = capability_reply(prompt)
                self.assertIsNotNone(reply)
                self.assertIn("Weaver", reply)
                self.assertIn("scene", reply.casefold())
                self.assertIn("Procedura", reply)
                self.assertIn("Save Recipe", reply)
                self.assertIn("not available yet", reply)

    def test_tool_inventory_stays_complete_when_the_action_api_changes(self):
        available_tools = {action["name"] for action in PROTOCOL["actions"]}
        self.assertEqual(CAPABILITY_TOOLS, available_tools)

    def test_scene_mutation_requests_do_not_get_routed_to_the_capability_answer(self):
        for prompt in (
            "Move the selected object to (1, 2, 3).",
            "Create a cube.",
            "Create a grid surface.",
        ):
            with self.subTest(prompt=prompt):
                self.assertIsNone(capability_reply(prompt))

    def test_unrelated_conversation_is_left_to_the_model(self):
        self.assertIsNone(capability_reply("What is a procedural graph?"))

    def test_model_scene_context_contains_only_the_requested_target(self):
        source = sanitize_scene_context({
            "schema": "loom.scene-context", "version": 1, "frame": 24,
            "selected_path": "/Scene/Hero", "truncated": False,
            "entities": [
                {"path": "/Scene/Hero", "name": "Hero", "type": "cube",
                 "local_position": [0, 0, 0], "local_scale": [1, 1, 1]},
                {"path": "/Environment/Wall", "name": "Wall", "type": "cube",
                 "local_position": [1, 2, 3], "local_scale": [2, 2, 2]},
            ],
        })
        compact = compact_scene_context_for_query("Move /Environment/Wall to (4, 5, 6).", source)
        self.assertEqual([item["path"] for item in compact["entities"]], ["/Environment/Wall"])
        self.assertNotIn("local_position", compact["entities"][0])
        self.assertTrue(compact["truncated"])

    def test_selected_transform_context_is_kept_only_for_selected_object_requests(self):
        source = sanitize_scene_context({
            "schema": "loom.scene-context", "version": 1, "frame": 24,
            "selected_path": "/Scene/Hero", "entities": [
                {"path": "/Scene/Hero", "name": "Hero", "type": "cube",
                 "world_position": [1, 2, 3], "local_position": [1, 2, 3],
                 "local_scale": [1, 1, 1], "local_rotation_xyzw": [0, 0, 0, 1]},
                {"path": "/Environment/Wall", "name": "Wall", "type": "cube"},
            ],
        })
        compact = compact_scene_context_for_query("What is this selected object?", source)
        self.assertEqual(len(compact["entities"]), 1)
        self.assertEqual(compact["entities"][0]["path"], "/Scene/Hero")
        self.assertEqual(compact["entities"][0]["world_position"], [1.0, 2.0, 3.0])

    def test_entity_listing_does_not_send_the_entire_scene_into_every_prompt(self):
        source = sanitize_scene_context({
            "schema": "loom.scene-context", "version": 1, "frame": 24,
            "selected_path": "/Scene/Hero", "entities": [
                {"path": "/Scene/Hero", "name": "Hero", "type": "cube"},
                {"path": "/Environment/Wall", "name": "Wall", "type": "cube"},
            ],
        })
        compact = compact_scene_context_for_query("List the live scene objects.", source)
        self.assertEqual(compact["entities"], [])
        self.assertTrue(compact["truncated"])


if __name__ == "__main__":
    unittest.main()
