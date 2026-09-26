# Loom — predaja projekta

Zadnje osvjezeno: 26. rujna 2026. (odjeljak 7.9: novi mascot, izravni Manny rig bez UniRiga — u radu)
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

Šest biblioteka, **nijedna ne ovisi o drugoj**:

| modul | što radi | ključni headeri |
|---|---|---|
| **`Loom`** (`src/Loom`, `src/Core`, `src/Vulkan`) | crta | `Loom/Loom.h` (tier 1, **bez ijednog `vk::`**), `Loom/Preset_Advanced.h` |
| **`Spool`** (`spool/src/Spool`) | čita i piše datoteke | `ImageFile.h`, `VideoFile.h`, `GaussianPly.h` |
| **`Engine`** (`engine/src/Engine`) | rekonstrukcija | `ScaleSpace.h`, `MatchGraph.h`, `Reconstruct.h`, `Bundle.h`, `ColmapExport.h`, `CameraHints.h` |
| **`Treadle`** (`treadle/src/Treadle`) | UI, **nula vanjskih ovisnosti** | `Ui.h`, `Draw.h` |
| **`Tracer`** (`tracer/src/Tracer`) | **LoomTracer** — fizikalni path tracer za render iz kamere, samo glm + dretve (procesor, referenca) | `Compiled.h`, `Renderer.h`, `Scene.h`, `Bsdf.h`, `Film.h` |
| **`Warp`** (`warp/src/Warp`) | scena: stablo entiteta s komponentama, ključevi kroz vrijeme (USD-oblik), samo glm; projekt se sprema kao pravi `.usda` (vlastiti čitač podskupa, bez OpenUSD-a) | `Stage.h`, `Project.h`, `Usda.h`, `UsdCamera.h` |

Tier disciplina u Loomu je **branjena testom**: `<Loom/Loom.h>` se preprocesira i u 1 622 367 znakova
ne smije biti nijedan `vk::`. Kontrola postoji jer detektor koji ništa ne nađe izgleda isto kao
detektor koji ne radi.

### Aplikacije

| meta | čemu služi |
|---|---|
| **`VideoSolve`** | glavni alat: .MP4 → poze + točke + slike u COLMAP formatu; `--samo-kamera` za matchmove (bez slika, ~18 % brže) |
| **`loom`** (LoomDesk) | editor: media lijevo, pogled, scena i svojstva desno, timeline; solve/splat desnim klikom, kocka kroz riješenu kameru preko snimke. `loom <mapa> --snimi x.png --rezultat <mapa_loom> --kadar N --kroz --kocka-u M` sprema vlastiti kadar (prozor se izvana ne da snimiti). Projekt: `loom projekt.usda`, Spremi/Ctrl+S. Splat se crta u pogledu (B), samo nulti SH clan |
| **`loom-render`** (LoomRender) | render projekta iz kamere bez prozora (LoomTracer): `loom-render projekt.usda --kadar 42 --uzorci 256` ili `--od 1 --do 120`; PNG + EXR (R G B A, `cg.*`, `shadow.*`, `Z`, `N.*`, `albedo.*`). Isti most kao gumb Render u editoru |
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

### a) Jedinični testovi — 118 u `ctest`

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

### 1. Polje ostataka — RIJESENO

Kad je model točan, ostaci reprojekcije moraju biti **prostorno bijeli**. Podijeli sliku na mrežu
8×8 i izračunaj **srednji vektor ostatka po ćeliji, po kadru**. Šum ide u nulu kao 1/√N; polje
ostaje.

| što se vidi | dijagnoza |
|---|---|
| sredine ~0 | zdravo |
| **isto polje u svakom kadru** | **neispravljena distorzija objektiva** |
| **polje se mijenja po kadru** | **stabilizacija ili rolling shutter** |
| polje ovisi o retku, raste s vodoravnim gibanjem | **rolling shutter** |

`Engine::analyzeResidualField` dijeli svaki kadar na 8x8, racuna srednji vektor ostatka po celiji i
iz rasapa opazanja u istoj celiji izvodi koliko je ta sredina nesigurna. Jednolik pomak cijelog
kadra uklanja se prije odluke jer ga poza moze upiti; energija suma oduzima se od energije polja,
a dokaz se skuplja preko svih popunjenih celija.

Izmjereno punim putem (Loom nacrta piksele -> oba grafa -> rekonstrukcija -> detektor):

| `TruthBench luk 30 0 1280` | dijagnoza | prostorni signal | promjenjivi signal | prava greska polozaja |
|---|---|---|---|---|
| bez warpa | **bijelo** | 0,075 px | 0,073 px | 0,025 % |
| warp 2 px | **mijenja se po kadru** | 1,089 px | 1,088 px | 0,197 % |
| warp 5 px | **mijenja se po kadru** | 1,045 px | 1,039 px | 0,782 % |

Signal nije mjera jacine warpa — bundle dio izoblicenja upije u poze i tocke — nego dokaz da
ostatak nije bijel. Prag amplitude je 0,25 px uz cetiri standardne pogreske energije. `VideoSolve`
sada ispisuje dijagnozu i upozorenje za staticko ili promjenjivo polje.

Brane ga devet cistih provjera (prazno, cisto, sum, jednolik pomak, staticno i promjenjivo polje,
slab koherentan signal) te dva puna `TruthBench` testa. Nakon integracije samokalibracije cijeli
sekvencijalni paket prolazi **87/87** (1277 s; clean 576,39 s, warp 377,11 s).

### 2. Žarišna — generička samokalibracija, bez ovisnosti o Sonyju (u radu)

Telemetrija ostaje samo **opcionalni prior**. Glavni put mora raditi za drugu kameru i mobitel, pa
je dodan `Engine::estimateViewGraphFocal`: fundamentalne matrice po jakim parovima, Bougnoux samo
kao filter degeneriranih parova, a zajednicki `f` iz Kruppa/essential uvjeta preko view-grapha.
Radijalna distorzija ulazi vec u inicijalizaciju; cekati bundle bilo je dokazano prekasno.

`Engine::selfCalibrateBundle` zatim naizmjence ispravlja sirova mjerenja, zajednicki bundle i
`f+k1`. `Engine::reconstructSelfCalibrated` zatvara stvarni redoslijed bez poznatih poza i tocaka:
view-graph -> ispravljena opazanja -> rekonstrukcija -> zajednicki `f+k1` bundle. Na sintetickoj
kameri 1280x720, iz pocetnog `f*0,65` i poremecene geometrije:

| istina | view graph | konacno | RMS |
|---|---|---|---|
| `f=920`, `k1=-0,045` | `f=934,86` | `f=934,66`, `k1=-0,04154` | 3,896 -> 0,160 px |

Pogreska zarista je 1,59 %. Cista rotacija i kriticni look-at luk vracaju **neodredjeno**, umjesto
broja koji izgleda uvjerljivo. `test_self_calibration` ima 17 provjera, ukljucujuci cijeli
rekonstrukcijski put i isti radijalni warp nad RGBA slikom, i prolazi ASan+UBSan. LeakSanitizer se
na ovom hostu ne moze pokrenuti pod `ptrace`; to nije proglaseno prolazom.

Put je sada spojen u `VideoSolve`. Poznati `cameras.txt` i rucni FOV i dalje imaju prednost;
genericka procjena je zadana kad ih nema, a stari sweep je jasno ispisani fallback samo kad je
geometrija neodredjena. Izvoz vise ne pise zakrivljene slike uz `PINHOLE`: isti procijenjeni `k1`
ispravlja i opazanja i izlazne PNG-ove, dok `cameras.txt` opisuje ravnu izlaznu kameru.

Prvi stvarni smoke na Sony 4K isjecku (8 ulaznih, 6 kljucnih kadrova, samo graf uglova) prosao je
do kraja i ponovno je procitan:

| mjera | rezultat |
|---|---:|
| procjena | `f=2963,02 px`, `k1=-0,17860`, HFOV `65,89 st` |
| rekonstrukcija | 6/6 kamera, 15 192 tocke, baza 3,30 st |
| zapis procitan natrag | 71 280 opazanja, 1,553 px (solver 1,387 px) |
| izdvojena opazanja | **2,64 — pada** |
| polje ostataka | **mijenja se po kadru — pada pinhole model** |

To dokazuje integraciju i konzistentan izvoz, **ne tocnost te procjene lece**. Sest pogleda nije
release materijal, a dvije neovisne dijagnoze ga odbijaju. Zadatak ostaje otvoren dok ista postavka
ne prodje pune S1/S2/S3/M1 snimke i poznatu/referentnu zarisnu gdje je dostupna.

### 3-UPOZORENJE. Brojke iz 17.-22. rujna su mjerene na NEOPTIMIZIRANOM buildu

Build mapa je od 17. rujna stajala na `CMAKE_BUILD_TYPE=Debug`, dakle bez ijedne `-O` zastavice.
Alat radi jednako, samo oko **3.4 puta sporije**, pa se nista nije vidjelo osim brojki koje izgledaju
kao rezultat.

`CMakeLists` vec brani od toga (postavlja RelWithDebInfo kad build type nije zadan), ali ta se
zastita NE aktivira kad je Debug vec u predmemoriji. Od commita 579995d `VideoSolve` i `loom` pitaju
sam prevoditelj (`__OPTIMIZE__`) i glasno se jave.

**Isti posao, ista masina, ista snimka (C0257, 231 kadar 4K):**

    faza                              Debug        -O2      omjer
    cijeli lanac                     8166 s     2381 s       3.4x
    graf poklapanja                  3942 s      606 s       6.5x
    - poklapanje                     2585 s      300 s       8.6x
    - potpisi                         951 s      179 s       5.3x
    rekonstrukcija (thorough)         694 s      202 s       3.4x
    bundle, puni graf, 15 iteracija  14.55 s     3.53 s      4.1x

**Kvaliteta je identicna:** 229/229 kamera, 1.202 px, baza 7.29 st, omjer izdvojenih 2.46 - svaka
brojka ista. Build ne mijenja rezultat, samo vrijeme. Zlatni hash bundlea je isti s -O0 i -O2.

**Sto se time mijenja u zakljuccima:**

  - Poklapanje NIJE usko grlo. U Debugu je bilo 47 % vremena, na -O2 je 12.6 %.
  - Trakasto rjesavanje je danas gotovo nevazno: gusto rjesavanje je 0.45 s od 3.53 s bundlea.
    Ostaje korisno tek oko tisucu kamera (8.4x na 1600, ne 14.4x kako je prvo izmjereno).
  - Vrijeme vise nema jednog velikog krivca nego je razmazano; najveci pojedini komad je
    dekodiranje videa i zapis 229 slika u 4K.
  - Pune slicice su 430 s, dakle 18 % lanca - rade savrseno (2072/2072) ali nisu besplatne.

**Relativne usporedbe na istom buildu i dalje vrijede** - lokalni bundle 5x, linearizacija 5.6x.

### 3. Brzina — VELIKI POMAK 21.9., ali jos nije gotovo

Tri ulancana dobitka, svaki izmjeren izolirano na PRAVOM grafu (kameni zid, 1 435 373 opazanja,
229 kamera, 265 452 tocke), ne na sintetici:

  1. **Schur po dretvama** (d748369) - izlaz bit po bit isti
  2. **Linearizacija po dretvama** (c26d13d) - 13.48 s -> 2.40 s, 5.6x, isti zlatni hash
     4084565953268247014. Teze od Schura jer ista petlja pise u DVA neovisna prostora (po tocki i
     po kameri), pa su tri prolaza: jakobijan po opazanju, gCamera/B po kameri, gPoint/C i E po
     tocki.
  3. **Lokalni bundle u rastu** (7608de3) - 3107 s -> 626 s, **5.0x**, uz nepromijenjene mjere
     (baza 6.82 -> 6.82 st, omjer izdvojenih 2.52 -> 2.53, 99.96% tocaka).

Prije prozora je probana **rjedja kadenca** i ODBACENA - Sol ju je odbacio tjedan ranije, ja sam
posumnjao da je usporedba bila nepostena, izmjerio, i on je bio u pravu: 1.10 daje omjer 3.09
(osnovica 2.52), 1.25 daje 5.23. Zanimljivo je da 1.25 ima NAJBOLJU reprojekciju (0.870 px) uz
NAJGORI omjer - prenaucenost: manje bundlea, manje tocaka prezivi filtar, preostanu lake.

Zamka koju sam sam napravio pa uhvatio: `refine()` ide kroz isti `runBundle`, pa bi s prozorom
nestao SVAKI globalni bundle i drift se nikad ne bi ispravio. **Lokalni svaki korak, globalni
povremeno.**

Sto jos stoji: graf (poklapanje) je i dalje velik trosak, i broj znacajki po kadru je izveden iz
povrsine slike ali nije mjerenjem optimiziran.

### 3b. Stariji zapis o brzini grafa

Graf je na istih 80 ulaznih / 78 kljucnih 4K kadrova u `Release` buildu spusten s **448,7 s na
337,8 s**. Matcher vise ne racuna uzajamno najboljeg susjeda u zasebnom punom prolazu, a SIFT-ovu
udaljenost za drugi najbolji koristi iz prvog prolaza. Izlaz je ostao jednak po svim mjerama
(2 339 952 znacajke, 118 507 tocaka, 808 021 opazanje, 1116/1450 parova, medijan 1070), a dva
zlatna testa brane bit-identican brzi i scale-space graf.

Fazno mjerenje prije -> poslije: znacajke `166,1 -> 175,8 s` (varijacija, nisu optimizirane),
poklapanje **`269,7 -> 149,5 s`**, geometrija `11,5 -> 10,9 s`. Pracenje je `81,9 s`, pa je put do
grafa oko **419,7 s**. Cijeli `VideoSolve` ipak nije zavrsio unutar 600 s: nakon grafa je timeout
uhvatio rekonstrukciju. Zato kriterij ispod ostaje **otvoren**, ne prolaz.

Izmjereno i odbaceno: rucni SSE2 (`8,1 -> 8,2 s` matchinga na 8 kadrova; prevoditelj je vec
vektorizirao) i trajni skup dretvi (`8,1 -> 8,7 s`). Oba su uklonjena, ne nosimo slozenost bez
dobitka. Sljedeca uska grla su scale-space znacajke i rekonstrukcija, ne ovaj matcher.

Zavrsna provjera: `Release`, sekvencijalni CTest na stvarnom NVIDIA RTX 5070 uredjaju, **87/87**
prolazi za 155,32 s. Sandbox nema pristup X serveru ni NVIDIA ICD-u i pada na llvmpipe; ti padovi
nisu zaobilazeni skipovima, nego je isti suite pokrenut u stvarnom GPU okruzenju.

Profil rekonstrukcije sada je ponovljiv bez ponovne gradnje grafa. Osmi argument `VideoSolvea`
prima verzionirani binary cache view-grapha; ima checksum, provjerava identitet snimke i postavke
te cuva float koordinate bit-identicno. Sony smoke je iz cachea ponovio iste kamere, tocke,
reprojekciju, putanju i dijagnozu, a vrijeme je palo `30,77 -> 15,76 s`. Puni cache za 80/78
kadrova ima 12 928 740 bajtova i tocno 118 507 tocaka / 808 021 opazanje.

Iz njega je izoliran stvarni zastoj: probna rekonstrukcija traje 202,6 s, od cega globalni bundle
uzima **198,9 s kroz 98 poziva**. Eksperimentalna geometrijska kadenca 1,25 skratila je poziv na
31,5 s, ali samokalibracija je pala na 61/78 kamera. Kadenca 1,10 vratila je 78/78 i skratila ga
na 58,7 s, ali puni rezultat jos ima held-out omjer 2,57. Zato nijedna nije ukljucena u glavni
alat.

Pojedini bundle je zatim profiliran i ubrzan bez promjene slijeda njegovih 97 poziva niti jednog
bita izlaza (zlatni hash `4084565953268247014`). Ravni niz Schurovih `CameraBlockova` zamijenio je
stotine tisuca sitnih alokacija po iteraciji; dva Jacobiana dijele jednu projekciju; medijan koristi
`nth_element` umjesto punog sorta; evaluacija troska vise ne gradi Jacobian koji odbaci. Na istom
cacheu samokalibracijski solve ostaje f=3307,24 px, k1=-0,19276, 78/78 i 1,328 px, a pada
**194,0 -> 101,3 s**. Bundle sam pada `190,4 -> 97,7 s`: linearizacija `60,7 -> 32,6`, Schur
`73,3 -> 46,0`, uvrstavanje `11,3 -> 5,2`, cost `37,3 -> 10,5 s`.

Ponovljeni probni solve nakon uspjesne samokalibracije sada je uklonjen: vec izgradjeni brzi
kandidat ponovno se koristi, a thorough polish i dalje krece iz nule s istim konacnim FOV-om.
Nakon joint bundlea centralno se osvjeze reprojekcija, triangulacijska baza i held-out mjera;
neovisni test ponovno bira izdvojena opazanja i potvrduje `496/496` te identican medijan
`0,152370274 px`. Na punom Sony grafu time nestaje jedan solve prethodno izmjeren na `101,3 s`.
Release testovi za zahvaceni put prolaze (`self-calibration 17/17`, `reconstruct 24/24`, bundle
golden hash nepromijenjen), kao i isti testovi pod ASan+UBSan bez prijava.
Puni sekvencijalni `Release` CTest na stvarnom RTX/X okruzenju nakon paralelizacije prolazi
**88/88**; zadnji run traje 96,55 s (prethodni 230,28 s zbog velike varijacije GPU testova).

Dvije zavrsne `f x 0,75/1,25` dijagnosticke rekonstrukcije sada krecu paralelno. Solver nema
globalni RNG: svaki RANSAC ima vlastiti `mt19937` s fiksnim seedem, a oba zadatka samo citaju isti
graf. Test racuna oba rjesenja prvo sekvencijalno pa paralelno i bit-po-bit usporeduje poze, tocke,
maske, brojacke i dijagnosticke medijane; prolazi `25/25` i pod ASan+UBSan. Poseban TSan build se
preveo, ali runtime na ovom hostu pada prije `main()` s `unexpected memory mapping`, pa nije
proglasen ni prolazom ni nalazom.

Cetiri thorough kandidata sada se takoder grade paralelno, ali izbor parova nije pogresno
pretpostavljen neovisnim. Prvo se cetiri puta izvrsi samo jeftina deterministicka faza izbora uz
isti `skipInitialPairs`; zatim svaki puni solve dobije tocno zadani par. Sekvencijalni referentni
put ostaje dostupan testu. Test usporeduje cijelog pobjednika bit-po-bit i dobiva isti par 5-6,
8/8 kamera, 300 tocaka i tocno `0,530486047 px`; prolazi `26/26` i pod ASan+UBSan.

Na punom Sony cacheu thorough izlaz ostaje tocno isti: 78/78, 111 187 tocaka, 1,358 px i baza
4,47 st. Faza pada **459,2 -> 150,2 s**, a cijeli cache run **701,65 -> 397,54 s** — usteda
304,11 s odnosno 43,3 %. Dijagnostike ostaju 1,383 i 1,249 px. Peak RSS raste
`548 704 -> 901 452 KiB` (oko 880 MiB), bez OOM-a i bitno ispod dostupne memorije stroja.

To jos nije dokaz hladnog cilja `<600 s`: cache namjerno preskace tracking i graf, a njihov zadnji
izmjereni put je oko 419,7 s. Zbroj izmjerenih faza je oko 817 s, pa ukupni kriterij i dalje
**PADA** dok se ne ubrza graf i ne ponovi hladni end-to-end run. Najveci sljedeci cilj vise nije
rekonstrukcijski polish nego gradnja grafa, osobito scale-space znacajke i matching.

Scale-space znacajke sada odvojeno mjere detekciju, zagladjivanje, gradijente i gradnju potpisa.
Na stvarnom kratkom Sony ulazu pokazalo se da blur uzima 9,4 od 12,1 s SIFT potpisa. Odvojiva
Gaussova konvolucija zato sada osam susjednih izlaznih piksela racuna zajedno, ali za svaki piksel
zadrzava isti kernel, isti red zbrajanja i iste rubne vrijednosti; unutarnji pikseli vise ne rade
nepotrebne clampove za svaki clan kernela. Oba zlatna grafa ostaju bit-identicna
(`11197454592418299683` i `2453899824703454841`). Na 8 ulaznih / 6 kljucnih 4K kadrova blur pada
**9,4 -> 3,8 s**, sve znacajke **15,5 -> 9,3 s**, a cijeli graf **20,9 -> 14,7 s** uz tocno istih
238 378 znacajki, 19 434 tocaka i 87 600 opazanja. Pokusaj ranog prekida SIFT udaljenosti bio je
bit-identican, ali je matching usporio 4,8 -> 6,3 s i zato je uklonjen. Release paket na stvarnom
GPU-u prolazi **88/88** (89,13 s), a `test_match_graph` pod ASan+UBSan prolazi 19/19; LeakSanitizer
na ovom hostu ne radi pod ptraceom i nije proglasen prolazom. Puni hladni benchmark jos treba dati
stvarnu ukupnu ustedu; kratki isjecak nije zamjena za tu brojku.

Puni hladni run je zatim izmjeren, bez cachea i bez izlazne mape. Novi cache i stari puni Sony
cache prolaze `cmp` byte-for-byte i imaju isti SHA-256
`9a8c7ea228493c46f299f4445aa7c319baefbeda655feb141674b3eaea32122a`. Graf pada
**337,8 -> 281,1 s** (56,7 s / 16,8 %); njegove znacajke padaju **175,8 -> 100,6 s** (75,2 s /
42,8 %). Tracking je 84,0 s. Cijeli alat ipak traje **14:06,02**, dakle kriterij `<600 s` i dalje
PADA za 246 s. Izlaz ostaje 78/78 kamera, 111 187 tocaka, 1,358 px i baza 4,47 st; peak RSS je
1 806 604 KiB, bez swapa. Hladni run je sporiji od prostog zbroja ranijih odvojenih benchmarka:
samokalibracijska rekonstrukcija traje 115,4 s, a thorough 173,8 s. `/usr/bin/time` javlja prosjek
687 % CPU-a uz dostupnih CPU 0-27 i bez vidljivog cpuset ogranicenja. Sljedeci veliki kandidat je
deterministicka paralelizacija bundle linearizacije/Schura po tockama; `Bundle.cpp` je sada
sekvencijalan unutar jednog solvea, a ta dva ispisana solvea sama uzimaju oko 289 s.

*Kriterij:* ispod 10 min za 30 s snimke, uz **bit-identičan** graf (postoji presedan — graf je već
jednom ubrzan 10× bit-identično).

### 3c. Zdravlje splata — NOVA MJERA (21.9.)

`tools/splat/splat_health.py` mjeri koliko se splat razisao izvan scene koju je solver rijesio.

**Vrijednost joj je u tome sto ju NE racuna nas program.** Sve ostalo - reprojekcija, baza,
izdvojena opazanja, polje ostataka - racuna isti kod koji je poze i nasao. Ovo mjeri sto je TRENER
napravio s tim pozama, a on o nasem solveru ne zna nista.

    slucaj                        tocaka   gaussiana   omjer splat/model   u 3x scene
    soba, nase poze (rast2)        59879      655247               1.3x        99.5%
    joystick, MapAnythingove       232677     3775883              1.4x        98.9%
    soba, nase poze (oba_boja)     81639      655247              23.1x        90.0%
    joystick, nase poze            17640      400331            7515.3x        10.8%

Dvije objasnjenja koja te brojke iskljucuju: nije mjerilo (omjer je bezdimenzijski) i nije rijetka
inicijalizacija (soba "rast2" ima MANJE tocaka a BOLJI omjer). Mjera razlikuje i dva NASA VLASTITA
rjesenja na istoj snimci.

Zdravo je 1 do 2. Preko desetak znaci da trener nije nasao dosljedno objasnjenje - i tada gledanje
splata nije test solvera nego test strpljenja.

### 3d. Pune slicice i `loom` kao alat — RIJESENO (21.9.)

**Pune slicice.** Solver uzima svaki step-ti kadar, pa je od 1968 kadara izlazilo 25 poza. Za splat
dosta, za match-move ne: CG objekt skace pet puta u sekundi, a USD izmedju uzoraka linearno
interpolira. Medjukadar se sada LOKALIZIRA umjesto da se rekonstruira - tocke se pratiteljem
prenesu iz najblizeg kljucnog kadra, poza izadje iz PnP-a. Nijedan novi algoritam: `Track.cpp` i
`SolvePose.cpp` su vec postojali.

    266 od 266 medjukadrova lokalizirano (100.0 %), medijan 646 prenesenih tocaka, 52.4 s

Provjereno Pixarovom USD bibliotekom: 291 uzorak, razmak izmedju svih tocno 1 (prije: 25 uzoraka
na razmaku 10).

Dvije odluke: lanac se RESETIRA na svakom kljucnom kadru (inace se nakuplja pomak), i staje se na
ZADNJEM kljucnom kadru - prvi pokusaj je isao do kraja snimke i javio "780 od 1088 lokalizirano"
dok je rjesenje pokrivalo 240 kadara.

**`./loom`** je desktop alat: izbornik, odabir snimke, pa cijeli lanac u jednom prozoru - solve,
trening splata, pregled scene. Suicelje je `Treadle::Ui` koji je vec postojao.

Solver se pokrece kao ZASEBAN PROCES, namjerno: njegov ispis je nastajao uz svako mjerenje i tocno
je ono sto suicelje treba pokazati; solve traje satima pa prozor mora prezivjeti njegov pad; i
nijedna linija solvera se ne mijenja da bi suicelje postojalo.

**Zivi oblak tocaka.** `ReconstructConfig::onProgress` javlja stanje svakih pet kamera (Engine i
dalje ne dira disk), `VideoSolve` to zapisuje atomski u `napredak.bin`, `loom` cita i crta - bez
ijednog novog shadera, jer Treadleov `DrawList` ima pravokutnik.

`test_progress_view` brani ono sto se ne vidi: binarni raspored (promijeni netko redoslijed polja i
scena postane smece a nijedan test ne padne) i mjerilo projekcije. Negativna kontrola je greska
koju smo vec jednom napravili - jedna tocka odbjegla na 1e5; da se srediste racuna po min/max
umjesto po postotcima, scena bi se skupila u piksel.

### 3e. GDJE JE GRANICA KVALITETE — izmjereno 22.9., i nije u solveru

Ovo je najvazniji nalaz o kvaliteti dosad, i mijenja gdje ima smisla raditi.

**Solver protiv poznate istine** (`TruthBench luk`, savrsen pinhole ulaz, bez suma i izoblicenja):

    kadrova   omjer izdvojenih   greska polozaja   polje ostataka
         40               1.36           0.013 %   bijelo
        120               1.26           0.021 %   bijelo
        229               1.23           0.014 %   bijelo

**Prava snimka na ISTOM broju kamera** (C0257, 229 kadrova): omjer **2.46**, polje ostataka **pada**.

Isti solver, isti kod, ista duljina niza. Dakle:

  - **Duljina niza nije kriva.** Omjer se s duljinom cak POPRAVLJA (1.36 -> 1.23), a greska
    polozaja na 229 kadrova je ista kao na 40. Nakupljanje drifta je ovime iskljuceno kao glavni
    uzrok, pa zatvaranje petlje NIJE prvi korak za kvalitetu (ostaje potrebno za stan).
  - **Optimizacija nije granica.** Solver postize 0.014 % i bijelo polje kad ulaz postuje model.
    Bolji bundle, dulji tragovi i finije poklapanje guraju nesto sto je vec tu.

**Sto onda jest.** Polje ostataka se dijeli na staticki dio (objektiv, glavna tocka - uvijek isti)
i promjenjivi (mijenja se po kadru):

    komponenta     sintetika 229   prava snimka   omjer
    staticka            0.032 px       0.088 px    2.8x
    promjenjiva         0.077 px       0.535 px    7.0x

Promjenjivi dio dominira. To ISKLJUCUJE distorziju i glavnu tocku, jer su one staticne i pokazale
bi se u prvom retku. Ostaje ono sto se mijenja po kadru s gibanjem kamere: **rolling shutter** ili
stabilizacija. Sony ZV-E10M2 u 4K ima rolling shutter, a stabilizacija je na snimanju bila
iskljucena.

**Sljedeci korak za kvalitetu je MODEL KAMERE, ne solver:** poza po RETKU slike umjesto po kadru.
Pri izmjerenih 10.57 st skretanja po kadru je gornji red snimljen milisekundama prije donjeg, a to
nijedna jedna poza ne moze objasniti. Zahvat je u `Bundle.cpp` i `TruthBench` ga moze simulirati,
pa se ucinak izmjeri prije nego udje.

**Prije toga vrijedi jos snimki.** Sve gore je jedna kamera. Druga kamera s drugacijim rolling
shutterom mora pokazati drugaciji promjenjivi signal; to je potvrda izvana i jaca je od bilo kojeg
racuna iznutra.

### 4. Provuci četiri snimke kroz prag (odjeljak 9)

### 5. Izvoz u Blender/Nuke — RIJESENO (USD)

`VideoSolve` sada uz COLMAP tekst pise i **`kamera.usda`**: kamera kroz vrijeme i oblak tocaka,
citljivo u Nuke 17 (GeoImport), Houdini 21 i Blenderu. Vidi `Engine/UsdExport.h`.

Izabran je USD, ne `.chan` ni FBX, iz tri razloga: ti formati nose rotaciju kao **tri Eulerova
kuta** a paketi se ne slazu kojim se redom mnoze (promasen redoslijed ne pada nego samo tise krivo
izgleda), trazie **biblioteku**, i ne mogu nositi splat. USD nosi punu matricu, obican je tekst, a
od OpenUSD 26.03 splat je prvorazredni prim pa scena i kamera stanu u istu datoteku.

Konvencija je gotovo trivijalna jer nasa `Pose` gleda niz **-Z s +Y gore**, bas kao USD kamera -
nema zrcaljenja osi, za razliku od COLMAP-a. Ostaje samo da je USD **po retcima** a glm po
stupcima.

Provjereno na tri razine: `test_usd_export` (5.83e-05 px, uz negativnu kontrolu koja daje 895557
px), pravi solve otvoren **Pixarovom USD bibliotekom** (`GetFieldOfView` vraca 75.378 st, tocno
ono sto je solver javio), i izostanak uzorka za nerijesenu kameru.

Zarisna se pise u **pravim milimetrima** kad se senzor zna. To je usput i dijagnoza: izvezeni
joystick pokazuje 15.2 mm na objektivu 18-50 mm, dakle nemoguce - "f = 2485 px" to covjeku nikad
nije pokazao.

**Sto jos fali za prvi VFX kadar:** nista u izvozu. Kamera i splat izlaze iz istog solvea pa su
vec u istom prostoru - provjereno. Relight NIJE nas problem: ni Framestore na Supermanu nije radio
fizikalni relight nego je pratio kameru s plate-a, skalirao ju u prostor splata i kompozitirao.

### 6. Rolling shutter kao parametar bundlea

Jedan parametar po kadru (vrijeme retka × brzina kamere). Pogađa **svaku** CMOS snimku, ne samo
mobitel.

---

### 7. MapAnything kao inicijalizator za degenerirane scene (novo)

Feed-forward model (Meta+CMU, Apache 2.0 komercijalno slobodan uz `--apache`) izmjeren na nase tri
najteze snimke - vidi `tools/solve/README.md`, odjeljak "MapAnything kao treca mjera". Sazetak:

  - **80 s** na joysticku naspram naseg **19 min** i COLMAP-ovog potpunog neuspjeha (2/197)
  - ali reprojekcija ~9.8% radne sirine slike - regresira TOPOLOGIJU dobro, ne dotjeruje subpikselno
  - stvarna granica VRAM-a na 12 GB kartici: ~32 kadra po pozivu, 96 vec puca

*Kriterij:* NIJE zamjena solvera. Sljedeci korak je nova ulazna putanja u `ReconstructConfig` -
zadani seed poza/tocaka koji nas bundle prima umjesto uvijek-vlastite inicijalizacije - da se
iskoristi ondje gdje nasa geometrija danas nema signala. `tools/solve/mapanything_solve.sh` vec
postoji i radi kao samostalan alat za slucajeve gdje i nas solver i COLMAP padnu.

### 8. Render iz kamere i LoomTracer — PRVA VERZIJA (25.9.)

**Što je.** U editoru: panel **RENDER** u lijevoj traci i gumb **Render (F12)** u alatnoj traci;
prozor sa slikom koja se čisti preko pogleda (**F11**). Render ide kroz riješenu (ili bilo koju)
kameru u pozadinskoj niti nad kopijom scene, pa se smije dalje uređivati. Bira se:

- **engine**: `LoomTracer` (path tracer) ili `Viewport` (kadrovi pogleda kroz kameru u PNG, brzo,
  rezolucija prozora, bez slojeva — pogled se za to vrijeme očisti od mreže, točaka, gizma i HUD-a)
- **što je u slici**: snimka iza CG-a, prozirna pozadina, shadow catcher, nebo vidljivo kameri,
  dubina (Z), normale, albedo
- **kadrovi**: trenutni ili cijeli timeline (`ime_####`), veličina 100/50/25 %
- **svjetlo**: Preethamovo nebo + sunce (elevacija, azimut, jakost, veličina diska = mekoća sjene,
  izmaglica), HDRI (`.hdr`, nekomprimirani `.exr`, `.png`) ili jednolika boja; emisijski materijali
  su sami svjetla
- **prikaz**: Standard (snimka se vraća bit po bit ista) ili AgX; ekspozicija; EXR i/ili PNG

**Shadow catcher.** Uz snimku (ili prozirnu pozadinu) su ravnine, proxy mesh i blokeri iz splata
(`_proxy`/`_blocker` u imenu datoteke) i sve s `catcher` u imenu — *stvarna scena*: kamera ih ne
vidi, ali skupljaju sjenu CG-a (sloj `shadow`), a u odrazu i lomu pokazuju **piksel snimke** u toj
točki (staklo lomi pravi pod, zlato ga reflektira).

**LoomTracer** (`tracer/`): binned-SAH BVH; Sobol s Owenovim miješanjem po parovima dimenzija;
principijelni BSDF (Lambert + GGX s uzorkovanjem vidljivih normala, metal, lak, hrapavo staklo s
lomom) s **nadoknadom višestrukog raspršenja** (Turquin) i skaliranjem difuzije albedom odsjaja;
NEE + BSDF uzorkovanje spojeni MIS-om (sunce kao disk, kugle/reflektori, emisijski trokuti, nebo
po važnosti); ruski rulet; A-trous filtar vođen albedom/normalom/dubinom/varijancom. Warp
materijal je dobio `transmission`, `ior`, `specular`, `clearcoat`, `clearcoatRoughness` (spremaju
se u `.usda`, uređuju u panelu materijala pod TRACER ONLY).

**Izmjereno** (`test_tracer`, 24 provjere protiv analitičkih odgovora, bez kartice):

| provjera | rezultat | istina |
|---|---|---|
| bijela peć: Lambert / plastika / hrapavi metal / staklo / lak | 0.9998 / 0.9998 / 0.9996 / 1.0001 / 0.9997 | 1 |
| negativna kontrola: GGX bez nadoknade, hrapavost 1 | E = 0.451 | (gubi 55 %) |
| sunce na Lambertu (točka i disk 0.53°) | 0.47746 | a·E/π = 0.47746 |
| točkasto i kuglasto svjetlo | 0.63662 | I/(π h²) = 0.63662 |
| svijetli kvadrat nad podom (MIS) | 0.55367 | faktor oblika 0.55413 |
| projekcija (pomaknuta glavna točka) | 0.014 px | formula pogleda editora |
| dubina | 6.00005 / 5.00000 | 6 / 5 |
| snimka izvan sjene | bit po bit | — |
| 1 dretva = 4 dretve | bit po bit | — |

`test_render_bridge` (12, cijeli put: Warp + snimka ffv1 bez gubitka → EXR/PNG natrag): kamera =
pogled editora na **3.8e-6 px**, pravi kadar snimke (plateFirstFrame + kadar − 1), PNG izvan sjene i
CG-a **jednak snimci bajt po bajt**, EXR slojevi, sekvenca u pozadinskoj sesiji. `test_spool_exr`
(5): half zaokruživanje za svih 63 488 konačnih vrijednosti, zapis/čitanje, komprimirani se odbije.

**Brzina** (CPU, 4 jezgre ovog sandboxa): ~9 M zraka/s; 1280×720, 64 uzorka, 10 k trokuta ≈ 18 s.

**LoomTracer na kartici (26.9.)** — `TracerGpu` (`src/TracerGpu/`, spaja Loom i Tracer kao
LoomPreset) + `shaders/tracer.slang` (port `Renderer::trace` redak po redak) i
`shaders/tracer_resolve.slang` (prosjek, composite, AgX/Standard, šahovnica → RGBA8). Scenu gradi
procesor jednom (`Tracer::compile`: BVH, svjetla, CDF-ovi, tablice energije) i **ista** se prepiše
u storage buffere — kartica nema svoju gradnju ni svoju težinu svjetala. Vulkan compute nad
vlastitim BVH-om, ne ray query: radi na svakoj kartici i na llvmpipeu (testira se ovdje).

- Posao: jedan dispatch = jedan uzorak za pojas redaka. `LoomRenderGpu.h` (pogon) ga raspoređuje
  kroz kadrove editora tako da render kadru doda ~12 ms (mjeri se kadar s renderom minus kadar
  bez njega), `loom-render` ~250 ms po predaji (ispod Windows TDR-a). Natrag se čita samo slika za
  prikaz (4 B/px, 2× u sekundi), film tek na kraju.
- `RenderSession` objavi posao (prevedenu scenu), pogon ga u niti koja crta izvrši i vrati film;
  filtar i zapis rade u render niti. Bez Vulkana ili s `--procesor` (panel: Engine CPU) sve ide
  na procesor, a padne li kartica usred kadra, taj kadar se ponovi na procesoru.
- `test_tracer_gpu` (13/13, llvmpipe): bijela peć 0.9997–1.0001, sunce 0.47746, kugla 0.63662,
  svijetli kvadrat 0.55415 (0.55413), projekcija 0.015 px, **slika kartica–procesor RMSE 0.0022**
  uz šum procesora 0.0099 (drugi materijal 0.25 — negativna kontrola), prikaz s kartice =
  procesorski composite+toDisplay **bajt po bajt**, 0 validacijskih poruka. `test_render_bridge`
  (14/14) isto kroz cijelu sesiju s pogonom: snimka izvan sjene netaknuta (4.6e-4, half).
- **Brzina na pravoj kartici NIJE izmjerena** — sandbox ima samo llvmpipe (softverski Vulkan,
  ovdje ~3.7× sporiji od CPU tracera). Prvo mjerenje na RTX-u: `loom-render x.usda --uzorci 256`
  sa i bez `--procesor`.

**Optimizacije CPU puta (isti rezultat, izmjereno na 1280×720, 16 uzoraka, demo scena):**

| korak | prije | poslije | kako |
|---|---|---|---|
| filtar šuma | 3.13 s | 0.62 s | sve jezgre po recima; vodiči u gusta polja; tri `exp` u jedan; x^64 kvadriranjem |
| zapis PNG+EXR | 1.08 s | 0.42 s | svaka datoteka u svojoj niti |
| render | 3.58 s | 3.09 s | Sobol 2. dimenzija tablicom po bajtu (bila je petlja od 32 koraka, callgrind 6.3 %), izbor svjetla binarnom pretragom, popisi sunaca/kugli, slab test u FMA obliku |

Filtrirana slika je ista (RMSE prema referenci 0.0262 prije i poslije).

**Post processing (27.9.)** — `Tracer/Post.h`: bloom (piramida pola-pola pa natrag, prosjek
razina = mekano zvono s dugim repom, **čuva energiju**), kromatska aberacija, vinjeta, balans
bijele (crno tijelo, Kelvin + tint, luminancija ostaje ista), kontrast oko 0.18, zasićenje, zrno
(relativni šum ~1/√svjetla, sjeme po kadru). Fizikalni redoslijed: leća → obrada → senzor, na
linearnoj slici prije Standard/AgX. Ide u **PNG i prozor; EXR ostaje sirov**. Zadnji gotov kadar
ostaje u memoriji, pa se post/prikaz/ekspozicija mijenjaju **poslije rendera** bez ponovnog
računanja (`RenderSession::restyle`, gumb *Save PNG With This Look*; 3 ms na 80×45). Panel:
sekcija POST; CLI: `--post`, `--bloom`, `--vinjeta`, `--aberacija`, `--zrno`, `--temperatura`...
`test_post` 9/9: isključeno = identitet bit po bit, bloom zbroj 1219.95 → 1216.94 (<1 %, rub),
monotono rasipanje, prag ne dira tamno, vinjeta u kutu 0.606 (0.6), zrno srednja 0.1799 i σ 0.0498
(0.05), zasićenje 0 = luminancija, 3200 K toplije uz luminanciju 0.5000, aberacija crveno van.
Na kartici se post još ne računa: progresivni prikaz GPU rendera je bez posta, gotov kadar s njim.

**Nadogradnje 27.9. (prijedlozi 1, 3, 4, 5, 6 — napravljeno redom):**
- **Distorzija leće.** VideoSolve piše `lens.txt` (Brown k1, k2 i objektiv), Warp::Camera ga nosi
  (`loom:distortionLens`, `loom:radialDistortion` u USD-u), tracer zraku puca kroz ispravljeni
  piksel (`Camera::undistortPixel`, 8 koraka), snimka i odrazi se projiciraju istom lećom. EXR
  dobiva ST-mapu (`<ime>_stmap.exr`, R/G undistort + `redistort.R/G`) za Nuke.
- **Svjetla kao entiteti** (`Warp::Light`, UsdLux: Distant, Sphere, Spot, Rect, Dome) — izbornik
  Add, komponenta LIGHT, gizmo u pogledu, pogled PBR slijedi prvo sunce. Nebo *Scene* = samo
  kupola i svjetla scene. **Light From Footage** pretvori `relight.py` JSON u sunce + kupolu.
  `test_render_lights` 8/8 (sunce a·E/π pod zakrenutom grupom, lampa, pravokutnik jednostran...).
- **Motion blur** — scena u `motionSteps` trenutaka unutar otvora (shutter u kadrovima, sredinom
  na kadru), uzorci podijeljeni, filmovi prosječeni, dubina iz srednjeg; snimka ostaje ona kadra.
  Kartica i procesor. `test_render_motion` 7/7 (razmaz 0.5 jedinice, linearna rampa, mirno = isto).
- **Prilagodljivo uzorkovanje** (Noise Threshold, 0.01): piksel stane kad je sqrt(var/n)/sqrt(L)
  ispod praga, provjera svakih 8 od 32 uzorka, isto pravilo u shaderu. **Staklene sjene**: zraka
  sjene prolazi kroz staklo oslabljena bojom i Fresnelom, kaustike putanjama se tada ne broje (MIS
  težina 1 kroz staklo) — umjesto path guidinga/MNEE, bez šuma. *Caustics* vraća točne kaustike.
  `test_tracer_adaptive` 11/11 (ploča stakla 0.9216 = (1−F0)², ravnoparalelna ploča = prava
  kaustika, 169/512 uzoraka uz RMSE 0.0133 prema 0.0189, svjetlina −0.35 %).
- **Mipmape po stošcu zrake** (ray cones): razine linearno usrednjene, stožac = kut piksela × put /
  cos, × gustoća teksela trokuta. `test_tracer_textures` 6/6 (daleki šah bez treperenja).
- **Dubinska oštrina u panelu**: f-broj → otvor iz žarišne (1 jedinica = 1 m, senzor 36 mm),
  fokus klikom na sliku rendera (dubina pod mišem, medijan 5×5) ili *Focus On Selection*.
  `test_tracer_dof` 8/8 (razmaz ruba 4.249 px prema analitičkih 4R/3π = 4.244).
- **Holdout iz splata**: splat projiciran kroz kameru (gaussiani α ≥ 0.4, krug jedne sigme,
  z-buffer, rupe medijanom) → `Scene::holdout`; uzorak koji pogodi CG iza stvarne plohe je
  pozadina (snimka). *Holdout from splat*, `--holdout`. `test_render_holdout` 8/8 (1 M gaussiana
  u 1080p za 0.15 s).

- **Volumetrijsko svjetlo — Volume Box** (`Warp::Volume`, izbornik Add → *Volume Box (fog)*,
  komponenta VOLUME, isprekidana kutija u pogledu): jednolika magla u kutiji entiteta (gustoća po
  jedinici scene, albedo raspršenja, Henyey-Greenstein g). Tracer (`Tracer/Volume.h` + shader):
  slobodni put točno po dijelovima konstantne gustoće (kutije se smiju preklapati), izravno
  svjetlo kroz fazu s MIS-om, **svaka zraka sjene oslabi za exp(−∫σ)** — zrake sunca/reflektora
  i pruge sjena u magli. USD: Xform s `loom:volume*`. `test_tracer_volume` 10/10 (upijanje
  exp(−0.8) = 0.4492, bijela peć 0.9986, jedno raspršenje 0.02263 = analitičko za g 0 i 0.6,
  pruga sjene 0, kartica isto).
- **Prilagodljivo uzorkovanje sa susjedima**: magla je pokazala da piksel kojem 32 uzorka ništa
  ne pogode (varijanca 0) stane crn — točkice u magli. Sad piksel stane tek kad je gotov i na
  prošloj provjeri i kad je svih 8 susjeda bilo gotovo na prošloj provjeri (bitovi po parnosti
  provjere, pa isto na procesoru i kartici). Svjetlina −0.14 % (prije −0.35 %), RMSE kao pun
  broj uzoraka uz 215/512 spp. Na lavapipeu uvjetni upis u buffer stanja ruši LLVM — upis je
  zato bezuvjetan.

- **Brži renderer (28.9., prijedlozi 1–6 redom):**
  1. **OIDN** (Intel Open Image Denoise 2.5) učitan pri pokretanju (`dlopen`, vlastito C sučelje) —
     `tools/oidn/fetch.sh` izvadi knjižnice iz pip paketa `pyoidn` u `tools/oidn/lib`; bez njih
     A-trous. Na 16 spp greška 0.0045 prema A-trous 0.0054 i sirovih 0.0118 (sirovo tek na 256 spp).
     **Filtar i post na kartici** (`tracer_finish.slang`, isti kod kao procesor, razlika ≤ 1 razina)
     za sliku koja se čisti.
  2. **Hardverske zrake** (`VK_KHR_ray_query`, neobavezno; `LOOM_NO_RAY_QUERY=1` isključi): kod
     shadera u `include/TracerCore.slang`, `tracer.slang` = BVH, `tracer_rq.slang` = ray query;
     BLAS iz istih trokuta, geometrija sa zastavicama kroz istu `accept()`. = BVH (RMSE 0.0001).
  3. **Motion blur jednim stablom** (`Scene::motion`): ključevi vrhova/normala/kamere, BVH jednom nad
     kutijama svih ključeva, vrijeme po uzorku; 16 ključeva 0.21 s umjesto 16 gradnji 2.40 s.
  4. **Sekvenca**: `Bvh::refit` (3.6× brže od gradnje, ista slika; svakih 8 kadrova gradi iznova) i
     `UploadCache` (teksture, nebo, mirna geometrija ostaju na kartici; snimka u svom spremniku).
  5. **Stablo svjetala + RIS** (Conty & Kulla; jezgra ReSTIR-a bez ponovne upotrebe): 256 svjetala,
     16 spp — greška po snazi 2.317, stablo 0.335, stablo + RIS 0.132 (= po snazi na 1024 spp).
  6. **Wavefront — izmjereno, nije rađeno**: `loom-render --profil` daje vrijeme po uzorku (BVH i
     ray query) i koherenciju po dubini (aktivne trake, materijala po valu, subgroup brojači).
     Na sceni s maglom: materijala po valu ≤ 1.43, ali od dubine 2 samo 35 % aktivnih traka, od 8
     12.5 % (val od 8 na lavapipeu; na pravoj kartici 32/64 — gore). Dakle ne razvrstavanje po
     materijalu nego **regeneracija/kompakcija putanja** (traka koja završi počne novi uzorak) —
     sljedeći korak, potvrditi `--profil` brojkama na pravoj kartici.

- **Magla — visina, ekviangularno, šum (26.9.):**
  1. **Eksponencijalna magla po visini** (Add → *Height Fog*, `Volume::Shape::Height`, USD
     `loom:volumeShape "height"`, `loom:volumeHeight`): σ = d0·e^(−h/visina) iznad ravnine entiteta
     (os +Y entiteta), beskonačna u širinu. Optička debljina analitička (i za zrake do neba),
     slobodni put Newton + bisekcija na zbroju svih medija. **Holdout magle**: na kameri magla staje
     na stvarnoj plohi iz splata (bez magle "iza zida"). **Dvostruki HG** (g, g2, udio): sjaj oko
     sunca i povratno raspršenje kapljica. Vodoravno 0.5459 / 0.5452, gore 0.7495 / 0.7496, peć 0.9997.
  2. **Ekviangularno uzorkovanje** (Kulla & Fajardo) prema lokalnim svjetlima, **MIS triju
     strategija** (ekviangularno + svjetlo, slobodni put + svjetlo/RIS, slobodni put + faza) —
     sve gustoće izračunljive, nepristrano. Svjetlo za odsječak bira stablo po **cijelom odsječku**
     (`chooseLightOnSegment`: čvor u najbližoj točki zrake, važnost ~1/D). Raspršenje s kamerine
     zrake ide u cg **bez pokrivenosti** (`PathResult::inscatter`). Točkasto svjetlo u magli =
     numerički integral (0.17277), **šum 12× manji** na 16 spp; `--bez-ekviangularnog` za usporedbu.
  3. **Nehomogena kutija**: meki rub (`loom:volumeEdge`) i šum gustoće (`loom:volumeNoise`,
     `loom:volumeNoiseScale`, 4 oktave vrijednosnog šuma u mjerilu kutije). **Delta tracking** po
     majoranti (homogeno ostaje analitički), zrake sjene **ratio tracking** s ruskim ruletom. Meki
     rub 0.5713 / e^−0.56 = 0.5712, ratio/delta = kvadratura (±0.003), peć sa šumom 0.9987.
  `test_tracer_volume` 25/25 (procesor i kartica).

- **Nadogradnje prema UHD i brzini (redom, commit po stavci):**
  1. **Regeneracija putanja na kartici**: `trace` je razbijen na `startPath` / `stepPath` (stanje
     putanje u `PathState`), a `tracePixel` vrti do 4 uzorka po pikselu u JEDNOJ petlji po
     odbijanjima — traka kojoj putanja završi odmah počne sljedeći uzorak. Uzorci su isti kao prije
     (isti indeksi), zbrojevi se upišu jednom. Prvi uzorci idu po 1, pa 2, pa 4; paket nikad ne
     prelazi provjeru prilagodljivog uzorkovanja. `GpuTracer::setSamplesPerDispatch`,
     `LOOM_SAMPLES_PER_DISPATCH`. Koherencija se sad broji po koraku petlje. Mješovita scena:
     aktivnih traka 74 % → 83 %; lavapipe (val 8): magla s lampama 537.7 → 487.6 ms po uzorku,
     visinska magla 199.7 → 186.6 ms. Na kartici s valom 32/64 očekivano više — izmjeriti.
  2. **Wavefront po materijalu — NE radi se** (izmjereno i obrazloženo): svi materijali idu kroz isti
     uber-BSDF (`makeBsdf/evalBsdf/sampleBsdf`), pa razni materijali u valu ne razdvajaju kod, samo
     čitanja tekstura; izmjereno 1.2–2.1 materijala po valu. Prazne trake zbog putanja različite
     duljine rješava regeneracija (1.). Puni wavefront s redovima = prepisivanje tracera bez dobitka.
  3. **ReSTIR DI na kartici** (prvi pogodak): glavni prolaz sprema RIS rezervoar i točku sjenčanja
     (G-buffer 48 B + rezervoar 16 B po pikselu), drugi dispatch istog uzorka spaja vlastiti s do 8
     susjeda (krug `širina/64`, 2–30 px), **pairwise MIS**, ciljevi bez vidljivosti, jedna zraka
     sjene — nepristrano (512 spp −0.04 %, MIS s velikim svjetlom = procesor, miješana scena
     +0.01 %). Uzorak `y` je u mjeri svjetla (baricentrične / smjer od središta / smjer), pa se
     procjenjuje iz bilo koje točke. 256 svjetala, greška prema RIS-u: 48 px 1 spp 0.585/0.856,
     4 spp 0.279/0.332; 192 px 1 spp 0.326/0.824 (2.5×), 4 spp 2.0×, 16 spp 1.3×, ali 64 spp
     0.036/0.026 — posuđeni uzorci nemaju stratifikaciju piksela. Vremenska ponovna upotreba
     izmjerena i izbačena (u progresivnom zbrajanju povezuje uzorke, greška raste). Zato
     `RenderSettings::restirSamples`: most ga uključi za sve uzorke kad ih je ≤ 16 (`--bez-restir`).
     Cijena ~15 % vremena po uzorku (lavapipe).
  4. **Filtar na kartici i vremenska stabilnost**: OIDN bira najbrži uređaj (`OIDN_DEVICE_TYPE_DEFAULT`
     — CUDA/HIP/SYCL kad su njihove biblioteke uz OIDN, `tools/oidn/fetch.sh --gpu`, inače CPU;
     `LOOM_OIDN_DEVICE=cpu|cuda|hip|sycl`), preko spremnika uređaja (`oidnNewBuffer`), a log kaže
     na čemu radi ("OIDN (CUDA)"). Ovdje nema kartice — provjeren je samo put s CPU uređajem.
     **`Tracer::stabilize`** (Temporal.h): prošli stabilizirani kadar prebačen po dubini i objema
     kamerama (distorzija uključena), prihvaćen samo na istoj plohi (dubina 2 %, normala 0.9) i
     unutar šuma piksela (promjena svjetla — sjena koja putuje — odbijena); miješa se osvjetljenje
     (boja/albedo, kao SVGF) Catmull-Romom stegnutim na susjede, pa tekstura ostaje oštra.
     `test_tracer_temporal`: mirna kamera titranje 0.00329 → 0.00184 (56 %), greška 0.00765 →
     0.00654; kamera u pomaku greška 0.00771 → 0.00687; kutija u pokretu bez duhova (0.01277 =
     0.01277). Sekvence: zadano 0.5, `--stabilnost X` (0 = bez). Pristrano (vremenski prosjek).
  5. **Render u nižoj razlučivosti + pametno povećanje — izmjereno, NE isplati se, nije u kodu.**
     Isprobano: pola razlučivosti, OIDN, pa zajedničko bilateralno povećanje osvjetljenja (boja/albedo)
     vođeno vodičima pune razlučivosti (albedo, normala, dubina, pokrivenost iz prolaza bez
     odbijanja). Pod povećanja (čist ulaz) je oštri detalj svjetla manji od piksela niske
     razlučivosti: 128×96 0.0154, 512×384 0.0071 — a puni render s OIDN-om već na 4 spp daje 0.0052
     u istom vremenu (pola + 16 spp + povećanje 0.0077). I podjela izravno u punoj / neizravno u pola
     razlučivosti (neizravno = puni − jednostruki put s istim sjemenom, točno) je samo izjednačena:
     4 spp 0.0055 (0.21 s) prema 0.0052 (0.30 s), 16 spp 0.0034 (0.78 s) prema 0.0031 (0.64 s).
     Za UHD je zato put: puna razlučivost, malo uzoraka, OIDN (na kartici) + ReSTIR do 16 spp.

**Što dalje:**
1. **Izmjeriti pravu karticu** (`loom-render projekt.usda --profil`) — sve dosad je lavapipe, gdje su
   i "hardverske" zrake softverske (ray query 11.1 ms prema BVH 9.2 ms po uzorku).
2. **Regeneracija putanja** na kartici ako `--profil` na pravoj kartici potvrdi prazne trake.
3. Holdout iz procijenjene dubine snimke (`tools/depth`, treba kalibraciju mjerila) i sjene CG-a
   na splat (normala iz dubine) — sada sjenu hvataju samo catcheri.

**Poznata ograničenja — ne skrivati:**
- Hardverske zrake za scene s pomakom ne (motion blur u hardveru je samo NVIDIA ekstenzija) — tada BVH.
- OIDN radi na procesoru (i u gotovom kadru); na kartici je A-trous (pregled).
- Staklene sjene su pristrane (nema fokusiranja svjetla iza leće); s *Caustics* su točne, ali
  šumne — kao Cycles bez caustics trikova.
- Prilagodljivo uzorkovanje zaustavlja po procijenjenoj varijanci (piksel i susjedi): područje u
  kojem SVI pikseli rijetko pogode svijetli događaj još može stati malo pretamno (−0.14 %).
- Magla: nema VDB-a (šum je proceduralan); majoranta je jedna po kutiji (d·(1+šum)) — u rijetkom
  šumu puno praznih koraka delta trackinga. U prozoru pogleda vidi se samo kutija / gizmo visinske
  magle, ne sama magla (treba composite s dubinom preko splata i mreža).
- Filtar nije OIDN: na 64+ uzoraka čisti, na 4–16 ostavlja mrlje; sirovi CG je uvijek u EXR-u.
- Catcher pod u neizravnom svjetlu uzima albedo ≈ linearni piksel snimke (pretpostavka jedinične
  rasvjete poda) — boja se prelije ispravno, jakost je približna.
- Splat se ne tracea (samo holdout i relight); iza CG-a je snimka. Holdout splata ne baca sjene.
- Motion blur gradi scenu po odsječku (16 × BVH po kadru): za teške scene sporo.
- Engine `Viewport` crta ravnine neprozirno (raster nema catcher).
- Samo prvi UV skup; glTF `occlusion` mapa se namjerno ignorira (tracer zaklanjanje računa).

**Editor se provjerava okom**: `loom projekt.usda --snimi x.png --render [--uzorci 32]` otvori panel,
renderira i spremi prozor kad render završi (`--render-pogled` za engine Viewport). Pod Xvfb-om s
lavapipeom (`VK_ICD_FILENAMES=.../lvp_icd.json xvfb-run -a ...`) radi i bez kartice.

### 9. Šake, prsti i hvat predmeta — STANJE 26.9. (navečer)

Cilj korisnika: (a) kad lik uhvati predmet (mač, pištolj...), prsti se prirodno sklope oko njega i
realno ga drže; (b) UI/UX hvata jednostavan i intuitivan. Lik za sve provjere je **desni klik >
HumanoidMascott** = `tools/autorig/outputs/humanoid-mascott/rigged.glb` (novi lik, izravni rig — vidi
"Novi mascot" niže; lokalno, nije u gitu), u editoru `./build/loom --mascott`. Stari lik (Clockwork
Sentinel) je još u `outputs/mascot-manny`, `mascot-01..03` i `rig_*` — nije obrisan.

**Gotovo i commitano (main):**

| commit | što | mjera |
|---|---|---|
| `106b548` | hand rig u auto-rigu: +X savija prst u dlan, Z prema dlanu | savijanje prema dlanu -1.000 → +0.999 |
| `fef29b1` | mirne šake na svakom rigu (`LoomRelaxedHands.h`, gumb Relax Hands); SOMA `Index1` je metakarpal — mapa retargeta bila pomaknuta za zglob | greška kutova prstiju 10.9 → 7.2° |
| `99f51c6` | prsti se prenose u sustavu kosti (`motionFingerAlignment`); UniRig peace sign provjeren (`test_unirig_fingers`) | A-poza + zakrenut dlan: savijanje/raširenost 8.1/16.0 → 0.44/0.44° |
| `b5f73f6` | hvat: drška u dlanu, prsti se omataju kao pravi prst, pištolj za rukohvat | vidi dolje |

`b5f73f6` u detalju (`test_grab_real`, mascot-manny 1.80 m + `~/Downloads/bastard_sword__lowpoly.glb`):
- točka dlana je bila **zapešće** (Manny nema `MiddleEnd`) → sad iz prstiju u stablu, 70 % prema zglobovima;
- lik bez animacije nije savijao prste → `syncHoldHandLayers` napravi prazan klip "Pose";
- omatanje: svi zglobovi zajedno u malim koracima + pokušaj od stisnute šake; vrh prsta (rig bez kosti
  vrha) nosi rotacija zadnjeg zgloba; granice anatomske 90/110/90°;
- dijagonalni power grip 20°, `pistolGrip` (rukohvat poprijeko na cijev, cijev naprijed za obje šake),
  debljina drške prema dlanu, potraga položaja šake na dršci, collider za prste iz mreže 2 mm;
- mač: dlan 4.6 cm (očekivano 4.6), omatanje 47 → 150°, vrhovi ≤ 1.0 cm od drške, bez prodora, 39 ms;
- pištolj (Desert Eagle, 460k trokuta): rukohvat u dlanu, cijev naprijed, 2955 → 285 ms; mali prst
  pada ispod rukohvata (šaka mascota je veća od rukohvata) — prihvatljivo, nije savršeno.
- CLI za snimke hvata: `--lik <glb>`, `--tool <glb>`, `--uhvati desna|lijeva`, `--pogled-saka <yaw> <pitch>`
  (npr. `./build/loom --mascott --tool ~/Downloads/bastard_sword__lowpoly.glb --uhvati desna --pogled-saka -1.2 -0.4 --snimi x.png`).

**HOLD u Inspectoru — provjereno na Xvfb-u (26.9. kasno):** sekcija **HOLD** umjesto plutajućeg TOOL EDITOR
panela. Kliknuto i radi: Right hand / Left hand (hvat od ovog kadra), Other hand, Let go here (traka na
timelineu staje, predmet ostaje gdje ga je šaka pustila), Remove, preseti (Open otvori prste), Hand on the
tool (šaka klizi po predmetu), Flip, Turn palm, Look at the hand. Kod: `loom_app.cpp`, traži
`//HOLD: sve o drzanju predmeta`. Popravljeno usput:
- mač je u Inspectoru pokazivao **ANIMATOR lika** (`motionCharacterForEntity` se od toola popne do
  `Scene_Root`); nakon hvata se raširio i gurnuo HOLD izvan pogleda → za tool nema Animatora;
- **dlan je ležao na oštrici**: `defaultGrip` je stavljao `palm = axes[1]` (širina). Sad `axes[2]`
  (plosnati dio u dlanu, oštrica/štitnik naprijed kao zglobovi). `test_grab_real` ima mjeru "oštrica":
  0° → 90°, omatanje 150 → 162°, dlan 4.3 cm (očekivano 4.3);
- OBJECT HUD je pokrivao šaku (ishodište toola je u dlanu) → za predmet u šaci ide u gornji desni kut;
- Look at the hand je gledao kroz podlakticu → `lookAtHand`: sa strane palca, malo odozgo, odmak 6× veličina
  šake. Isto iz CLI: `--pogled-saka auto` (odabir ostaje, snimka je ono što korisnik vidi);
- klizač Hand on the tool je kod Flip skakao 12 → 88 % → postotak uvijek od istog kraja predmeta.

**Novi mascot i izravni Manny rig bez UniRiga — U RADU, NIJE COMMITANO (26.9. noću):**
- Korisnik je dao novi model: premješten iz `~/Downloads` u `assets/characters/HumanoidMascott.glb`
  (47 MB, 400k vrhova, bez riga, A-poza, razdvojeni prsti; nije u gitu — `assets/` je necommitan).
- UniRig na njemu predvidi samo **40 kostiju** (3 prsta po šaci) za seed 42, 1 i 7 → `manny_rig.py` ga
  odbije. Korisnik je tražio "direktni Manny bez UniRiga", pa je napisan **`tools/autorig/direct_rig.py`**
  (samo numpy, radi u Blenderu): zglobovi iz geometrije — međunožje (sredina x≈0), noge i kralježnica po
  omjerima Manny predloška između izmjerenih sidara, ruka praćenjem horizontalnih presjeka (pazuh, vrh
  ramena, zapešće = najuži presjek prije prstiju, lakat na 50.5 %), prsti kao grane stabla presjeka šake
  (`finger_branches`: palac se odvoji prvi, ostali po udaljenosti od palca), zglobovi prstiju po omjerima
  članaka; težine = najbliži segment kosti s uskim prijelazom, lijeva/desna strana odvojene. Izlaz je
  raspored UniRig-52 (`bone_N`, isto stablo roditelja) → postojeći `manny_rig.py` + `hand_rig.py` +
  `validate.py`.
- `run.py --backend direct` (od 26.9. zadano je `auto`: prvo direct, a ako padne i UniRig je instaliran,
  UniRig u isti izlaz; direct izlaz ostaje u `<izlaz>-direct-failed`. Editorov Auto Rig gumb koristi `auto`
  i treba samo `.venv` + `direct_rig.py`, `vendor/UniRig` više nije uvjet):
  `tools/autorig/.venv/bin/python tools/autorig/run.py --backend direct --input assets/characters/HumanoidMascott.glb --output tools/autorig/outputs/humanoid-mascott`
  → 49 s (UniRig ~4 min), prolazi Blender validaciju. Slika zglobova preko rendera: ramena/laktovi/koljena
  na zglobnim diskovima, svih 5 prstiju prati članke, lijevo = desno na mm.
- `manny_rig.py`: **metakarpali** se sad stavljaju na pravac zapešće → baza svog prsta (omjer iz Manny
  predloška, ~0.41) umjesto reziduala najbližih zglobova — kod raširenih prstiju je metakarpal malog
  prsta prelazio preko prstenjaka (raširenost u `test_hand_rig` 2.49 → 0.66°). Mijenja i UniRig put.
- Mjere (skripta u scratchpadu, isti kriterij kao zamka niže): šaka 427 → ~6200 vrhova; vrhovi prstiju
  vezani za kost dalje od 3 cm 23.7 % → 3.9 % (desna) / 5.7 % (lijeva); kosti prstiju bez vrha 10/19 → samo
  4 metakarpala (u Mannyju bez težina, tako i treba). `test_grab_real` 5/5: omatanje 162 → 175°, vrhovi
  ≤ 0.8 cm, dlan 3.4/3.4 cm. `test_hand_rig` 10/10.
- Kod prebačen na novi lik: `addHumanoidMascott` u `loom_app.cpp` (samo `humanoid-mascott`, bez starih
  rezervi), `tests/test_hand_rig.cpp` i `tests/test_grab_real.cpp`. `test_unirig_fingers` ostaje na
  starom `mascot-03` (vidi zamke).
- Editor se gradi; snimke novog lika (`n0.png` cijeli lik, `n1.png` mač u desnoj s `--pogled-saka auto`)
  su snimljene, ali **nisu pregledane**.

**Zamke:**
- `test_unirig_fingers` i `applyUniRigActionDetails` / UniRig preset mirne poze (`LoomMotionRetarget.h`,
  `LoomRelaxedHands.h`) okreću prste i ruke oko **svjetskih osi** — podešeno za stari UniRig lik u T-pozi.
  Na `rigged_direct52.glb` (A-poza) peace sign pada (kažiprst 53°, srednji 64° umjesto ≤ 25°). Glavni
  mascot ide Manny putem pa to ne dira; kad se `mascot-03` obriše, test se preskače.
- `pkill -f`/`pgrep -f` s uzorkom iz vlastite naredbe ubije i vlastitu ljusku — gasi samo po spremljenom PID-u.
- (stari lik) **Mesh šake mascota je loš za prste**, ne algoritam: cijela desna šaka ~430 vrhova; 6 kostiju prstiju
  (`index_01`, `middle_01`, `pinky_01`, `pinky_02`, `ring_02`, `thumb_01`) nije glavna kost nijednom vrhu;
  24 % vrhova prstiju vezano je za kost dalje od 3 cm (neki `index_02` vrhovi 10.7 cm). Kod savijanja se
  razvuku u "krhotine" — vidi se već u mirnoj pozi. Novi čišći model (odvojeni prsti, dovoljno petlji na
  zglobovima, ~2-5k vrhova po šaci, T/A-poza s blago raširenim prstima) kroz auto-rig bi to riješio.
- Kad mjeriš prste, kut bez predznaka laže (miješa bočni otklon sa savijanjem) — mjeri s predznakom u
  ravnini savijanja (tako je "7° ostatak" bio artefakt).
- Model uvezen kao `scene.gltf` (Sketchfab zip) ne prepozna se po imenu → ostane 514 m dug. Treba
  procjena veličine iz oblika kad ime ništa ne kaže.
- `WeaverMotion/Manny_reference.glb` kroz `--lik` se razleti (mjerilo/hijerarhija) — nije vezano za hvat.
- Drugi agent (Codex) istodobno mijenja `loom_app.cpp`, Proceduru i render; commitaj samo svoje hunkove
  (`git apply --cached` s izdvojenim hunkovima). Njihov build zna biti privremeno slomljen.
- Xvfb `:78` je dvaput ugašen izvana; koristi vlastiti (npr. `:79`).

**Sljedeći koraci, redom:**
1. **Pregledati snimke novog lika** (`./build/loom --mascott --snimi x.png`, i s mačem:
   `--tool ~/Downloads/bastard_sword__lowpoly.glb --uhvati desna --pogled-saka auto`): trga li se mesh
   (oklop ramena, zdjelica, kablovi preko laktova), jesu li prsti čisti u hvatu. Ako se dijelovi trgaju:
   u `direct_rig.skin_weights` kruto vezati zavarene dijelove i kad su dulji (sad samo < 8 % visine), a u
   `manny_rig._rigidify_fragmented_mesh` ne vezati kruto UV-otoke koji prelaze zglob (sad svih 19914 otoka).
2. Provjeriti Kimodo pokret na novom liku (hod, peace sign kroz Manny put) i `bend_preview.glb`.
3. **Commit** (samo svoje): `tools/autorig/direct_rig.py`, `run.py`, `manny_rig.py`, `tests/test_hand_rig.cpp`,
   `tests/test_grab_real.cpp`, hunk `addHumanoidMascott` u `loom_app.cpp` (tuđi hunkovi ostaju — isti
   postupak kao 4e40b78: HEAD verzija + moje zamjene → `git update-index --cacheinfo`), HANDOVER; mjerenje
   u `benchmarks/mjerenja.jsonl` + `MJERENJA.md`. `assets/characters/HumanoidMascott.glb` (47 MB) pitati
   korisnika hoće li u git.
4. **Obrisati stari lik** (korisnik je tražio): `tools/autorig/outputs/mascot-manny`, `mascot-01..03`,
   `rig_*` (sve Clockwork Sentinel). Prije toga odlučiti o `test_unirig_fingers` (preskakat će se).
5. ~~`direct` kao zadani backend Auto Rig gumba~~ — GOTOVO 26.9. (`auto`, UniRig kao rezerva). Otvoreno:
   T-poza nije isprobana na directu (horizontalni presjeci ruke) — ako padne, radi UniRig rezerva.
6. UniRig-52 detalji pokreta i preset mirne poze s osima iz dlana umjesto svjetskih (vidi zamke).
7. Stare stavke: veličina toola bez imena (514 m), pištolj (okidač, high grip, dvoručni), hvat kroz
   Kimodo klip, Right hand → Other hand na istom mjestu (dvoklik), Animator stavke (`animator-plan-2026-09`).

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
| polje ostataka | bez uzorka koji se mijenja po kadru | ✓ (`TruthBench`: cisto/2 px/5 px) |
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
cd build && ctest -j1                      # 88 testova

./build/TruthBench luk 30                  # apsolutna greška, minute
./tools/solve/bench.sh                     # cijela tablica

./build/VideoSolve snimka.mp4 10 80 0 izlaz/
./build/ModelInfo izlaz/                   # zdravlje rješenja, bez istine

./build/loom-render projekt.usda --uzorci 256 --normale   # render iz kamere (LoomTracer)
./build/test_tracer                        # tracer protiv analitičkih odgovora, bez kartice
```

**Ključna dokumentacija: `tools/solve/README.md`** — svako mjerenje, svaka odbačena ideja i svaki
ispravak vlastite greške, s brojevima. Pročitati prije dodirivanja solvera.
