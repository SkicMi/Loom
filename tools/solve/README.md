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

Jaz je s 3.91 pao na 2.78 dB. (Redak "najgori" ostaje ovdje samo zato sto je izmjeren; ne znaci
nista - vidi "Koliko je PSNR uopce ponovljiv" nize, gdje se pokazalo da najgori kadar varira 6.67
dB izmedju dva pokretanja ISTOG modela.)

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

### Rastavljanje sukobljene komponente: prva izmjena koja popravlja oboje

Komponenta koja isti kadar dodirne dvaput sadrzi bar jedan krivi brid, i dosad se cijela bacala.
To je bila najskuplja odluka u grafu, i skuplja nego sto je ovdje pisalo: brojac je javljao 13 957
opazanja, ali su se opazanja spojenog kadra izbacivala jos ranije, pa je stvarni gubitak bio preko
polovice grafa - 275 tisuca naspram 626 tisuca.

I gubile su se bas NAJDUZE komponente: trag koji prezivi kroz vise kadrova ima i vise prilika da
pokupi jedan krivi brid.

Umjesto bacanja, komponenta se sada RASTAVLJA - njezini se bridovi slazu ponovno, najpouzdaniji
prvi, i spoj koji bi opet doveo dva opazanja u isti kadar se ne izvede.

**Prvi pokusaj je pao, i to je bilo poucno.** Bez ikakvog dodatnog praga tragovi su skocili na 8.22
kadra - prakticki COLMAP-ovih 8.7 - a poze su se raspale: 35 % polozaja, 119 st rotacije, 132 st
smjera koraka. Isti potpis koji ima i SIFT-ov put (124 st). Dakle **odsustvo sukoba nije tocnost**:
sukob je bio SIMPTOM, a rastavljanje ga uklanja ne dirajuci uzrok.

Provjerena je i ocita sumnja - je li rjesenje samo zrcalno. Nije: uz dopusteno zrcaljenje u
Umeyami greska ne padne nego rotacija ode na 180 st.

**Popravak je stroziji prag unutar sumnjive komponente.** Ona je vec dokazano pokvarena, pa se u
njoj ne vjeruje bridu koji ima samo jednog svjedoka u trecem kadru. Takav se brid BACA, ne odgadja.

| svjedoka u komponenti | tragovi | polozaj | rotacija | smjer koraka |
|---|---|---|---|---|
| 0 (svi bridovi) | 8.22 | 35.0 % | 119.08 st | 132.56 st |
| **2** | **7.77** | **1.3 %** | **4.69 st** | **1.87 st** |
| 3 | 6.84 | 15.3 % | 142.98 st | 110.10 st |
| 4 | 6.84 | 25.7 % | 78.80 st | 96.51 st |

Uz dvojku se popravlja SVE sto se mjeri, i po prvi put i poze i splat:

| | tragovi | baza | polozaj | rotacija | zaokret/kadar | smjer koraka |
|---|---|---|---|---|---|---|
| bacanje | 5.16 | 3.36 st | 1.6 % | 6.60 st | 0.073 st | 3.05 st |
| rastavljanje, svj. >= 2 | **7.77** | **4.72 st** | **1.3 %** | **4.69 st** | **0.035 st** | **1.87 st** |

I u decibelima, na istom skupu izdvojenih kadrova i s COLMAP-om treniranim istom naredbom (7
izdvojenih kadrova - zato se ovi brojevi ne smiju usporedjivati s tablicom gore, koja je imala 10):

| poze | PSNR medijan | SSIM |
|---|---|---|
| COLMAP | **32.00 dB** | **0.907** |
| Loom, rastavljanje | 30.29 dB | 0.873 |
| Loom, bacanje | 29.29 dB | 0.860 |

**Jaz je s 2.71 pao na 1.71 dB**, a to je osam puta iznad izmjerene ponovljivosti od 0.13 dB. Prva
izmjena otkad se mjeri koja popravlja i poze i splat - dosad su tri popravile poze a pokvarile
splat.

Na 30 kadrova s pocetka snimke, koje COLMAP nije registrirao uopce: tocaka 938 -> 5074, baza
1.55 -> 5.44 st.

**Prozor je uzak i to treba znati.** Nula, tri i cetiri svjedoka sve ruse rjesenje.

Prvo sam to opisao kao skretanje u krivu granu. **Nije - to je nakupljeni drift**, i brojke to
kazu jasno:

| | zaokret po koraku | najgori korak | ukupna greska rotacije | 64 koraka x medijan |
|---|---|---|---|---|
| svjedoka 2 | 0.035 st | 0.354 | 4.69 st | 2.2 st |
| svjedoka 0 | 1.109 st | 2.141 | 119.08 st | 71 st |
| prozor 20, bez rastavljanja | 1.217 st | 7.404 | 139.98 st | 78 st |

Da je rijec o jednom krivom skoku, medijan koraka bi ostao dobar a najgori bi bio golem. Umjesto
toga je SVAKI korak losiji tridesetak puta, a najgori je tek dva do sest puta iznad medijana. Kut
od stotinjak stupnjeva je zbroj sezdeset cetiri sitne greske, ne jedna velika.

To mijenja i lijek. Ne treba provjera koja bi odbila granu, nego **veza koja seze dalje od deset
kadrova**: nista u nasem grafu ne povezuje kadar 5 s kadrom 60, pa drift preko tog razmaka nema sto
zaustaviti. COLMAP takve veze ima.

### Cetiri pokusaja da se drift smanji, i sto je svaki rekao

Sve mjereno na istoj snimci, uz rastavljanje i dva svjedoka, dakle protiv 1.3 % / 4.69 st / 1.87 st.

**Prozor poklapanja 20 umjesto 10.** Ideja je bila da veza preko dvadeset kadrova zaustavi drift
koji preko deset nema sto zaustaviti.

| | kamere | tocke | polozaj | rotacija | smjer koraka |
|---|---|---|---|---|---|
| prozor 20, bez rastavljanja | 101/101 | 19 279 | 33.4 % | 139.98 st | 137.13 st |
| prozor 20, uz rastavljanje | 93/101 | 24 865 | 18.2 % | 152.94 st | 133.27 st |

Oba se rusu. Binarni potpis ne prezivi dvadeset kadrova, pa siri prozor ne donosi vezu nego smece
- a rastavljanje to smece sada CUVA umjesto da ga baci kao sukob. Prozor ostaje 10.

**Radna sirina 1920 umjesto 960**, dakle kvantizacija 2 px umjesto 4:

| | kamere | baza | polozaj | rotacija | zaokret medijan | najgori korak |
|---|---|---|---|---|---|---|
| 960 | 101/101 | 4.72 st | 1.3 % | 4.69 st | 0.035 st | 0.354 st |
| 1920 | 100/101 | 5.17 st | 2.8 % | 10.06 st | **0.028 st** | **7.322 st** |

Medijan koraka je ondje bolji, i baza sira - ali JEDAN korak promasi 7.3 st i odnese cijelo
poravnanje. Nije kvantizacija ono sto drzi 960 na mjestu nego to sto na 1920 detektor opet pocinje
hvatati sum. Ostaje 960.

**Dotjerivanje prema referentnom kadru.** `refineCorner` dotjeruje ugao sam za sebe i time razdvaja
trag (izmjereno ranije: monotona steta). Ispravna inacica je dotjerati zakrpu PREMA referentnom
opazanju istog traga, Lucas-Kanadeom, na punoj slici - tada svi clanovi opisuju istu tocku.

Napravljeno je (`Track::refineToward`) i na sintetici radi: kroz tri kadra rasap pada s 3.759 na
0.156 px. Na pravoj snimci ne radi.

Mjereno protiv COLMAP-ovih subpikselnih opazanja kao istine, na 12 pravih 4K kadrova. Mjera je
RASAP UNUTAR TRAGA - koliko se opazanja jednog traga medjusobno razilaze nakon sto se svakom oduzme
istinit polozaj. Koliki je zajednicki pomak, nevazno je; triangulaciji smeta samo razilazenje.

| poluprozor LK | rasap prije | rasap poslije | dalo se | cijeli trag |
|---|---|---|---|---|
| 4 | 1.931 | 1.338 | 80 % | 45 % |
| 5 | 1.914 | 1.379 | 81 % | 50 % |
| 6 | 1.917 | 1.339 | 81 % | 49 % |
| 8 | 1.961 | 1.338 | 83 % | 52 % |

Trideset posto manje razilazenja, ali samo na tragovima koji su se CIJELI dali - a to je polovica.
U punom lancu je to dalo 119 st: mjesavina subpikselnih i kvantiziranih clanova je gora od
ujednaceno grubih, a pravilo "sve ili nista po tragu" tada baci sedamdeset posto tragova.

**Referenca prethodno dotjerana na pravi ugao**, da zakrpa koju ostali prate sjedi na necemu
prepoznatljivom. Gore: udio koji se dade pada s 81 na 70 posto, a odmak reference od COLMAP-ove
tocke RASTE s 1.53 na 1.84 px.

To je ujedno i objasnjenje: **COLMAP-ove znacajke nisu nasi uglovi**. Njegove su SIFT-ovi ekstremi
u prostoru mjerila, nase su Shi-Tomasijevi uglovi - pa "dotjerati na pravi ugao" vodi na drugo
mjesto, ne na tocnije. Isti razlog zbog kojeg je i `refineAtFullResolution` monotono stetio.

Oba polja ostaju u kodu, zadano iskljucena. **Subpikselna tocnost se ne dobiva dotjerivanjem nego
detekcijom u prostoru mjerila** - dakle SIFT-ovim putem, koji je jos zatvoren.

### Kriv je bio pocetni par - i to objasnjava sve neuspjehe odjednom

Sve sto je dosad rusilo rjesenje davalo je isti potpis: zaokret po koraku oko 1.1 st i ukupno sto
i nesto stupnjeva. SIFT, dva svjedoka po bridu, rastavljanje bez praga, prozor 20 - sve isto.

**Prvo mjerenje: je li kriv graf.** Iz spremljenih grafova je za svaki par susjednih kadrova
rijesena dvoprizorna poza i njezina rotacija usporedjena s COLMAP-ovom. Ta usporedba ne ovisi ni o
kakvom poravnanju, mjerilu ni ishodistu.

| graf | parova | tocaka po paru | rotacija medijan |
|---|---|---|---|
| binarni, bacanje | 64 | 1653 | 0.847 st |
| binarni, rastavljanje | 64 | 3724 | 0.792 st |
| SIFT | 64 | 1501 | 0.792 st |
| rastavljanje bez praga | 64 | 4042 | 0.778 st |

**Sva cetiri jednaka.** Dakle graf nije kriv - ni SIFT-ov. Kvar je u rekonstrukciji.

**Drugo mjerenje: koliko se kamera stvarno krece.** Zaokret po kadru je 1.654 st (medijan preko 64
susjedna para COLMAP-ovog rjesenja). Nase neuspjele postavke grijese po koraku 1.1 st, dakle dvije
trecine samog gibanja - to nije nakupljeni drift nego korak koji je sam po sebi kriv. **Time pada i
jucerasnja formulacija o driftu**; ispravna je da je svaki korak los, a ne da se mali zbrajaju.

**Nalaz.** Pocetni par se bira medju kandidatima poredanima po broju zajednickih tocaka, a gleda ih
se samo prvih trideset. U gustom grafu su to redom SUSJEDNI kadrovi, dakle najuza baza - pa sto je
graf bogatiji, to je izbor gori. Otud i to da je svako poboljsanje grafa rusilo poze.

Izmjereno na istom grafu:

| pocetni par | kut | tocaka | ishod |
|---|---|---|---|
| 86-89 | 1.65 st | 176 | **4.69 st** |
| 80-89 | 3.43 st | 9742 | 119.94 st |

Oba prolaze sve provjere koje izbor para ima. **Razlika se ne vidi dok se scena ne izgradi do
kraja** - i reprojekcija je ne prijavi, jer je kod oba oko 1.65 px.

**Popravak: ne bira se nego se pokusava.** Prvih nekoliko kandidata izgradi se do kraja i zadrzi
najbolji, po broju kamera pa po medijanu kuta pod kojim se zrake sijeku. Reprojekcija se NE koristi.

Uz cetiri pokusaja, na istim spremljenim grafovima:

| graf | prije | poslije | smjer koraka |
|---|---|---|---|
| binarni, bacanje | 6.60 st | 6.60 st | 3.05 -> 3.05 |
| binarni, rastavljanje svj. 2 | 4.69 st | 4.69 st | 1.87 -> 1.87 |
| **SIFT** | **163.80 st** | **10.71 st** | 124.25 -> 5.64 |
| **rastavljanje bez praga** | **119.08 st** | **4.65 st** | 132.56 -> 2.18 |

Gdje je prvi izbor bio dobar, nista se ne mijenja. Gdje nije, razlika je dvadeset do trideset puta.

**SIFT-ov put vise nije zatvoren**, a 124 st nikad nije ni bio njegov - bio je nas.

I jos jedno: rastavljanje BEZ ikakvog praga svjedoka sada daje 4.65 st i bazu 5.09 st, dakle bolje
od praga 2 (4.69 st, baza 4.72). Uski prozor praga opisan gore bio je posljedica ovog istog kvara,
a ne svojstvo praga.

Cijena: jedan pokusaj na 101 kadru je 325 s, cetiri su cetiri puta toliko. Zadano je cetiri.

**Cetiri nije uvijek dosta.** Jedan od ta cetiri grafa - rastavljanje uz tri svjedoka - ostaje kriv
i nakon cetiri pokusaja (126.48 st), a s osam padne na 5.75 st uz bazu 4.67 i smjer koraka 1.97.
Kad rjesenje izgleda lose a baza mu je bitno uza nego sto graf dopusta, prvo sto vrijedi probati je
vise pokusaja.

**I jedna ideja uz to koja je pala.** Svi izabrani parovi bili su izmedju kadra 80 i 99 - cetiri
pokusaja su probala jednu petinu snimke - pa je izgledalo da ih treba razmaknuti. Uz razmak od
dvanaest kadrova:

| | bez razmicanja | s razmicanjem |
|---|---|---|
| rastavljanje bez praga | **4.65 st** | 119.08 st |
| rastavljanje, svjedoka 3 | 126.48 st | 142.98 st |
| SIFT | 10.71 st | 9.71 st |

Dobri parovi zive BAS u tom susjedstvu: najbolji je 90-92, a prvi izbor 82-85 - sredista su im
sedam kadrova razmaknuta, pa ih razmicanje od dvanaest razdvoji. Bogato podrucje nije zamka nego
mjesto gdje se scena stvarno dade rijesiti. Ostaje kao polje `initialPairSpread`, zadano nula.

### Peti put: bolje poze, losiji splat

S ispravljenim pocetnim parom rastavljanje BEZ ikakvog praga svjedoka je po svakoj mjeri poze bolje
od praga 2 - pa je trebalo provjeriti daje li i bolji splat. Isti trener, ista naredba, isti
izdvojeni kadrovi.

| postavka | tragovi | baza | polozaj | rotacija | PSNR | SSIM |
|---|---|---|---|---|---|---|
| COLMAP | 8.7 | 7.93 st | - | - | **32.00 dB** | **0.907** |
| rastavljanje, svjedoka 2 | 7.77 | 4.72 st | 1.3 % | 4.69 st | **30.29 dB** | **0.873** |
| bacanje | 5.16 | 3.36 st | 1.6 % | 6.60 st | 29.29 dB | 0.860 |
| rastavljanje bez praga | **8.22** | **5.09 st** | **1.2 %** | **4.65 st** | 28.42 dB | 0.840 |

Zadnji redak je po SVAKOJ mjeri poze najbolji koji smo ikad imali, a splat mu je losiji za 1.87 dB.

**A onda se pokazalo da mjera laze.** Greska rotacije se dosad racunala tako da se nase poze
slicnoscu poklope s COLMAP-ovima pa se usporede orijentacije. Ta se slicnost racuna IZ POLOZAJA
KAMERA - a kad putanja lezi gotovo u ravnini, zaokret oko osi te ravnine njome nije odredjen:
polozaji se njime jedva pomaknu, a orijentacije se sve zaokrenu.

Ova putanja je bas takva: omjeri svojstvenih brojeva rasapa polozaja su 1 : 0.349 : 0.100. Dakle
izduzena i gotovo ravna.

Mjereno bez ikakvog poravnanja - svaka kamera u odnosu na PRVU zajednicku, u oba modela:

| postavka | rotacija poravnata | rotacija bez poravnanja | PSNR |
|---|---|---|---|
| bacanje | 6.60 st | **1.497 st** | 29.29 dB |
| rastavljanje, svjedoka 2 | 4.69 st | 1.601 st | **30.29 dB** |
| rastavljanje bez praga | **4.65 st** | 2.879 st | 28.42 dB |

Poravnata mjera kaze da je zadnji redak najbolji; neporavnata kaze da je dvostruko gori od ostalih
- i ona se slaze s decibelima.

**Peti "paradoks" dakle nije bio paradoks nego kriva mjera.** Poze bez praga svjedoka NISU bolje,
nego losije.

Ostaje pravilo, ali u tocnijem obliku:

> Poravnata greska rotacije se ne smije citati na ravnoj ili izduzenoj putanji. Mjere koje vrijede
> su one bez poravnanja: zaokret iz kadra u kadar i rotacija u odnosu na referentnu kameru. I dalje
> vrijedi da svaka izmjena mora zavrsiti decibelom.

Ranija cetiri slucaja (pod jacine ugla, stapanje po sredini, stapanje po prvom, radna sirina 1920)
mjerena su ISTOM poravnatom mjerom i treba ih ponoviti prije nego se na njih pozove.

Zasto - jos ne znamo, ali jedno objasnjenje je vec palo. Napisao sam da je vjerojatno u
POKRIVENOSTI: da duzi tragovi znace manje i zgusnutijih pocetnih tocaka, a treneru one nisu samo
geometrija. Izmjereno na izvezenim modelima koji su i trenirani:

| model | tocaka | trag | zauzetih celija (32^3) | po kadru | najprazniji kadar | PSNR |
|---|---|---|---|---|---|---|
| COLMAP | 26 761 | 6.00 | **1336** | **1317** | **107** | **32.00 dB** |
| rastavljanje, svjedoka 2 | 59 879 | 6.00 | 2170 | 5213 | 656 | 30.29 dB |
| rastavljanje bez praga | 60 198 | 7.00 | 2201 | 5726 | 635 | 28.42 dB |
| bacanje | 28 869 | 4.00 | 2063 | 2289 | 676 | 29.29 dB |

**COLMAP ima najgoru pokrivenost od svih** - upola manje zauzetih celija, cetvrtinu tocaka po
kadru, najprazniji kadar sa 107 tocaka naspram nasih 635 - i najbolji splat. Pokrivenost pocetnog
oblaka dakle nije to.

**Presadjivanje oblaka.** Trener iz `points3D.txt` cita samo polozaje i boje - dvodimenzionalna
opazanja preskace - pa se dade sastaviti model s tudjim pozama i nasim tockama, i obrnuto. Tocke se
pritom prenose slicnoscu koja poklopi kamere.

| poze | tocke | PSNR | SSIM |
|---|---|---|---|
| COLMAP | COLMAP | **32.00 dB** | 0.907 |
| nase | COLMAP | 30.38 dB | 0.890 |
| nase | nase | 30.29 dB | 0.873 |
| COLMAP | nase | 28.99 dB | 0.863 |

Zanimljivo je sto NIJEDNA zamjena ne prenosi kakvocu. COLMAP-ove poze s nasim tockama daju 28.99 -
losije od naseg svega. Razlog je najvjerojatnije sam prijenos: oblak se prenosi slicnoscu koja ima
ostatak, pa u tudjem okviru sjedi malo pokraj. Ono sto se iz toga dade procitati jest da trener u
7000 koraka NE popravi krivo postavljen oblak - a ne koliko vrijede nase poze same za sebe.

### SIFT s ispravljenim pocetnim parom: najbolji graf, jedna pokvarena kamera

Uz rastavljanje, prag dva svjedoka i cetiri pocetna para:

| | tocaka | duljina traga | baza | reprojekcija |
|---|---|---|---|---|
| binarni | 63 048 | 7.77 | 4.72 st | 1.635 px |
| **SIFT** | **88 464** | 7.49 | **5.23 st** | 1.663 px |
| COLMAP | 26 761 | 8.7 | 7.93 st | 0.746 px |

Vise tocaka nego COLMAP i najsira baza koju smo imali. Ali:

```
zaokret iz kadra u kadar: medijan 0.101 st, NAJGORI KORAK 26.072 st
rotacija bez poravnanja: 56.900 st
PSNR: 28.10 dB
```

Jedna kamera nosi sve. I to je ujedno prvi put da se vidi cemu sluzi mjera bez poravnanja: medijan
koraka je odlican, poravnata rotacija kaze 26.57 st, a mjera u odnosu na prvu kameru kaze 56.9 -
jer jedan krivi korak zaokrene sve iza sebe.

**Reprojekcija takvu kameru ne prijavi.** Trazio sam je po tome sto bi joj vlastita reprojekcija
trebala biti visestruko veca od opce - nijedna nema ni trostruko. Zaglavljena kamera je
samodosljedno kriva, jer je registrirana nad tockama koje su i same krive.

**Detektor koji okida koristi ono sto reprojekcija ne zna: kadrovi idu redom.** I tu je prvo
pravilo bilo krivo. Trazio sam kameru kojoj su OBA susjedna koraka velika - a zaokret krive kamere
se s jedne strane zbraja s gibanjem a s druge oduzima:

```
kamera zaokrenuta 25 st, korak 11.46 st  ->  susjedni koraci 36.32 i 13.90
```

Jedan golem, drugi posve obican. Ono sto jest svojstvo krive kamere: **put KROZ nju je dulji nego
put PREKO nje**. Za ispravnu su ta dva gotovo jednaka jer se zaokreti zbrajaju oko iste osi; za
zaokrenutu je razlika dvostruki zaokret.

Takva kamera dobiva pozu iznova, polazeci od susjedne rijesene - ne od vlastite, jer bi se vratila
u isti minimum.

**Na sintetici radi, na snimci ne okida - i to je opet poucno.** Ondje nije jedna kamera iskocila
pa se vratila, nego je CIJELI REP niza zaokrenut za 26 st u odnosu na pocetak, a medjusobno se
slaze. Put kroz takvu kameru nije dulji od puta preko nje - jer i put preko nje prelazi isti lom.
To nije iskocena kamera nego LOM LANCA.

Prva sumnja je bila da lom nastaje ondje gdje malo toga prelazi preko reza. Izmjereno, za svaki
moguci rez u nizu, koliko tocaka ima opazanja s obje strane:

| graf | najuzi most | kod kadra | medijan |
|---|---|---|---|
| binarni, rastavljanje | 910 | 36 | 2503 |
| SIFT, rastavljanje | **1189** | 35 | **4991** |
| binarni, bacanje | 463 | 59 | 1989 |

SIFT-ov je most SIRI od binarnog, dakle ni to nije. Najuzi je kod kadra 35-36 u oba - tocno ondje
gdje snimka prelazi s tamnog parketa na teksturirani zid, i tocno odakle COLMAP uopce pocinje
registrirati.

### Sto jos nije rijeseno

**Nista u grafu ne seze dalje od deset kadrova.** Prozor poklapanja je deset, pa najduza veza u
rekonstrukciji spaja kadrove udaljene deset. Preko tog razmaka drift nema sto zaustaviti, i kad
korak oslabi rjesenje se zaokrene za stotinjak stupnjeva - zbroj sezdeset cetiri sitne greske, ne
jedna velika (vidi tablicu gore). Reprojekcija to ne prijavi jer se poze slozu oko tocaka koje su
i same odnesene driftom.

**Jaz u decibelima je 1.71 dB** (30.29 naspram 32.00). Duljina traga je 7.77 naspram COLMAP-ovih
8.7, baza 4.72 st naspram 7.93 - blize nego ikad, ali jos nije tu.

**Sto se u alatu jos ne rjesava**: distorzija se ne racuna u solveru nego se slike ispravljaju
unaprijed; trener i dalje ignorira k1; `VideoSolve` nije provjeren od .MP4 do kraja; nema izvoza u
Blender ni Nuke.

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
