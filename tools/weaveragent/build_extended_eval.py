#!/usr/bin/env python3
"""Create an additional held-out protocol/generalization set, never used for training."""
from __future__ import annotations

import json
from pathlib import Path

from build_dataset import DATA, action, sample, target


CASES = [
    ("Put a cube called Crate_01 at (2.25, 0, -1.5).", "Creating Crate_01.", [action("scene.create_primitive", primitive="cube", name="Crate_01", translation=[2.25, 0, -1.5])]),
    ("Napravi ravninu StageFloor na koordinati (0, -0.2, 3).", "Kreiram StageFloor.", [action("scene.create_primitive", primitive="plane", name="StageFloor", translation=[0, -0.2, 3])]),
    ("Add a cube named LightBlock.", "Adding LightBlock.", [action("scene.create_primitive", primitive="cube", name="LightBlock")]),
    ("Stvori kocku Podium bez pomaka.", "Dodajem Podium.", [action("scene.create_primitive", primitive="cube", name="Podium")]),
    ("Create an XZ plane called ShadowFloor at (0, 0, 0).", "Creating ShadowFloor.", [action("scene.create_primitive", primitive="plane", name="ShadowFloor", translation=[0, 0, 0])]),
    ("Translate /Scene/Props/Box_1 to (4, 2, -3).", "Moving /Scene/Props/Box_1.", [action("scene.set_transform", target="/Scene/Props/Box_1", translation=[4, 2, -3])]),
    ("Postavi lokaciju odabranog na (-1.5, 0.25, 8).", "Postavljam položaj selected.", [action("scene.set_transform", target="selected", translation=[-1.5, 0.25, 8])]),
    ("Rotate /Scene/Robot to (0, 45, 0) degrees.", "Setting rotation for /Scene/Robot.", [action("scene.set_transform", target="/Scene/Robot", rotation_degrees=[0, 45, 0])]),
    ("Postavi rotaciju /Scene/Robot na (-15, 0, 90) stupnjeva.", "Mijenjam rotaciju /Scene/Robot.", [action("scene.set_transform", target="/Scene/Robot", rotation_degrees=[-15, 0, 90])]),
    ("Scale /Scene/Block to (0.75, 1.5, 2.25).", "Setting scale for /Scene/Block.", [action("scene.set_transform", target="/Scene/Block", scale=[0.75, 1.5, 2.25])]),
    ("Smanji odabrani objekt jednoliko na 0.4.", "Skaliram selected.", [action("scene.set_transform", target="selected", scale=[0.4, 0.4, 0.4])]),
    ("Move /Scene/Robot to (1, 2, 3) and keep its current rotation.", "Updating /Scene/Robot's position.", [action("scene.set_transform", target="/Scene/Robot", translation=[1, 2, 3])]),
    ("Give /Scene/Robot a scale of (1, 1, 3) and leave its other channels alone.", "Setting scale for /Scene/Robot.", [action("scene.set_transform", target="/Scene/Robot", scale=[1, 1, 3])]),
    ("Rename /Environment/WallOld as Wall_Main.", "Renaming it to Wall_Main.", [action("scene.rename", target="/Environment/WallOld", name="Wall_Main")]),
    ("Promijeni naziv selected u Marker_3.", "Mijenjam ime u Marker_3.", [action("scene.rename", target="selected", name="Marker_3")]),
    ("Call /Props/Cube_1 HeroProp.", "Changing its name to HeroProp.", [action("scene.rename", target="/Props/Cube_1", name="HeroProp")]),
    ("Hide /World/Proxy_2.", "Hiding /World/Proxy_2.", [action("scene.set_visibility", target="/World/Proxy_2", visible=False)]),
    ("Učini /World/Proxy_2 ponovno vidljivim.", "Prikazat ću /World/Proxy_2.", [action("scene.set_visibility", target="/World/Proxy_2", visible=True)]),
    ("Turn visibility off for selected.", "Hiding selected.", [action("scene.set_visibility", target="selected", visible=False)]),
    ("Make /Scene/Camera_Block visible.", "Showing /Scene/Camera_Block.", [action("scene.set_visibility", target="/Scene/Camera_Block", visible=True)]),
    ("Delete /World/Group_A.", "I need confirmation before deleting /World/Group_A.", [action("scene.delete", target="/World/Group_A")]),
    ("Ukloni selected iz scene.", "Brisanje selected traži potvrdu.", [action("scene.delete", target="selected")]),
    ("Remove /Props/OldSign and its children.", "Deleting /Props/OldSign requires confirmation.", [action("scene.delete", target="/Props/OldSign")]),
    ("Obriši /Props/OldSign zajedno s djecom.", "Brisanje /Props/OldSign traži potvrdu.", [action("scene.delete", target="/Props/OldSign")]),
    ("Take the playhead to frame 57.", "Going to frame 57.", [action("timeline.set_playhead", frame=57)]),
    ("Premjesti playhead na frame 139.", "Idem na kadar 139.", [action("timeline.set_playhead", frame=139)]),
    ("Go to timeline frame 8.5.", "Moving the playhead to frame 8.5.", [action("timeline.set_playhead", frame=8.5)]),
    ("Postavi glavu timelinea na kadar 201.", "Premještam playhead na kadar 201.", [action("timeline.set_playhead", frame=201)]),
    ("Frame /World/Car in the viewport.", "Framing /World/Car in the viewport.", [action("viewport.frame_entity", target="/World/Car")]),
    ("Uokviri odabrani objekt.", "Uokvirujem odabrani objekt.", [action("viewport.frame_entity", target="selected")]),
    ("Center the orbit view on /Environment/Set.", "Focusing the viewport on /Environment/Set.", [action("viewport.frame_entity", target="/Environment/Set")]),
    ("Fokusiraj viewport na /Scene/Camera.", "Fokusiram viewport na /Scene/Camera.", [action("viewport.frame_entity", target="/Scene/Camera")]),
    ("List the live scene objects.", "Reading the live scene entity list.", [action("scene.list_entities")]),
    ("Prikaži mi objekte u sceni.", "Čitam trenutni popis objekata iz scene.", [action("scene.list_entities")]),
    ("List the children under /Environment.", "Listing entities under /Environment.", [action("scene.list_entities", parent="/Environment")]),
    ("Which entities are children of /Props?", "Reading the live entities under /Props.", [action("scene.list_entities", parent="/Props")]),
    ("Generate a rope between these two points.", "Rope generation is not exposed in the current Loom action API. I will not send an unsupported action.", []),
    ("Add a torus primitive.", "The current Loom action API exposes cubes and XZ planes, but not a torus.", []),
    ("Pomakni objekt.", "Koji objekt želiš pomaknuti i na koju poziciju?", []),
    ("Make this one bigger.", "Which scene entity should I scale, and what scale should I use?", []),
    ("Increase the metallic value of /Scene/Hero.", "The current Loom action API does not expose material editing yet, so I will not send an unsupported action.", []),
    ("Create a whole city with streets and buildings.", "Procedura generation is not exposed in this first Loom agent API. I can create cubes and planes until the Procedura toolset is trained and connected.", []),
]


def main() -> int:
    path = DATA / "eval_extended.jsonl"
    path.parent.mkdir(parents=True, exist_ok=True)
    seen = set()
    rows = []
    for user, reply, actions in CASES:
        normalized = " ".join(user.casefold().split())
        if normalized in seen:
            raise ValueError(f"duplicate eval prompt: {user}")
        seen.add(normalized)
        rows.append(sample(user, target(reply, *actions)))
    path.write_text("".join(json.dumps(row, ensure_ascii=False) + "\n" for row in rows), encoding="utf-8")
    print(f"Wrote {len(rows)} extended held-out examples to {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
