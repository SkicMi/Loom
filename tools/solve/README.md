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
| Loom | 25.68 dB | 18.25 | 0.835 |

Cetiri decibela, i vidi se golim okom - nasa scena je mekana, njegova ostra. Greska poza od 4.4 %
i 7.1 st je za splatanje jos uvijek puno.

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
