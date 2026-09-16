# Od snimke do poza

Loomov vlastiti solver je ono sto se razvija; COLMAP je ovdje **mjerilo** i radni put dok solver
jos ne stize. Oba zavrsavaju u istom formatu, pa se citaju istim alatom i usporedjuju istim
brojkama.

## Zasto oba

Na istom isjecku snimke (Sony 4K 50p, soba, gimbal), 101 kadar:

| | COLMAP | Loom, rujan 2026. |
|---|---|---|
| rijesene kamere | 65 / 101 | **101 / 101** |
| tocaka | 26 761 | 51 778 |
| opazanja po kameri | 3 564 | 3 398 |
| reprojekcija | 0.75 px | 1.25 px |

Broj kamera i gustoca opazanja vise nisu problem. Ostalo ih je dvoje, i oba se mjere.

### Sto je rijeseno

**Sam solver.** Kad mu se predaju COLMAP-ove vlastite korespondencije, Loom daje rjesenje jednako
njegovom: objedinjeno 0.735 px naspram 0.746, medijan po kameri 0.76 naspram 0.77, najgora kamera
1.33 naspram 1.44. Dakle na dobrom ulazu nema zaostatka.

**Korespondencije.** Trazenje i poklapanje idu na sirini od 960 px, ne na punih 4K: ondje detektor
hvata sum senzora koji se izmedju kadrova ne ponavlja. Udio poklapanja koja prezive geometrijsku
provjeru ide s 36 na 82 posto, a opazanja po kadru s 300 na 3400.

### Koliko se poze slazu s COLMAP-ovima

Mjeri se poravnanjem slicnoscu (Umeyama) na zajednickim kamerama; mjerac je provjeren na poznatom
odgovoru - COLMAP-ove poze preslikane zadanom slicnoscu pa usporedjene same sa sobom daju tocnu
nulu i tocno zadano mjerilo.

| ulaz | kamere | polozaj | rotacija |
|---|---|---|---|
| COLMAP-ove korespondencije | 65 / 65 | **0.2 %** | **0.51 st** |
| nase korespondencije | 96 / 101 | 4.4 % | 7.09 st |

Prvi redak je vazan jer zatvara jedno pitanje zauvijek: **solver nije problem**. Na dobrom ulazu
daje poze koje se od COLMAP-ovih ne razlikuju.

### Sto je popravilo drugi redak

Bio je 15.7 % i 26.8 st. Dvije stvari, obje izmjerene:

**Pomijesani tragovi.** Trag nastaje kao prijelazno zatvorenje poklapanja, pa jedno krivo
poklapanje slijepi dva neovisna traga u jedan. Prepozna se po tome sto takva komponenta jedan kadar
dodirne dvaput - jedna tocka ne moze biti na dva mjesta u istoj slici. Takvih je 11 361 i nose
92 848 od 343 161 opazanja, dakle **dvadeset sedam posto svega**. Prije se zadrzavalo prvo vidjeno
po kadru, sto ne popravlja nista: trag ostane jedan, samo pomijesan iz dvije tocke, i triangulira
negdje izmedju njih. Sada se cijela takva komponenta baca (`dropConflicting`).

**Pod paralakse.** Na COLMAP-ovim korespondencijama je svejedno je li 0.5, 1.0 ili 1.5 - rezultat
je isti do zadnje znamenke. Na nasima nije: 0.5 daje 21 % i 177 st, 1.0 daje 4.4 % i 7.1 st.
Zadano je sada 1.0.

### Suglasnost trojki

Brid A-B provjerava dvoprizorna geometrija, a ona propusta sve sto lezi na epipolarnoj crti -
ukljucujuci krivo poklapanje na ponavljajucoj teksturi. Trojka je jaca provjera i ne trazi poze:
ako postoji znacajka C koja se poklapa i s A i s B, onda se TRI kadra slazu. C je nuzno u trecem
kadru, jer se poklapa samo izmedju razlicitih.

| svjedoka po bridu | kamere | polozaj | rotacija | duljina traga |
|---|---|---|---|---|
| 0 | 96/101 | 4.4 % | 7.09 st | 3.09 |
| **1** | **101/101** | **1.6 %** | **6.60 st** | **5.16** |
| 2 | 101/101 | 24.9 % | 86.06 st | 5.55 |

Jedan svjedok otklanja gotovo tri cetvrtine greske polozaja; sukobljenih komponenti ostane 5762
umjesto 11361. Dva ruse sve - odbace 639 tisuca bridova umjesto 338, i s njima i ono sto je scenu
drzalo na okupu.

Tragovi su duzi iako se bridovi BACAJU, i to nije proturjecje: bacaju se oni koji su tragove krivo
spajali, pa ono sto ostane prezivi ciscenje sukoba umjesto da padne s njim.

Filtar se NE primjenjuje kad trojka ne moze ni nastati - ispod tri kadra ili uz prozor 1.

### Prag jacine ugla: popravi poze, pokvari splat

Nase su tocke bile rasute po praznom bijelom zidu, a COLMAP-ove sjede na fugama kamena - pa je
izgledalo ocito da ih treba odbaciti. Prag se izvodi iz SUMA slike, ne zadaje: bodovanje je
Shi-Tomasi, a za cisti sum obje svojstvene vrijednosti imaju poznatu ocekivanu vrijednost.

Poze i tragovi se time popravljaju - rotacija 6.60 -> 5.06 st, duljina traga 5.16 -> 5.95 - ali
splat postaje losiji: 26.81 -> 26.34 dB, najgori kadar 24.69 -> 22.35.

Slabe tocke na zidu jesu geometrijski slabe, ali treneru daju POKRIVENOST: bez njih zid nema od
cega poceti. Zadano iskljuceno.

**Poanta je sira od tog polja**: slaganje poza s COLMAP-om nije pouzdan pokazatelj kvalitete
splata. Svaka izmjena mora zavrsiti mjerenjem u decibelima.

### Dvije ideje koje su pale na mjerenju

**Spajanje koje odbija sukob** umjesto da pokvarenu komponentu poslije baca. Radi ono sto obecava -
sukobljenih ostane nula, tragovi se produze s 3.72 na 5.88 kadra, opazanja s 343 na 946 tisuca - a
poze su svejedno losije: najbolje sto daje je 11.5 % i 26.2 st, protiv 4.4 % i 7.1 st. Razlog je u
sucu: "prvi stigao pobjedjuje" znaci da krivo poklapanje s manjom udaljenoscu potpisa zauzme mjesto
i pravo se ODBIJE. Ostaje kao polje (`conflictFreeMerge`), zadano iskljuceno.

**Kraci prozor poklapanja.** Poklapanje drzi do razmaka od sedam kadrova (1680 provjerenih parova) i
na deset pada na 25, pa je izgledalo da prozor od deset radi uprazno i samo unosi smece. Ne: s
prozorom 7 tragovi se skrate s 3.72 na 3.12 kadra, a poze daju 4.5 % i 12.2 st uz 87 kamera umjesto
4.4 % i 7.1 st uz 96. Onih par poklapanja na velikom razmaku nosi bazu koju nista drugo ne daje.

### A daju li nase poze bolji splat

Ne. To je jedina mjera koja stvarno broji, i dugo nije bila napravljena - sve brojke o kvaliteti
splatova dosad bile su trenirane na COLMAP-ovim pozama.

Usporedba je postena koliko se dalo: ISTIH 65 kadrova (nasih 31 dodatnih izostavljeno da izdvojeni
skup bude isti), iste ISPRAVLJENE slike, isti pinhole model, isti trener, istih 7000 koraka,
isti izdvojeni odsjecci. Razlikuju se samo poze.

| poze | PSNR medijan | najgori | SSIM |
|---|---|---|---|
| COLMAP | **29.59 dB** | 23.85 | 0.903 |
| Loom, sa suglasnoscu trojki | 26.81 dB | **24.69** | 0.861 |
| Loom, prije nje | 25.68 dB | 18.25 | 0.835 |

Jaz je s 3.91 pao na 2.78 dB, a NAJGORI kadar nam je sada bolji od njegovog - 24.69 naspram 23.85.
Medijan jos zaostaje, ali scena vise nema mjesta koja se raspadnu.

Uz to su usput pronadjene dvije rupe u lancu:

**Izvoz nije nosio imena slika.** `writeColmapText` je pisao `frame_0000.png`, a trener model i
slike spaja bas po imenu - pa se nas solver do sada nije mogao ni nahraniti. Sada nosi prava imena.

**Trener ignorira distorziju.** Cita f, cx i cy, a k1 preskace. Na ovoj snimci k1 pomice rubni
piksel za 5.62 px, pa je svaka dosad trenirana scena imala rubove nacrtane krivo. Ovdje je
zaobidjeno tako da su slike ispravljene prije treninga; u samom treneru jos nije.

### Fina lokalizacija: ideja tocna, izvedba nije

Polozaji znacajki su kvantizirani na cetiri piksela jer se poklapa na smanjenoj slici, a
COLMAP-ovi su subpikselni - pa je izgledalo da je dotjerivanje na punoj slici najizravniji potez.

`refineCorner` je tocan i ima vlastiti test: iz procjene promasene 1.54 px vraca kut na 0.13 px.
Ali primijenjen po opazanju NEOVISNO, steti na svakoj postavci:

| najveci dopusteni pomak | kamere | polozaj | rotacija |
|---|---|---|---|
| bez dotjerivanja | **96/101** | **4.4 %** | **7.09 st** |
| 1 px | 91/101 | 7.2 % | 11.94 st |
| 2 px | 66/101 | 28.1 % | 120.54 st |
| 4 px | 90/101 | 33.0 % | 57.78 st |

Monotono, i vec na jednom pikselu losije nego bez njega. Razlog: na 4K je unutar cetiri piksela
vise uglova, pa se dva opazanja istog traga zalijepe na RAZLICITE. Trag koji je bio kvantiziran ali
dosljedan postane tocan ali nedosljedan - a triangulaciji treba ovo drugo.

Prava inacica trazi dosljednost: jedno opazanje traga je referentno, a ostala se dotjeruju
Lucas-Kanadeom prema njegovoj zakrpi.

### Sira baza postoji u snimci, ali je nas potpis ne vidi

Drift se svodi na bazu: zaokret po koraku je 0.073 st, a SMJER koraka griješi 3.05 st - a smjer
pomaka izmedju dvije kamere odredjuje paralaksa. Nasa baza je medijan 3.36 st, COLMAP-ova 7.93.

Gdje je sira baza: u COLMAP-ovom modelu se vidi da kamera OBILAZI i vraca se. Zajednickih tocaka
po razmaku kadrova:

| razmak | 1 | 5 | 10 | 15 | 20 | 30+ |
|---|---|---|---|---|---|---|
| medijan zajednickih | 732 | 376 | 119 | **764** | **613** | ~0 |

Najjace preklapanje NIJE na najmanjem razmaku nego na 15 do 20, jer se tada gleda isti zid s druge
strane sobe. Parova s razmakom vecim od 15 i preko sto zajednickih tocaka ima 137, a nas prozor
staje na 10 - ne gledamo ih uopce.

Ali prosirivanje prozora ne pomaze, nego ruši:

| prozor | kamere | polozaj | rotacija | smjer koraka |
|---|---|---|---|---|
| 7 | 87/101 | 4.5 % | 12.16 st | - |
| **10** | **101/101** | **1.6 %** | **6.60 st** | **3.05 st** |
| 20 | 101/101 | 33.4 % | 139.98 st | 137.13 st |

Razlog je u potpisu: na razmaku 15 do 25 nas potpis nalazi 16 do 25 provjerenih parova ondje gdje
COLMAP ima 600 do 760 zajednickih tocaka. Tridesetak puta manje, i vecina toga je sum - a sum na
velikom razmaku steti vise nego na malom, jer stvara lazne veze izmedju udaljenih dijelova snimke.

**Prozor 10 je time optimalan ZA OVAJ POTPIS, a ne po sebi.** Sirina baze koja u snimci postoji
ceka razlucljiviji potpis.

### SIFT-ov potpis: dvostruko bolji po paru, jos ne i u lancu

Napisan je potpis preko histograma gradijenata - 4x4 celije po 8 smjerova, trolinearna raspodjela,
128 bajtova. Ima vlastiti test (isti detalj ostaje blizu, razliciti daleko, svjetlina ne mijenja
nista, rub vraca false).

Po paru kadrova je bitno bolji od binarnog, koliko parova prezivi geometrijsku provjeru:

| razmak kadrova | 1 | 10 | 15 | 20 |
|---|---|---|---|---|
| binarni | 4322 | 25 | 18 | 22 |
| SIFT | **9424** | **61** | **29** | **40** |

Dvije stvari su ga dovele dotle, obje izmjerene:

**Zagladjivanje prije gradijenata.** Izvorni SIFT ih racuna na slici zamucenoj na mjerilo znacajke.
Bez toga potpis opisuje najfiniju teksturu koja se izmedju dva pogleda ne ponavlja: 2677 -> 6248
poklapanja na susjednom kadru. Ista greska koju smo vec jednom napravili s binarnim potpisom.

**Drugi po redu mora biti prostorno odvojen.** Prag omjera pretpostavlja da je drugi po redu KRIVO
poklapanje. Kod uglova na tri piksela razmaka i zakrpe od 24 to ne stoji - susjedni ugao gleda
gotovo istu okolinu, pa je drugi po redu SUSJED PRAVOG i prag odbija tocna poklapanja. Na razmaku
od deset kadrova: 7 poklapanja s pragom, 1815 bez njega, 388 uz prostornu ogradu.

U CIJELOM LANCU jos ne prolazi. Tragovi se produze i sukobi nestanu, ali poze ostaju krive:

| uglovi | tragovi | sukobljenih | polozaj | smjer koraka |
|---|---|---|---|---|
| binarni, razmak 3 px | 5.16 | 5762 | **1.6 %** | **3.05 st** |
| SIFT, razmak 3 px | 4.74 | 8390 | 18.2 % | 124.25 st |
| SIFT, razmak 8 px | 6.32 | 1435 | 36.2 % | 124.32 st |
| SIFT, razmak 12 px | **7.30** | **173** | 33.9 % | 126.01 st |

Duljina traga od 7.30 je najbliza COLMAP-ovih 8.7 sto smo ikad imali, i sukoba prakticki nema - a
rjesenje je svejedno zrcaljeno, i to ISTIM kutom kroz sve postavke. Ponovljeni 124 st kroz posve
razlicite postavke nije ugadjanje nego nesto sustavno u tom putu, i to je sljedece sto treba naci.
Zadano je `useSift = false`.

### Nasa poklapanja su 97 posto tocna - meta je duljina traga

Dvoprizorna geometrija propusta sve sto lezi na epipolarnoj crti, pa "prezivjelo geometriju" ne
znaci "tocno". COLMAP-ov model zna koja su dva piksela ista tocka, i to je jedina istina koju
imamo.

PRVO MJERENJE JE BILO KRIVO, i vrijedi zapisati zasto. Poklapanje se proglasavalo tocnim ako oba
kraja padnu uz COLMAP-ovo opazanje ISTOG rednog broja tocke. Po toj mjeri je ispravnost ispala 60
do 69 posto, pa je izgledalo da je svaki treci brid kriv.

Ali razlicit redni broj ne znaci razlicito mjesto: COLMAP-ovi se tragovi lome, pa ista fizicka
tocka kod njega postoji kao dvije. Kad se umjesto brojeva usporede 3D POLOZAJI tih tocaka:

| potpis | s istinom | isti broj | razlicit broj, isto mjesto | stvarno krivo |
|---|---|---|---|---|
| binarni | 2315 | 1599 (69.1 %) | 656 (28.3 %) | **60 (2.6 %)** |
| SIFT | 2652 | 1749 (66.0 %) | 827 (31.2 %) | **76 (2.9 %)** |

Prag "isto mjesto" je 0.2 posto mjerila scene; medijan razmaka onih stvarno krivih je 0.3 posto,
dakle i oni su uglavnom blizu.

**Poklapanja nisu problem.** Ni nasa ni SIFT-ova - oba su oko 97 posto tocna.

I jos jedno mjerenje koje je time dobilo drugo znacenje: ispravnost po broju svjedoka u trecem
kadru raste samo sa 62 na 75 posto (mjereno starom, krivom mjerom). Uz ispravljenu mjeru to znaci
da svjedoci razlikuju uglavnom LOMLJENJE TRAGA, a ne krivo poklapanje.

**Meta je duljina traga**: nasa je 5.16 kadrova, COLMAP-ova 8.7. Poklapanja su tocna, ali se ne
spajaju u dovoljno duge lance - a upravo duljina traga daje siroku bazu.

I `dropConflicting` time dobiva drugo lice: baca 27 posto opazanja kao "sukobljena", a ako su
poklapanja 97 posto tocna, vecina tih sukoba nije greska nego posljedica GUSTIH uglova - dva
susjedna ugla na tri piksela razmaka oba se poklope, i trag ih obojicu skupi u isti kadar. To nije
krivo poklapanje nego dvostruko uzorkovanje iste tocke, i treba ga SPOJITI a ne baciti.

### Koliko je PSNR uopce ponovljiv

Isti model, isti argumenti, dva pokretanja trenera:

| | PSNR medijan | najgori kadar | SSIM |
|---|---|---|---|
| prvi put | 26.81 dB | **24.69** | 0.861 |
| drugi put | 26.68 dB | **18.02** | 0.860 |

Medijan varira 0.13 dB, dakle razlike ispod otprilike dvije desetinke ne znace nista. Ali NAJGORI
KADAR varira 6.67 dB - on nije mjera nego sum.

**Time pada jedna tvrdnja koja je ovdje stajala**: da nam je najgori kadar bolji od COLMAP-ovog
(24.69 naspram 23.85). Taj se broj ne ponavlja i ne smije se koristiti kao dokaz.

Medijan se smije, uz zalihu od dvije desetinke.

### Stapanje dvostrukih opazanja: bolje poze, losiji splat

Komponenta koja dva puta dodirne isti kadar nije nuzno greska - uglovi su razmaknuti najmanje
minDistance, pa su to dva SUSJEDNA ugla cije se zakrpe preklapaju. Umjesto bacanja cijele
komponente, bliska se opazanja stapaju u jedno.

| | tragovi | polozaj | rotacija | smjer koraka | PSNR | SSIM |
|---|---|---|---|---|---|---|
| bacanje | 5.16 | 1.6 % | 6.60 st | 3.05 st | **26.81** | **0.861** |
| stapanje, sredina | 5.57 | 1.5 % | 6.07 st | 3.00 st | 26.27 | 0.844 |
| stapanje, prvi | 5.57 | **1.4 %** | **5.86 st** | **2.15 st** | 26.40 | 0.840 |

Smjer koraka od 2.15 st je najbolji koji smo imali - drift pada za trecinu - i vraca 22 247
opazanja koja se inace bacaju. Ali splat je losiji za 0.4 dB, a splat je ono sto se isporucuje.

Sredina je losija od prvog jer sredina dvaju uglova NIJE ugao: to je mjesto izmedju njih, gdje
detektor nije nista nasao.

Zadano iskljuceno, ali je to najtjesnja odluka u cijelom ovom dokumentu.

### Sto jos nije rijeseno

Jaz od 0.2 % do 4.4 % je jos dvadeset puta. Duljina traga je 3.7 kadra po tocki naspram
COLMAP-ovih 8.7, a baza medijan 2.75 st naspram 7.93 - tocke jos ne zive dovoljno dugo da bi ih
vidjelo dovoljno kamera. Poklapanje drzi do razmaka od sedam kadrova (1680 provjerenih parova), a
na deset pada na 25 - pa prozor od deset kadrova vecinom radi uprazno.

## Solve

```
tools/solve/colmap_solve.sh snimka.mp4 izlazna_mapa [svaki_n_ti_kadar]
```

Izlaz je vec u rasporedu koji treneri splatova ocekuju: `images/` uz `sparse/0/`, plus `txt/` koji
Loomov `ColmapImport` cita.

**Ogranicenje memorije u skripti nije oprez nego iskustvo.** COLMAP s 28 dretvi na 4K slikama uzme
29.7 GB. Na stroju s 31 GB kernel pozove OOM killer, a kako proces trci unutar cgroupa aplikacije
koja ga je pokrenula, pod nozem se nadje **sve u tom opsegu** - ukljucujuci samu aplikaciju. Zato
`systemd-run --scope` s `MemoryMax`: kad se limit probije, umire samo COLMAP. Uz 4 dretve i SIFT na
1920 px potrosnja je 2.4 GB.

## Provjera

Dvije, i mjere razlicite stvari.

```
ModelInfo  mapa_s_txt              # brojke: reprojekcija, pokrivenost, putanja, baza
OverlayBox mapa_s_txt slike izlaz  # kocka na fiksnom mjestu, nacrtana preko pravih kadrova
```

`ModelInfo` mjeri ono sto se dade izmjeriti bez poznate istine. Najvazniji redak je **baza** - kut
pod kojim se zrake sijeku. Dubina iz uske baze ne postoji koliko god tocaka bilo, pa taj kut kaze
koliko se rezultatu smije vjerovati.

`OverlayBox` otvara krug koji reprojekcija ne otvara: ona mjeri slaganje rjesenja s tockama koje je
**samo naslo**, a kocka se postavi na jedno mjesto u svijetu i crta iz svake poze redom. Ako poze
valjaju, stoji zalijepljena za scenu.

## Sto snimka mora imati

Izmjereno na prvoj pravoj snimci, po kadrovima:

```
tamni parket, stepenice:   313 do 886 tocaka
zid od kamena:          11 648 do 20 499 tocaka
```

Sezdeset puta razlike, i zato je registrirano 65 od 101 slike a ne sve. **Drzati teksturirane plohe
u kadru**; ravan parket i bijeli zid kao jedini sadrzaj kadra ne daju nista nijednom solveru.

I jos jedno: gimbal daje ostrinu i mirnu rotaciju, ali **ne stvara paralaksu**. Za dubinu treba
pomak u stranu - obilazak, ne zaokret oko sebe.
