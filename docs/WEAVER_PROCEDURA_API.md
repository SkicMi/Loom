# WeaverProcedura Recipe and Engine API

WeaverProcedura recipes are versioned, acyclic graphs of nodes and typed links. The engine evaluates them deterministically on CPU and returns either `MeshData` or a point cloud; Loom renders mesh output directly and shows point clouds as viewport markers. Saving a `.loomrecipe.json` file stores the editable recipe graph, not a baked scene mesh.

The engine-facing API is `engine/src/Engine/WeaverProcedura.h`. The editor bridge and Recipe file UI are in `src/LoomProcedura.h` and `src/LoomProceduraRecipe.h`.

## Current node catalogue

| Node | Input ports | Output port | Parameters and behavior |
|---|---|---|---|
| `CurveNode` | — | `0: Curve` | 3D polyline control points and open/closed state. Points are retained during sweep resampling. |
| `RectangleProfileNode` | — | `0: Profile` | Positive width and height in meters. |
| `SweepNode` | `0: Curve`, `1: Profile` | `0: Mesh` | Sample spacing, reference-up vector, end caps, and vertex budget. End caps require a convex profile. |
| `GridNode` | — | `0: PointGrid` | Width, depth, cells in X/Z; makes a regular XZ point lattice. A 4 × 4 cell grid has 5 × 5 points. |
| `SetGridPointHeightNode` | `0: PointGrid` | `0: PointGrid` | Zero-based column, row, and absolute Y height in meters. Chain several nodes to edit several points. |
| `GridToMeshNode` | `0: PointGrid` | `0: Mesh` | Triangulates the evaluated XZ grid, computes normals, and writes normalized grid UVs. |
| `InteriorBlockoutNode` | — | `0: Mesh` | One straight central corridor with 1–8 equal room bays on each side, a floor slab, outer walls, room dividers, and centered door openings. |
| `AddPrimitiveNode` | — | `0: Mesh` | Creates one of the existing Loom primitive shapes: cube, plane, sphere, pyramid, or capsule, with editable dimensions. |
| `MoveNode` | `0: Mesh` | `0: Mesh` | Applies a world-space XYZ offset in meters. |
| `RotateNode` | `0: Mesh` | `0: Mesh` | Applies XYZ Euler angles in degrees around an editable pivot. |
| `ScaleNode` | `0: Mesh` | `0: Mesh` | Applies per-axis scale around an editable pivot and transforms normals correctly. Negative scale is supported for mirroring. |
| `ExtrudeNode` | `0: Mesh` | `0: Mesh` | Selects a connected coplanar face using a seed triangle index, moves it along its normal, and creates side walls. |
| `BevelNode` | `0: Mesh` | `0: Mesh` | Insets convex planar faces and creates a segmented chamfer band. Amount is in meters; segment count is 1–8. |
| `MeshToPointNode` | `0: Mesh` | `0: Points` | Emits one point per unique mesh-vertex position, merging coincident face-split vertices within 0.00001 m. |
| `PointFromMeshNode` | `0: Mesh` | `0: Points` | Deterministically samples a requested number of points over the mesh surface, weighted by triangle area and controlled by a seed. |

The evaluator supports exactly one terminal geometry output per Recipe: either a mesh or a point cloud. Mesh modifiers chain through typed mesh ports; point nodes are terminal outputs in this first round. Links must join matching port types, input ports accept at most one link, and graph cycles are rejected. Graph validation also enforces finite dimensions, point counts, node/link limits, and safe output budgets.

The interior blockout is intentionally a tlocrt and wall massing tool. It does not generate ceilings, windows, furniture, multiple floors, curved corridors, or individually authored room polygons yet. It creates geometry directly from its parameters rather than exposing each wall as a separate editable node.

## C++ API examples

### Raise two points on a grid and triangulate it

```cpp
namespace Proc = Engine::WeaverProcedura;

Proc::Graph recipe;
Proc::GridNode grid;
grid.width = 8.0f;
grid.depth = 8.0f;
grid.cellsX = 4;
grid.cellsZ = 4;
const auto source = Proc::addNode(recipe, grid);

Proc::SetGridPointHeightNode first;
first.column = 2; // zero-based X sample
first.row = 2;    // zero-based Z sample
first.height = 2.0f;
const auto liftA = Proc::addNode(recipe, first);

Proc::SetGridPointHeightNode second;
second.column = 3;
second.row = 2;
second.height = 1.0f;
const auto liftB = Proc::addNode(recipe, second);
const auto mesh = Proc::addNode(recipe, Proc::GridToMeshNode{});

recipe.links = {
    {source, 0, liftA, 0},
    {liftA, 0, liftB, 0},
    {liftB, 0, mesh, 0},
};

const Proc::ValidationResult valid = Proc::validate(recipe);
const Proc::EvaluationResult result = Proc::evaluate(recipe);
if(valid && result.succeeded){
    // result.mesh contains CPU vertices, normals, UVs, and triangle indices.
}
```

`validate()` checks graph schema, unique IDs, port compatibility, finite values, and acyclic links. `evaluate()` also checks that the graph is complete, has one geometry output, and produces valid geometry. For mesh output it returns `outputNode` and `mesh`; for point output it sets `pointCloudOutput` and returns `points`. Both forms return an error string on failure.

### Chain primitive and mesh operations

```cpp
namespace Proc = Engine::WeaverProcedura;

Proc::Graph recipe;
const auto source = Proc::addNode(recipe, Proc::AddPrimitiveNode{}); // defaults to cube
Proc::MoveNode move;
move.offset = {0.0f, 1.0f, 0.0f};
const auto moved = Proc::addNode(recipe, move);
Proc::RotateNode rotate;
rotate.degrees.y = 45.0f;
const auto rotated = Proc::addNode(recipe, rotate);
Proc::ScaleNode scale;
scale.factor = {2.0f, 1.0f, 2.0f};
const auto scaled = Proc::addNode(recipe, scale);
recipe.links = {{source,0,moved,0},{moved,0,rotated,0},{rotated,0,scaled,0}};
const Proc::EvaluationResult result = Proc::evaluate(recipe);
```

`ExtrudeNode::faceIndex` refers to a triangle in the input index buffer. The evaluator groups edge-connected, coplanar triangles so that a selected seed triangle extrudes the full planar face. `BevelNode` currently supports convex planar faces; it reports an error for concave faces or amounts that collapse the inset.

### Convert or sample points from a mesh

```cpp
namespace Proc = Engine::WeaverProcedura;

Proc::Graph recipe;
const auto source = Proc::addNode(recipe, Proc::AddPrimitiveNode{});
Proc::PointFromMeshNode sampling;
sampling.count = 500;
sampling.seed = 42;
const auto points = Proc::addNode(recipe, sampling);
recipe.links = {{source,0,points,0}};
const Proc::EvaluationResult result = Proc::evaluate(recipe);
if(result.succeeded && result.pointCloudOutput){
    // result.points contains 500 deterministic surface samples.
}
```

### Make the hallway and rooms blockout

```cpp
Proc::Graph recipe;
Proc::InteriorBlockoutNode interior;
interior.roomsPerSide = 2;
interior.roomWidth = 4.0f;
interior.roomDepth = 4.0f;
interior.corridorWidth = 2.0f;
interior.wallHeight = 3.0f;
const auto output = Proc::addNode(recipe, interior, 120.0f, 80.0f);
const Proc::EvaluationResult result = Proc::evaluate(recipe);
```

The default result has four rooms total, a central hallway, door gaps facing the hallway, exterior and partition walls, and one continuous floor slab. The hallway ends remain open.

Other public functions are `makeGrid`, `gridToMesh`, `makeInteriorBlockout`, `makeRectangleProfile`, `makeCircularProfile`, and `sweep`. Mesh-producing functions leave the caller's previous output untouched when they fail.

## Recipe file format

The editor's Recipe controls save and load `loom.weaverprocedura.recipe` JSON. The current `schema_version` is `4`; version 3 recipes are upgraded on load because their existing node payloads retain the same meaning. Unknown node types and other schema versions are rejected. The importer caps files at 1 MB, graphs at 256 nodes and 1,024 links, and curve point arrays at 4,096 points. Save writes a temporary file and renames it into place.

```json
{
  "format": "loom.weaverprocedura.recipe",
  "schema_version": 4,
  "name": "Raised Grid Surface",
  "seed": 1,
  "nodes": [
    {"id": 1, "position": [24, 100], "parameters": {
      "type": "grid", "width": 8, "depth": 8, "cells_x": 4, "cells_z": 4
    }},
    {"id": 2, "position": [280, 100], "parameters": {
      "type": "set_grid_point_height", "column": 2, "row": 2, "height": 2
    }},
    {"id": 3, "position": [540, 100], "parameters": {"type": "grid_to_mesh"}}
  ],
  "links": [
    {"from": 1, "from_port": 0, "to": 2, "to_port": 0},
    {"from": 2, "from_port": 0, "to": 3, "to_port": 0}
  ]
}
```

The file stores node IDs, graph editor positions, typed links, parameters, and the graph seed. The seed is reserved metadata; the current deterministic grid and blockout evaluators do not use random variation. Files can be created, tuned, saved, and loaded in the WeaverProcedura panel. The default relative path is `WeaverProcedura/Untitled.loomrecipe.json`.

## Current behavior and extension boundary

- Preview geometry is derived from the Recipe; changing the graph marks the preview dirty. The last successful preview stays visible if a later evaluation fails.
- Grid point edits are explicit and reproducible: the height node sets one zero-based point's absolute Y value. Multiple edits chain through typed `PointGrid` ports.
- Interior blockout parameters are stored in the graph, so layout edits can be replayed from the Recipe file.
- The agent action protocol exposes `procedura.create_recipe` for the grid surface and hallway/rooms generators. It constructs typed nodes, validates and evaluates the graph on the editor thread, and puts the result into the live Procedura panel and viewport preview. The user can then edit the graph or save it as a `.loomrecipe.json` file. Arbitrary graph JSON, editing existing graphs from chat, roads, exterior building generation, ropes, and chains remain unsupported.

## Geometry checks

`tests/test_weaverprocedura.cpp` exercises the grid lattice, upward mesh normals, chained point edits, interior output, each primitive and mesh operation, deterministic surface point sampling, point-cloud preview geometry, Recipe round trips, legacy schema upgrades, and schema-version rejection.
