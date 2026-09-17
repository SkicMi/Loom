# Loom — predaja projekta

Zadnje osvjezeno: 17. rujna 2026.
Repo: `https://github.com/SkicMi/Loom.git`, grana **`main`** (radi se isključivo na njoj).

Ovo je **radni brief**, ne pregled. Piše što projekt jest, gdje stoji **s brojkama**, u što se smije
vjerovati, gdje su zamke, i što je sljedeće — tim redom.

Ako čitaš samo jedan odjeljak, neka bude **6 (Zamke)**. Svaka od njih je jednom prevarila, a većina
u istom danu.

---

## 1. Što je Loom

C++/Vulkan lanac koji iz **obične snimke** izvede **gdje je kamera bila** i **kako scena izgleda u
3D-u**, pa iz toga istrenira **gaussian splat** — scenu koju se gleda iz kuteva iz kojih se nije
snimalo.

Dva ravnopravna cilja:

1. **Gaussian splatting alat** — snimka unutra, splat van
2. **Camera solver za VFX** — poze dovoljno točne za match-move

Sve je vlastito osim `gsplat`-a (rasterizacija i zgušnjavanje pri treningu) i FFmpeg-a
(dekodiranje). Nema OpenCV-a. **COLMAP je mjerilo, ne ovisnost.**

---

## 2. Arhitektura

Četiri biblioteke, **nijedna ne ovisi o drugoj**:

| modul | što radi | ključni headeri |
|---|---|---|
| **`Loom`** (`src/Loom`, `src/Core`, `src/Vulkan`) | crta | `Loom/Loom.h` (tier 1, **bez ijednog `vk::`**), `Loom/Preset_Advanced.h` |
| **`Spool`** (`spool/src/Spool`) | čita i piše datoteke | `ImageFile.h`, `VideoFile.h`, `GaussianPly.h` |
| **`Engine`** (`engine/src/Engine`) | rekonstrukcija | `ScaleSpace.h`, `MatchGraph.h`, `Reconstruct.h`, `Bundle.h`, `ColmapExport.h`, `CameraHints.h` |
| **`Treadle`** (`treadle/src/Treadle`) | UI, **nula vanjskih ovisnosti** | `Ui.h`, `Draw.h` |

Tier disciplina u Loomu je **branjena testom**: `<Loom/Loom.h>` se preprocesira i u 1 622 367 znakova
ne smije biti nijedan `vk::`. Kontrola postoji jer detektor koji ništa ne nađe izgleda isto kao
detektor koji ne radi.

### Aplikacije

| meta | čemu služi |
|---|---|
| **`VideoSolve`** | glavni alat: .MP4 → poze + točke + slike u COLMAP formatu |
| **`TruthBench`** | **apsolutna** greška na snimci koju Loom sam nacrta (istina poznata) |
| **`ModelInfo`** | što vrijedi rekonstrukcija **bez poznate istine** — baza, šavovi |
| **`OverlayBox`** | kocka zalijepljena za scenu preko pravih kadrova — prava VFX provjera |
| **`SplatViewer`** | pregled splata, Blender-like kontrole, brisanje kockom |
| `SolveMovie` / `SolveViewer` / `VideoInfo` | solve kao snimka / interaktivno / što piše u datoteci |

Trening splatova: `tools/splat/train_splats.py` (`gsplat`, ulaz je COLMAP tekst).

---

## 3. Lanac

```
.MP4
 └─ Spool::VideoReader
 └─ Engine::Tracker → chooseKeyframes
 └─ buildMatchGraph (uglovi, 960 px)      POKRIVENOST: ~5200 opažanja/kadar, 101/101 kamera
 └─ buildMatchGraph (ScaleSpace, puna)    TOČNOST: subpikselni vrh, točke na 0,933 %
 └─ mergeGraphs                           oba u jednu scenu — tragovi se NE miješaju
 └─ reconstruct                           4 početna para, popravak šava, bundle
 └─ writeColmapText + pointColours        cameras/images/points3D + PRAVE boje
 └─ train_splats.py → SplatViewer
```

**Zašto dva grafa:** uglovi daju 101/101 kameru ali točke na **7,891 %** opsega putanje od najbliže
COLMAP-ove. Prostor mjerila daje **0,933 %** — gotovo njegovu točnost — ali samo 64/101 kameru.
Spojeno drži oboje.

**Tragovi se ne miješaju**: svaki trag dolazi cijeli iz jednog grafa. Mješavina grubih i finih
položaja *unutar* traga jednom je srušila rješenje na 119°.

---

## 4. Gdje smo — brojke

### Prva snimka (Sony 4K 50p, soba, gimbal, 101 kadar)

Splatovi trenirani istom naredbom, istim slikama, 7000 koraka, isti izdvojeni kadrovi, **ista
granica veličine modela**:

| | COLMAP | **Loom** |
|---|---|---|
| **PSNR** | 32,00–32,12 dB | **32,89 dB** |
| **SSIM** | 0,905–0,907 | **0,910** |
| riješene kamere | 65 / 101 | **101 / 101** |
| točaka | 26 761 | **83 374** |
| baza | **7,93°** | 5,01° |
| reprojekcija | **0,746 px** | 1,289 px |

Splat je bolji; geometrija je slabija. **Jedan uzorak** — vidi zamku 9.

### Apsolutna greška na nacrtanoj snimci (`TruthBench`)

| putanja | kamera | točaka | baza | položaj | rotacija |
|---|---|---|---|---|---|
| luk | 30/30 | 15 553 | 7,91° | **0,020 %** | **0,028°** |
| drhtaj | 30/30 | 15 362 | 8,03° | 0,016 % | 0,008° |
| prolaz (ravno) | 30/30 | 11 609 | 4,59° | degen | 0,000° |
| zaokret u mjestu | 30/30 | **59** | 0,32° | degen | — |
| luk, šum 0,08 | 30/30 | 9 841 | 8,09° | 0,023 % | 0,000° |

**Naš pod je 0,02 % i 0,03°. Na pravoj snimci imamo 1,2 %** — šezdeset puta gore, a šum je
izmjereno nevin. Razlika dolazi iz mutnoće gibanja, rolling shuttera, kompresije ili prave teksture.
**To je mjerljivo pitanje, ne nagađanje.**

### Druga snimka (Sony ZV-E10 II, 4K 50p, 10-bit, 80 kadrova)

Prošla od .MP4 do kraja: 78/78 kamera, 115 541 točka, reprojekcija 1,607 px (čitano natrag),
glatka putanja. **Ali žarišna nije određena** — vidi zadatak 2.

---

## 5. Kako se testira — četiri razine

### a) Jedinični testovi — 83 u `ctest`

```bash
cmake --build build -j8 && cd build && ctest --output-on-failure -j1
```

Svaki test je program koji vraća 0 samo ako sve tvrdnje stoje. **Ništa ne ispisuje broj i ne
prepušta sud čovjeku.** GPU testovi padaju pod `ctest -j` zbog natjecanja za karticu — puštaj `-j1`
ili ponovi pojedinačno prije nego proglasiš pad.

### b) Apsolutna greška — `TruthBench`

```bash
./build/TruthBench [luk|drhtaj|prolaz|zaokret] [kadrova] [sum] [sirina] [izoblicenje_px]
./tools/solve/bench.sh          # cijela tablica, uključujući lažnu stabilizaciju
```

Loom nacrta scenu i vodi kameru **poznatim putem**; solver dobije samo piksele. `zaokret` je
**negativna kontrola** — ondje paralakse nema i uspjeh bi bio laž.

Ne mjeri: šum senzora (osim dodanog), mutnoću gibanja, rolling shutter, kompresiju.

### c) Bez ikakve istine — na pravoj snimci

`ModelInfo` javlja bazu i **šavove**. `VideoSolve` javlja **omjer izdvojenih opažanja** — svako
deseto ne ulazi u račun nego služi provjeri:

| omjer | značenje (izmjereno protiv poznate istine) |
|---|---|
| ~1,4–1,5 | zdravo |
| > 2,5 | bundle je upio šum umjesto scene |
| 3,33 | vrtnja u mjestu, bez paralakse |

### d) Krajnja mjera — decibel

```bash
python tools/splat/train_splats.py <model> <slike> <izlaz.ply> \
    --holdout 10 --holdout-block 1 --max-gaussians N
```

Ponovljivost medijana PSNR-a je **±0,13 dB**. Razlike ispod toga ne znače ništa.

---

## 6. Zamke — pročitati prije bilo kakvog mjerenja

Sve je **izmjereno**, ne pretpostavljeno.

1. **Reprojekcija ne otkriva krivo rješenje.** Krivo rješenje se sa sobom slaže jednako dobro kao
   ispravno. Dogodilo se **šest puta u jednom danu**. Nikad ne biraj između dva rješenja po njoj.

2. **Poravnata greška rotacije laže na ravnoj putanji.** Umeyama se računa iz *položaja*; kad
   putanja leži u ravnini, zaokret oko te osi njome nije određen. Omjeri rasapa na pravoj snimci:
   1 : 0,349 : 0,100. Ista mjera davala je 4,65° ondje gdje je prava greška bila 0,595°.

3. **Rotacija prema prvoj kameri laže ako je baš ta kamera loša.** Prava mjera ne bira referencu:
   `G = naša · njegova^T` po kameri, pa rasap oko najsredišnjeg. Pazi na poredak — obrnuti daje G
   konjugiran kamerom i izmjerio je 16° ondje gdje je greška 0,6°.

4. **Broj gaussiana je 22,7 × broj početnih točaka.** Dva modela s različitim brojem točaka nisu
   usporediva decibelom bez `--max-gaussians`.

5. **Najgori kadar u PSNR-u varira ±6,67 dB** između dva pokretanja istog modela. Nije mjera.

6. **Pokrivenost početnog oblaka ne objašnjava decibel** — COLMAP ima najgoru pokrivenost i najbolji
   splat.

7. **Blaga stabilizacija višestruko kvari poze, a nijedna mjera to ne prijavi.** Izmjereno na
   `TruthBench`-u s ubrizganim warpom:

   | izobličenje | prolaz geometrije | omjer izdvojenih | **prava greška** |
   |---|---|---|---|
   | 0 px | 100 % | 1,42 | 0,018 % |
   | 2 px | 100 % | **1,38** | **0,207 %** |
   | 5 px | 100 % | 2,53 | 0,828 % |
   | 10 px | 100 % | 4,71 | 1,934 % |

   **Dva piksela množe grešku jedanaest puta**, a omjer izdvojenih je ondje *niži* nego na čistoj
   snimci. Udio parova koji prođu geometriju je **beskoristan** — ostaje 100 % i na deset piksela.
   **Snimati s isključenom stabilizacijom.**

8. **Alat i sonda nisu isto.** Tri kvara u jednom danu bila su na putu do korisnika, nevidljiva
   svakoj metrici: graf nije bio zadan, pretraga žarišne nije završavala, a izvoz je pisao opažanja
   iz trackera umjesto onih s kojima je riješeno (`ModelInfo` čitao **1314 px** ondje gdje je solver
   javljao **1,312**). **Pokreni alat i pročitaj izlaz natrag.**

9. **Sve protiv COLMAP-a mjereno je na jednoj snimci.** Pragovi (`contrast = 0,001`,
   `splitSupport = 2`, prag omjera, razmak) namješteni su na njoj. **Ne mijenjaj ih bez `bench.sh`.**

---

## 7. Zadaci, po prioritetu

Svaki ima **kriterij uspjeha**, jer bez njega izmjena postaje dojam.

### 1. Polje ostataka — detektor koji otkriva tri kvara odjednom (pola dana)

Kad je model točan, ostaci reprojekcije moraju biti **prostorno bijeli**. Podijeli sliku na mrežu
8×8 i izračunaj **srednji vektor ostatka po ćeliji, po kadru**. Šum ide u nulu kao 1/√N; polje
ostaje.

| što se vidi | dijagnoza |
|---|---|
| sredine ~0 | zdravo |
| **isto polje u svakom kadru** | **neispravljena distorzija objektiva** |
| **polje se mijenja po kadru** | **stabilizacija ili rolling shutter** |
| polje ovisi o retku, raste s vodoravnim gibanjem | **rolling shutter** |

*Kriterij:* na `TruthBench luk 30 0 1280 5` mora prijaviti polje koje se mijenja po kadru; na
`TruthBench luk 30 0` ne smije prijaviti ništa. **Ubrizgani warp je poznatog oblika, pa se detektor
provjerava prije nego uđe.**

Košta jedan prolaz preko opažanja koja već postoje.

### 2. Žarišna — dva koraka, jeftiniji prvi

**(a) Pročitaj telemetriju.** Druga snimka javlja `telemetrija postoji (data none) - jos je ne
citamo`. Sony ondje zapisuje podatke o objektivu. To vjerojatno riješi problem **bez ikakve
matematike**. Mjesto: `Engine::CameraHints`.

**(b) Samokalibracija u bundleu.** `∂/∂f` uz postojeće pinhole jakobijane.

*Zašto:* na drugoj snimci 94°/102°/110° daju 1,311/1,312/1,312 px — kriterij je **ravan plato**.
Alat to sada javlja kao upozorenje, ali ne rješava.

*Kriterij:* `TruthBench` zna pravu žarišnu; procijenjena mora biti unutar 2 % na `luk` i `prolaz`.

### 3. Brzina (dan-dva)

Graf: **460 s na 80 kadrova 4K**. Snimka od 3384 kadra je neupotrebljiva.

*Kriterij:* ispod 10 min za 30 s snimke, uz **bit-identičan** graf (postoji presedan — graf je već
jednom ubrzan 10× bit-identično).

### 4. Provuci četiri snimke kroz prag (odjeljak 9)

### 5. Izvoz u Blender/Nuke

Bez toga VFX namjena ne postoji, ma kakav solve bio.

### 6. Rolling shutter kao parametar bundlea

Jedan parametar po kadru (vrijeme retka × brzina kamere). Pogađa **svaku** CMOS snimku, ne samo
mobitel.

---

## 8. Testni materijal — koje snimke i kako ih snimiti

Cilj nije "četiri snimke" nego **četiri različita kvara**. Drona nema i neće ga biti neko vrijeme;
to je u redu — bez njega se pokriva sve osim daleke scene.

| # | uređaj | što snima | što ispituje |
|---|---|---|---|
| **S1** | Sony | **obilazak** oko predmeta/prostora, bogata tekstura, mirno, 30–60 s | kontrola koja **mora** raditi; široka baza |
| **S2** | Sony | **prolaz ravno** kroz prostor/hodnik, pogled naprijed | uska baza — ovako izgleda većina VFX kadrova |
| **S3** | Sony | **teška**: tamne i glatke plohe, brže gibanje, malo mutnoće | granica loma. Pitanje nije uspije li, nego **kaže li da nije uspio** |
| **M1** | iPhone | **isti prostor kao S1**, stabilizacija **isključena** | izolira medij, jer je scena ista |
| *M2* | iPhone | *isti kadar kao M1, stabilizacija UKLJUČENA* | *30 s dodatnog snimanja, a pretvara zamku 7 iz simulirane u izmjerenu — **vrijedi napraviti*** |

### Kako snimiti — ovo nije oprez nego uvjet

- **Stabilizacija ISKLJUČENA.** Sony: SteadyShot off. iPhone: **nativna kamera uvijek stabilizira**
  — treba aplikacija koja to dopušta (Blackmagic Camera, Filmic Pro). Ako se to ne može isključiti,
  M1 mjeri stabilizaciju, ne telefon.
- **Fiksna žarišna, bez zuma. Ručni fokus, zaključan.** Autofokus koji lovi mijenja žarišnu usred
  kadra.
- **Zaključana ekspozicija i balans bijelog.**
- **Pomicanje u stranu, ne samo rotacija.** Bez paralakse nema dubine — to je `zaokret` iz
  `TruthBench`-a i ondje ispadne 59 točaka.
- **Tekstura u kadru.** Prazan bijeli zid ne daje ništa nijednom solveru.
- **Bez ljudi i pokretnih stvari.**
- **30–60 s je dovoljno.** Duže samo produljuje račun.
- **Zapiši žarišnu duljinu i objektiv u tekstualnu datoteku uz snimku.** Time se iz testa uklanja
  pogađanje žarišne i mjeri se solver, a ne nagađanje.

### Što s njima napraviti

Za svaku: `VideoSolve` → `ModelInfo` → trening splata → PSNR. Za usporedbu s COLMAP-om pustiti
`tools/solve/colmap_solve.sh` preko noći — samo ondje gdje se želi vanjska referenca.

---

## 9. Prag za „spremno" — odlučiti **prije** mjerenja

Vrijedi za **sve četiri snimke s jednom te istom postavkom**:

| uvjet | prag | stanje danas |
|---|---|---|
| **bez ručnog ugađanja po snimci** | jedna postavka za sve | neprovjereno |
| omjer izdvojenih | < 2,0 | ✓ na poznatim snimkama |
| šavova | 0 | ✓ |
| riješenih kamera | > 90 % | ✓ (101/101 i 78/78) |
| baza | > 3° | ✓ (5,01°) |
| polje ostataka | bez uzorka koji se mijenja po kadru | **nema detektora** |
| žarišna | određena, ne plato | **PADA** |
| vrijeme | < 10 min po 30 s snimke | **PADA** |
| splat PSNR | ≥ COLMAP ondje gdje COLMAP uspije | ✓ na jednoj snimci |

**Dva reda su crvena i oba su u zadacima 2 i 3.**

---

## 10. Konvencije

- **Radi se isključivo na `main`.** Sporedne grane su korisnikove.
- **Ime „Weaver" je zauzeto** — korisnikov drugi engine. Obitelj imena je **Loom, Spool, Treadle,
  Engine**.
- **Komentari su hrvatski**, bez dijakritike u kodu, i objašnjavaju **zašto**, ne što. Gdje god stoji
  broj, stoji i mjerenje iz kojeg je došao.
- **Svaka izmjena završava mjerenjem.** Odbačene ideje se zapisuju s brojkama.
- Prije `ctest` uvijek `cmake --build build` — stare binarke su dvaput dale krive zaključke.
- Poruke commita opisuju **što je izmjereno**, ne samo što je promijenjeno.

---

## 11. Brzi početak

```bash
cmake -S . -B build && cmake --build build -j8
cd build && ctest -j1                      # 83 testa

./build/TruthBench luk 30                  # apsolutna greška, minute
./tools/solve/bench.sh                     # cijela tablica

./build/VideoSolve snimka.mp4 10 80 0 izlaz/
./build/ModelInfo izlaz/                   # zdravlje rješenja, bez istine
```

**Ključna dokumentacija: `tools/solve/README.md`** — svako mjerenje, svaka odbačena ideja i svaki
ispravak vlastite greške, s brojevima. Pročitati prije dodirivanja solvera.
