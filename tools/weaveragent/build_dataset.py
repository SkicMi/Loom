#!/usr/bin/env python3
"""Build a small, auditable Loom tool-use set from hand-written templates.

The templates are intentionally explicit: no generated sample may claim a command
ran, and every mutation maps to an operation in data/tools.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA_ROOT = ROOT / "data"
DATA = DATA_ROOT / "v1"
SYSTEM = (ROOT / "data" / "system_prompt.txt").read_text(encoding="utf-8").strip()
SEED = 5070


def action(tool_name: str, **arguments: object) -> dict:
    return {"tool": tool_name, "arguments": arguments}


def target(reply: str, *actions: dict) -> str:
    return json.dumps({"version": 1, "reply": reply, "actions": list(actions)},
                      ensure_ascii=False, separators=(",", ":"))


def sample(user: str, answer: str) -> dict:
    return {"messages": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": user},
        {"role": "assistant", "content": answer},
    ]}


def training_examples() -> list[dict]:
    rng = random.Random(SEED)
    examples: list[dict] = []
    names = ["Cube", "Kocka", "Hero", "Main_Block", "Pod", "Marker", "Blockout", "Wall_A",
             "Floor", "Proxy", "Reference", "Anchor", "Pillar", "Base", "Camera_Block"]
    values = [-4.0, -2.5, -1.0, -0.25, 0.0, 0.4, 1.0, 1.8, 3.0, 5.5, 8.0]

    # Primitive creation examples reflect Warp::Stage and Loom's current viewport meshes.
    create_templates = [
        ("hr", "Dodaj {prim} pod imenom {name} na {xyz}.", "Dodajem {name}."),
        ("hr", "Stavi {prim} {name} na {xyz}.", "Dodajem {name} na zadanu poziciju."),
        ("hr", "Napravi {name}, neka bude {prim}, pozicija {xyz}.", "Kreiram {name}."),
        ("hr", "U scenu dodaj {prim} {name}; stavi ga na koordinatu {xyz}.", "Dodajem {name} u scenu."),
        ("en", "Add a {prim} named {name} at {xyz}.", "Adding {name}."),
        ("en", "Create {name} as a {prim} and place it at {xyz}.", "Creating {name}."),
        ("en", "Put a {prim} called {name} at position {xyz}.", "Adding {name} to the scene."),
    ]
    for index in range(210):
        language, prompt, reply = create_templates[index % len(create_templates)]
        primitive = ("cube" if index % 2 == 0 else "plane")
        primitive_label = primitive if language == "en" else ("kocku" if primitive == "cube" else "ravninu")
        name = names[(index * 7 + 3) % len(names)] + (f"_{index // len(names)}" if index >= len(names) else "")
        xyz = [rng.choice(values), rng.choice(values), rng.choice(values)]
        shown_xyz = "(" + ", ".join(f"{v:g}" for v in xyz) + ")"
        arguments: dict[str, object] = {"primitive": primitive, "name": name, "translation": xyz}
        if index % 5 == 0:
            arguments.pop("translation")
            shown_xyz = "(0, 0, 0)"
        examples.append(sample(prompt.format(prim=primitive_label, name=name, xyz=shown_xyz),
            target(reply.format(name=name), action("scene.create_primitive", **arguments))))

    # Transform requests use exact paths or the user's explicit selection reference.
    transform_templates = [
        ("hr", "Pomakni {target} na {xyz}.", "Mijenjam položaj {target}."),
        ("hr", "Postavi lokaciju objekta {target} na {xyz}.", "Postavljam položaj {target}."),
        ("hr", "Na {target} promijeni položaj u {xyz}.", "Ažuriram položaj {target}."),
        ("en", "Move {target} to {xyz}.", "Moving {target}."),
        ("en", "Set the position of {target} to {xyz}.", "Updating {target}'s position."),
        ("en", "Change {target}'s location to {xyz}.", "Changing {target}'s location."),
    ]
    for index in range(126):
        language, prompt, reply = transform_templates[index % len(transform_templates)]
        entity = "selected" if index % 3 == 0 else ("/Scene/Hero" if index % 3 == 1 else "/Main_Block")
        target_label = ("odabrani objekt" if entity == "selected" else entity) if language == "hr" else entity
        xyz = [rng.choice(values), rng.choice(values), rng.choice(values)]
        shown_xyz = "(" + ", ".join(f"{v:g}" for v in xyz) + ")"
        examples.append(sample(prompt.format(target=target_label, xyz=shown_xyz),
            target(reply.format(target=target_label), action("scene.set_transform", target=entity, translation=xyz))))

    rotation_templates = [
        ("hr", "Rotiraj {target} na {xyz} stupnjeva.", "Postavljam rotaciju {target}."),
        ("hr", "Za {target} postavi rotaciju na {xyz}°.", "Mijenjam rotaciju {target}."),
        ("en", "Rotate {target} to {xyz} degrees.", "Setting rotation for {target}."),
        ("en", "Set {target}'s rotation to {xyz} degrees.", "Updating {target}'s rotation."),
    ]
    for index in range(80):
        language, prompt, reply = rotation_templates[index % len(rotation_templates)]
        entity = "selected" if index % 2 == 0 else "/Scene/Hero"
        target_label = "odabrani objekt" if language == "hr" and entity == "selected" else entity
        degrees = [rng.choice([0, 15, 30, 45, 90, 180, -30, -90]) for _ in range(3)]
        shown = "(" + ", ".join(f"{v:g}" for v in degrees) + ")"
        examples.append(sample(prompt.format(target=target_label, xyz=shown),
            target(reply.format(target=target_label), action("scene.set_transform", target=entity,
                                                             rotation_degrees=degrees))))

    scale_templates = [
        ("hr", "Postavi mjerilo {target} na {xyz}.", "Mijenjam mjerilo {target}."),
        ("hr", "Skaliraj {target} na {xyz}.", "Skaliram {target}."),
        ("en", "Set {target}'s scale to {xyz}.", "Setting scale for {target}."),
        ("en", "Scale {target} to {xyz}.", "Scaling {target}."),
    ]
    for index in range(80):
        language, prompt, reply = scale_templates[index % len(scale_templates)]
        entity = f"/Scene/Props/Object_{index:02d}"
        target_label = entity
        scale = [rng.choice([0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0]) for _ in range(3)]
        shown = "(" + ", ".join(f"{v:g}" for v in scale) + ")"
        examples.append(sample(prompt.format(target=target_label, xyz=shown),
            target(reply.format(target=target_label), action("scene.set_transform", target=entity, scale=scale))))

    rename_templates = [
        ("hr", "Preimenuj {target} u {name}.", "Preimenujem objekt u {name}."),
        ("hr", "Nazovi objekt {target} imenom {name}.", "Mijenjam ime u {name}."),
        ("en", "Rename {target} to {name}.", "Renaming it to {name}."),
        ("en", "Call {target} {name}.", "Changing its name to {name}."),
    ]
    for index in range(72):
        language, prompt, reply = rename_templates[index % len(rename_templates)]
        entity = f"/Scene/Props/Old_{index:02d}"
        target_label = entity
        name = f"New_{index:02d}"
        examples.append(sample(prompt.format(target=target_label, name=name),
            target(reply.format(name=name), action("scene.rename", target=entity, name=name))))

    visibility_templates = [
        ("hr", "Sakrij {target}.", False, "Sakrit ću {target}."),
        ("hr", "Prikaži {target}.", True, "Prikazat ću {target}."),
        ("hr", "Učini {target} nevidljivim.", False, "Sakrit ću {target}."),
        ("hr", "Vrati vidljivost za {target}.", True, "Prikazat ću {target}."),
        ("en", "Hide {target}.", False, "Hiding {target}."),
        ("en", "Show {target}.", True, "Showing {target}."),
        ("en", "Make {target} invisible.", False, "Hiding {target}."),
        ("en", "Make {target} visible again.", True, "Showing {target}."),
    ]
    for index in range(96):
        language, prompt, visible, reply = visibility_templates[index % len(visibility_templates)]
        entity = f"/Scene/Props/Proxy_{index:02d}"
        target_label = entity
        examples.append(sample(prompt.format(target=target_label),
            target(reply.format(target=target_label), action("scene.set_visibility", target=entity, visible=visible))))

    # Destructive examples require a host confirmation and must never be silently applied.
    delete_templates = [
        ("hr", "Obriši {target}.", "Za brisanje {target} trebam tvoju potvrdu."),
        ("hr", "Makni {target} iz scene.", "Brisanje {target} traži potvrdu."),
        ("en", "Delete {target}.", "I need confirmation before deleting {target}."),
        ("en", "Remove {target} from the scene.", "Deleting {target} requires confirmation."),
    ]
    for index in range(48):
        language, prompt, reply = delete_templates[index % len(delete_templates)]
        entity = f"/Scene/Props/Object_{index:02d}"
        target_label = entity
        examples.append(sample(prompt.format(target=target_label),
            target(reply.format(target=target_label), action("scene.delete", target=entity))))

    frame_templates = [
        ("hr", "Postavi playhead na kadar {frame}.", "Premještam playhead na kadar {frame}."),
        ("hr", "Idi na frame {frame}.", "Idem na kadar {frame}."),
        ("en", "Set the playhead to frame {frame}.", "Moving the playhead to frame {frame}."),
        ("en", "Go to frame {frame}.", "Going to frame {frame}."),
    ]
    frames = [1, 12, 24, 48, 72, 100, 120, 240]
    for index in range(64):
        language, prompt, reply = frame_templates[index % len(frame_templates)]
        frame = index * 3 + 1
        examples.append(sample(prompt.format(frame=frame),
            target(reply.format(frame=frame), action("timeline.set_playhead", frame=frame))))

    viewport_templates = [
        ("hr", "Uokviri {target} u viewportu.", "Uokvirujem {target}."),
        ("hr", "Fokusiraj pogled na {target}.", "Fokusiram viewport na {target}."),
        ("en", "Frame {target} in the viewport.", "Framing {target} in the viewport."),
        ("en", "Focus the view on {target}.", "Focusing the viewport on {target}."),
    ]
    for index in range(48):
        language, prompt, reply = viewport_templates[index % len(viewport_templates)]
        entity = f"/Scene/Props/Target_{index:02d}"
        target_label = entity
        examples.append(sample(prompt.format(target=target_label),
            target(reply.format(target=target_label), action("viewport.frame_entity", target=entity))))

    # Multi-action turn teaches compound control while keeping output explicit and bounded.
    for index in range(24):
        name = f"Block_{index:02d}"
        x, z = rng.choice(values), rng.choice(values)
        if index % 2:
            user = f"Napravi kocku {name} na ({x:g}, 0, {z:g}) i sakrij /Scene/Proxy."
            reply = f"Kreiram {name} i sakrivam /Scene/Proxy."
            prim = "kocku"
        else:
            user = f"Create cube {name} at ({x:g}, 0, {z:g}) and hide /Scene/Proxy."
            reply = f"Creating {name} and hiding /Scene/Proxy."
            prim = "cube"
        examples.append(sample(user, target(reply,
            action("scene.create_primitive", primitive="cube", name=name, translation=[x, 0.0, z]),
            action("scene.set_visibility", target="/Scene/Proxy", visible=False))))

    # Focused answers teach boundaries and ask before guessing object identity/intent.
    boundaries = [
        ("Možeš napraviti ulicu?", "Trenutni Loom agent zna upravljati postojećim scenama i osnovnim kockama/ravninama. Ulice i Procedura generator još nisu izloženi kao aktivan alat; to dodajemo u kasnijem Procedura treningu."),
        ("Can you generate a procedural city right now?", "The current action API supports scene primitives and transforms. Procedura city generation is not exposed yet, so I will not claim to create it."),
        ("Pomakni objekt.", "Koji objekt želiš pomaknuti i na koju poziciju? Vratit ću praznu listu akcija dok ne navedemo oba podatka."),
        ("Move it.", "Which scene entity should I move, and where should it go? I need both details before proposing an action."),
                ("Kako Loom sprema transformacije?", "Loom scene stores hierarchical entities with local translation, quaternion rotation, and scale. Timed edits go through Warp::Stage::setLocalAt at the current frame."),
        ("How do cameras work in Loom?", "Loom cameras look along local -Z with +Y up. Focal length and principal point are stored in image pixels, matching the solve data."),
        ("Mogu li povećati metalnost objekta preko ovog API-ja?", "Trenutni Loom agent action API još nema promjenu materijala, pa neću slati nedostupnu akciju."),
        ("Can this agent delete a parent and its children without asking?", "No. scene.delete removes the target and all descendants, so the host must request confirmation before running it."),
    ]
    for user, answer in boundaries:
        examples.append(sample(user, target(answer)))

    # More paraphrased boundaries are important because the host refuses any ungrounded action.
    guarded_boundaries = [
        ("Build streets through a neighborhood.", "Street generation is not exposed in this Loom action API."),
        ("Generate a chain between these two points.", "Chain generation is not exposed in this Loom action API."),
        ("Create a rope following the selected curve.", "Rope generation is not exposed in this Loom action API."),
        ("Make buildings along this spline.", "Procedural building generation is not exposed in this Loom action API."),
        ("Generate a procedural bridge.", "Procedural bridge generation is not exposed in this Loom action API."),
        ("Add a sphere primitive.", "The current scene API supports cubes and planes, not spheres."),
        ("Create a cylinder called Column.", "The current scene API supports cubes and planes, not cylinders."),
        ("Paint /Scene/Hero metallic.", "Material editing is not exposed in this Loom action API."),
        ("Change roughness on the selected object.", "Material editing is not exposed in this Loom action API."),
        ("Texture /Scene/Hero with brick.", "Material and texture editing is not exposed in this Loom action API."),
        ("Izradi proceduralnu cestu iz ove krivulje.", "Proceduralne ceste još nisu izložene u Loom action API-ju."),
        ("Generiraj niz kuća uz cestu.", "Proceduralne zgrade još nisu izložene u Loom action API-ju."),
        ("Dodaj lanac između ove dvije točke.", "Generiranje lanaca još nije izloženo u Loom action API-ju."),
        ("Napravi sferu imena Ball.", "Trenutni scene API podržava kocke i ravnine, ali ne sfere."),
        ("Postavi metalnost objekta /Scene/Hero na 0.8.", "Promjena materijala još nije izložena u Loom action API-ju."),
        ("Premjesti /Scene/Robot naprijed.", "Koju točnu poziciju želiš za /Scene/Robot?"),
        ("Set /Scene/Robot's scale.", "What scale should /Scene/Robot have?"),
        ("Rotate the selected object.", "Which rotation in degrees should I use?"),
        ("Hide it.", "Which scene entity should I hide?"),
        ("Rename it.", "Which scene entity should I rename, and to what name?"),
        ("Delete it.", "Which scene entity should I delete?"),
        ("Move /Scene/Robot to a new position.", "What exact position should I use for /Scene/Robot?"),
        ("Make /Scene/Robot larger.", "What scale should I set for /Scene/Robot?"),
        ("Rotate /Scene/Robot by a bit.", "What rotation in degrees should I set for /Scene/Robot?"),
        ("Postavi /Scene/Robot na novu poziciju.", "Koju točnu poziciju želiš za /Scene/Robot?"),
        ("Povećaj /Scene/Robot.", "Koje mjerilo želiš za /Scene/Robot?"),
        ("Zavrti odabrani objekt.", "Koju rotaciju u stupnjevima želiš?"),
        ("Sakrij to.", "Koji objekt u sceni želiš sakriti?"),
        ("Preimenuj objekt.", "Koji objekt želiš preimenovati i kako?"),
        ("Obriši to.", "Koji objekt u sceni želiš obrisati?"),
    ]
    for user, answer in guarded_boundaries:
        examples.append(sample(user, target(answer)))
    examples.append(sample("Što trenutno postoji u sceni?", target("Čitam trenutni popis objekata iz scene.", action("scene.list_entities"))))
    examples.append(sample("What objects are in this scene?", target("Reading the live scene entity list.", action("scene.list_entities"))))

    # Randomize reproducibly, then ensure every user turn is unique.
    rng.shuffle(examples)
    seen: set[str] = set()
    unique = []
    for example in examples:
        user = next(message["content"] for message in example["messages"] if message["role"] == "user")
        normalized = " ".join(user.casefold().split())
        if normalized in seen:
            continue
        seen.add(normalized)
        unique.append(example)
    return unique


def evaluation_examples() -> list[dict]:
    # Held-out phrasings and values do not appear in the training templates.
    cases = [
        ("Stavi ravninu Floor_B na (0, -0.5, 0).", target("Dodajem Floor_B.", action("scene.create_primitive", primitive="plane", name="Floor_B", translation=[0, -0.5, 0]))),
        ("Put a plane called Ground at (0, -1, 0).", target("Adding Ground.", action("scene.create_primitive", primitive="plane", name="Ground", translation=[0, -1, 0]))),
        ("Set /Scene/Hero scale to (1.25, 2, 1.25).", target("Setting scale for /Scene/Hero.", action("scene.set_transform", target="/Scene/Hero", scale=[1.25, 2, 1.25]))),
        ("Zavrti odabrani objekt oko Y osi za 90 stupnjeva.", target("Postavljam rotaciju odabranog objekta.", action("scene.set_transform", target="selected", rotation_degrees=[0, 90, 0]))),
        ("Promijeni ime /Scene/Old na NewName.", target("Mijenjam ime u NewName.", action("scene.rename", target="/Scene/Old", name="NewName"))),
        ("Toggle visibility for /Scene/Proxy to true.", target("Showing /Scene/Proxy.", action("scene.set_visibility", target="/Scene/Proxy", visible=True))),
        ("Hide the selected scene object.", target("Hiding selected.", action("scene.set_visibility", target="selected", visible=False))),
        ("Move the timeline to frame 37.", target("Moving the playhead to frame 37.", action("timeline.set_playhead", frame=37))),
        ("Obriši /Scene/Old.", target("Za brisanje /Scene/Old trebam tvoju potvrdu.", action("scene.delete", target="/Scene/Old"))),
        ("Make a rope along this curve.", target("Rope generation is not exposed in the current Loom action API. I will not send an unsupported action.")),
        ("Pomakni /Scene/Hero.", target("Na koju poziciju želiš premjestiti /Scene/Hero?")),
        ("Add a torus.", target("The current Loom action API exposes cubes and XZ planes, but not a torus.")),
    ]
    return [sample(user, answer) for user, answer in cases]


def write_jsonl(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as stream:
        for row in rows:
            stream.write(json.dumps(row, ensure_ascii=False) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", default="v1", help="dataset output folder, e.g. v2")
    args = parser.parse_args()
    output = DATA_ROOT / args.version
    train = training_examples()
    evaluation = evaluation_examples()
    write_jsonl(output / "train.jsonl", train)
    write_jsonl(output / "eval.jsonl", evaluation)
    manifest = {
        "dataset_version": f"loom-agent-{args.version}",
        "seed": SEED,
        "train_examples": len(train),
        "eval_examples": len(evaluation),
        "train_sha256": hashlib.sha256((output / "train.jsonl").read_bytes()).hexdigest(),
        "eval_sha256": hashlib.sha256((output / "eval.jsonl").read_bytes()).hexdigest(),
        "source": "hand-authored Loom API templates and focused boundary examples",
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({**manifest, "output": str(output)}, indent=2))


if __name__ == "__main__":
    main()
