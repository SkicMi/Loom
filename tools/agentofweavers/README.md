# AgentOfWeavers tools

Data and encoder for the first AgentOfWeavers model (houses, English only). Plan and log:
`docs/AgentOfWeavers/`.

## Setup

    cd tools/agentofweavers
    uv venv .venv --python 3.12
    uv pip install --python .venv/bin/python torch --index-url https://download.pytorch.org/whl/cu128
    uv pip install --python .venv/bin/python -r requirements.txt

`.venv` is git-ignored. Verified on RTX 5070 (SM120) with torch 2.11.0+cu128.

## 1. Data: `procedura-gen`

    cmake --build build --target ProceduraGen
    build/procedura-gen --houses 10000 --seed 1 --out .cache/agentofweavers/data/houses-v0.1
    build/procedura-gen --schema schema.json          # the action schema alone

`houses.jsonl`, one record per attempt:

| field | meaning |
|---|---|
| `id`, `house` | record number; house number (a house and its retries share it) |
| `generator`, `seed`, `split` | `houses-v0.1`, run seed, `train` / `val` (stable per house) |
| `status`, `reason` | `pass`, or `fail` with the engine's or the house rules' reason (Fail → Why) |
| `retry_of`, `repair` | on a retry: the failed record and the one change made (Retry) |
| `descriptions` | pass only: brief, medium, detailed English prompts; every fact in them is true |
| `facts` | pass only: rooms by type, floors, footprint area, height, style, furniture count, triangles |
| `actions` | the recipe as actions (below); every record round-trips actions → recipe → actions |

Measured (seed 1, 10 000 houses): 9992 pass, 478 of them after a repair, 511 failed attempts, 97 actions
per house, 28 s on one CPU thread.

## 2. The action language

    ADD <type>               new node; nodes are numbered 0, 1, ... in order
    SET <param> <value>      every parameter, in schema order, on the schema grid
    CONNECT <node> <port>    earlier node's output into this node's input <port>, ports ascending
    END                      the last node is the output

`schema.json` lists every node type with its parameters (kind, min, max, step or options) and port
types. A decoder can mask every illegal action from it alone: the next parameter name is fixed,
values are grid bins or options, CONNECT sources must have the matching output type. The C++ reference
checker is `actionsToDocument` in `src/LoomProceduraActions.h`; it reports the first illegal step.

V0 uses one template (footprint → floor stack → room split → walls, slab, roof, interior, furnish →
merge → materials for exterior walls, roof, frames, doors → UV projection), so the model learns
parameters, materials and UV scale; learning graph structure is a later data set.

## 3. Encoder: `embed.py`

    .venv/bin/python embed.py .cache/agentofweavers/data/houses-v0.1
    .venv/bin/python embed.py --prompts docs/AgentOfWeavers/heldout/prompts_v1.jsonl

Frozen `BAAI/bge-base-en-v1.5` (768-d, CLS pooling, L2-normalised), run once. Writes
`embeddings.f16.npy`, `texts.jsonl` (row → record id and description index) and `encoder.json`
(model revision and the input's SHA-256). Training reads the vectors, so the encoder is never on
the GPU during training.
