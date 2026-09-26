# WeaverProcedura čvorovi — stanje za AgentOfWeavers

Razina: **N** = niska (za ljude), **S** = srednja (ono što model bira). Stanje: radi / popravak / fali.
Tipovi portova: Curve, Profile, PointGrid, Mesh, Points, **Footprint** (shema 7).

## Postojeći (shema 7)

| Čvor | Razina | Stanje | Napomena |
|---|---|---|---|
| `CurveNode` | N | radi | izlomljena linija; glađenje je `CurveSmooth` |
| `CurveSmoothNode` | N | radi | Catmull-Rom kroz sve kontrolne točke |
| `CatenaryCurveNode` | S | radi | uže, lanac |
| `RectangleProfileNode`, `CircleProfileNode` | N | radi | |
| `SweepNode` | N | radi | prima bilo koji Curve/Profile izvor |
| `GridNode`, `GridToMeshNode` | N | radi | |
| `SetGridPointHeightNode` | N | radi | jedna točka po čvoru — neupotrebljivo za model; treba Noise/Displace |
| `InteriorBlockoutNode` | S | popravak | monolit bez stropa, prozora, katova; zamijenit će ga `RoomSplit` nad Footprintom |
| `AddPrimitiveNode` | N | radi | kocka, ravnina, kugla, piramida, kapsula, valjak, torus |
| `Move`/`Rotate`/`ScaleNode` | N | radi | cijeli mesh, bez selekcije |
| `ExtrudeNode` | N | radi | po indeksu ili po `TriangleFilter` (model koristi filter) |
| `BevelNode` | N | popravak | samo konveksne ravne plohe |
| `MeshToPointNode`, `PointFromMeshNode` | N | radi | točke nose normale i teku grafom |
| `MergeNode` | S | radi | do 8 ulaza |
| `SetSemanticNode`, `SetMaterialNode` | S | radi | filter po semantici i smjeru; zatvoreni vokabulari |
| `SmoothNormalsNode`, `UVProjectNode` | S | radi | |
| `CopyToPointsNode` | S | radi | poravnanje po normali, slučajni zakret i skala |
| `CopyAlongCurveNode` | S | radi | lanac (alternateRoll 90°), ograde, rasvjeta |
| `FootprintNode` | S | radi | rectangle / l_shape / u_shape, širina, dubina, krilo, centar, zakret; brid 0 gleda prema +Z |
| `FootprintFromCurveNode` | S | radi | zatvorena krivulja → obris bez dijelova (samo ravni krov) |
| `FloorStackNode` | S | radi | broj katova, visina kata, visina prizemlja |
| `WallsNode` | S | radi | zidovi svih katova; prozori ravnomjerno po bridu, vrata na bridu `door_edge`; otvori su paneli, bez booleana |
| `SlabNode` | S | radi | ploča na svakom katu (uvučena u zid), strop zadnjeg kata, podnožje do elevacije |
| `RoofNode` | S | radi | flat (+ parapet) / gable / hip / shed; kosi krovovi po dijelovima obrisa (L i U dobiju križne krovove) |
| `StairsNode` | S | radi | ravne pune stepenice s ogradom |
| `RoadFromCurveNode` | S | radi | asfalt, rubnjak, pločnik s obje strane |

Zadane oznake novih čvorova (mijenjaju se sa `SetMaterial` + filter po semantici):
zid vani `wall_exterior`/plaster, zid unutra `wall_interior`/plaster, špalete `frame`/plaster, staklo `window`/glass,
vrata `door`/wood_planks, krov `roof`/roof_tiles (ravni: concrete), zabat `wall_exterior`/plaster,
podgled `trim`/wood_planks, ploče `floor`+`ceiling`/concrete, podnožje `foundation`/concrete,
stepenice `stairs`/concrete, ograda `railing`/metal, cesta `road`/asphalt, `curb`/concrete, `sidewalk`/paving.

## Fale

| Čvor | Razina | Za što |
|---|---|---|
| `RoomSplit` | S | sobe i hodnik unutar Footprinta, unutarnji zidovi s vratima, tip interijera po seedu |
| `FloorStack` s uvlačenjem | S | različit tlocrt po katu (terase, neboderi) |
| Otvori po pravilu | S | trenutačno samo razmak prozora i jedna vrata; fale balkoni, izlozi u prizemlju, prozori po katu |
| Stepenice između katova | S | `Stairs` s visinom iz Footprinta i otvorom u ploči |
| `Noise` / `Displace` | S | teren |
| Raskrižja | S | spajanje više `RoadFromCurve` |

## Vokabulari (samo dodavanje na kraj)

Semantika: floor, ceiling, wall_exterior, wall_interior, roof, window, door, frame, stairs, railing, foundation,
trim, glass, road, sidewalk, curb, rope, chain_link, terrain, prop.

Materijali: plaster, brick, stone, concrete, wood_planks, wood_beam, roof_tiles, roof_metal, glass, metal,
steel_chain, asphalt, paving, rope_fiber, ground_dirt, grass.

Imena enum parametara u receptu: `shape` = rectangle / l_shape / u_shape, `roof_type` = flat / gable / hip / shed.
