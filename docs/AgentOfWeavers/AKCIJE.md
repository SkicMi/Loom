# AgentOfWeavers — dnevnik akcija

Najnovije gore. Svaka akcija: datum, što, zašto, kako je provjereno.

## Sadašnje

### STAO SAM OVDJE (2026-09-26, ~19:00) — faza 1, drugi krug
Gotovo i testirano (test_weaverprocedura 51/51, agent 29/29, PBR 8/8), commitano u ovom commitu:
- `PrimitiveType::Cylinder`, `Torus` (+ `tubeRatio`); `ExtrudeNode.useFilter` + `filter` (sve plohe koje odgovaraju);
  `CurveSmoothNode` (Catmull-Rom), `CatenaryCurveNode` (uže/lanac), `CopyAlongCurveNode` (lanac: alternateRoll 90°).
- Recipe shema 6 (učitava 3–6). `inputPortCount()` u engineu.
- Viewport: preview se crta po materijalu, svaki svojom bojom (`proceduralMaterialColour` u `src/LoomPbr.h`).
- Panel: ručno spajanje (CONNECT: From / To / Input, Remove link), gumbi "Create rope" / "Create chain",
  Torus/Cylinder u izboru primitiva, Extrude filter.
- Editor zastavica `--recept <datoteka>`: učita recept u Procedura panel i uokviri preview (za snimke: `--snimi out.png`).
- Vizualno provjereno na Xvfb :78: lanac + uže + stupovi i kućica s krovom izgledaju ispravno.

**Sljedeći korak koji NIJE napravljen:** boje materijala u viewportu su isprane (cigla i crijep gotovo iste, trava blijeda).
Plan: u `proceduralMaterialColour` vratiti `glm::pow(boja, vec3(2.2))` (tablica je sRGB, faktor je linearan) i
crijep potamniti na (0.48, 0.20, 0.14); zatim ponovno snimiti:
`DISPLAY=:78 ./build/loom --recept <house.loomrecipe.json> --snimi house.png`.
Testni recepti (lanac, kuća) su bili u scratchpadu — kod nastavka ih treba ponovno napisati ili spremiti u `docs/AgentOfWeavers/recepti/`.

Nakon toga faza 1 je gotova → ažurirati CVOROVI.md (torus, valjak, krivulje, Extrude filter = radi) i krenuti na fazu 2.

## Buduće

Redom kojim se radi; kad se počne, stavka ide u Sadašnje.

1. **Faza 1, ostatak** (većina gotova — vidi "STAO SAM OVDJE"; ostaju samo boje materijala)
   - Viewport: preview crta svaki `material` iz `materialLibrary()` svojim PBR materijalom (sad je jedan materijal za sve).
   - Procedura panel: spajanje linkova rukom (Merge i Copy to Points sad se mogu učitati iz JSON-a i podešavati, ali ne spojiti klikom).
   - `ExtrudeNode` i `BevelNode` s `TriangleFilter` umjesto indeksa trokuta (AI ne smije ovisiti o indeksima).
   - Torus i valjak u `AddPrimitiveNode` (lanac, stupovi).
   - Glađenje krivulje (Catmull-Rom resample) i `CopyAlongCurve` s orijentacijom po tangenti.
   - `CatenaryCurve` (dvije točke + progib) za uže i lanac.
   - Tangente za normal mape (ako ih renderer ne računa sam — provjeriti `shaders/pbr.slang`).
2. **Faza 2: čvorovi srednje razine** — `Footprint`, `FloorStack`, `WallsFromFootprint` (otvori u zidu), `Openings`,
   `Roof` (flat/gable/hip/shed), `RoomSplit`, `Slab`, `Stairs`, `RoadFromCurve`; svaki s testovima i semantikom.
3. **Faza 3: `ProceduraGen`** — headless C++ program: sampler recepata iz gramatike, evaluate, validatori pravila,
   petlja Pass / Fail → Why → Retry, JSONL zapis (prompt + akcije + metrike + seed + verzija), popravci kao primjeri.
4. **Faza 4** — korisnik piše ~200 held-out promptova (hr/en).
5. **Faza 5** — model 3–5M, zamrznuti višejezični encoder (kandidati: multilingual-e5, bge-m3), maskiranje akcija.
6. **Faza 6** — skaliranje, kontrastni parovi za uređivanje, vizualni evaluator.

## Prošle

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
