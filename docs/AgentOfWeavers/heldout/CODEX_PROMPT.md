# Prompt for Codex: 200 held-out house prompts (AgentOfWeavers V0)

Copy everything below the line into Codex.

---

You are writing the held-out test set for AgentOfWeavers V0, a small model that turns an English
request for a house into a procedural building recipe. These prompts are never used for training;
they measure whether the model understands how real people ask for buildings. Write in English only.

## Rules for working in this repository

- Create exactly two files and change nothing else:
  - `docs/AgentOfWeavers/heldout/prompts_v1.jsonl`
  - `docs/AgentOfWeavers/heldout/check_prompts.py`
- Another agent is working in this repository at the same time. Do not edit, format, stage, commit,
  build or delete any other file. Do not run `git add -A`, `git commit`, `git stash` or `git checkout`.
- Do not open `src/LoomProceduraHouses.h`, `src/procedura_gen.cpp` or anything under `.cache/`. The
  training descriptions come from there, and the test set must not copy their wording.

## What the model can build (V0)

One house or small office building per prompt. It can vary:

| key | values |
|---|---|
| `program` | `home`, `office` |
| `floors` | 1 to 4 |
| `shape` | `rectangle`, `l_shape`, `u_shape` (footprint) |
| `width_m`, `depth_m` | footprint size in meters (width 5 to 30, depth 5 to 24) |
| `size` | `small` (under 110 m² of floor), `medium` (110 to 260), `large` (over 260) |
| `roof_type` | `flat`, `gable`, `hip`, `shed` (a shed roof only on a rectangular footprint) |
| `roof_pitch` | `steep` (42° or more), `low` (25° or less); gable, hip and shed only |
| `parapet` | true: a flat roof with a parapet |
| `roof_material` | `roof_tiles`, `roof_metal`, `concrete` |
| `wall_material` | `plaster`, `brick`, `stone`, `wood_planks` (timber cladding), `concrete` |
| `frame_material` | window frames or surrounds: `plaster`, `wood_beam`, `metal`, `stone` |
| `door_material` | front door: `wood_planks`, `lacquer` (painted), `metal` |
| `style` | furniture and overall character: `basic` (simple, plain), `modern`, `rustic` |
| `bedrooms`, `bathrooms`, `offices` | room counts; use `{"min": n}`, `{"max": n}` or an exact integer |
| `windows` | `large` or `small` |
| `raised` | true: ground floor raised above the ground |
| `high_ceilings` | true |
| `furnished` | `sparse`, `normal`, `full` |

It cannot build yet: balconies, terraces, garages, carports, porches, verandas, chimneys, dormers,
basements, pools, gardens, fences, several buildings, towers, domes, round or curved walls, castles,
churches, shops, multi-storey car parks, or anything that is not one house or office building.

## The file `prompts_v1.jsonl`

Exactly 200 lines, one JSON object per line:

```json
{"id": "h001", "category": "specific", "prompt": "...", "expect": {"floors": 2, "roof_type": "gable", "wall_material": "brick"}, "unsupported": []}
```

- `id`: `h001` to `h200`.
- `prompt`: what a person would actually type. Vary length (3 to 60 words), tone (command, wish,
  question, description), vocabulary and detail. Use everyday words ("bungalow", "cottage",
  "two-story", "pitched roof", "red tiles", "white render", "log cabin look", "open plan",
  "for a family of five", "home office") and let the `expect` field say what they mean in the keys
  above. Include American and British spelling, a few typos, a few numbers written as words, metric
  and a few imperial sizes (feet, square feet), and a few prompts in lower case without punctuation.
  Do not start more than 15 prompts with the same word, and do not reuse a sentence frame.
- `expect`: only facts the prompt really states or clearly implies, with the keys and values from the
  table. Leave out anything the prompt leaves open. Convert units to meters and square meters. "Family
  of five" can imply `{"bedrooms": {"min": 3}}`; "bungalow" implies `"floors": 1`; "cottage" implies
  `"size": "small"`. Do not guess beyond such common readings.
- `unsupported`: things the prompt asks for that V0 cannot build (from the list above, in plain
  words, e.g. `["balcony", "garage"]`), otherwise `[]`. Put the supported part of the request in
  `expect` as usual.
- `category`, with these counts:
  - `specific` (50): four or more facts in `expect`.
  - `partial` (50): one to three facts.
  - `vague` (30): mood or character only ("something cozy for a retired couple"); `expect` may be
    empty or hold only what the words imply.
  - `numeric` (25): exact sizes, floor counts or room counts, including units to convert.
  - `office` (15): `program` office, any mix of the above.
  - `unsupported` (20): a request that includes at least one thing V0 cannot build.
  - `contradictory` (10): impossible or conflicting (a shed roof on a U-shaped house, a one-storey
    house with stairs to the third floor, a tiny house with 8 bedrooms). Put what is stated in
    `expect`, and add `"conflict": "<one sentence>"` next to `unsupported`.

Across the whole file, every value of every key in the table must occur at least 3 times.

## The file `check_prompts.py`

A standard-library Python 3 script, run as `python3 docs/AgentOfWeavers/heldout/check_prompts.py`.
It must check that there are exactly 200 lines of valid JSON; that ids run from h001 to h200; that the
category counts match; that `expect` uses only the keys and values above (ranges for floors, sizes
and counts); that `conflict` appears exactly on the contradictory prompts; that every value of every
key occurs at least 3 times; that no two prompts are identical; and that no more than 15 prompts
start with the same word. It prints a coverage table (key → value → count) and exits with status 1
on any failure. Run it, fix the data until it passes, and report the table in your final message.
