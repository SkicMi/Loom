# AgentOfWeavers — dnevnik akcija

Najnovije gore. Svaka akcija: datum, što, zašto, kako je provjereno.

## Sadašnje

### STAO SAM OVDJE (2026-09-26, ~21:30) — interijer za slobodnu krivulju gotov, namještaj NIJE započet
Gotovo i commitano (8fc8adc): FootprintFromCurve `rectify`, rastav na zone, planer na stablu zona, niše, pravilo
fasade 2.2 m. `tests/test_procedura_300.cpp` 7/7 (286/300, 93/100 skica, 0 rupa/tamnih/premalih soba).

**Sljedeće: namještaj — dogovoreni pristup (korisnik: "podaci moraju dolaziti zasebno", AI će pisati i alate,
namještaj itd., ne samo kuće):**
1. **Asset = zaseban proceduralni recept** (JSON), npr. `procedura/assets/furniture/bed_basic.loomasset.json`:
   `format: loom.weaverprocedura.asset`, `id`, `category` (bed, wardrobe, sofa, armchair, table, chair, desk,
   kitchen_counter, fridge, toilet, sink, bathtub, shower, shelf, nightstand, tv_stand...), `parameters`
   [{name, default, min, max}], `bounds` (širina/dubina/visina kao izrazi parametara), `placement` (wall / center /
   corner), `clearance_front`, i `recipe` — običan recept u kojem broj može biti `{"param":"width","scale":0.5,
   "offset":0}` (JSON predložak, razriješi se prije parsiranja; treba `parse(const AgentJsonValue&)`).
   Isti format će AI učiti i za alate/rekvizite → treniraju se odvojeno od kuća.
2. **Engine:** sučelje `AssetLibrary` (build(id, params) → MeshData; info(id); popis po kategoriji),
   `evaluate(graph, const AssetLibrary*)`. Novi tip porta **Placements**; `FurnishNode` (Footprint s planom →
   Placements: asset id, parametri, položaj, zakret 0/90/180/270, kat, soba) — raspored je PODATAK, bez geometrije;
   `PlaceAssetsNode` (Placements → Mesh) gradi svaki (asset, parametri) jednom i instancira. EvaluationResult
   izlaže i placements (za dataset).
3. **Loom:** `RecipeAssetLibrary` čita mapu asseta (LOOM_ROOT_DIR), koristi je panel, --recept i testovi.
4. **Pravila rasporeda po tipu sobe** (pravokutnici u lokalnom okviru sobe, sudari AABB, slobodna zona vrata
   0.9×0.9, prozorski zid bez visokog namještaja): spavaća (krevet uza zid dalje od vrata + noćni ormarići, ormar),
   dnevni (sofa nasuprot TV-a, stolić, fotelja, polica), kuhinja (niz elemenata uz najdulji zid bez vrata, hladnjak
   na kraju, stol sa stolicama ako stane), kupaonica (WC, umivaonik, kada ≥ 1.7 m inače tuš), ured (stolovi uz
   prozore), sastanci (stol + stolice), predsoblje (ormarić za cipele).
   Debljina zidova: plan treba znati vanjski zid i pregradu (dodati u RoomSplit, Walls/Interior ih uzimaju iz plana).
5. **Test:** proširiti test od 300 kuća: bez sudara, vrata slobodna, svaka spavaća ima krevet, kuhinja niz, kupaonica
   WC; render presjeka s namještajem. Novi materijali (fabric, ceramic) samo dodavanjem na kraj; boje u
   `src/LoomPbr.h` — pažnja, ta datoteka ima necommitane izmjene drugog agenta (commitati samo svoj hunk).

Alati za pregled (scratchpad, prenijeti u `tools/` ako trebaju): OBJ izvoz iz recepta, Blender presjeci/pogledi
(workbench), kopija testa koja piše OBJ-ove.

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

### 2026-09-26 — Interijer za tlocrt iz slobodne krivulje
- `FootprintFromCurveNode.rectify` (JSON `rectify`, bez polja = false): okvir po najduljem bridu, bridovi se svrstaju
  u x/z, nizovi se spoje na srednju liniju, paralelne linije bliže od 0.5 m postanu jedna (inače trake između krakova).
- Pravokutni obris → zone: najbolji od tri rastava (najveći pravokutnik, vodoravne trake, okomite trake) po broju
  zona manjih od 2.4 m; krovni dijelovi zalaze do pola roditelja sa `joined` stranom (kosi krovovi i za skice).
- Planer: stablo zona (roditelj = prva zona s ≥ 1 m zajedničkog zida), hodnik okomito na granicu, bočni hodnik glavnog
  dijela prema većini djece, dijete prema sredini roditelja, spojnice kroz red roditelja. Rezultati za pravokutnik/L/U
  ostali isti (287/300).
- Zona premalena za sobe (≥ 1.2 m) = niša/ostava; ćelija bez 2.2 m fasade na jednom zidu = kupaonica/ostava.
  Nađeno testom: tamna kupaonica pretvarana u spavaću (servisni pojas), dnevni boravak mogao biti taman.
- Test od 300: zadnjih 100 su ručne skice (T, H, Z, križ, stepenice, zarez; šum ±0.3 m, zakret) → 286/300,
  93/100 skica, 0 rupa, 0 tamnih soba. Render 10 skica izvana i u presjeku: krovovi i planovi ispravni; manja mana:
  kod križa se dijelovi krova različitog raspona malo probijaju.


### 2026-09-26 — Interijer po pravilima (RoomSplit, Interior) i test od 300 kuća
- Korisnik pitao kako ručno štimanje pomaže AI generatoru — odgovor: ne štima se kuća nego generatori/pravila; dokaz
  sampler: 1000 nasumičnih kuća, 0 rupa, svi padovi su pravila s razlogom; PNG 20 kuća poslan.
- Popravci vanjštine iz tog pregleda: zabat krila probijao glavni krov (FootprintPart.joined: spojena strana bez
  prepusta i hipa), pukotine na uglovima (krajevi brida su točno spremljeni uglovi; test zatvorenosti na točnim
  floatovima), podnožje u ravnini ploče (samo prsten), stepenice do podignutih vrata, pravila tlocrta (dvorište U ≥ 2 m,
  krila strše ≥ 1.5 m, jednostrešni krov samo na pravokutniku). Commit 605c524.
- `RoomSplitNode` (Footprint → Footprint, plan u `Footprint.plan`) i `InteriorNode` (Footprint → Mesh); pravila u
  CVOROVI.md → "Pravila interijera". Footprint dobio lokalni okvir (`frameCenter`, `frameAxis`) i `zones`.
  FloorStack briše plan (plan vrijedi za jedan broj katova), pa je redoslijed Footprint → FloorStack → RoomSplit.
- Walls s planom: prozori po sobama i tipu, ulazna vrata gdje plan kaže; rubovi otvora se poravnaju na mrežu prijeloma
  (prozori različite visine razlikovali su se u zadnjem bitu → 101 od 300 kuća imala je pukotine).
- Slab s planom: ćelije u lokalnom okviru, otvor stubišta na katovima iznad prizemlja.
- Recipe: `room_split` (program residential/office, seed, corridor_width, door_width, entrance_edge), `interior`.
  Panel: kontrole, gumbi "Room Split" / "Interior", "Create house" sada s interijerom. Recepti kuca_l, vila_u (dom) i
  blok (ured) imaju interijer.
- Iteracije na testu od 300 (pass / problemi): 187 s 101 rupom → 0 rupa → 267 → 280 → realne mjere (spavaća 17.6 → 14.2 m²,
  kupaonica 12.7 → 8.8 m² uvođenjem dvoslojnih redova i ograničenja širine) → dnevni boravak spaja ćelije (L 12×10
  prije nije prolazio) → stubište 3 m duboko → **287/300, 0 rupa, 0 soba bez prozora, 0 premalih soba, svaki dom
  ima kuhinju i kupaonicu**; preostalih 13 su stvarno premale kuće (razlog u poruci).
- Provjera: `test_weaverprocedura` 75/75 (8 novih za interijer), `test_procedura_300` 6/6 (0.3 s za 300 kuća),
  agent 29/29; vizualno: presjeci prizemlja 20 kuća i 4 kadra u razini očiju (hodnik, dnevni boravak, stubište, kuhinja).
- Nije napravljeno: namještaj, RoomSplit na obrisu iz krivulje, stubište s krakovima po dubini reda.


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
