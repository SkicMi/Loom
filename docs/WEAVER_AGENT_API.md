# Loom Agent API

This document describes every action currently exposed to the local Loom Agent and the scene snapshot supplied by the editor. It is the supported agent contract; it does not expose arbitrary C++ functions, file access, shell commands, or unrestricted scene mutation.

## Runtime endpoints

The local Python service binds to loopback by default at `http://127.0.0.1:8765`.

| Method and path | Purpose |
|---|---|
| `GET /health` | Reports whether the model is loaded, protocol version, indexed source chunk count, and uptime. |
| `GET /v1/tools` | Returns the versioned tool schemas from `tools/weaveragent/data/tools.json`. |
| `POST /v1/agent/turn` | Produces a reply and up to 16 proposed, schema-checked Loom actions. |

The POST body is limited to 128,000 bytes. Send either `message` or a `conversation` array of up to 32 `{ "role": "user" | "assistant", "content": "..." }` messages. The latest message must be 1–4,000 characters. `scene_context` is optional; when supplied, the service validates it before prompt construction.

```json
{
  "conversation": [
    {"role": "user", "content": "Pomakni odabranu kocku na (1, 0, 2)."}
  ],
  "scene_context": {
    "schema": "loom.scene-context",
    "version": 1,
    "frame": 1,
    "selected_path": "/World/Cube",
    "entities": [
      {
        "path": "/World/Cube",
        "name": "Cube",
        "type": "cube",
        "parent_path": "/World",
        "visible": true,
        "world_position": [0, 0, 0],
        "local_position": [0, 0, 0],
        "local_scale": [1, 1, 1],
        "local_rotation_xyzw": [0, 0, 0, 1]
      }
    ],
    "truncated": false
  }
}
```

The response is `{ "protocol": 1, "result": { "version": 1, "reply": "...", "actions": [...] }, "diagnostics": { "model_action_count": 0, "actions_recovered_by_host_guard": false }, "retrieved": [...] }`. A model action has `tool`, `arguments`, and `confirmation_required`. Diagnostics distinguish model-proposed actions from safe deterministic recovery: `model_action_count` is the count after protocol parsing, and `actions_recovered_by_host_guard` is true when request-grounded host logic supplied an action because the model proposed none. The model service validates and returns proposed actions; Loom resolves targets against the live scene, performs native validation, and executes them on the editor thread. Native execution returns `succeeded`, `confirmationRequired`, `message`, and, for scene listing, `entities` containing `path`, `name`, `type`, `parent_path`, and `visible`. For Procedura creation, the host also returns the generated graph and evaluated preview mesh to the editor, which installs it in the Procedura panel and viewport preview. The chat appends this host result after the model reply. A model reply alone does not mean an action succeeded.

The service accepts loopback connections only. It does not enable CORS or expose a remote bind option. Keep the model and scene context local to the machine.

## Scene context format, version 1

The editor sends a compact semantic snapshot rather than an encoded copy of the project file. Fields are:

| Field | Meaning |
|---|---|
| `schema`, `version` | Fixed identifier `loom.scene-context`, version `1`. |
| `frame` | Timeline frame used to evaluate animated transforms. |
| `selected_path` | Exact selected entity path, or `null`. |
| `entities` | Selected entity first, followed by scene hierarchy order; capped at 24 entities. |
| `truncated` | True when the live scene has more entities than the snapshot includes. |
| entity `path`, `name`, `type`, `parent_path`, `visible` | Stable live identity, display name, component summary, parent path, and visibility. |
| entity `world_position` | World-space translation at `frame`. |
| entity `local_position`, `local_scale` | Evaluated local transform channels at `frame`. |
| entity `local_rotation_xyzw` | Evaluated local quaternion in `[x, y, z, w]` order. |

Supported current `type` values are `cube`, `plane`, `camera`, `points`, `splat`, `model`, `joint`, `animator`, and `group`. Scene paths, names, and transforms are facts from the live host snapshot. Retrieved source code is implementation reference only and must not be treated as a list of live scene entities.

### Encoding versus encryption

Encryption is not a representation for model comprehension: ciphertext hides the structure and meaning from the model. The current JSON snapshot is already the useful kind of encoding: named, typed fields with units and stable paths. For future scene understanding, extend this versioned semantic snapshot with only queried information such as mesh bounds, material summaries, or a viewport image. Do not send raw project bytes or every mesh vertex by default. If the project needs encryption at rest, decrypt locally before building the local model prompt; encryption is a privacy control, not a reasoning feature.

## Action output contract

Every turn has:

```json
{
  "version": 1,
  "reply": "Short user-facing response.",
  "actions": [
    {"tool": "scene.create_primitive", "arguments": {"primitive": "cube"}}
  ]
}
```

Unknown tool names, extra or missing arguments, non-finite values, malformed vectors, and unsupported ranges are rejected. Targets are exact scene paths unless the user explicitly refers to the current selection, in which case `selected` is valid only when `selected_path` exists. Destructive deletion requires user confirmation.

## Current tools

### `scene.list_entities`

Read-only. With no `parent`, returns the full scene hierarchy in traversal order. An exact `parent` path limits the result to that entity and its descendants.

```json
{"version":1,"reply":"Čitam objekte u sceni.","actions":[
  {"tool":"scene.list_entities","arguments":{}}
]}
```

Arguments: optional `parent: string | null`.

### `scene.create_primitive`

Creates a unit `cube` or unit XZ `plane`. Omitted translation means the local origin under the parent (or world origin at the scene root); omitted name uses a unique `Cube` or `Plane` name. Optional `parent` is an exact scene path or `null` for the scene root. Optional `color` is solid viewport RGB in `[0, 1]`.

```json
{"version":1,"reply":"Kreiram zelenu kocku na ishodištu.","actions":[
  {"tool":"scene.create_primitive","arguments":{
    "primitive":"cube","name":"Marker","translation":[0,0,0],"color":[0.10,0.78,0.18]
  }}
]}
```

Arguments: required `primitive: "cube" | "plane"`; optional `name` (1–80 characters), `parent` (path or null), `translation` (three finite numbers), `color` (three finite numbers, each 0–1).

### `scene.set_transform`

Sets one or more **local** transform channels at the current timeline frame. Rotation is supplied as `[X, Y, Z]` Euler degrees; scale components must be positive.

```json
{"version":1,"reply":"Pomaknut ću odabrani objekt.","actions":[
  {"tool":"scene.set_transform","arguments":{
    "target":"selected","translation":[1,0,2]
  }}
]}
```

Arguments: required `target: exact path | "selected"`; at least one optional channel: `translation`, `rotation_degrees`, or `scale`, each a three-number vector.

### `scene.rename`

Renames one live entity. Loom keeps sibling names unique.

```json
{"version":1,"reply":"Preimenovat ću objekt u Entrance.","actions":[
  {"tool":"scene.rename","arguments":{"target":"/World/Cube","name":"Entrance"}}
]}
```

Arguments: required `target` path or `selected`; required `name` (1–80 characters).

### `scene.set_visibility`

Sets one live entity's visibility.

```json
{"version":1,"reply":"Sakrit ću označeni objekt.","actions":[
  {"tool":"scene.set_visibility","arguments":{"target":"selected","visible":false}}
]}
```

Arguments: required `target` path or `selected`; required `visible: boolean`.

### `scene.delete`

Deletes the target and all descendants. The first host response asks the user to confirm; Loom executes the delete only after confirmation.

```json
{"version":1,"reply":"Brisanje uključuje i djecu objekta. Potvrdi prije izvršenja.","actions":[
  {"tool":"scene.delete","arguments":{"target":"/World/Temporary"},"confirmation_required":true}
]}
```

Arguments: required `target` path or `selected`.

### `procedura.create_recipe`

Creates a validated Recipe graph from one supported generator, evaluates it, and shows its mesh in the viewport. The graph becomes the current editable graph in the Procedura panel. It stays in editor memory until the user saves it with **Save Recipe**. It does not create a persistent scene entity or write a file by itself.

Grid surface example with a raised center point (point coordinates are zero-based):

```json
{"version":1,"reply":"Izrađujem grid Recipe s podignutom središnjom točkom.","actions":[
  {"tool":"procedura.create_recipe","arguments":{
    "generator":"grid_surface","name":"Raised Courtyard Surface",
    "width":12,"depth":10,"cells_x":4,"cells_z":4,
    "point_heights":[{"column":2,"row":2,"height":2.5}]
  }}
]}
```

Hallway and rooms example:

```json
{"version":1,"reply":"Izrađujem tlocrt hodnika i soba.","actions":[
  {"tool":"procedura.create_recipe","arguments":{
    "generator":"interior_blockout","name":"Studio Floor Plan",
    "rooms_per_side":3,"room_width":4,"room_depth":5,
    "corridor_width":2.2,"wall_height":3.1
  }}
]}
```

Arguments: required `generator: "grid_surface" | "interior_blockout"`; optional `name` (1–120 characters). Grid-only fields are `width`, `depth`, `cells_x`, `cells_z`, and `point_heights`; omitted values default to an 8×8 meter grid with 4×4 cells. Each point-height entry is `{ "column": integer, "row": integer, "height": finite number }`, uses a zero-based grid vertex, and sets absolute Y in meters. At most 64 unique points can be edited; coordinates must be within the selected cell counts. Interior-only fields are `rooms_per_side`, `room_width`, `room_depth`, `corridor_width`, `wall_height`, `wall_thickness`, `floor_thickness`, and `door_width`; defaults are 2 rooms per side, 4×4 meter rooms, a 2 meter corridor, 3 meter walls, 0.15 meter walls, 0.12 meter floor, and 0.9 meter door openings. The engine rejects dimensions that make an invalid wall, floor, or door layout. General road networks, exterior building generators, ropes, chains, and arbitrary graph JSON are not exposed by this tool.

### `timeline.set_playhead`

Moves the playhead to a finite frame in the supported range; the host clamps the result to the project frame range.

```json
{"version":1,"reply":"Postavljam playhead na kadar 48.","actions":[
  {"tool":"timeline.set_playhead","arguments":{"frame":48}}
]}
```

Arguments: required `frame: number`, range 0–10,000,000 before project-range clamping.

### `viewport.frame_entity`

Frames an exact live entity or the explicit current selection in the orbit viewport.

```json
{"version":1,"reply":"Uokvirujem odabrani objekt.","actions":[
  {"tool":"viewport.frame_entity","arguments":{"target":"selected"}}
]}
```

Arguments: required `target` path or `selected`.

## Source of truth and maintenance

The machine-readable schema is `tools/weaveragent/data/tools.json`; validation is implemented in `tools/weaveragent/agent_protocol.py`; the native action boundary and scene snapshot are in `src/LoomAgentActions.h` and `src/LoomAgentJson.h`. Procedura graph recipes are built and evaluated by `engine/src/Engine/WeaverProcedura.*` and are passed to the editor's Procedura panel. Update the schema, validators, host executor, examples, and held-out tool-call cases together when adding an action. Never document a tool as available until its native executor exists.

WeaverProcedura graph and Recipe APIs are documented separately in `WEAVER_PROCEDURA_API.md`.
