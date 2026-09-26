"""Machine-readable WeaverProcedura term resolver and vocabulary integrity checks."""
from __future__ import annotations

import json
import re
import unicodedata
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
DEFAULT_VOCABULARY = ROOT / "data/procedura_vocabulary_v3.json"


def normalize(text: str) -> str:
    decomposed = unicodedata.normalize("NFKD", text.casefold())
    plain = "".join(ch for ch in decomposed if not unicodedata.combining(ch))
    return re.sub(r"[^\w]+", " ", plain, flags=re.UNICODE).strip()


def load_vocabulary(path: Path = DEFAULT_VOCABULARY) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    validate_vocabulary(data)
    return data


def validate_vocabulary(data: dict[str, Any]) -> None:
    if data.get("vocabulary") != "loom-weaverprocedura" or data.get("version") not in {1, 2, 3}:
        raise ValueError("unsupported WeaverProcedura vocabulary version")
    entries = data.get("entries")
    if not isinstance(entries, list) or not entries:
        raise ValueError("vocabulary entries must be a non-empty list")
    ids: set[str] = set()
    aliases: dict[str, str] = {}
    for entry in entries:
        concept_id = entry.get("id")
        if not isinstance(concept_id, str) or not concept_id or concept_id in ids:
            raise ValueError(f"concept IDs must be non-empty and unique: {concept_id!r}")
        ids.add(concept_id)
        if entry.get("status") not in {"implemented", "composed_preview_only", "stored_not_evaluated", "reserved_not_exposed"}:
            raise ValueError(f"{concept_id} has an unknown implementation status")
        for language in ("hr", "en"):
            values = [entry.get(f"term_{language}", ""), *entry.get(f"aliases_{language}", [])]
            if len(values) < 2 or not all(isinstance(value, str) and value.strip() for value in values):
                raise ValueError(f"{concept_id} needs a canonical {language} term and aliases")
            for value in values:
                normalized = normalize(value)
                if not normalized:
                    raise ValueError(f"{concept_id} contains an empty normalized alias")
                owner = aliases.get(normalized)
                if owner is not None and owner != concept_id:
                    raise ValueError(f"alias collision between {owner} and {concept_id}: {value!r}")
                aliases[normalized] = concept_id
        cases = entry.get("test_cases")
        if not isinstance(cases, list) or len(cases) < 2:
            raise ValueError(f"{concept_id} must have at least one test in Croatian and English")
        case_ids: set[str] = set()
        languages: set[str] = set()
        for case in cases:
            if case.get("id") in case_ids:
                raise ValueError(f"{concept_id} has duplicate test IDs")
            case_ids.add(case.get("id"))
            languages.add(case.get("language"))
            if not isinstance(case.get("prompt"), str) or not case["prompt"].strip():
                raise ValueError(f"{concept_id} has an empty test prompt")
            groups = case.get("must_include_any")
            if not isinstance(groups, list) or not groups or any(
                not isinstance(group, list) or not group or
                not all(isinstance(item, str) and item.strip() for item in group)
                for group in groups
            ):
                raise ValueError(f"{concept_id} test must declare semantic fact groups")
            forbidden = case.get("must_not_include_any", [])
            if not isinstance(forbidden, list) or any(
                not isinstance(group, list) or not group or
                not all(isinstance(item, str) and item.strip() for item in group)
                for group in forbidden
            ):
                raise ValueError(f"{concept_id} has invalid forbidden-claim groups")
        if not {"hr", "en"}.issubset(languages):
            raise ValueError(f"{concept_id} must have Croatian and English model tests")


def resolve_terms(text: str, data: dict[str, Any] | None = None) -> list[str]:
    """Resolve longest, non-overlapping vocabulary phrases to concept IDs."""
    data = data or load_vocabulary()
    normalized_text = normalize(text)
    if not normalized_text:
        return []
    candidates: list[tuple[int, int, str, str]] = []
    for entry in data["entries"]:
        for language in ("hr", "en"):
            for value in [entry[f"term_{language}"], *entry[f"aliases_{language}"]]:
                alias = normalize(value)
                pattern = re.compile(r"(?<!\w)" + re.escape(alias) + r"(?!\w)", re.UNICODE)
                candidates.extend((match.start(), match.end(), entry["id"], alias)
                                  for match in pattern.finditer(normalized_text))
    candidates.sort(key=lambda item: (-(item[1] - item[0]), item[0], item[2], item[3]))
    accepted: list[tuple[int, int, str]] = []
    for start, end, concept_id, _ in candidates:
        if any(start < old_end and old_start < end for old_start, old_end, _ in accepted):
            continue
        accepted.append((start, end, concept_id))
    accepted.sort(key=lambda item: (item[0], item[1]))
    result: list[str] = []
    for _, _, concept_id in accepted:
        if concept_id not in result:
            result.append(concept_id)
    return result


def alias_inventory(data: dict[str, Any] | None = None) -> list[tuple[str, str, str]]:
    data = data or load_vocabulary()
    rows = []
    for entry in data["entries"]:
        for language in ("hr", "en"):
            for alias in [entry[f"term_{language}"], *entry[f"aliases_{language}"]]:
                rows.append((entry["id"], language, alias))
    return rows
