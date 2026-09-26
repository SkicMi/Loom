# WeaverProcedura čvorovi — stanje za AgentOfWeavers

Razina: **N** = niska (za ljude), **S** = srednja (ono što model bira). Stanje: radi / popravak / fali.

## Postojeći (shema 5)

| Čvor | Razina | Stanje | Napomena |
|---|---|---|---|
| `CurveNode` | N | radi | samo izlomljena linija; fali glađenje |
| `RectangleProfileNode` | N | radi | |
| `CircleProfileNode` | N | radi | novo; uže, cijevi |
| `SweepNode` | N | radi | prima bilo koji Curve/Profile izvor |
| `GridNode`, `GridToMeshNode` | N | radi | |
| `SetGridPointHeightNode` | N | radi | jedna točka po čvoru — neupotrebljivo za model; treba Noise/Displace |
| `InteriorBlockoutNode` | S | popravak | monolit bez stropa, prozora, katova; zidovi su preklopljene kutije; označava semantiku |
| `AddPrimitiveNode` | N | radi | fali torus i valjak |
| `Move`/`Rotate`/`ScaleNode` | N | radi | cijeli mesh, bez selekcije |
| `ExtrudeNode` | N | popravak | odabir po indeksu trokuta; treba `TriangleFilter` |
| `BevelNode` | N | popravak | samo konveksne ravne plohe |
| `MeshToPointNode`, `PointFromMeshNode` | N | radi | točke nose normale i teku grafom |
| `MergeNode` | S | radi | novo; do 8 ulaza |
| `SetSemanticNode` | S | radi | novo; filter po semantici i smjeru |
| `SetMaterialNode` | S | radi | novo; zatvorena knjižnica materijala |
| `SmoothNormalsNode` | S | radi | novo; kut glađenja |
| `UVProjectNode` | S | radi | novo; box projekcija u metrima |
| `CopyToPointsNode` | S | radi | novo; poravnanje po normali, slučajni zakret i skala |

## Fale (faza 1 ostatak i faza 2)

| Čvor | Razina | Za što |
|---|---|---|
| `CurveSmooth` (Catmull-Rom) | N | ceste, uže |
| `CatenaryCurve` | S | uže, lanac |
| `CopyAlongCurve` | S | lanac (torus, 90° naizmjence), ograde, rasvjeta |
| `Footprint` (rect, L, U, poligon) | S | kuća, zgrada |
| `FloorStack` | S | katovi, različit tlocrt po katu |
| `WallsFromFootprint` | S | zidovi s otvorima, bez booleana |
| `Openings` | S | prozori i vrata po pravilu |
| `Roof` (flat, gable, hip, shed) | S | krov s prepustom |
| `RoomSplit` | S | sobe i hodnik, tip interijera po seedu |
| `Slab` / `Ceiling`, `Stairs` | S | katovi, strop, stepenice |
| `RoadFromCurve` | S | trake, pločnik, rubnik |
| `Noise` / `Displace` | S | teren |

## Vokabulari (samo dodavanje na kraj)

Semantika: floor, ceiling, wall_exterior, wall_interior, roof, window, door, frame, stairs, railing, foundation,
trim, glass, road, sidewalk, curb, rope, chain_link, terrain, prop.

Materijali: plaster, brick, stone, concrete, wood_planks, wood_beam, roof_tiles, roof_metal, glass, metal,
steel_chain, asphalt, paving, rope_fiber, ground_dirt, grass.
