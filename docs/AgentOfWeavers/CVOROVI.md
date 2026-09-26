# WeaverProcedura čvorovi — stanje za AgentOfWeavers

Razina: **N** = niska (za ljude), **S** = srednja (ono što model bira). Stanje: radi / popravak / fali.
Tipovi portova: Curve, Profile, PointGrid, Mesh, Points, **Footprint** (shema 7).

## Postojeći (shema 8)

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
| `FootprintFromCurveNode` | S | radi | zatvorena krivulja → obris; `rectify` poravna skicu na prave kutove → zone, kosi krov, RoomSplit |
| `FloorStackNode` | S | radi | broj katova, visina kata, visina prizemlja |
| `WallsNode` | S | radi | zidovi svih katova; prozori ravnomjerno po bridu, vrata na bridu `door_edge`; otvori su paneli, bez booleana |
| `SlabNode` | S | radi | ploča na svakom katu (uvučena u zid), strop zadnjeg kata, podnožje do elevacije |
| `RoofNode` | S | radi | flat (+ parapet) / gable / hip / shed; kosi krovovi po dijelovima obrisa (L i U dobiju križne krovove) |
| `StairsNode` | S | radi | ravne pune stepenice s ogradom |
| `RoadFromCurveNode` | S | radi | asfalt, rubnjak, pločnik s obje strane |
| `RoomSplitNode` | S | radi | pravila interijera: program (home/office), seed, širina hodnika, vrata, ulazni brid → plan u Footprintu |
| `InteriorNode` | S | radi | pregradni zidovi s otvorima (stupići na spojevima), krila vrata otvorena 90°, podovi po sobi, stubište s dva kraka |
| `FurnishNode` | S | radi | Footprint s planom → **Placements** (podatak: asset, parametri, položaj, zakret, kat, soba); pravila po tipu sobe |
| `PlaceAssetsNode` | S | radi | Placements → Mesh; svaki (asset, parametri) gradi jednom |
| `AssetNode` | S | radi | jedan asset kao mesh u ishodištu (rekvizit, alat) |

Asseti su zasebni recepti (`procedura/assets/**/*.loomasset.json`, pišu ih `procedura/assets/napravi.py`): `id`,
`category`, `parameters` [{name, default, min, max}], `bounds` (širina, visina, dubina), `placement`, `clearance_front`,
`recipe`. Broj u receptu ili `bounds` smije biti `{"param": ime, "scale": s, "offset": o}`. Prostor asseta: baza na y = 0,
sredina u ishodištu, širina po X, prednja strana prema +Z. `evaluate(graph, &library)`; bez knjižnice Furnish/Place
Assets/Asset padaju s razlogom. Editor koristi `defaultAssetLibrary()` (LOOM_ROOT_DIR/procedura/assets).
Asset ima i `style` (basic / modern / rustic, zatvoren popis, samo dodavanje; bez polja = basic). `Furnish.style`
daje cijeloj kući jedan stil (prazno = po seedu); kategorija bez asseta tog stila uzima basic. 51 asset: 22 basic,
15 modern, 14 rustic.
Alati, oružje i rekviziti su isti format (mape `tools/`, `weapons/`, `props/`, uče se odvojeno od namještaja).
Zatvoren popis `assetCategories()` s vrstom `assetKind()`: furniture / tool (hammer, axe, saw, shovel, pickaxe,
wrench, screwdriver, knife) / weapon (sword, spear, mace, club) / prop (crate, barrel, bucket, lantern, bottle,
book, plant_pot, chest). Nepoznata kategorija ne učita se. Prostor alata: kraj drške na y = 0, glava/oštrica prema
+Y, udarna strana prema +Z. Alat i oružje moraju imati `grips` (polja Warp Grip: point, axis, palm, thickness,
preset, hand; brojevi smiju biti `{"param"}`), preset iz poza šake (grip, pistol, cup, fist, point, relaxed, open).
`writeGlb` (src/LoomProceduraGlb.h) izvozi mesh s hvatovima u `extras.loom_tool`.
Kategorije namještaja: bed, nightstand, wardrobe, desk, chair, sofa, armchair, coffee_table, tv_stand, shelf, table,
kitchen_counter, fridge, toilet, sink, bathtub, shower, shoe_cabinet, rug, floor_lamp, table_lamp, wall_cabinet.
Placement ima i `base` (visina dna iznad poda: lampa na ormariću, gornji element) i `under` (indeks stola pod koji je
stolica uvučena); komadi koji se ne preklapaju po visini, stolica pod svojim stolom i tepih nisu sudar.

Zadane oznake novih čvorova (mijenjaju se sa `SetMaterial` + filter po semantici):
zid vani `wall_exterior`/plaster, zid unutra `wall_interior`/plaster, špalete `frame`/plaster, staklo `window`/glass,
vrata `door`/wood_planks, krov `roof`/roof_tiles (ravni: concrete), zabat `wall_exterior`/plaster,
podgled `trim`/wood_planks, ploče `floor`+`ceiling`/concrete, podnožje `foundation`/concrete,
stepenice `stairs`/concrete, ograda `railing`/metal, cesta `road`/asphalt, `curb`/concrete, `sidewalk`/paving.

## Fale

| Čvor | Razina | Za što |
|---|---|---|
| Asseti za alate i rekvizite | S | isti format kao namještaj, druge kategorije |
| Kosi obris (ne pravokutni) za RoomSplit | S | sada treba `rectify` |
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

## Pravila namještaja (Furnish)

1. Korisni pod sobe = pravokutnik sobe umanjen za vanjski zid (`wall_thickness`) ili pola pregrade.
2. Vrata: u sobi u koju se krilo otvara slobodno je cijelo krilo (≥ 1 m), s druge strane korak od 0.6 m; ulazna vrata
   1.5 × 1.3 m. Kupaonica se otvara prema van (i u `Interior`). Otvorena strana predsoblja ostaje prohodna.
3. Komad stoji uz zid (leđa uz zid) ili u sredini (stol); tijelo ne dira vrata, druga tijela ni slobodni pod ispred njih;
   slobodni pod komada ostaje u sobi i ne dira tijela.
4. Visoki komadi (> 1.2 m) ne stoje uz vanjski zid s prozorima ni ispred prozorskog pojasa susjednog zida.
5. Spavaća: krevet (1.6 / 1.4 / 0.9) što dalje od vrata, ne pod prozorom, s prolazom 0.55 uz bokove (bračni oba),
   noćni ormarići, ormar u kutu; stol ako soba ≥ 11 m². Dnevni: sofa (razmak do suprotnog zida 2.2–5 m), stolić ispred,
   TV nasuprot, fotelja, polica, blagovaonski stol ako ≥ 24 m². Kuhinja: najdulji slobodni niz (1.2–4.2 m), hladnjak
   uz njega, stol sa stolicama ako stane. Kupaonica: kada, inače tuš, inače ništa — WC i umivaonik moraju stati.
   Ured: stolovi sa stolicom (prednost prozoru) dok stanu. Sastanci: stol sa stolicama. Predsoblje: ormarić za cipele.
   Ostava: police.
   Dodaci: kuhinjski niz u kutu dobiva krak na susjednom zidu (bez sudopera i ploče, `fixtures` 0); gornji elementi
   (dno 1.45 m) nad svakim krakom koji nije pod prozorom, skraćeni do prozorskog pojasa susjednog zida; tepih pod
   stolićem i pod donjim dijelom kreveta (ne u zamahu vrata); stolne lampe na noćnim ormarićima, podna lampa uz sofu;
   stolice 12 cm pod stolom i zakrenute do ±8° (za radnim stolom ±10°). WC isprobava sva slobodna mjesta dok ne stane
   i umivaonik.
6. Obavezni komadi (krevet, sofa, kuhinjski niz, WC + umivaonik, stol u uredu i sastancima) — ako ne stanu, greška s
   razlogom i mjerama sobe. Ostali komadi ovise o `fill` (1 = svi koji stanu).

## Vokabulari (samo dodavanje na kraj)

Semantika: floor, ceiling, wall_exterior, wall_interior, roof, window, door, frame, stairs, railing, foundation,
trim, glass, road, sidewalk, curb, rope, chain_link, terrain, prop, furniture.

Materijali: plaster, brick, stone, concrete, wood_planks, wood_beam, roof_tiles, roof_metal, glass, metal,
steel_chain, asphalt, paving, rope_fiber, ground_dirt, grass, fabric, ceramic, lacquer, leather, linen.

Stilovi namještaja: basic, modern, rustic.

Imena enum parametara u receptu: `shape` = rectangle / l_shape / u_shape, `roof_type` = flat / gable / hip / shed.
