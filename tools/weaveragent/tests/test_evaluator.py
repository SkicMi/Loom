import unittest

from evaluate_agent_turns import actions_match, scene_fixture


class AgentEvaluatorTest(unittest.TestCase):
    def test_selected_target_matches_its_resolved_live_path(self):
        actual = [{"tool": "scene.set_visibility", "arguments": {
            "target": "/Scene/Hero", "visible": False}, "confirmation_required": False}]
        expected = [{"tool": "scene.set_visibility", "arguments": {
            "target": "selected", "visible": False}, "confirmation_required": False}]
        self.assertTrue(actions_match(actual, expected, {"selected_path": "/Scene/Hero"}))

    def test_selected_target_does_not_match_a_different_live_path(self):
        actual = [{"tool": "scene.set_visibility", "arguments": {
            "target": "/Scene/Robot", "visible": False}, "confirmation_required": False}]
        expected = [{"tool": "scene.set_visibility", "arguments": {
            "target": "selected", "visible": False}, "confirmation_required": False}]
        self.assertFalse(actions_match(actual, expected, {"selected_path": "/Scene/Hero"}))

    def test_scene_fixture_keeps_path_when_prompt_ends_with_sentence_punctuation(self):
        scene = scene_fixture("Delete /Scene/Old.")
        self.assertIn("/Scene/Old", {item["path"] for item in scene["entities"]})

    def test_scene_fixture_keeps_dotted_live_names_without_trailing_punctuation(self):
        scene = scene_fixture("Hide /World/Proxy_2.")
        self.assertIn("/World/Proxy_2", {item["path"] for item in scene["entities"]})

    def test_scene_fixture_does_not_treat_delete_children_as_a_read_only_listing(self):
        scene = scene_fixture("Remove /Props/OldSign and its children.")
        self.assertIn("/Props/OldSign", {item["path"] for item in scene["entities"]})


if __name__ == "__main__":
    unittest.main()
