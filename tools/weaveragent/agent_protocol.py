"""Strict versioned protocol shared by dataset checks and the local agent API."""
from __future__ import annotations

import json
import math
import re
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
PROTOCOL = json.loads((ROOT / "data" / "tools.json").read_text(encoding="utf-8"))
ACTION_SPECS = {item["name"]: item for item in PROTOCOL["actions"]}
VECTOR_KEYS = {"translation", "rotation_degrees", "scale"}


class ProtocolError(ValueError):
    pass


def references_selected_entity(text: str) -> bool:
    """Recognize explicit pronouns without mistaking English prepositions for Croatian 'to'."""
    if re.search(r"\b(selected|selection|this|that|it|odabran\w*|ovo)\b|"
                 r"\bcurrent\s+(?:selection|object|entity)\b", text, re.IGNORECASE):
        return True
    croatian_action = r"(?:pomakni|premjesti|zavrti|rotiraj|skaliraj|sakrij|prikazi|prikaži|obriši|obrisi|ukloni|uokviri|fokusiraj|promijeni|povecaj|povećaj|smanji)\w*"
    return bool(re.search(r"\b" + croatian_action + r"\s+to\b|\bto\b\s+" + croatian_action,
                          text, re.IGNORECASE))


def parse_turn(text: str) -> dict[str, Any]:
    """Parse a model turn, allowing only a single JSON object (no code fences)."""
    start = text.find("{")
    end = text.rfind("}")
    if start < 0 or end < start:
        raise ProtocolError("Model output did not contain a JSON object")
    try:
        payload = json.loads(text[start:end + 1])
    except json.JSONDecodeError as exc:
        raise ProtocolError(f"Invalid JSON: {exc.msg}") from exc
    if not isinstance(payload, dict) or payload.get("version") != PROTOCOL["version"]:
        raise ProtocolError("Unsupported action protocol version")
    reply = payload.get("reply")
    actions = payload.get("actions")
    if not isinstance(reply, str) or len(reply) > 2000:
        raise ProtocolError("reply must be a string of at most 2000 characters")
    if not isinstance(actions, list) or len(actions) > 16:
        raise ProtocolError("actions must be a list containing at most 16 entries")
    normalized = []
    for index, candidate in enumerate(actions):
        if not isinstance(candidate, dict):
            raise ProtocolError(f"actions[{index}] must be an object")
        if "tool" in candidate and "name" in candidate:
            raise ProtocolError(f"actions[{index}] must use only one tool identifier key")
        name = candidate.get("tool", candidate.get("name"))
        args = candidate.get("arguments")
        if name not in ACTION_SPECS:
            raise ProtocolError(f"actions[{index}] uses unavailable mutation tool {name!r}")
        if not isinstance(args, dict):
            raise ProtocolError(f"actions[{index}].arguments must be an object")
        spec = ACTION_SPECS[name]
        allowed = set(spec["arguments"])
        required = set(spec.get("required", []))
        extra = set(args) - allowed
        missing = required - set(args)
        if extra or missing:
            raise ProtocolError(f"{name}: extra arguments {sorted(extra)}, missing {sorted(missing)}")
        _validate_arguments(name, args)
        normalized.append({"tool": name, "arguments": args,
                           "confirmation_required": bool(spec.get("confirmation", False))})
    return {"version": PROTOCOL["version"], "reply": reply, "actions": normalized}



def _requested_color(text: str) -> tuple[list[float], str] | None:
    colors = (
        (r"\bgreen\b|\bzelen\w*", [0.10, 0.78, 0.18], "green"),
        (r"\bred\b|\bcrven\w*", [0.86, 0.10, 0.08], "red"),
        (r"\bblue\b|\bplav\w*", [0.08, 0.28, 0.90], "blue"),
        (r"\byellow\b|\bžut\w*", [0.95, 0.82, 0.08], "yellow"),
        (r"\bwhite\b|\bbijel\w*", [0.92, 0.92, 0.92], "white"),
        (r"\bblack\b|\bcrn\w*", [0.06, 0.06, 0.06], "black"),
        (r"\borange\b|\bnaranč\w*", [0.95, 0.38, 0.06], "orange"),
        (r"\bpurple\b|\bljubičast\w*", [0.52, 0.18, 0.82], "purple"),
    )
    for pattern, rgb, name in colors:
        if re.search(pattern, text, re.IGNORECASE):
            return rgb, name
    return None


def _scene_origin_requested(text: str) -> bool:
    return bool(re.search(
        r"\b(?:center|centre)\s+(?:of\s+)?(?:the\s+)?(?:scene|world)\b|"
        r"\b(?:scene|world)\s+(?:center|centre|origin)\b|\bworld origin\b|"
        r"\b(?:ishodišt\w*|središtu scene|sredini scene|centru scene)\b", text, re.IGNORECASE))


def _decimal(text: str) -> str:
    return text.replace(",", ".")


_COUNT_WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5,
    "six": 6, "seven": 7, "eight": 8,
    "jedan": 1, "jedna": 1, "jedno": 1, "dva": 2, "dvije": 2,
    "tri": 3, "četiri": 4, "cetiri": 4, "pet": 5, "šest": 6,
    "sest": 6, "sedam": 7, "osam": 8,
}
_COUNT_PATTERN = r"(?:\d+|one|two|three|four|five|six|seven|eight|jedan|jedna|jedno|dva|dvije|tri|četiri|cetiri|pet|šest|sest|sedam|osam)"


def _count_value(value: str) -> int:
    return int(value) if value.isdigit() else _COUNT_WORDS[value.casefold()]


def _request_target(message: str, scene_context: dict[str, Any] | None) -> str | None:
    if references_selected_entity(message):
        return "selected"
    path = re.search(r"(?<![\w])(/[\w.-]+(?:/[\w.-]+)*)", message)
    if path:
        return path.group(1).rstrip(".,")
    matches = []
    for entity in (scene_context or {}).get("entities", []):
        name = entity.get("name", "")
        if name and re.search(r"(?<![\w])" + re.escape(name) + r"(?![\w])", message, re.IGNORECASE):
            matches.append(entity.get("path", ""))
    return matches[0] if len(set(matches)) == 1 and matches[0] else None


def _requested_primitive_name(message: str) -> str | None:
    explicit = re.search(r"\b(?:named|called|name it|nazovi|imenom)\s+([\w.-]+)",
                         message, re.IGNORECASE)
    if explicit:
        return explicit.group(1).rstrip(".,")
    label = re.search(r"\b(?:cube|kocka|kocku|plane|ravnina|ravninu)\s+([A-Z][\w.-]*|[\w.-]*[_\d][\w.-]*)\b",
                      message)
    return label.group(1).rstrip(".,") if label else None


def _explicit_transform_arguments(message: str, scene_context: dict[str, Any] | None) -> dict[str, Any] | None:
    target = _request_target(message, scene_context)
    if not target:
        return None
    wants_scale = bool(re.search(r"\b(scale|scaling|bigger|smaller|mjerilo|skaliraj|povećaj|povecaj|smanji)\b",
                                 message, re.IGNORECASE))
    wants_rotation = bool(re.search(r"\b(rotate|rotation|rotiraj|rotaci\w*|zavrti)\b", message, re.IGNORECASE) and
                          not re.search(r"\b(keep|leave|preserve|ostavi|zadrži|zadrzi)\b.{0,30}\b(rotation|rotaciju|rotacija)\b",
                                        message, re.IGNORECASE))
    args: dict[str, Any] = {"target": target}
    tuple_match = re.search(r"\(([^()]*)\)", message)
    tuple_values = []
    if tuple_match:
        tuple_values = [float(_decimal(value)) for value in re.findall(
            r"(?<![\w.])-?(?:\d+(?:[.,]\d*)?|[.,]\d+)(?![\w])", tuple_match.group(1))]
    if len(tuple_values) == 3:
        if re.search(r"\b(move|translate|position|location|pomakni|premjesti|položaj|polozaj|lokaciju)\b", message, re.IGNORECASE):
            args["translation"] = tuple_values
        if wants_rotation:
            args["rotation_degrees"] = tuple_values
        if wants_scale:
            args["scale"] = tuple_values

    if wants_scale and "scale" not in args and re.search(
            r"\b(uniform(?:ly)?|jednoliko|ravnomjerno)\b", message, re.IGNORECASE):
        scalar = re.search(r"(?<![\w.])-?(?:\d+(?:[.,]\d*)?|[.,]\d+)(?![\w])", message)
        if scalar:
            value = float(_decimal(scalar.group(0)))
            args["scale"] = [value, value, value]

    if wants_rotation and "rotation_degrees" not in args:
        axis_match = re.search(r"(?:around|about)\s+(x|y|z)(?:\s+axis)?|oko\s+(x|y|z)\s+osi",
                               message, re.IGNORECASE)
        angle_match = re.search(r"(?<![\w.])-?(?:\d+(?:[.,]\d*)?|[.,]\d+)(?![\w])\s*(?:degrees?|stupnjev\w*|stupanj\w*)?",
                                message, re.IGNORECASE)
        if axis_match and angle_match:
            axis = (axis_match.group(1) or axis_match.group(2)).lower()
            angle = float(_decimal(re.search(r"-?(?:\d+(?:[.,]\d*)?|[.,]\d+)", angle_match.group(0)).group(0)))
            args["rotation_degrees"] = [angle if component == axis else 0.0 for component in "xyz"]

    if not any(key in args for key in ("translation", "rotation_degrees", "scale")):
        return None
    return args


def _grid_recipe_arguments(text: str) -> dict[str, Any]:
    """Recover only directly labeled grid values when the model omits its tool call."""
    number = r"(-?(?:\d+(?:[.,]\d*)?|[.,]\d+))"
    args: dict[str, Any] = {"generator": "grid_surface"}
    for key, patterns in {
        "width": (rf"\bwidth\s*(?:of\s*)?{number}", rf"\bširin\w*\s+{number}",
                  rf"{number}\s*(?:m|meters?|metres?)\s+wide\b"),
        "depth": (rf"\bdepth\s*(?:of\s*)?{number}", rf"\bdubin\w*\s+{number}",
                  rf"{number}\s*(?:m|meters?|metres?)\s+deep\b"),
    }.items():
        for pattern in patterns:
            match = re.search(pattern, text, re.IGNORECASE)
            if match:
                args[key] = float(_decimal(match.group(1)))
                break

    dimensions = re.search(rf"\b{number}\s*(?:m|meters?|metres?)?\s*(?:by|x)\s*{number}\s*(?:m|meters?|metres?)?\b",
                           text, re.IGNORECASE)
    if dimensions:
        args.setdefault("width", float(_decimal(dimensions.group(1))))
        args.setdefault("depth", float(_decimal(dimensions.group(2))))

    for axis in ("x", "z"):
        patterns = (
            rf"\bcells?[_ ]{axis}\s*[:=]?\s*(\d+)\b",
            rf"\b({_COUNT_PATTERN})\s*(?:(?:cells?|ćelij\w*)\s*)?(?:along|on|po)\s*{axis}\b",
        )
        for pattern in patterns:
            match = re.search(pattern, text, re.IGNORECASE)
            if match:
                args[f"cells_{axis}"] = _count_value(match.group(1))
                break
    same_cells = re.search(rf"\b({_COUNT_PATTERN})\s*(?:cells?|ćelij\w*)\s+(?:on\s+)?(?:each|both|svakoj|svako)\s+(?:grid\s+)?axes?\b|"
                           rf"\b({_COUNT_PATTERN})\s*(?:cells?|ćelij\w*)\s+(?:po|on)\s+osi\b", text, re.IGNORECASE)
    if same_cells:
        count = _count_value(next(value for value in same_cells.groups() if value is not None))
        args.setdefault("cells_x", count)
        args.setdefault("cells_z", count)

    point = re.search(r"\b(?:column|stupac)\s*(\d+)\s*[,;]\s*(?:row|red)\s*(\d+)\b", text,
                      re.IGNORECASE)
    if not point:
        point = re.search(r"\b(?:point|točk\w*|tock\w*)\s*(?:\(|\[)\s*(\d+)\s*[,;]\s*(\d+)\s*(?:\)|\])", text,
                          re.IGNORECASE)
    if point:
        tail = text[point.end():point.end() + 100]
        height = re.search(rf"\b(?:height|visin\w*|y)\b\s*(?:to|of|at|=|:)?\s*{number}|"
                           rf"\b(?:to|at|na|do)\s+(?:y\s*)?{number}", tail, re.IGNORECASE)
        if height:
            numeric = next(value for value in height.groups() if value is not None)
            args["point_heights"] = [{"column": int(point.group(1)), "row": int(point.group(2)),
                                      "height": float(_decimal(numeric))}]
    return args


def _interior_recipe_arguments(text: str) -> dict[str, Any]:
    """Recover explicitly stated hallway dimensions, leaving other values at engine defaults."""
    number = r"(-?(?:\d+(?:[.,]\d*)?|[.,]\d+))"
    args: dict[str, Any] = {"generator": "interior_blockout"}
    patterns: dict[str, tuple[str, ...]] = {
        "rooms_per_side": (rf"\b({_COUNT_PATTERN})\s+rooms?\s+(?:per side|on each side|on both sides)\b",
                            rf"\b({_COUNT_PATTERN})\s+sob\w*\s+(?:sa\s+)?svake\s+strane\b"),
        "corridor_width": (rf"\bcorridor(?:\s+width)?\s*(?:of|is|=|:)\s*{number}",
                            rf"\bhodnik\w*\s+(?:širok\w*|širin\w*)\s*{number}",
                            rf"\b{number}[- ]m(?:eter|eters)?[- ]wide\s+corridor\b"),
        "wall_height": (rf"\bwall\s+height\s*(?:of|is|=|:)\s*{number}",
                        rf"\b{number}\s*(?:m|meters?|metres?)\s+(?:high\s+)?walls?\b",
                        rf"\b{number}[- ]m(?:eter|meters)?[- ]high\s+walls?\b",
                        rf"\bzidov\w*\s+(?:visin\w*|visok\w*)\s*{number}"),
        "room_width": (rf"\broom\s+width\s*(?:of|is|=|:)\s*{number}",
                       rf"\bširin\w*\s+sobe\s*{number}"),
        "room_depth": (rf"\broom\s+depth\s*(?:of|is|=|:)\s*{number}",
                       rf"\bdubin\w*\s+sobe\s*{number}"),
    }
    for key, alternatives in patterns.items():
        for pattern in alternatives:
            match = re.search(pattern, text, re.IGNORECASE)
            if match:
                raw = next(value for value in match.groups() if value is not None)
                args[key] = _count_value(raw) if key == "rooms_per_side" else float(_decimal(raw))
                break
    return args


def guard_turn_for_request(message: str, turn: dict[str, Any], context: str | None = None,
                           scene_context: dict[str, Any] | None = None) -> dict[str, Any]:
    """Fail closed when a valid tool call does not match user intent or the live scene."""
    text = message.casefold()
    context_text = (context or message).casefold()
    empty = lambda reply: {"version": PROTOCOL["version"], "reply": reply, "actions": []}

    action_words = r"\b(add|create|generate|build|make|draw|spawn|place|put|napravi|napraviti|stvori|izradi|generiraj|izgradi|dodaj|nacrtaj|stavi)\b"
    primitive_words = r"\b(cube|kocka|kocku|plane|ravnina|ravninu|ploha)\b"
    unsupported_words = r"\b(streets?|roads?|cities|city|buildings?|ropes?|chains?|torus|spheres?|cylinders?|ulic\w*|cest\w*|grad\w*|zgrad\w*|už\w*|lanc\w*|metallic|roughness|shader|material|materijal|metalnost|hrapavost)\b"
    if re.search(action_words, text) and re.search(unsupported_words, text):
        return empty("That operation is not exposed in this Loom agent yet, so I did not send a scene action.")

    grid_recipe_words = r"\b(grid|grid surface|lattice|surface|mesh|mrež\w*|ploha|površin\w*|povrsin\w*)\b"
    interior_recipe_words = r"\b(hallway|corridor|room(?:s)?|interior|floor ?plan|hodnik\w*|sob\w*|interijer\w*|tlocrt\w*)\b"
    wants_procedura = bool(re.search(action_words, text) and
                           (re.search(grid_recipe_words, text) or re.search(interior_recipe_words, text)))
    wants_grid_recipe = wants_procedura and bool(re.search(grid_recipe_words, text))
    wants_interior_recipe = wants_procedura and bool(re.search(interior_recipe_words, text))
    if wants_grid_recipe and wants_interior_recipe:
        return empty("Should I create a grid surface or a hallway with rooms?")

    color = _requested_color(text)
    clarification = bool(re.search(
        r"which\s+(?:cube|plane)|which\s+one\s+should\s+i\s+(?:add|create)|"
        r"koju\s+(?:kocku|ravninu)|koji\s+(?:objekt|objekat)\s+da\s+(?:dodam|napravim)",
        context_text))
    prior_create = bool(re.search(action_words, context_text) and re.search(primitive_words, context_text))
    color_followup = bool(color and len(text.split()) <= 5 and clarification and prior_create)
    intent_text = text + (" cube" if color_followup else "")

    expected: set[str] = set()
    wants_create = color_followup or bool(re.search(action_words, intent_text) and re.search(primitive_words, intent_text))
    wants_delete = bool(re.search(r"\b(delete|remove|erase|obriši|obrisi|ukloni|izbriši|izbrisi)\b", text))
    wants_visibility = bool(re.search(r"\b(hide|show|visible|visibility|invisible|sakrij|prikaži|prikazi|vidljiv\w*|nevidljiv\w*)\b", text))
    wants_rename = bool(re.search(r"\b(rename|call|preimenuj|preimenovati|nazovi|promijeni ime|promijeni naziv)\b", text))
    wants_timeline = bool(re.search(r"\b(timeline|playhead|glavu timelinea)\b", text) or
                          (re.search(r"\b(frame|kadar)\b", text) and
                           re.search(r"\b(to|at|na|go|move|pomakni|premjesti)\b", text) and
                           re.search(r"\d", text) and "viewport" not in text))
    wants_frame = bool(re.search(
        r"\b(frame|focus|uokviri|fokusiraj|fokus)\b|"
        r"\bcenter\s+(?:on\s+)?(?:the\s+)?(?:selected|object|entity|it)\b|"
        r"\b(?:center|centre)\s+(?:the\s+)?(?:orbit\s+)?view\b", text)) and not wants_timeline
    wants_list = bool(re.search(r"\b(list|objects|entities|objekata|objekte|postoji|children)\b", text))
    if wants_list and re.search(r"\b(show|prikaži|prikazi)\b", text):
        wants_visibility = False
    wants_translation = bool(re.search(r"\b(move|translate|position|location|pomakni|premjesti|položaj|polozaj|lokaciju)\b", text)) and not wants_timeline
    wants_rotation = bool(re.search(r"\b(rotate|rotation|rotiraj|rotaci\w*|zavrti)\b", text) and
                          not re.search(r"\b(keep|leave|preserve|ostavi|zadrži|zadrzi)\b.{0,30}\b(rotation|rotaciju|rotacija)\b", text))
    wants_scale = bool(re.search(r"\b(scale|scaling|bigger|smaller|mjerilo|skaliraj|povećaj|povecaj|smanji)\b", text))

    if wants_create:
        expected.add("scene.create_primitive")
    if wants_procedura:
        expected.add("procedura.create_recipe")
    if wants_delete:
        expected.add("scene.delete")
    if wants_visibility:
        expected.add("scene.set_visibility")
    if wants_rename:
        expected.add("scene.rename")
    if wants_timeline:
        expected.add("timeline.set_playhead")
    if wants_frame:
        expected.add("viewport.frame_entity")
    if wants_list and not expected:
        expected.add("scene.list_entities")
    if wants_translation or wants_rotation or wants_scale:
        expected.add("scene.set_transform")

    actions = turn.get("actions", [])
    if actions and not expected:
        return empty("I couldn't identify a supported Loom action in that request. Please clarify what you want changed.")
    if not actions and wants_procedura:
        args = (_grid_recipe_arguments(message) if wants_grid_recipe
                else _interior_recipe_arguments(message))
        name_match = re.search(r"\b(?:named|called|name it|nazovi|imenom)\s+([\w.-]+)", message, re.IGNORECASE)
        if name_match:
            args["name"] = name_match.group(1).rstrip(".,")
        try:
            _validate_arguments("procedura.create_recipe", args)
        except ProtocolError:
            return empty("I couldn't safely apply those Procedura values. Check the grid points and dimensions, then try again.")
        actions = [{"tool": "procedura.create_recipe", "arguments": args,
                    "confirmation_required": False}]
        label = "grid surface" if wants_grid_recipe else "interior blockout"
        turn = {**turn, "actions": actions, "reply": f"Creating a {label} Recipe."}
    elif not actions and wants_create:
        primitive = "cube" if re.search(r"\b(cube|kocka|kocku)\b", intent_text) else "plane"
        args: dict[str, Any] = {"primitive": primitive}
        requested_name = _requested_primitive_name(message)
        if requested_name:
            args["name"] = requested_name
        if color:
            args["color"] = color[0]
        location_text = context_text if color_followup else text
        if _scene_origin_requested(location_text):
            args["translation"] = [0.0, 0.0, 0.0]
        actions = [{"tool": "scene.create_primitive", "arguments": args,
                    "confirmation_required": False}]
        color_label = f" {color[1]}" if color else ""
        primitive_label = "cube" if primitive == "cube" else "plane"
        turn = {**turn, "actions": actions,
                "reply": f"Creating a{color_label} {primitive_label} at the scene origin." if _scene_origin_requested(location_text)
                else f"Creating a{color_label} {primitive_label}."}
    elif not actions and expected == {"scene.list_entities"}:
        args = {}
        parent_match = re.search(r"(?:under|beneath|children of|ispod|djeca od)\s+(/[\w./-]+)", message, re.IGNORECASE)
        if parent_match:
            args["parent"] = parent_match.group(1).rstrip(".,")
        actions = [{"tool": "scene.list_entities", "arguments": args, "confirmation_required": False}]
        turn = {**turn, "actions": actions,
                "reply": "Reading the live Loom scene contents."}
    elif not actions and expected == {"scene.set_transform"}:
        args = _explicit_transform_arguments(message, scene_context)
        if args:
            try:
                _validate_arguments("scene.set_transform", args)
            except ProtocolError:
                return empty("I couldn't safely apply those transform values. Check the target and values, then try again.")
            actions = [{"tool": "scene.set_transform", "arguments": args,
                        "confirmation_required": False}]
            turn = {**turn, "actions": actions, "reply": "Applying the explicitly requested transform."}
        else:
            return empty("Which object should I change, and what exact transform value should I use?")
    elif not actions and expected == {"scene.set_visibility"}:
        target = _request_target(message, scene_context)
        explicit_state = re.search(r"\b(?:to|as|=|:)\s*(true|false)\b", message, re.IGNORECASE)
        if explicit_state:
            visible = explicit_state.group(1).casefold() == "true"
        elif re.search(r"\b(hide|invisible|off|sakrij|nevidljiv\w*)\b", message, re.IGNORECASE):
            visible = False
        elif re.search(r"\b(show|visible|on|prikaži|prikazi|vidljiv\w*)\b", message, re.IGNORECASE):
            visible = True
        else:
            visible = None
        if target and visible is not None:
            actions = [{"tool": "scene.set_visibility", "arguments": {"target": target, "visible": visible},
                        "confirmation_required": False}]
            turn = {**turn, "actions": actions,
                    "reply": f"Showing {target}." if visible else f"Hiding {target}."}
        else:
            return empty("Which object should I show or hide?")
    elif not actions and expected == {"scene.delete"}:
        target = _request_target(message, scene_context)
        if target:
            actions = [{"tool": "scene.delete", "arguments": {"target": target},
                        "confirmation_required": True}]
            turn = {**turn, "actions": actions,
                    "reply": f"Deleting {target} requires confirmation because its children will also be removed."}
        else:
            return empty("Which scene object should I delete? Nothing was removed.")
    elif not actions and expected == {"timeline.set_playhead"}:
        frame_match = re.search(r"\b(?:frame|kadar)\s*(-?(?:\d+(?:\.\d*)?|\.\d+))", message, re.IGNORECASE)
        if frame_match:
            frame = float(frame_match.group(1))
            try:
                _validate_arguments("timeline.set_playhead", {"frame": frame})
            except ProtocolError:
                return empty("That frame value is outside Loom's supported range.")
            actions = [{"tool": "timeline.set_playhead", "arguments": {"frame": frame},
                        "confirmation_required": False}]
            turn = {**turn, "actions": actions, "reply": f"Moving the playhead to frame {frame:g}."}
        else:
            return empty("Which frame should I move the playhead to?")
    elif not actions and expected == {"viewport.frame_entity"}:
        target = _request_target(message, scene_context)
        if target:
            actions = [{"tool": "viewport.frame_entity", "arguments": {"target": target},
                        "confirmation_required": False}]
            turn = {**turn, "actions": actions, "reply": f"Framing {target} in the viewport."}
        else:
            return empty("Which live scene object should I frame in the viewport?")
    elif not actions and "scene.rename" in expected:
        target = _request_target(message, scene_context)
        new_name_match = re.search(r"\b(?:as|to|u|na)\s+([a-z_][\w.-]*)", message, re.IGNORECASE)
        call_match = re.search(r"\bcall\s+/[\w./-]+\s+([a-z_][\w.-]*)", message, re.IGNORECASE)
        new_name = (call_match.group(1) if call_match else new_name_match.group(1) if new_name_match else None)
        if target and new_name:
            target_path = target.rstrip(".,")
            new_name = new_name.rstrip(".,")
            actions = [{"tool": "scene.rename", "arguments": {"target": target_path,
                        "name": new_name}, "confirmation_required": False}]
            turn = {**turn, "actions": actions, "reply": f"Renaming it to {new_name}."}

    actual_tools = {action["tool"] for action in actions}
    if actions and not actual_tools.issubset(expected):
        filtered = [action for action in actions if action["tool"] in expected]
        if not filtered:
            return empty("I didn't send an action because it did not match the requested operation.")
        turn = {**turn, "actions": filtered}
        actions = filtered
    if expected and not actions and turn.get("actions") == []:
        return turn

    live_entities = (scene_context or {}).get("entities", [])
    live_paths = {item.get("path", ""): item for item in live_entities}
    user_paths = {path.casefold(): path for path in re.findall(r"(?<![\w])(/[\w.-]+(?:/[\w.-]+)*)", message)}
    selected_path = (scene_context or {}).get("selected_path")
    if selected_path and selected_path not in live_paths:
        live_paths[selected_path] = {"path": selected_path, "name": selected_path.rstrip("/").rsplit("/", 1)[-1]}
    names: dict[str, list[str]] = {}
    for path, entity in live_paths.items():
        name = entity.get("name")
        if isinstance(name, str):
            names.setdefault(name.casefold(), []).append(path)

    numbers = {float(value) for value in re.findall(r"(?<![\w.])-?(?:\d+(?:\.\d*)?|\.\d+)(?![\w])", text)}
    for item in actions:
        tool, args = item["tool"], item["arguments"]
        if tool == "scene.list_entities" and isinstance(args.get("parent"), str):
            parent = args["parent"]
            exact = next((path for path in live_paths if path.casefold() == parent.casefold()), None)
            if exact is None:
                exact = user_paths.get(parent.casefold())
            if exact:
                args["parent"] = exact
        target = args.get("target")
        if isinstance(target, str):
            if target != "selected" and target.rstrip("/").rsplit("/", 1)[-1].casefold() == "selected":
                args["target"] = target = "selected"
            if target == "selected":
                if not references_selected_entity(message):
                    return empty("Which scene entity do you mean? I didn't change anything.")
                if scene_context is not None and not selected_path:
                    return empty("There is no selected scene object to change.")
            elif scene_context is not None:
                if target not in live_paths:
                    matches = names.get(target.casefold(), [])
                    if len(matches) == 1:
                        args["target"] = target = matches[0]
                    elif len(matches) > 1:
                        return empty("That name matches more than one live scene object. Please specify its full scene path.")
                    else:
                        return empty("I couldn't find that target in the current scene, so I didn't change anything.")
            else:
                leaf = target.rstrip("/").rsplit("/", 1)[-1].casefold()
                if leaf not in text and leaf not in context_text:
                    return empty("I couldn't verify the target from your message, so I didn't send the action.")
        vector_match = re.search(r"\(([^()]*)\)", text)
        tuple_values = ([float(value) for value in re.findall(r"(?<![\w.])-?(?:\d+(?:\.\d*)?|\.\d+)(?![\w])", vector_match.group(1))]
                        if vector_match else [])
        if tool == "scene.create_primitive":
            primitive = args.get("primitive")
            aliases = {"cube": ("cube", "kocka", "kocku"), "plane": ("plane", "ravnina", "ravninu", "ploha")}
            if primitive not in aliases or not any(word in intent_text for word in aliases[primitive]):
                return empty("Please specify a supported cube or plane before I create an object.")
            if color:
                args["color"] = color[0]
            requested_name = _requested_primitive_name(message)
            if requested_name:
                args["name"] = requested_name
            name = args.get("name", "").casefold()
            generic_names = {"cube", "kocka", "plane", "ravnina"}
            if name and name not in text and name not in generic_names and not (color and color[1] in name):
                explicit_name = re.search(r"\b(?:named|called|name it|nazovi|imenom)\s+[\w.-]+", message, re.IGNORECASE)
                if explicit_name:
                    return empty("I couldn't verify the requested object name, so I didn't create it.")
                args.pop("name", None)
            if "translation" not in args and tuple_values and len(tuple_values) == 3:
                args["translation"] = tuple_values
            if "translation" not in args and _scene_origin_requested(text if not color_followup else context_text):
                args["translation"] = [0.0, 0.0, 0.0]
            if "translation" in args and any(float(v) not in numbers for v in args["translation"]):
                if args["translation"] != [0.0, 0.0, 0.0] or not _scene_origin_requested(text if not color_followup else context_text):
                    return empty("I couldn't verify the requested position, so I didn't create the object.")
        if tool == "scene.set_transform":
            channels = {"translation": wants_translation, "rotation_degrees": wants_rotation, "scale": wants_scale}
            if not any(channels.get(key, False) for key in ("translation", "rotation_degrees", "scale") if key in args):
                return empty("I didn't change the object because the requested transform channel was unclear.")
            if len(tuple_values) == 3:
                for key, wanted in channels.items():
                    if wanted:
                        args[key] = tuple_values
            elif wants_scale and re.search(r"\b(uniform|uniformly|jednoliko|ravnomjerno)\b", text) and len(numbers) == 1:
                value = next(iter(numbers))
                args["scale"] = [value, value, value]
            for key, wanted in channels.items():
                if key in args and not wanted:
                    del args[key]
            axis_match = re.search(r"(?:around|about)\s+(x|y|z)(?:\s+axis)?|oko\s+(x|y|z)\s+osi", text)
            if "rotation_degrees" in args and axis_match:
                axis = (axis_match.group(1) or axis_match.group(2)).lower()
                requested = [float(v) for v in re.findall(r"(?<![\w.])-?(?:\d+(?:\.\d*)?|\.\d+)(?![\w])", text)]
                if len(requested) == 1:
                    value = requested[0]
                    args["rotation_degrees"] = [value if name == axis else 0.0 for name in "xyz"]
                    numbers.add(0.0)
            for key in ("translation", "rotation_degrees", "scale"):
                if key in args and any(float(v) not in numbers for v in args[key]):
                    return empty("I need explicit transform values before I can safely change the object.")
        if tool == "scene.set_visibility":
            explicit_visibility = re.search(r"\b(?:to|as|=|:)\s*(true|false)\b", message, re.IGNORECASE)
            if explicit_visibility:
                args["visible"] = explicit_visibility.group(1).casefold() == "true"
            elif re.search(r"\b(hide|invisible|off|sakrij|nevidljiv\w*)\b", message, re.IGNORECASE):
                args["visible"] = False
            elif re.search(r"\b(show|visible|on|prikaži|prikazi|vidljiv\w*)\b", message, re.IGNORECASE):
                args["visible"] = True
        if tool == "timeline.set_playhead":
            frame_match = re.search(r"\b(?:frame|kadar)\s+(-?(?:\d+(?:\.\d*)?|\.\d+))", text)
            if frame_match:
                args["frame"] = float(frame_match.group(1))
            elif float(args["frame"]) not in numbers:
                return empty("Please specify a frame number before moving the playhead.")
        if tool == "procedura.create_recipe":
            generator = args.get("generator")
            if generator == "grid_surface" and not wants_grid_recipe:
                return empty("I didn't create a Recipe because the request doesn't specify a grid surface.")
            if generator == "interior_blockout" and not wants_interior_recipe:
                return empty("I didn't create a Recipe because the request doesn't specify a hallway or room blockout.")
            if generator == "grid_surface" and wants_grid_recipe:
                explicit = _grid_recipe_arguments(message)
                for key in ("width", "depth", "cells_x", "cells_z", "point_heights"):
                    if key in explicit:
                        args[key] = explicit[key]
                    else:
                        args.pop(key, None)
            elif generator == "interior_blockout" and wants_interior_recipe:
                explicit = _interior_recipe_arguments(message)
                for key in ("rooms_per_side", "room_width", "room_depth", "corridor_width",
                            "wall_height", "wall_thickness", "floor_thickness", "door_width"):
                    if key in explicit:
                        args[key] = explicit[key]
                    else:
                        args.pop(key, None)
            recipe_name = args.get("name")
            if recipe_name and recipe_name.casefold() not in text:
                named_request = re.search(r"\b(?:named|called|name it|nazovi|imenom)\s+([\w .-]+)", message, re.IGNORECASE)
                if named_request:
                    return empty("I couldn't verify the requested Recipe name, so I didn't create it.")
                args.pop("name", None)
    return turn

def _finite_vector(value: Any, key: str, *, positive: bool = False) -> None:
    if not isinstance(value, list) or len(value) != 3:
        raise ProtocolError(f"{key} must be an array of exactly three numbers")
    for item in value:
        if isinstance(item, bool) or not isinstance(item, (int, float)) or not math.isfinite(item):
            raise ProtocolError(f"{key} entries must be finite numbers")
        if positive and item <= 0:
            raise ProtocolError(f"{key} entries must be positive")


def _validate_arguments(name: str, args: dict[str, Any]) -> None:
    if name == "scene.list_entities":
        if args.get("parent") is not None:
            _bounded_string(args["parent"], "parent", 1, 240)
    elif name == "scene.create_primitive":
        if args["primitive"] not in {"cube", "plane"}:
            raise ProtocolError("primitive must be cube or plane")
        if args.get("name") is not None:
            _bounded_string(args["name"], "name", 1, 80)
        if args.get("parent") is not None:
            _bounded_string(args["parent"], "parent", 1, 240)
        if "translation" in args:
            _finite_vector(args["translation"], "translation")
        if "color" in args:
            _finite_vector(args["color"], "color")
            if any(value < 0 or value > 1 for value in args["color"]):
                raise ProtocolError("color channels must be from 0 to 1")
    elif name == "scene.set_transform":
        _bounded_string(args["target"], "target", 1, 240)
        present = VECTOR_KEYS.intersection(args)
        if not present:
            raise ProtocolError("scene.set_transform needs at least one transform channel")
        for key in present:
            _finite_vector(args[key], key, positive=(key == "scale"))
        if any(abs(float(v)) > 1e6 for key in present for v in args[key]):
            raise ProtocolError("transform values exceed the safe range")
    elif name == "scene.rename":
        _bounded_string(args["target"], "target", 1, 240)
        _bounded_string(args["name"], "name", 1, 80)
    elif name == "scene.set_visibility":
        _bounded_string(args["target"], "target", 1, 240)
        if not isinstance(args["visible"], bool):
            raise ProtocolError("visible must be boolean")
    elif name == "scene.delete" or name == "viewport.frame_entity":
        _bounded_string(args["target"], "target", 1, 240)
    elif name == "timeline.set_playhead":
        value = args["frame"]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ProtocolError("frame must be a finite number")
        if not 0 <= value <= 10_000_000:
            raise ProtocolError("frame is outside the supported range")
    elif name == "procedura.create_recipe":
        _bounded_string(args["generator"], "generator", 1, 40)
        generator = args["generator"]
        if generator not in {"grid_surface", "interior_blockout"}:
            raise ProtocolError("generator must be grid_surface or interior_blockout")
        if "name" in args:
            _bounded_string(args["name"], "name", 1, 120)
        grid_keys = {"width", "depth", "cells_x", "cells_z", "point_heights"}
        interior_keys = {"rooms_per_side", "room_width", "room_depth", "corridor_width",
                         "wall_height", "wall_thickness", "floor_thickness", "door_width"}
        allowed = grid_keys if generator == "grid_surface" else interior_keys
        unexpected = (grid_keys | interior_keys) - allowed
        if unexpected.intersection(args):
            raise ProtocolError(f"{generator} does not accept {sorted(unexpected.intersection(args))}")
        if generator == "grid_surface":
            for key in ("width", "depth"):
                if key in args:
                    _bounded_number(args[key], key, 0, 10000, exclusive_low=True)
            for key in ("cells_x", "cells_z"):
                if key in args:
                    value = args[key]
                    if isinstance(value, bool) or not isinstance(value, int) or not 1 <= value <= 128:
                        raise ProtocolError(f"{key} must be an integer from 1 to 128")
            cells_x, cells_z = args.get("cells_x", 4), args.get("cells_z", 4)
            if (cells_x + 1) * (cells_z + 1) > 16384:
                raise ProtocolError("grid exceeds the point-count limit")
            edits = args.get("point_heights", [])
            if not isinstance(edits, list) or len(edits) > 64:
                raise ProtocolError("point_heights must contain at most 64 entries")
            seen_points = set()
            for edit in edits:
                if not isinstance(edit, dict) or set(edit) != {"column", "row", "height"}:
                    raise ProtocolError("each point height needs only column, row, and height")
                column, row = edit["column"], edit["row"]
                if isinstance(column, bool) or not isinstance(column, int) or not 0 <= column <= cells_x:
                    raise ProtocolError("point-height column is outside the grid")
                if isinstance(row, bool) or not isinstance(row, int) or not 0 <= row <= cells_z:
                    raise ProtocolError("point-height row is outside the grid")
                if (column, row) in seen_points:
                    raise ProtocolError("point_heights cannot edit the same grid point twice")
                seen_points.add((column, row))
                _bounded_number(edit["height"], "point height", -10000, 10000)
        else:
            if "rooms_per_side" in args:
                value = args["rooms_per_side"]
                if isinstance(value, bool) or not isinstance(value, int) or not 1 <= value <= 8:
                    raise ProtocolError("rooms_per_side must be an integer from 1 to 8")
            for key in ("room_width", "room_depth", "corridor_width", "wall_height",
                        "wall_thickness", "floor_thickness", "door_width"):
                if key in args:
                    _bounded_number(args[key], key, 0, 1000, exclusive_low=True)
            room_width = args.get("room_width", 4.0)
            room_depth = args.get("room_depth", 4.0)
            corridor_width = args.get("corridor_width", 2.0)
            wall_height = args.get("wall_height", 3.0)
            wall_thickness = args.get("wall_thickness", 0.15)
            floor_thickness = args.get("floor_thickness", 0.12)
            door_width = args.get("door_width", 0.9)
            if door_width >= room_width - 2.0 * wall_thickness:
                raise ProtocolError("door_width must leave room for the side walls")
            if 2.0 * wall_thickness >= min(room_depth, corridor_width):
                raise ProtocolError("wall_thickness is too large for the room or corridor")
            if floor_thickness >= wall_height:
                raise ProtocolError("floor_thickness must be lower than wall_height")
    else:
        raise ProtocolError(f"No validator exists for {name}")


def _bounded_string(value: Any, field: str, low: int, high: int) -> None:
    if not isinstance(value, str) or not low <= len(value.strip()) <= high:
        raise ProtocolError(f"{field} must be a string of {low}–{high} characters")


def _bounded_number(value: Any, field: str, low: float, high: float, *, exclusive_low: bool = False) -> None:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ProtocolError(f"{field} must be a finite number")
    if (value <= low if exclusive_low else value < low) or value > high:
        relation = "greater than" if exclusive_low else "at least"
        raise ProtocolError(f"{field} must be {relation} {low} and at most {high}")
