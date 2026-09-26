# AgentOfWeavers — dnevnik akcija

Najnovije gore. Svaka akcija: datum, što, zašto, kako je provjereno.

## Sadašnje

### STAO SAM OVDJE (2026-09-26, ~19:40) — faza 2, prvi krug gotov
Faza 1 je zatvorena, a faza 2 ima prvi krug čvorova srednje razine (vidi Prošle, zadnji unos).
**Sljedeće:** `RoomSplit` (sobe i hodnik unutar Footprinta, unutarnji zidovi s vratima) pa `Stairs` između katova
(otvor u ploči). Nakon toga faza 3 (`ProceduraGen`).
Testni recepti su u `docs/AgentOfWeavers/recepti/` (`python3 napravi.py` ih ponovno piše), a snimka:
`HOME=<scratch>/home DISPLAY=:78 ./build/loom --recept docs/AgentOfWeavers/recepti/kuca_l.loomrecipe.json --snimi out.png`
(vlastiti HOME da se ne pojavi dijalog korisnikovog autosavea). Editor se gradi metom `LoomDesk`.

## Buduće

Redom kojim se radi; kad se počne, stavka ide u Sadašnje.

2. **Faza 2, ostatak** — `RoomSplit`, stepenice između katova, FloorStack s uvlačenjem po katu, bogatija pravila
   otvora (izlozi u prizemlju, balkoni), `Noise`/`Displace` za teren, raskrižja cesta. Popis: CVOROVI.md → Fale.
3. **Faza 3: `ProceduraGen`** — headless C++ program: sampler recepata iz gramatike, evaluate, validatori pravila,
   petlja Pass / Fail → Why → Retry, JSONL zapis (prompt + akcije + metrike + seed + verzija), popravci kao primjeri.
4. **Faza 4** — korisnik piše ~200 held-out promptova (hr/en).
5. **Faza 5** — model 3–5M, zamrznuti višejezični encoder (kandidati: multilingual-e5, bge-m3), maskiranje akcija.
6. **Faza 6** — skaliranje, kontrastni parovi za uređivanje, vizualni evaluator.

## Prošle

### 2026-09-26 — Faza 2, prvi krug: zgrade i ceste (shema 7)
- Novi tip porta `Footprint` (obris u XZ s pozitivnom površinom + pravokutni dijelovi za krov + elevacija, katovi,
  visina kata). Geometrija je u novoj datoteci `engine/src/Engine/WeaverProceduraBuilding.cpp`.
- Čvorovi: `Footprint` (rectangle / l_shape / u_shape, brid 0 gleda prema +Z pa su `door_edge` 0 ulazna vrata),
  `FootprintFromCurve`, `FloorStack`, `Walls` (prozori ravnomjerno po bridu, vrata; otvori su paneli, bez booleana),
  `Slab` (ploča po katu, strop zadnjeg kata, podnožje), `Roof` (flat + parapet / gable / hip / shed; kosi po dijelovima,
  pa L i U dobiju križne krovove), `Stairs`, `RoadFromCurve` (asfalt, rubnjak, pločnik).
  `Openings` iz plana je spojen u `Walls` (parametri otvora), `RoomSplit` još fali.
- Pomoćne funkcije: `triangulatePolygon` (ear clipping, konkavni obrisi), `offsetPolygon` (uvlačenje/izvlačenje s miterom).
- Svaki trokut dobije semantiku i zadani materijal (popis u CVOROVI.md); model mijenja izgled sa `SetMaterial` + filter.
- Greške su objašnjive (za Fail → Why): "windows do not fit in the storey height", "door does not fit on outline edge N",
  "gable, hip and shed roofs need a footprint made of rectangles; use a flat roof for traced outlines" itd.
- Zidovi su zatvoreno tijelo bez T-spojeva: svaka ploha je mreža nad zajedničkim prijelomima (rubovi svih otvora na bridu
  kroz sve katove × visine otvora na katu), kape su prošivene između dva lanca. Prva verzija imala je vidljive
  pukotine na granici katova; nakon toga ostao je z-fighting (bočne plohe ploče 5 cm iza fasade, kosina krova točno kroz
  gornji brid zida) — uvučena ploča više nema bočnih ploha, kosi krov je 5 cm iznad zida, zadani uvlak ploče 0.1 m.
- Recipe shema 7 (učitava 3–7); enum parametri kao imena (`shape`, `roof_type`).
- Panel: nazivi, sažeci i kontrole svih novih čvorova; "Create house" (L, 2 kata, cigla), "Create street", "Create stairs";
  red "ADD BUILDING PARTS" (Footprint, Floors, Walls, Slab, Roof, Stairs, Merge) — čvorovi koji čitaju Footprint sami
  se spoje na zadnji Footprint.
- Testni recepti: `docs/AgentOfWeavers/recepti/` — kuca_l (L, 2 kata, zabat, cigla), vila_u (U, hip), blok (5 katova,
  ravni krov s parapetom), ulica, plus stari kuca i lanac (primitivi). `python3 napravi.py` ih ponovno piše.
- Provjera: `test_weaverprocedura` 66/66 (15 novih: obrisi i površine, triangulacija L, uvlačenje, očuvanje površine
  fasade s otvorima, zatvorenost zidova bez T-spojeva, visina sljemena, parapet, odbijanje kosog krova na obrisu iz
  krivulje, ploče, stepenice, širina ceste, cijela kuća kroz evaluate s oznakom na svakom trokutu, tip porta,
  JSON round-trip), `test_weaver_agent_actions` 29/29, `test_editor_pbr` 8/8. Vizualno na Xvfb :78: sva četiri recepta.
- Nije provjereno klikom: gumbi panela (Create house/street/stairs, ADD BUILDING PARTS) — prevedeni su i koriste iste
  funkcije kao testovi, ali nisu kliknuti na Xvfb.

### 2026-09-26 — Faza 1, drugi krug (shema 6) i zatvaranje faze 1
- `PrimitiveType::Cylinder`, `Torus` (+ `tubeRatio`); `ExtrudeNode.useFilter` + `filter` (sve plohe koje odgovaraju);
  `CurveSmoothNode` (Catmull-Rom), `CatenaryCurveNode` (uže/lanac), `CopyAlongCurveNode` (lanac: alternateRoll 90°).
- Recipe shema 6 (učitava 3–6). `inputPortCount()` u engineu.
- Viewport: preview se crta po materijalu, svaki svojom bojom (`proceduralMaterialColour` u `src/LoomPbr.h`); boje se
  iz sRGB tablice pretvaraju u linearno (`pow 2.2`), crijep potamnjen — cigla, crijep i trava se sada razlikuju.
- Panel: ručno spajanje (CONNECT: From / To / Input, Remove link), "Create rope" / "Create chain", Torus/Cylinder, Extrude filter.
- Editor zastavica `--recept <datoteka>`: učita recept u Procedura panel i uokviri preview (za snimke: `--snimi out.png`).
- Tangente za normal mape: nije trebalo ništa — `shaders/pbr.slang` ih računa po pikselu iz derivacija.
- Provjera: test_weaverprocedura 51/51, agent 29/29, PBR 8/8; vizualno lanac + uže + stupovi i kućica (commit 865b4ca,
  snimka boja nakon toga).

### 2026-09-26 — Infrastruktura, prvi krug (shema 5)
- `MeshData.triangles`: po trokutu `createdBy` (čvor), `semantic`, `material`. Evaluator popunjava `createdBy`
  za svaki trokut koji čvor doda; Move/Rotate/Scale/Merge/Copy ga čuvaju, Extrude i Bevel označavaju nove trokute
  svojim čvorom, a naslijede semantiku i materijal plohe.
- Zatvoreni vokabulari `semanticVocabulary()` (20) i `materialLibrary()` (16); ID = položaj + 1, samo dodavanje na kraj.
  Validacija odbija nepoznata imena — to je osnova maskiranja ilegalnih akcija za model.
- Novi čvorovi: `MergeNode` (8 ulaza), `SetSemanticNode`, `SetMaterialNode` (+ `TriangleFilter`: semantika i/ili smjer),
  `SmoothNormalsNode`, `UVProjectNode` (box, metri), `CopyToPointsNode`, `CircleProfileNode`.
- Točke više nisu samo završni izlaz: teku grafom s normalama (Mesh to Point, Point from Mesh → Copy to Points).
- Sweep prima bilo koji Curve i Profile izvor (prije samo Curve + Rectangle izravno).
- `InteriorBlockoutNode` označava `floor`, `wall_exterior`, `wall_interior`.
- Recipe JSON shema 5; učitava 3, 4 i 5.
- Procedura panel: nazivi, sažeci i parametri novih čvorova; gumbi Semantic / Material / Smooth / UV.
- Provjera: `test_weaverprocedura` 43/43 (12 novih), `test_weaver_agent_actions` 29/29, `test_editor_pbr` 8/8.
- Popravljen test koji je trebao provjeravati učitavanje sheme 3, a nakon promjene verzije ne bi ništa provjeravao.

### 2026-09-26 — Uklonjen stari Loom Agent
- Obrisani `tools/weaveragent/` (Python servis, trening, podaci v1/v2, benchmarki), njegov `.venv` (7.3 GB),
  `unsloth_compiled_cache/`, `.cache/weaverprocedura/app-chat`.
- Iz editora uklonjen F8 AI Chat (rail stavka, tipka, panel, pozadinska nit); `callLocalAgentApi` izbačen iz `LoomAgentJson.h`.
- Obrisani `docs/WEAVER_AGENT_API.md`, `docs/WEAVER_AGENT_VOCABULARY.md`, `docs/WEAVER_PROCEDURA_VOCABULARY.md`.
- Zadržani `src/LoomAgentActions.h` (whitelist i izvršavanje akcija na niti editora) i JSON parser u `LoomAgentJson.h`
  (koristi ga Recipe JSON). Težine Qwen3.5-4B i LoRA adaptera na disku već nisu postojale.
- Sve je u gitu (commit prije ovoga) ako ikad zatreba usporedba.

### 2026-09-26 — Dogovor
- Korisnik prihvatio kontra-pitch: prvo knjižnica, zatim generator podataka, zatim model iz nule; projekt u `docs/AgentOfWeavers`.
