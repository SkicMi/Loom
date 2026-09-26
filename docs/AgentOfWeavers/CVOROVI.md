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
| `RoomSplitNode` | S | radi | pravila interijera: program (home/office), seed, širina hodnika, vrata, ulazni brid → plan u Footprintu |
| `InteriorNode` | S | radi | pregradni zidovi s otvorima (stupići na spojevima), krila vrata otvorena 90°, podovi po sobi, stubište s dva kraka |

Zadane oznake novih čvorova (mijenjaju se sa `SetMaterial` + filter po semantici):
zid vani `wall_exterior`/plaster, zid unutra `wall_interior`/plaster, špalete `frame`/plaster, staklo `window`/glass,
vrata `door`/wood_planks, krov `roof`/roof_tiles (ravni: concrete), zabat `wall_exterior`/plaster,
podgled `trim`/wood_planks, ploče `floor`+`ceiling`/concrete, podnožje `foundation`/concrete,
stepenice `stairs`/concrete, ograda `railing`/metal, cesta `road`/asphalt, `curb`/concrete, `sidewalk`/paving.

## Fale

| Čvor | Razina | Za što |
|---|---|---|
| Namještaj | S | krevet, ormar, kuhinjski element, sanitarije, stol — po tipu sobe i pravilima razmaka |
| RoomSplit za obris iz krivulje | S | sada samo tlocrti od pravokutnika |
| Stubište po dubini | S | sada krakovi uvijek idu uzduž reda; plitki redovi (< 2.6 m) nemaju mjesta |
| `FloorStack` s uvlačenjem | S | različit tlocrt po katu (terase, neboderi) |
| Otvori po pravilu | S | trenutačno samo razmak prozora i jedna vrata; fale balkoni, izlozi u prizemlju, prozori po katu |
| `Noise` / `Displace` | S | teren |
| Raskrižja | S | spajanje više `RoadFromCurve` |

## Pravila interijera (RoomSplit)

1. Zone popločavaju tlocrt (pravokutnik: 1, L: glavni dio + krilo, U: glavni dio + 2 krila).
2. Hodnik po dubini zone: ≥ 8 m središnji, 4.4–8 m bočni (glavni dio L/U prema krilima, krilo prema dvorištu,
   pravokutnik po seedu), < 4.4 m bez hodnika (niz soba). Hodnik krila ide do hodnika glavnog dijela (spojnica kroz red).
3. Ulaz: vrata u hodnik ako hodnik dira brid; inače predsoblje u redu iza brida ili na čelu reda; krajnje: soba iza vrata
   postaje predsoblje.
4. Stubište (više katova): 4.4 m dugo, 3 m duboko uz hodnik, isto mjesto na svim katovima; soba iza njega ako je red dublji.
5. Red dublji od 5.2 m: sobe ~4 m uz fasadu + servisni pojas (kupaonice, ostave) s prolazom do svake sobe.
6. Ćelije ~3.4 m (ured 5 m), tipovi po površini: dnevni boravak (spaja susjedne ćelije do 16 m²), kuhinja, kupaonica,
   spavaće (≥ 7 m², ≥ 2.4 m), ostava; kupaonica ≤ 2.6 m, kuhinja ≤ 4.2 m širine; nedostajuća kuhinja/kupaonica se izreže.
7. Vrata: otvoreni prostori (hodnik, predsoblje, stubište) su jedan prostor; svaka soba vrata prema otvorenom prostoru
   s najduljim zajedničkim zidom, inače prema dostupnom susjedu; vrata 0.35 m od kuta.
8. Prozori (Walls): samo unutar sobe; kupaonica mali visoki, hodnik/predsoblje samo na čelu, ostava bez.
9. Greške s razlogom: premalo za dom, nema mjesta za stubište, nema ulaza, soba bez pristupa, zona preplitka.

## Vokabulari (samo dodavanje na kraj)

Semantika: floor, ceiling, wall_exterior, wall_interior, roof, window, door, frame, stairs, railing, foundation,
trim, glass, road, sidewalk, curb, rope, chain_link, terrain, prop.

Materijali: plaster, brick, stone, concrete, wood_planks, wood_beam, roof_tiles, roof_metal, glass, metal,
steel_chain, asphalt, paving, rope_fiber, ground_dirt, grass.

Imena enum parametara u receptu: `shape` = rectangle / l_shape / u_shape, `roof_type` = flat / gable / hip / shed.
