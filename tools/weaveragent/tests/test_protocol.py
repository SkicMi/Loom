import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_protocol import ProtocolError, guard_turn_for_request, parse_turn
from model_runtime import sanitize_scene_context


class AgentProtocolTest(unittest.TestCase):
    def test_valid_create_is_kept_typed(self):
        result = parse_turn('{"version":1,"reply":"Creating it.","actions":[{"tool":"scene.create_primitive","arguments":{"primitive":"cube","name":"Hero","translation":[1,2,3]}}]}')
        self.assertEqual(result["actions"][0]["tool"], "scene.create_primitive")
        self.assertEqual(result["actions"][0]["arguments"]["translation"], [1, 2, 3])

    def test_common_tool_name_alias_is_normalized(self):
        result = parse_turn('{"version":1,"reply":"Move.","actions":[{"name":"scene.set_transform","arguments":{"target":"selected","translation":[1,2,3]}}]}')
        self.assertEqual(result["actions"][0]["tool"], "scene.set_transform")

    def test_delete_always_requires_host_confirmation(self):
        result = parse_turn('{"version":1,"reply":"Confirm deletion.","actions":[{"tool":"scene.delete","arguments":{"target":"/Scene/Parent"}}]}')
        self.assertTrue(result["actions"][0]["confirmation_required"])

    def test_unknown_tool_is_rejected(self):
        with self.assertRaises(ProtocolError):
            parse_turn('{"version":1,"reply":"Run this.","actions":[{"tool":"os.execute","arguments":{"command":"rm -rf /"}}]}')

    def test_non_finite_transform_is_rejected(self):
        with self.assertRaises(ProtocolError):
            parse_turn('{"version":1,"reply":"Move.","actions":[{"tool":"scene.set_transform","arguments":{"target":"selected","translation":[0,NaN,0]}}]}')

    def test_partial_transform_is_rejected(self):
        with self.assertRaises(ProtocolError):
            parse_turn('{"version":1,"reply":"Move.","actions":[{"tool":"scene.set_transform","arguments":{"target":"selected","scale":[1,0,1]}}]}')

    def test_unsupported_procedura_request_cannot_emit_actions(self):
        turn = parse_turn('{"version":1,"reply":"Creating a proxy.","actions":[{"tool":"scene.create_primitive","arguments":{"primitive":"cube","name":"Proxy"}}]}')
        guarded = guard_turn_for_request("Make a rope along this curve.", turn)
        self.assertEqual(guarded["actions"], [])

    def test_missing_transform_value_cannot_be_invented(self):
        turn = parse_turn('{"version":1,"reply":"Moving it.","actions":[{"tool":"scene.set_transform","arguments":{"target":"/Scene/Hero","translation":[0.4,0.4,0.4]}}]}')
        guarded = guard_turn_for_request("Move /Scene/Hero.", turn)
        self.assertEqual(guarded["actions"], [])

    def test_rotation_axis_is_mapped_to_requested_component(self):
        turn = parse_turn('{"version":1,"reply":"Rotating.","actions":[{"tool":"scene.set_transform","arguments":{"target":"selected","rotation_degrees":[90,0,0]}}]}')
        guarded = guard_turn_for_request("Rotate the selected object around Y axis by 90 degrees.", turn)
        self.assertEqual(guarded["actions"][0]["arguments"]["rotation_degrees"], [0.0, 90.0, 0.0])

    def test_preserved_transform_channels_are_not_written(self):
        turn = parse_turn('{"version":1,"reply":"Moving.","actions":[{"tool":"scene.set_transform","arguments":{"target":"/Scene/Robot","translation":[1,2,3],"rotation_degrees":[1,2,3]}}]}')
        guarded = guard_turn_for_request("Move /Scene/Robot to (1, 2, 3) and keep its current rotation.", turn)
        self.assertEqual(guarded["actions"][0]["arguments"], {"target": "/Scene/Robot", "translation": [1.0, 2.0, 3.0]})

    def test_scene_listing_intent_is_grounded_as_read_only_tool(self):
        turn = {"version": 1, "reply": "There is no output scene.", "actions": []}
        guarded = guard_turn_for_request("Prikaži mi objekte u sceni.", turn)
        self.assertEqual(guarded["actions"][0]["tool"], "scene.list_entities")

    def test_scene_listing_preserves_case_of_the_requested_parent_path(self):
        turn = {"version": 1, "reply": "", "actions": []}
        guarded = guard_turn_for_request("List the children under /Environment.", turn)
        self.assertEqual(guarded["actions"][0]["arguments"], {"parent": "/Environment"})

    def test_explicit_rename_can_be_recovered_from_exact_path_and_name(self):
        turn = {"version": 1, "reply": "I could not form a tool call.", "actions": []}
        guarded = guard_turn_for_request("Rename /Environment/WallOld as Wall_Main.", turn)
        self.assertEqual(guarded["actions"][0]["arguments"], {"target": "/Environment/WallOld", "name": "Wall_Main"})

    def test_explicit_selected_rename_can_be_recovered(self):
        turn = {"version": 1, "reply": "", "actions": []}
        guarded = guard_turn_for_request("Rename the selected object to Marker_3.", turn)
        self.assertEqual(guarded["actions"][0]["arguments"], {"target": "selected", "name": "Marker_3"})

    def test_unnamed_cube_at_scene_center_is_recovered_without_guessing_a_name(self):
        turn = {"version": 1, "reply": "Which cube should I add?", "actions": []}
        guarded = guard_turn_for_request("Can you spawn a cube in center of scene?", turn,
                                         scene_context={"selected_path": None, "entities": []})
        self.assertEqual(guarded["actions"][0]["arguments"],
                         {"primitive": "cube", "translation": [0.0, 0.0, 0.0]})

    def test_short_color_followup_completes_previous_cube_request(self):
        turn = {"version": 1, "reply": "I need an exact scene path.", "actions": []}
        context = "Can you spawn a cube in center of scene?\nWhich cube should I add?\ngreen one"
        guarded = guard_turn_for_request("green one", turn, context=context,
                                         scene_context={"selected_path": None, "entities": []})
        self.assertEqual(guarded["actions"][0]["arguments"],
                         {"primitive": "cube", "color": [0.10, 0.78, 0.18],
                          "translation": [0.0, 0.0, 0.0]})

    def test_source_code_name_cannot_be_used_as_live_scene_target(self):
        turn = parse_turn('{"version":1,"reply":"Moving.","actions":[{"tool":"scene.set_transform","arguments":{"target":"/Test","translation":[1,2,3]}}]}')
        guarded = guard_turn_for_request("Move /Test to (1,2,3).", turn, context="The project has a Test class.",
                                         scene_context={"selected_path": None, "entities": []})
        self.assertEqual(guarded["actions"], [])

    def test_english_preposition_to_does_not_authorize_the_selected_entity(self):
        turn = parse_turn('{"version":1,"reply":"Moving.","actions":[{"tool":"scene.set_transform","arguments":{"target":"selected","translation":[4,5,6]}}]}')
        scene = {"selected_path": "/Scene/Hero", "entities": [{"path": "/Environment/Wall", "name": "Wall"}]}
        guarded = guard_turn_for_request("Move /Environment/Wall to (4, 5, 6).", turn, scene_context=scene)
        self.assertEqual(guarded["actions"], [])

    def test_live_scene_name_resolves_to_unique_path(self):
        turn = parse_turn('{"version":1,"reply":"Moving.","actions":[{"tool":"scene.set_transform","arguments":{"target":"Hero","translation":[1,2,3]}}]}')
        scene = {"selected_path": None, "entities": [{"path": "/Set/Hero", "name": "Hero"}]}
        guarded = guard_turn_for_request("Move Hero to (1,2,3).", turn, scene_context=scene)
        self.assertEqual(guarded["actions"][0]["arguments"]["target"], "/Set/Hero")

    def test_create_color_must_be_in_unit_range(self):
        with self.assertRaises(ProtocolError):
            parse_turn('{"version":1,"reply":"Creating.","actions":[{"tool":"scene.create_primitive","arguments":{"primitive":"cube","color":[0,1.2,0]}}]}')

    def test_scene_listing_is_read_only_tool(self):
        result = parse_turn('{"version":1,"reply":"Listing scene.","actions":[{"tool":"scene.list_entities","arguments":{}}]}')
        self.assertFalse(result["actions"][0]["confirmation_required"])

    def test_grid_recipe_tool_call_is_valid_and_held_to_explicit_generator_intent(self):
        turn = parse_turn('{"version":1,"reply":"Izrađujem grid.","actions":[{"tool":"procedura.create_recipe","arguments":{"generator":"grid_surface","name":"Raised Surface","width":12,"depth":10,"cells_x":4,"cells_z":4,"point_heights":[{"column":2,"row":2,"height":2.5}]}}]}')
        guarded = guard_turn_for_request("Napravi grid površinu 12 x 10 metara i podigni točku (2, 2) na 2.5 metara.", turn)
        self.assertEqual(guarded["actions"][0]["tool"], "procedura.create_recipe")
        self.assertEqual(guarded["actions"][0]["arguments"]["point_heights"][0]["column"], 2)
        wrong = guard_turn_for_request("Napravi hodnik sa sobama.", turn)
        self.assertEqual(wrong["actions"], [])

    def test_hallway_recipe_tool_call_is_valid_for_croatian_and_english(self):
        for prompt in ("Napravi tlocrt hodnika s tri sobe sa svake strane.",
                       "Create a hallway with three rooms on each side."):
            with self.subTest(prompt=prompt):
                turn = parse_turn('{"version":1,"reply":"Creating the blockout.","actions":[{"tool":"procedura.create_recipe","arguments":{"generator":"interior_blockout","rooms_per_side":3}}]}')
                guarded = guard_turn_for_request(prompt, turn)
                self.assertEqual(guarded["actions"][0]["tool"], "procedura.create_recipe")

    def test_explicit_grid_parameters_are_recovered_when_model_omits_the_tool_call(self):
        prompt = ("Napravi grid površinu širine 12 metara i dubine 8 metara, s 4 ćelije po osi. "
                  "Podigni točku stupac 2, red 1 na Y 2.5 metra.")
        turn = {"version": 1, "reply": "", "actions": []}
        recovered = guard_turn_for_request(prompt, turn)
        self.assertEqual(recovered["actions"][0]["tool"], "procedura.create_recipe")
        self.assertEqual(recovered["actions"][0]["arguments"], {
            "generator": "grid_surface", "width": 12.0, "depth": 8.0,
            "cells_x": 4, "cells_z": 4,
            "point_heights": [{"column": 2, "row": 1, "height": 2.5}],
        })

    def test_spelled_out_grid_and_room_counts_and_hyphenated_wall_height_are_recovered(self):
        grid = guard_turn_for_request(
            "Create a 10 by 6 meter grid surface, with five cells along X and three cells along Z.",
            {"version": 1, "reply": "", "actions": []})
        self.assertEqual(grid["actions"][0]["arguments"], {
            "generator": "grid_surface", "width": 10.0, "depth": 6.0,
            "cells_x": 5, "cells_z": 3,
        })
        interior = guard_turn_for_request(
            "Create a hallway with two rooms per side and 3-meter-high walls.",
            {"version": 1, "reply": "", "actions": []})
        self.assertEqual(interior["actions"][0]["arguments"], {
            "generator": "interior_blockout", "rooms_per_side": 2, "wall_height": 3.0,
        })

    def test_grid_recovery_does_not_emit_an_out_of_bounds_point(self):
        prompt = "Create a grid surface and raise column 8, row 1 to height 2 meters."
        recovered = guard_turn_for_request(prompt, {"version": 1, "reply": "", "actions": []})
        self.assertEqual(recovered["actions"], [])

    def test_procedura_recipe_rejects_invalid_point_coordinates_duplicates_and_generator_fields(self):
        invalid_arguments = (
            {"generator": "grid_surface", "cells_x": 2, "point_heights": [{"column": 3, "row": 0, "height": 1}]},
            {"generator": "grid_surface", "point_heights": [
                {"column": 1, "row": 1, "height": 1}, {"column": 1, "row": 1, "height": 2}]},
            {"generator": "grid_surface", "rooms_per_side": 2},
            {"generator": "interior_blockout", "door_width": 3.8},
            {"generator": "street_network"},
        )
        for arguments in invalid_arguments:
            with self.subTest(arguments=arguments):
                payload = {"version": 1, "reply": "Create.", "actions": [
                    {"tool": "procedura.create_recipe", "arguments": arguments}]}
                with self.assertRaises(ProtocolError):
                    parse_turn(__import__("json").dumps(payload))

    def test_procedural_grid_does_not_fall_through_to_generic_scene_primitive(self):
        turn = parse_turn('{"version":1,"reply":"Creating a cube.","actions":[{"tool":"scene.create_primitive","arguments":{"primitive":"cube"}}]}')
        guarded = guard_turn_for_request("Create a grid surface.", turn)
        self.assertEqual(guarded["actions"], [])

    def test_visibility_actions_are_recovered_with_explicit_target_and_state(self):
        for prompt, expected in (("Hide /World/Proxy_2.", False),
                                 ("Make /World/Proxy_2 visible.", True),
                                 ("Toggle visibility for /World/Proxy_2 to true.", True)):
            with self.subTest(prompt=prompt):
                guarded = guard_turn_for_request(prompt, {"version": 1, "reply": "", "actions": []})
                self.assertEqual(guarded["actions"][0]["tool"], "scene.set_visibility")
                self.assertEqual(guarded["actions"][0]["arguments"],
                                 {"target": "/World/Proxy_2", "visible": expected})

    def test_explicit_delete_is_recovered_with_host_confirmation_required(self):
        guarded = guard_turn_for_request("Delete /World/Group_A.", {"version": 1, "reply": "", "actions": []})
        self.assertEqual(guarded["actions"][0], {
            "tool": "scene.delete", "arguments": {"target": "/World/Group_A"},
            "confirmation_required": True,
        })

    def test_timeline_and_viewport_actions_are_recovered_from_explicit_targets(self):
        timeline = guard_turn_for_request("Go to timeline frame 8.5.",
                                          {"version": 1, "reply": "", "actions": []})
        self.assertEqual(timeline["actions"][0]["arguments"], {"frame": 8.5})
        viewport = guard_turn_for_request("Center the orbit view on /Environment/Set.",
                                          {"version": 1, "reply": "", "actions": []})
        self.assertEqual(viewport["actions"][0]["arguments"], {"target": "/Environment/Set"})

    def test_transform_actions_are_recovered_only_with_explicit_values(self):
        cases = (
            ("Translate /Scene/Props/Box_1 to (4, 2, -3).", {"translation": [4, 2, -3]}),
            ("Scale /Scene/Block to (0.75, 1.5, 2.25).", {"scale": [0.75, 1.5, 2.25]}),
            ("Rotate /Scene/Robot around Y axis by 90 degrees.", {"rotation_degrees": [0, 90, 0]}),
            ("Smanji odabrani objekt jednoliko na 0.4.", {"scale": [0.4, 0.4, 0.4]}),
        )
        for prompt, channel in cases:
            with self.subTest(prompt=prompt):
                guarded = guard_turn_for_request(prompt, {"version": 1, "reply": "", "actions": []})
                args = guarded["actions"][0]["arguments"]
                self.assertIn("target", args)
                self.assertEqual({key: value for key, value in args.items() if key != "target"}, channel)

        ambiguous = guard_turn_for_request("Make the selected object bigger.",
                                           {"version": 1, "reply": "", "actions": []})
        self.assertEqual(ambiguous["actions"], [])

    def test_explicit_primitive_names_are_recovered_without_guessing_unnamed_labels(self):
        named = guard_turn_for_request("Stavi ravninu Floor_B na (0, -0.5, 0).",
                                       {"version": 1, "reply": "", "actions": []})
        self.assertEqual(named["actions"][0]["arguments"]["name"], "Floor_B")
        unnamed = guard_turn_for_request("Create a cube in the scene.",
                                         {"version": 1, "reply": "", "actions": []})
        self.assertNotIn("name", unnamed["actions"][0]["arguments"])

    def test_scene_context_keeps_versioned_semantic_transform(self):
        scene = sanitize_scene_context({
            "schema": "loom.scene-context", "version": 1, "frame": 12,
            "selected_path": "/Set/Hero", "entities": [{
                "path": "/Set/Hero", "name": "Hero", "type": "model",
                "parent_path": "/Set", "visible": True,
                "world_position": [1, 2, 3], "local_position": [0, 1, 0],
                "local_scale": [1, 1, 1], "local_rotation_xyzw": [0, 0, 0, 1]
            }]
        })
        self.assertEqual(scene["schema"], "loom.scene-context")
        self.assertEqual(scene["version"], 1)
        self.assertEqual(scene["entities"][0]["local_rotation_xyzw"], [0.0, 0.0, 0.0, 1.0])

    def test_scene_context_rejects_malformed_transform_vectors_and_versions(self):
        with self.assertRaises(ValueError):
            sanitize_scene_context({"schema": "loom.scene-context", "version": 2, "entities": []})
        with self.assertRaises(ValueError):
            sanitize_scene_context({"entities": [{"path": "/Hero", "local_rotation_xyzw": [0, 0, 1]}]})


if __name__ == "__main__":
    unittest.main()
