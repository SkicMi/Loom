# Loom — predaja projekta

Zadnje osvjezeno: 17. rujna 2026.
Repo: `https://github.com/SkicMi/Loom.git`, grana **`main`** (radi se isključivo na njoj).

Ovaj dokument je za nekoga tko preuzima rad. Nije pregled nego **radni brief**: što projekt jest,
gdje stoji, što je izmjereno, u što se smije vjerovati i gdje su zamke.

---

## 1. Što je Loom

C++/Vulkan lanac koji iz **obične snimke** izvede **gdje je kamera bila** i **kako scena izgleda u
3D-u**, pa iz toga istrenira **gaussian splat** — scenu koju se može gledati iz kuteva iz kojih se
nije snimalo.

Cilj je dvostruk i oba dijela su ravnopravna:

1. **Gaussian splatting alat** — snimka unutra, splat van
2. **Camera solver za VFX** — poze dovoljno točne za match-move

Sve je vlastito osim `gsplat`-a (rasterizacija i strategija zgušnjavanja pri treningu) i FFmpeg-a
(dekodiranje). Nema OpenCV-a, nema COLMAP-a u lancu — COLMAP je **mjerilo**, ne ovisnost.

---

## 2. Arhitektura

Četiri biblioteke, **nijedna ne ovisi o drugoj**:

| modul | što radi | ključni headeri |
|---|---|---|
| **`Loom`** (`src/Loom`, `src/Core`, `src/Vulkan`) | crta — Vulkan renderer, dvije razine pristupa | `Loom/Loom.h` (tier 1, bez ijednog `vk::`), `Loom/Preset_Advanced.h` (vrata u tier 2) |
| **`Spool`** (`spool/src/Spool`) | čita i piše datoteke | `ImageFile.h`, `VideoFile.h`, `GaussianPly.h`, `Sequence.h`, `DepthFile.h` |
| **`Engine`** (`engine/src/Engine`) | rekonstrukcija — sve što nije crtanje ni I/O | `Track.h`, `ScaleSpace.h`, `Describe.h`, `Sift.h`, `MatchGraph.h`, `Reconstruct.h`, `Bundle.h`, `ColmapExport.h` |
| **`Treadle`** (`treadle/src/Treadle`) | UI sloj, **nula vanjskih ovisnosti** — ulaz su brojevi, izlaz `DrawList` u pikselima | `Ui.h`, `Draw.h`, `Input.h` |

**Tier disciplina u Loomu je branjena testom**, ne dogovorom: `<Loom/Loom.h>` se preprocesira i u
1 622 367 znakova ne smije biti nijedan `vk::` ni `vulkan`. Kontrola postoji jer detektor koji ništa
ne nađe izgleda isto kao detektor koji ne radi.

### Aplikacije (`src/`, mete u `CMakeLists.txt`)

| meta | čemu služi |
|---|---|
| **`VideoSolve`** | glavni alat: .MP4 → poze + točke + slike u COLMAP formatu |
| **`TruthBench`** | *apsolutna* greška solvera na snimci koju Loom sam nacrta (istina poznata) |
| **`ModelInfo`** | što vrijedi jedna rekonstrukcija, **bez poznate istine** |
| **`OverlayBox`** | kocka na fiksnom mjestu nacrtana preko pravih kadrova — oko vidi ono što brojka ne |
| **`SplatViewer`** | pregled splata, Blender-like kontrole, brisanje kockom |
| **`SolveMovie` / `SolveViewer`** | solve kao snimka / interaktivno |
| **`VideoInfo`** | što piše u datoteci |

Trening splatova: `tools/splat/train_splats.py` (koristi `gsplat`, ulaz je COLMAP tekst).

---

## 3. Lanac od snimke do splata

```
.MP4
 └─ Spool::VideoReader                    dekodiranje
 └─ Engine::Tracker → chooseKeyframes     koji kadrovi ulaze
 └─ buildMatchGraph  (uglovi, 960 px)     POKRIVENOST: ~5200 opažanja/kadar
 └─ buildMatchGraph  (ScaleSpace, puna)   TOČNOST: subpikselni vrh
 └─ mergeGraphs                           oba u jednu scenu, tragovi se NE miješaju
 └─ reconstruct                           4 početna para, popravak šava, bundle
 └─ writeColmapText + pointColours        cameras/images/points3D + PRAVE boje
 └─ train_splats.py                       gsplat → .ply
 └─ SplatViewer                           pregled
```

### Zašto dva grafa

Izmjereno: uglovi na smanjenoj slici daju **101/101 kameru** ali točke na **7,891 %** opsega putanje
od najbliže COLMAP-ove. Prostor mjerila daje točke na **0,933 %** — gotovo njegove točnosti — ali
samo **64/101** kameru. Spojeno drži punu pokrivenost i bolje je od samih uglova po svakoj mjeri.

**Tragovi se ne miješaju**: svaki trag dolazi cijeli iz jednog grafa. Mješavina grubih i finih
položaja *unutar* traga jednom je srušila rješenje na 119°.

---

## 4. Gdje smo — brojke

Prva snimka (Sony 4K 50p, soba, gimbal, 101 kadar). Splatovi trenirani **istom naredbom**, istim
slikama, istih 7000 koraka, isti izdvojeni kadrovi, **ista granica veličine modela**:

| | COLMAP | **Loom** |
|---|---|---|
| **PSNR** | 32,00–32,12 dB | **32,89 dB** |
| **SSIM** | 0,905–0,907 | **0,910** |
| riješene kamere | 65 / 101 | **101 / 101** |
| točaka | 26 761 | **83 374** |
| baza | **7,93°** | 5,01° |
| reprojekcija | **0,746 px** | 1,289 px |

Splat je bolji; geometrija je i dalje slabija.

### Apsolutna greška na nacrtanoj snimci (`TruthBench`)

| putanja | kamera | baza | položaj | rotacija |
|---|---|---|---|---|
| luk | 30/30 | 7,91° | **0,020 %** | **0,028°** |
| drhtaj | 30/30 | 8,03° | 0,016 % | 0,008° |
| prolaz (ravno) | 30/30 | 4,59° | degen | 0,000° |
| zaokret u mjestu | 30/30 | 0,32° | degen, **59 točaka** | — |
| luk, šum 0,08 | 30/30 | 8,09° | 0,023 % | 0,000° |

**Najvažniji zaključak u projektu trenutno:** naš pod je **0,02 %**, a na pravoj snimci imamo
**1,2 %** — šezdeset puta gore. Šum je izmjereno nevin. Razlika dolazi iz mutnoće gibanja, rolling
shuttera, kompresije ili prave teksture, i **to je sada mjerljivo pitanje**, ne nagađanje.

---

## 5. Kako se testira — tri razine

### a) Jedinični testovi — 83 u `ctest`

```bash
cmake --build build -j8 && cd build && ctest --output-on-failure
```

Svaki test je jedan izvršni program koji vraća 0 samo ako sve tvrdnje stoje. **Ništa ne ispisuje
broj i ne prepušta sud čovjeku.** GPU testovi padaju pod paralelnim `ctest -j` zbog natjecanja za
karticu — puštaj `-j1` ili ponovi pojedinačno prije nego proglasiš pad.

### b) Apsolutna greška — `TruthBench`

```bash
./build/TruthBench [luk|drhtaj|prolaz|zaokret] [kadrova] [sum] [sirina]
./tools/solve/bench.sh          # cijela tablica odjednom
```

Loom nacrta scenu i vodi kameru **poznatim putem**; solver dobije samo piksele. `zaokret` je
**negativna kontrola** — ondje paralakse nema i uspjeh bi bio laž.

Ne mjeri: šum senzora (osim dodanog), mutnoću gibanja, rolling shutter, kompresiju.

### c) Bez ikakve istine — na pravoj snimci

`ModelInfo` javlja bazu i **šavove** (mjesta gdje se lanac presidrio — kvar koji reprojekcija ne
prijavljuje). `VideoSolve` javlja **omjer izdvojenih opažanja**: svako deseto opažanje ne ulazi u
račun nego služi provjeri.

| omjer | značenje (izmjereno protiv poznate istine) |
|---|---|
| ~1,4–1,5 | zdravo |
| > 2,5 | bundle je upio šum umjesto scene |
| 3,33 | vrtnja u mjestu, bez paralakse |

### d) Krajnja mjera — decibel

```bash
python tools/splat/train_splats.py <model> <slike> <izlaz.ply> --holdout 10 --holdout-block 1 --max-gaussians N
```

Ponovljivost medijana PSNR-a je **±0,13 dB**. Razlike ispod toga ne znače ništa.

---

## 6. Zamke — pročitati prije bilo kakvog mjerenja

Sve dolje je **izmjereno**, ne pretpostavljeno, i svaka je zamka jednom prevarila.

1. **Reprojekcija ne otkriva krivo rješenje.** Krivo rješenje se sa sobom slaže jednako dobro kao
   ispravno. Dogodilo se šest puta u jednom danu. Nikad ne biraj između dva rješenja po njoj.

2. **Poravnata greška rotacije laže na ravnoj putanji.** Umeyama se računa iz *položaja* kamera; kad
   putanja leži u ravnini, zaokret oko te osi njome nije određen. Omjeri rasapa na pravoj snimci:
   1 : 0,349 : 0,100. Ista mjera davala je 4,65° ondje gdje je prava greška bila 0,595°.

3. **Rotacija prema prvoj kameri laže ako je baš ta kamera loša.** Prava mjera ne bira referencu:
   `G = naša · njegova^T` po kameri, pa rasap oko najsredišnjeg. (I pazi na poredak — obrnuti daje
   G konjugiran kamerom i izmjerio je 16° ondje gdje je greška 0,6°.)

4. **Broj gaussiana je 22,7 × broj početnih točaka.** Dva modela s različitim brojem točaka nisu
   usporediva decibelom bez `--max-gaussians`.

5. **Najgori kadar u PSNR-u varira ±6,67 dB** između dva pokretanja istog modela. Nije mjera.

6. **Pokrivenost početnog oblaka ne objašnjava decibel** — COLMAP ima najgoru pokrivenost i najbolji
   splat.

7. **Alat i sonda nisu isto.** Tri kvara u jednom danu bila su na putu do korisnika, nevidljiva
   svakoj metrici: graf nije bio zadan, pretraga žarišne nije završavala, a izvoz je pisao opažanja
   iz trackera umjesto onih s kojima je riješeno (`ModelInfo` čitao 1314 px ondje gdje je solver
   javljao 1,312). **Pokreni alat i pročitaj izlaz natrag.**

---

## 7. Otvoreni problemi, po prioritetu

1. **Žarišna se ne određuje.** Na drugoj snimci 94°/102°/110° daju 1,311/1,312/1,312 px — kriterij
   je ravan plato. Pravo rješenje: **samokalibracija u bundleu** (∂/∂f uz postojeće pinhole
   jakobijane). Alat sad barem *javlja* da nije određena.
2. **Generalizacija.** Sve protiv COLMAP-a mjereno je na **jednoj** snimci. Pragovi (`contrast =
   0,001`, `splitSupport = 2`, prag omjera, razmak) namješteni su na njoj.
3. **Brzina.** Graf: 460 s na 80 kadrova 4K. Za snimku od 3384 kadra neupotrebljivo.
4. **Baza 5,01° naspram 7,93°** — tragovi žive 7,77 kadrova, njegovi 8,7. Ništa u grafu ne seže
   dalje od deset kadrova; **nema zatvaranja petlje**.
5. **Pola oblaka je grubo** — spajanje dvaju grafova je krpanje, ne rješenje.
6. **Nema izvoza u Blender/Nuke** — bez toga VFX namjena ne postoji.
7. **Distorzija** se ne rješava u solveru (slike se ispravljaju unaprijed), a trener ignorira `k1`.
8. **Nema maskiranja pokretnih objekata.**

---

## 8. Konvencije koje se moraju poštovati

- **Radi se isključivo na `main`.** Sporedne grane su korisnikove.
- **Ime „Weaver" je zauzeto** — tako se zove korisnikov drugi engine. Obitelj imena ovdje je
  **Loom, Spool, Treadle, Engine**.
- **Komentari su hrvatski**, bez dijakritike u kodu, i objašnjavaju **zašto**, ne što. Gdje god
  stoji broj, stoji i mjerenje iz kojeg je došao.
- **Svaka izmjena završava mjerenjem.** Odbačene ideje se zapisuju s brojkama — `tools/solve/README.md`
  je trajni zapis svega izmjerenog i odbačenog.
- Prije `ctest` uvijek `cmake --build build` — stare binarke su dvaput dale krive zaključke.

---

## 9. Brzi početak

```bash
cmake -S . -B build && cmake --build build -j8
cd build && ctest -j1                      # 83 testa

./build/TruthBench luk 30                  # apsolutna greška, nekoliko minuta
./tools/solve/bench.sh                     # cijela tablica

./build/VideoSolve snimka.mp4 10 80 0 izlaz/
./build/ModelInfo izlaz/                   # zdravlje rješenja, bez istine
```

Ključna dokumentacija: **`tools/solve/README.md`** — svako mjerenje, svaka odbačena ideja i svaki
ispravak vlastite greške, s brojevima. Pročitati prije dodirivanja solvera.
