# Mjerenje ubacenog svjetla

`make_portrait.py` pravi scenu koja zna svoj odgovor, `measure_relight.py` nad njom (ili nad
pravom fotkom) mjeri je li svjetlo stvarno u prostoru.

## Sinteticki portret

Stalak za mjerenje, dok ne dode prava fotka.

Nad pravom fotkom ne postoji nista sto bi reklo gdje je subjekt, koliko je dalek, ni gdje mu
je silueta - pa se o rezultatu moze samo imati dojam. Ova scena je izracunata a ne snimljena,
pa **dolazi s odgovorom**:

```
tools/depth/.venv/bin/python tools/verify/make_portrait.py -o portret

portret.png              slika - ono sto model vidi
portret_truth.pfm        tocna dubina, u zapisu estimate_depth.py (0..1, 1 = najblize)
portret_truth_mask.png   tocna maska subjekta
portret_truth.txt        geometrija u metrima
```

Zraka se prati na procesoru, **namjerno bez Looma**: da je scena nacrtana istim rendererom
koji je poslije i osvjetljava, greske bi se mogle pokratiti. Ovako je plate potpuno drugi kod.
Sve je deterministicki (sjeme suma je fiksno), pa se fileovi ne drze u gitu nego se
pregenerira.

### Sto je ovime izmjereno

**Dubina** (Depth Anything V2 `small`) protiv tocne karte, `fromRange(1.24, 3.20)`:

```
korelacija dispariteta   0.9943
subjekt    istina 1.305 m   model 1.355 m   (+0.050)
pozadina   istina 3.200 m   model 2.694 m   (-0.506)
deveti decil (subjekt) 1.303 -> 1.356, prvi decil (pozadina) 3.200 -> 2.797
```

Model dakle **stisce daljinu**: pozadina mu je 13 % bliza nego sto jest. To nije sitnica jer
LoomApp iz ta dva decila izvodi `spread = (Zp - Zs) / (Zs - Zl)`, dakle i pomak svjetla i
mjesto sjene - i to je prvi broj koji o toj gresci nesto kaze.

**Maska** (SAM2 `small`, prompt iz te iste procijenjene dubine): **IoU 0.9871**, glava
uhvacena 98 %, manjak 1168 piksela, visak 7.

### Sto ovo NE dokazuje

Da Depth Anything i SAM2 rade na pravoj kozi, kosi i tkanini - kugla pred zidom je za oba
modela laksi zadatak od covjeka. Ovime se dokazuje da **lanac radi i da mjerenje hvata ono
sto tvrdi**, a to je jedino sto se bez prave fotke uopce moze dokazati.

## Mjerenje

```
tools/verify/measure_relight.py --plate portret.png --depth portret.pfm \
    --mask portret_mask.png --near 1.24 --far 3.20 --fov 40 \
    --truth portret_truth.txt
```

**Kontrola je isti kut BEZ traga, a ne drugi kut.** Usporedba dvaju kutova mijesa sjenu s
padom svjetla po strani: tako mjereno je "sjena" ispala jednako tamna kao svoja okolina i
dala laznu nulu. Zato se svaki kut snima dvaput, pa je razlika tocno ono sto je trag
napravio i nista drugo.

Izmjereno nad sintetskim portretom:

```
app tvrdi: subjekt 1.36 m, pozadina 2.80 m, sjena 0.68 m u stranu -> 200 px od subjekta

ok  kut 90: trag baca sjenu                zatamnjeno 54586 px, najdublje 73.0 razina
ok  kut 90: sjena pada na pozadinu         na subjektu promijenjeno 28 px naspram 54586
ok  kut 270: trag baca sjenu               zatamnjeno 53458 px, najdublje 50.7 razina
ok  kut 270: sjena pada na pozadinu        na subjektu promijenjeno 49 px naspram 53458
ok  sjena skace na suprotnu stranu         -169 px na kutu 90, +168 px na kutu 270
ok  obje strane jednako daleko             169 px naspram 168 px
ok  pomak je u redu velicine geometrijskog izmjereno 84 % predvidjenog
ok  subjekt se sjenca kao oblik            nagib subjekta 51.8 razina, pozadine 9.5 (5.5x)
ok  subjekt je tamo gdje uistinu jest      app 1.36 m, istina 1.30 m (+4.6 %)
ok  pozadina je tamo gdje uistinu jest     app 2.80 m, istina 3.20 m (-12.5 %)
```

Da je 84 % manje od 100 % **nije** ono sto je ovdje prvo pisalo. Tvrdio sam da sjenu prema
subjektu povlaci kutija debljine; izmjereno je da ne:

```
debljina   zatamnjeno   teziste x
  0.05 m      21573        210
  0.20 m      52841        227
  0.47 m      54508        231     <- izvedeno iz maske
  1.00 m      54572        231
  6.00 m      54572        231
```

Iznad pola metra je debljina mrtva, jer svjetlo stoji ISPRED subjekta (0.68 m naspram 1.36) pa
zrake s pozadine prelaze preko njega blizu njegove vlastite dubine - `inFront` je tamo blizu
nule i gornja granica se nikad ne dosegne. Zagrize tek ispod 0.2 m.

Ostaje ono sto se vidi u brojkama: **sjena je velika regija, a predvidjeni pomak je tocka.**
Regija je odrezana rubom kadra s jedne strane i subjektom s druge, pa njeno teziste nije ondje
gdje je sredina neodrezane sjene. Postotak je zato gruba mjera; prava provjera trazi
predvidjenu SILUETU, i to je sljedeci korak.

### Sto ovo mjerenje NE moze uhvatiti

**Samodosljedno je.** Pusteno s krivom lecom - 50 stupnjeva na portretu snimljenom s 40 -
prolazi svih osam provjera jednako dobro, jer app tada rekonstruira drugu scenu, ispise drugu
sjenu (0.87 m umjesto 0.68) i nacrta je dosljedno toj drugoj sceni. Obje polovice imaju istu
manu, pa se ne mogu uhvatiti jedna drugom.

To nije propust skripte nego svojstvo zadatka: **svjetlo koje ubacujemo je izmisljeno, ne
izmjereno**, pa nema vanjske cinjenice s kojom bi se njegova sjena mogla sukobiti. Kut lece se
iz same slike ne da izvesti - dolazi iz EXIF-a, iz kalibracije, ili se pogodi.

`--truth` je jedino vanjsko sidro koje postoji, i ono provjerava ono sto se izvana DA
provjeriti: udaljenosti koje je app ocitao iz karte dubine. Odstupanje koje ono pokazuje
(pozadina -12.5 %) nije greska Looma nego pristranost modela dubine.

### Provjera koja je usput bacena

Prva verzija tvrdnje "svjetlo je u prostoru, a ne naljepnica" usporedivala je srednju svjetlinu
subjekta i pozadine kroz kutove. Bila je **prazna**: kod zrcalnih kutova nad simetricnom
scenom se te dvije srednje vrijednosti po simetriji moraju poklopiti. Davala je 0.64 %
razlike, sto ne znaci ni da svjetlo jest ni da nije u prostoru.

Zamijenjena je onime sto se da tvrditi: subjekt ima normale koje se okrecu od svjetla, a
pozadina je ravnina na jednoj dubini - pa se nagib svjetline preko subjekta mora promijeniti
mnogo vise nego preko pozadine, koja je kontrola u istoj slici pod istim svjetlom. Izmjereno
5.5 puta.
