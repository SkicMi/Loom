# WeaverProcedura vocabulary v3

This vocabulary is grounded in the current Engine and Recipe editor behavior. Machine-readable definitions and bilingual model prompts are in `tools/weaveragent/data/procedura_vocabulary_v3.json`. The previous v2 dataset and its baseline report remain frozen for historical comparison.

Every concept has Croatian and English factual prompts; every alias has a deterministic exact-mapping test. The current vocabulary includes supported nodes, graph rules, geometry behavior, and reserved capabilities. The chat API exposes one bounded graph action, `procedura.create_recipe`, for grid surfaces and hallway/room blockouts; it does not expose arbitrary graph JSON or editing an existing Recipe.

Run the deterministic vocabulary and protocol checks:

```sh
python3 -m unittest discover -s tools/weaveragent/tests
```

When the local model is available, score the v3 vocabulary prompts without changing their hash:

```sh
tools/weaveragent/.venv/bin/python tools/weaveragent/evaluate_procedura_vocabulary.py
```

The evaluator writes per-prompt factual coverage, forbidden claims, unexpected actions, latency, adapter identity, and vocabulary hash to `.cache/weaverprocedura/benchmarks/procedura-vocabulary.json`.

## Supported concepts

| Concept | Current implementation |
|---|---|
| Graph, node, link | Version 3 acyclic graph with typed ports and one mesh output per recipe. |
| Curve, profile, Sweep | Open or closed 3D polyline, rectangular profile, CPU sweep mesh. Curves remain linear polylines, not Bezier splines. |
| Grid | Regular XZ point lattice with bounded dimensions and one extra point per cell count. |
| Grid point height | A chained node sets one zero-based grid point's absolute Y coordinate. |
| Grid to Mesh | Triangulates the point grid, computes normals, and emits UVs. |
| Interior blockout | Straight central hallway, equal rooms on both sides, floor slab, perimeter and partition walls, and centered door openings. |
| Recipe JSON | Editable versioned graph saved as `loom.weaverprocedura.recipe`; import rejects unknown schema versions and node types. |
| Seed | Stored in the Recipe but not used by current deterministic generators. |
| Road preset | Composes Curve + Rectangle Profile + Sweep as a temporary viewport preview; it is not yet a scene asset. |

The interior node is an initial floor-plan blockout. It does not generate ceilings, windows, furniture, multiple floors, or arbitrary room polygons. A general building generator, street network, rope/chain nodes, and smooth spline interpolation remain unsupported.

## Geometry verification

`tests/test_weaverprocedura.cpp` checks regular grid sample positions, grid mesh winding/normals, chained height edits, out-of-range indices, interior floor/wall geometry, Recipe JSON round trips, and schema-version rejection. New vocabulary nodes must have both Croatian and English prompts plus deterministic Engine behavior tests before they are considered implemented.

The vocabulary benchmark and geometry tests measure different things: model factual grounding versus deterministic Engine output. A separate locked tool-call benchmark lives in `tools/weaveragent/data/benchmarks/procedura-action-cases-v1.jsonl`; run it with `tools/weaveragent/.venv/bin/python tools/weaveragent/evaluate_procedura_actions.py`. It measures supported grid/interior tool selection, explicit arguments, unsupported requests, and abstention in Croatian and English. Keep future training traces separate from these evaluation prompts.

The first frozen tool-call baseline is [procedura-actions-baseline-v1.json](../tools/weaveragent/data/benchmarks/procedura-actions-baseline-v1.json), captured 26 September 2026 using the existing 4B LoRA adapter without additional training. The complete report is generated under `.cache/weaverprocedura/benchmarks/procedura-actions-v1.json`. The benchmark reports end-to-end host-validated accuracy separately from raw model accuracy; its eight cases are a regression baseline, not a broad intelligence score. Latency p95 uses nearest-rank percentile so the slowest case is included in this small suite.

## Frozen v2 baseline

The historical v2 baseline remains the 40-query run from 26 September 2026: 13/40 prompts passed every required fact group (32.5%), 55/117 fact groups were covered (47.0%), there were four contradictory claims, and no unexpected actions. It measured terminology and retrieval, not procedural graph execution. The detailed report is `.cache/weaverprocedura/benchmarks/procedura-vocabulary-v2-baseline.json`; the committed summary is `tools/weaveragent/data/benchmarks/procedura-vocabulary-baseline-v2.json`.

Do not compare a v3 model score to v2 as if the prompt suite were identical. Keep the original v2 files and hashes intact; v3 has new capabilities and requires its own local-model baseline before training examples are collected.
