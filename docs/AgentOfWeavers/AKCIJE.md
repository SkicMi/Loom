# AgentOfWeavers — dnevnik akcija

Najnovije gore. Svaka akcija: datum, što, zašto, kako je provjereno.

## Sadašnje

### STAO SAM OVDJE (2026-09-27, ~00:30) — podaci za prvi model (zgrade, engleski) gotovi
Korisnik: opisi i model **isključivo na engleskom**; odobreno preuzimanje encodera; prije treninga riješiti
**UV i materijale** tako da ih model zna.
- **1. Shema kao podatak** — `src/LoomProceduraSchema.h`: 12 tipova čvorova (footprint, floor_stack, room_split,
  walls, slab, roof, interior, furnish, place_assets, merge, set_material, uv_project), svaki parametar s vrstom,
  rasponom, korakom ili opcijama i tipovi portova; `schemaJson()`; `actionSchemaVersion = 1`.
- **2. Recept ↔ akcije** — `src/LoomProceduraActions.h`: `ADD/SET/CONNECT/END`, kanonski redoslijed, sve vrijednosti
  na mreži sheme; `actionsToDocument` javlja prvi nedozvoljeni korak (isto što dekoder maskira).
- **3. ProceduraGen** — `src/procedura_gen.cpp` (`build/procedura-gen`), predložak i uzorkovanje u
  `src/LoomProceduraHouses.h`: kuća + materijal po semantici (vanjski zid, krov, okviri, vrata) + UV projekcija.
  Fail → Why → Retry: pad se zapisuje s razlogom, pravilo popravka mijenja jednu stvar, ponovni pokušaj nosi
  `retry_of` i `repair`. 10 000 kuća (seed 1): 9992 prolazi, 478 nakon popravka, 97 akcija po kući, 28 s.
- **4. Opisi** — tri engleska opisa po kući (kratak, srednji, detaljan), samo istinite činjenice iz izračunatog plana
  (sobe, katovi) i parametara (krov, materijali, prozori, stil).
- **5. Encoder** — `tools/agentofweavers/` (venv, torch 2.11 cu128, `embed.py`): zamrznuti `BAAI/bge-base-en-v1.5`,
  768-d; 29 976 opisa → `.cache/agentofweavers/data/houses-v0.1/embeddings.f16.npy` (44 MB).
- **6. Codex** — `docs/AgentOfWeavers/heldout/CODEX_PROMPT.md`: 200 held-out promptova s `expect` ključevima i
  provjernom skriptom.
- Testovi: `test_procedura_actions` 15/15 (povratni put, 9 vrsta nedozvoljenih koraka, shema, 60 kuća s opisima).
  Gradi se u `build-aow` jer je drugi agent gradio u `build/`.

**Sljedeće (redom):**
1. **UV i materijali prije treninga** (korisnik): prave PBR teksture po materijalu (boja, hrapavost, normal) umjesto
   same boje, UV u metrima po plohi (zid, pod, krov) i provjera u editoru/Blenderu da se teksture ne rastežu;
   materijali poda i unutarnjih zidova (sad samo vanjski zid, krov, okviri, vrata) u predložak i shemu.
2. Prvi model (3–5M, dekoder akcija nad ugradnjom opisa, maskiranje iz `schema.json`), pa metrike: udio valjanih
   recepata, točnost po parametru, prolaz kroz evaluate, na val skupu i na held-out promptovima.
3. Kad Codex napiše held-out: `embed.py --prompts` i evaluator koji uspoređuje `expect` s receptom.
4. Parafraze opisa (sada su predlošci; "cozy", "cottage" itd. nisu u rječniku).
5. Preostalo iz ranijeg popisa: više oblika alata/rekvizita, rekviziti u rasporedu.

### 2026-09-26, ~22:40 — Export GLB i uvoz hvata (bivši "STAO SAM OVDJE")
- **1. Export GLB** (`862f2a6`): gumb u Procedura panelu piše `Ime.glb` uz recept; ako recept završava jednim
  Asset čvorom alata/oružja, datoteka nosi `extras.loom_tool` s hvatom za te parametre. Tablica boja premještena u
  `src/LoomProceduraLook.h` (u tuđem `LoomPbr.h` samo include i brisanje funkcije). Test 26/26.
- **2. Uvoz hvata iz recepta** (`aac7eef`): `readGlbTool` (LoomProceduraGlb.h); uvoz alata uzima hvat i vrstu iz
  datoteke umjesto procjene iz oblika i postavlja svjetsko mjerilo 1 (uvoz inače pogađa metar iz visine kamere →
  mač je bio 147 cm umjesto 98). Editor: `--recept X --izvezi Y.glb`. Provjera na Xvfb :78:
  `--mascott --tool mac.glb --uhvati desna`: mač 98 cm u desnoj ruci, dlan na dršci (9.6 % duljine). Test 27/27.
- Nezgoda bez štete: djelomični commit tuđeg `loom_app.cpp` s `git apply --unidiff-zero` stavio je umetanja na krive
  retke; commit je složen iznova iz roditelja (samo mojih 34 retka), radna datoteka drugog agenta netaknuta.
- Test 300 kuća i dalje 16/16 (286/300; padovi: 4 premalo za dom, 4 stubište, 5 skica s preuskom zonom, 1 vrata).

**Sljedeće:** 3. ProceduraGen, 4. više oblika alata/rekvizita, 5. rekviziti u rasporedu. Korisnik je pitao što još
treba na zgradama prije prvog modela — odgovor je u razgovoru i u "Prije prvog modela (zgrade)" dolje.

### Prije prvog modela (zgrade) — popis, 2026-09-26
Obavezno: (a) shema čvorova kao podatak (ime, tip, raspon, korak po parametru) za maskiranje i diskretizaciju;
(b) kanonski niz akcija ↔ recept u oba smjera; (c) ProceduraGen s uzorkovanjem po predlošku kuće, JSONL + Fail → Why;
(d) opisi hr/en iz parametara i iz izračunatog plana (broj soba, katova, tip krova, stil); (e) zamrznuti encoder
(preuzimanje ~2 GB, uz odobrenje) i embeddinzi unaprijed; (f) ~200 ručnih promptova (korisnik).
Dobro bi bilo: balkoni, terase/uvlačenje katova, garaža/nadstrešnica, dimnjak, fasada po katu, ograda i dvorište;
proširiti vokabular tek poslije V0.

### 2026-09-26, noć — stanje nakon namještaja, stilova i alata (bivši "STAO SAM OVDJE")
**Napravljeno u ovoj sesiji** (detalji u Prošle, commitovi cf7115f → ddded43):
- Namještaj po pravilima: asseti kao zasebni recepti (`procedura/assets`), `AssetLibrary`, `Furnish` → Placements
  (podatak), `PlaceAssets`, `Asset`, shema 8; vrata kupaonice prema van.
- Drugi krug: kutna kuhinja, gornji elementi, tepisi, lampe, stolice pod stolom (`Placement.base`, `under`).
- Stilovi basic / modern / rustic (zatvoren vokabular, jedan stil po kući, stil je podatak u konektoru).
- Alati, oružje, rekviziti: zatvoren popis kategorija, hvat za šaku (`AssetGrip` = Warp Grip), `writeGlb`.
- Klik-test u editoru na Xvfb-u (Furnish, Create house), plan arhitekture faze 5 (mali modeli po jeziku).
- Stanje: 71 asset; `test_procedura_assets` 25/25, `test_procedura_300` 16/16 (286/300 kao bez namještaja,
  ~25 000 komada, 0 sudara / u vratima / uz prozor, stilovi 90/104/92), `test_weaverprocedura` 75/75.
- Nezgoda: ispražnjen i vraćen tuđi `src/LoomPbr.h` (vidi Prošle); drugi agent neka ga pregleda.

**Sljedeće (redom):**
1. Gumb "Export GLB" u Procedura panelu — treba tablicu boja iz `src/LoomPbr.h` (premjestiti u zaseban header).
   Dira tuđu datoteku: tek uz odobrenje korisnika ili kad drugi agent commita svoj rad.
2. Uvoz alata koji čita `extras.loom_tool` (hvat iz recepta umjesto procjene iz oblika) — `loom_app.cpp` /
   `LoomImporter.h`, isti uvjet kao 1. Zatim provjera na Xvfb-u: lik drži procedurni mač (`--tool`, `--uhvati`).
3. Faza 3 (`ProceduraGen`): headless generator — kuće s namještajem i stilom, asseti po kategoriji; JSONL s
   receptom, Placements, stilom, metrikama i Fail → Why; odvojeni skupovi po jeziku (zgrade / raspored / objekti).
4. Više asseta po kategoriji alata/rekvizita (2–3 oblika) i stilovi i za njih, da model objekata ima raznolikost.
5. Rekviziti u rasporedu: Furnish (ili novi čvor) stavlja knjige na police, bocu i lampu na stol, sanduke u ostavu.

### 2026-09-26, kasno — stanje nakon prvog kruga namještaja (bivši "STAO SAM OVDJE")
Gotovo: asseti kao zasebni recepti, `AssetLibrary`, `Furnish` → Placements (podatak), `PlaceAssets`, `Asset`, shema 8,
18 asseta, pravila po tipu sobe, test od 300 kuća s namještajem 14/14 (285/300, 92/100 skica, 18 835 komada, 0.56 s),
`test_procedura_assets` 15/15, panel (gumb Furnish, "Create house" s namještajem). Detalji u Prošle.

**Sljedeće (redom):**
1. ~~Klik-test na Xvfb~~ — gotovo, vidi Prošle.
2. ~~Namještaj: kutni niz, gornji elementi, tepisi, lampe, stolice pod stolom~~ — gotovo, vidi Prošle.
3. ~~Stilovi~~ — gotovo, vidi Prošle.
4. ~~Asseti za alate/rekvizite~~ — gotovo, vidi Prošle. Ostaje: gumb "Export GLB" u panelu i uvoz koji čita
   `extras.loom_tool` (hvat iz recepta umjesto procjene iz oblika) — oboje dira tuđe datoteke (LoomPbr.h za boje,
   uvoz alata u loom_app/LoomImporter), pa prvo pitati korisnika.
5. Faza 3 (`ProceduraGen`) može sad bilježiti i Placements u JSONL.

## Buduće

Redom kojim se radi; kad se počne, stavka ide u Sadašnje.

2. **Faza 2, ostatak** — `RoomSplit`, stepenice između katova, FloorStack s uvlačenjem po katu, bogatija pravila
   otvora (izlozi u prizemlju, balkoni), `Noise`/`Displace` za teren, raskrižja cesta. Popis: CVOROVI.md → Fale.
3. **Faza 3: `ProceduraGen`** — headless C++ program: sampler recepata iz gramatike, evaluate, validatori pravila,
   petlja Pass / Fail → Why → Retry, JSONL zapis (prompt + akcije + metrike + seed + verzija), popravci kao primjeri.
4. **Faza 4** — korisnik piše ~200 held-out promptova (hr/en).
5. **Faza 5** — mali modeli 3–5M, zamrznuti višejezični encoder (kandidati: multilingual-e5, bge-m3), maskiranje akcija.
   **Arhitektura (dogovor s korisnikom 2026-09-26):** više malih specijaliziranih modela na zajedničkom zamrznutom
   encoderu, podijeljenih po jeziku koji pišu, ne po kategoriji:
   - **Zgrade**: Footprint, FloorStack, RoomSplit, Walls, Slab, Roof, Interior (čvorovi srednje razine).
   - **Raspored**: bira stil, gustoću, parametre i iznimke za `Furnish` ("stol uz prozor", "bez TV-a"); položaje
     i dalje daju pravila. Kasnije i ulica, dvorište, rekviziti po sceni.
   - **Objekti (asseti)**: novi `.loomasset.json` iz primitiva s parametrima i `bounds`. Namještaj, alati i oružje
     su isti jezik → jedan model s kategorijom kao ulazom; zaseban model za neku kategoriju samo ako held-out
     pokaže da joj zajedničko učenje šteti.
   - **Konektori su podaci, ne veze između mreža**: Footprint s planom (zgrade → raspored), Placements (raspored →
     scena), zahtjev za asset (kategorija + bounds + stil → objekt). Validator provjerava svaku granicu, pa
     Fail → Why → Retry radi i između modela; svaki model se trenira zasebno.
   - **Stil** (npr. "rustikalno") putuje kroz konektor kao nekoliko parametara/tokena, da zgrada i namještaj budu
     usklađeni.
   - **Usmjerivač**: mali klasifikator na izlazu encodera (ili pravilo); za složene promptove planer koji rastavi
     prompt na podzadatke ("kuća s mačem na zidu" → zgrada + raspored + objekt).
   - **Provjera**: na istim podacima jedan zajednički model protiv specijaliziranih, na held-out promptovima iz
     faze 4; mjeri se udio koji prolazi validatore i broj Retry pokušaja.
6. **Faza 6** — skaliranje, kontrastni parovi za uređivanje, vizualni evaluator.

## Prošle

### 2026-09-26 — Alati, oružje i rekviziti kao asseti (s hvatom za šaku)
- Zatvoren popis kategorija (`assetCategories()`, `assetKind()`), 20 novih asseta u `procedura/assets/{tools,weapons,
  props}` (čekić, sjekira, pila, lopata, pijuk, ključ, odvijač, nož; mač, koplje, buzdovan, toljaga; sanduk, bačva,
  kanta, fenjer, boca, knjiga, tegla s biljkom, škrinja), ukupno 71 asset.
- `AssetGrip` = polja Warp Grip; `AssetLibrary::grips(id, parametri)`; hvat ovisi o parametrima (dulja drška,
  hvat drugdje). Alat/oružje bez hvata, nepoznat preset ili kategorija se ne učitaju.
- `src/LoomProceduraGlb.h`: `writeGlb` (primitiv po materijalu, boje od pozivatelja, hvat u `extras.loom_tool`).
- Test hvata: mreža se presiječe ravninom kroz točku hvata okomitom na os; najbliži presjek mora biti unutar debljine
  drške + 1 cm (prva verzija je tražila vrhove oko točke, a valjak ih ima samo na krajevima — lažni pad).
- Provjera: `test_procedura_assets` 25/25 (51 hvat na dršci na min/default/max, preseti = `gripPresets()` iz
  LoomHandPose.h, mač → .glb → Spool ga učita s istim brojem trokuta i hvatom), 300 kuća 16/16, 75/75, 29/29, 8/8.
  Vizualno: svi alati/oružje/rekviziti u Blenderu s označenim hvatovima.
- **Nezgoda:** Python skripta `open(p,'w').write(f(open(p).read()))` ispraznila je `src/LoomPbr.h` s necommitanim
  HDRI radom drugog agenta. Vraćeno iz Codex zapisnika (`~/.codex/sessions`, njegove apply_patch zakrpe) nad
  `865b4ca` + moji commitani redci (3-way merge); `git diff --stat` opet +128/−11 kao prije, editor i PBR test
  prolaze. Premještanje tablice boja iz LoomPbr.h zato odgođeno.

### 2026-09-26 — Stilovi namještaja (basic, modern, rustic)
- Stil je zatvoren vokabular u engineu (`styleNames()`, samo dodavanje), polje `style` u assetu (bez polja = basic,
  nepoznat stil ne učita se) i `FurnishNode.style` (prazno = po seedu; JSON `style`, stari recepti bez polja rade).
  Cijela kuća dobiva jedan stil; kategorija bez asseta tog stila uzima basic. Stil je time podatak u konektoru, kako
  je dogovoreno za arhitekturu faze 5.
- Asseti: 15 modern (bijeli lak, lan, staklo, metalne noge, niski oblici) i 14 rustic (tamno drvo, debele ploče i
  noge, koža, lan, krevet sa stupovima, kamena radna ploča) u `procedura/assets/napravi.py`. Novi materijali lacquer,
  leather, linen (boje u `src/LoomPbr.h`). Panel: izbor Style (By seed / Basic / Modern / Rustic).
- Provjera: `test_procedura_assets` 20/20 (svih 51 asseta ispunjava kutiju na min/default/max; svaki stil ima glavne
  komade; kuća zadanog stila ne miješa stilove; nepoznat stil odbijen; JSON), `test_procedura_300` 16/16 (286/300,
  24 956 komada, 0.73 s; stilovi 90 / 104 / 92 kuća, 0 miješanih), `test_weaverprocedura` 75/75, agent 29/29,
  PBR 8/8. Vizualno: ista kuća u tri stila (Blender).
- Test sada ispisuje sve assete čija se kutija ne slaže s `bounds`, ne samo prvi (tako su nađene tri greške odjednom).

### 2026-09-26 — Namještaj, drugi krug: kutna kuhinja, gornji elementi, tepisi, lampe, stolice pod stolom
- Novi asseti (22): rug, floor_lamp, table_lamp, wall_cabinet; kitchen_counter dobio `fixtures` (0 = sudoper i ploča
  spušteni u korpus, za krak kutnog niza — predložak ne može brisati geometriju, pa je spušta).
- `Placement.base` i `Placement.under`; `put` zna podignuti komad, ostaviti pod slobodnim (tepih, lampa na ormariću,
  gornji element) i zakrenuti ga (površina je tada kutija oko zakrenutog komada).
- Pravila: vidi CVOROVI.md → "Pravila namještaja", Dodaci. Test od 300 dobio provjeru: sudar se ne broji kad su komadi
  razdvojeni po visini, stolica je pod svojim stolom ili je jedan tepih; nova provjera da se dodaci pojavljuju.
- Iteracije: 283/300 (tri kupaonice ~2 m² ovisile su o slučajnom redoslijedu) → WC isprobava sva slobodna mjesta dok
  ne stane umivaonik → **286/300 = isto kao bez namještaja**; gornjih elemenata 54 → niz uz zid bez prozora kao blaga
  prednost (bez učinka) → element skraćen do prozorskog pojasa susjednog zida → 139.
- Rezultat: `test_procedura_300` 15/15 (24 928 komada, 0.68 s; 215/228 kuhinja kutne, 2113 tepiha, 3421 lampa na
  ormariću, 218 podnih lampi, 3746 stolica pod stolom, 139 gornjih elemenata; 0 sudara, 0 u vratima, 0 uz prozor),
  `test_procedura_assets` 15/15, `test_weaverprocedura` 75/75, agent 29/29, PBR 8/8. Vizualno: tlocrti 8 kata i
  Blender perspektiva dviju kuća.

### 2026-09-26 — Klik-test namještaja u editoru (Xvfb :93)
- Editor pokrenut s `HOME` u scratchpadu: autosave nespremljene scene ide u `$HOME/.local/share/loom`, pa se korisnikov
  autosave ne dira i nema dijaloga "Restore Autosave".
- `--recept kuca_namjestena` učita i izračuna kuću; gumb **Furnish** doda Furnish + Place Assets iza Room Splita
  (14 → 16 čvorova, poruka "Join Place Assets to the output with a Merge"); **New Recipe → Create house** daje L kuću
  s namještajem (11 čvorova / 14 veza), bez greške.
- Testni recept bez krova imao je strop zadnjeg kata preko namještaja → `napravi.py`: `top_ceiling` samo s krovom.
  Snimka odozgo: kreveti, ormari, sofa, kuhinjski stol sa stolicama, kada/WC/umivaonik, boje fabric/ceramic.

### 2026-09-26 — Namještaj: asseti kao recepti, Furnish, Place Assets (shema 8)
- Korisnik: "podaci moraju dolaziti zasebno" → asset je zaseban Procedura recept (`procedura/assets/furniture/
  *.loomasset.json`, generira `procedura/assets/napravi.py`) s parametrima i `bounds`; broj u receptu smije biti
  `{"param","scale","offset"}`. 18 asseta (sve kategorije namještaja) od kvadara.
- Engine: `AssetLibrary` (ids/info/bounds/build), port **Placements**, `FurnishNode` (seed, wall_thickness,
  partition_thickness, fill), `PlaceAssetsNode`, `AssetNode`, `evaluate(graph, library)`, `EvaluationResult.placements`.
  Pravila u novoj `engine/src/Engine/WeaverProceduraFurnish.cpp`, opisana u CVOROVI.md → "Pravila namještaja".
  Vokabular: semantika `furniture`, materijali `fabric`, `ceramic` (boje u `src/LoomPbr.h`).
- Loom: `src/LoomProceduraAssets.h` (`RecipeAssetLibrary`, `defaultAssetLibrary()`), `parse(const AgentJsonValue&)`.
- Odstupanje od plana: debljine zidova su parametri Furnisha (ne plana), da Walls/Interior ne mijenjaju značenje.
- Iteracije na testu od 300: prvi krug 263/300 (23 kupaonice bez mjesta za WC, 7 komada u vratima, 66 visokih uz
  prozor) → vrata kupaonice otvaraju se prema van (`doorLeafRoom`, i u Interior) → stolica za stolom mora proći provjeru
  kao tijelo → visoki komadi ne ispred prozorskog pojasa susjednog zida → WC/umivaonik u oba redoslijeda →
  **285/300, 0 sudara, 0 izvan sobe, 0 u vratima, 0 uz prozor, svaka soba ima glavni komad, 0.56 s**. Jedina nova greška:
  kupaonica 1.48 × 1.35 m (s razlogom).
- Provjera: `test_procedura_assets` 15/15 (kutija svakog asseta = bounds na min/default/max, JSON, knjižnica),
  `test_procedura_300` 14/14, `test_weaverprocedura` 75/75; vizualno tlocrti 8 kuća (12 katova) s oznakama komada i
  Blender presjek (alati u scratchpadu: pregled.cpp, tlocrt.py, presjek.py).
- Nije provjereno klikom: gumb Furnish i "Create house" u editoru.

### 2026-09-26, ~21:30 — stanje prije namještaja (bivši "STAO SAM OVDJE")
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
