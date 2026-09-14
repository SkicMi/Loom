# Od snimke do poza

Loomov vlastiti solver je ono sto se razvija; COLMAP je ovdje **mjerilo** i radni put dok solver
jos ne stize. Oba zavrsavaju u istom formatu, pa se citaju istim alatom i usporedjuju istim
brojkama.

## Zasto oba

Na istom isjecku snimke (Sony 4K 50p, soba, gimbal):

| | COLMAP | Loom |
|---|---|---|
| rijesene kamere | 65 / 101 | 7 / 93 |
| tocaka | 26 761 | 706 |
| opazanja po kameri | 3 564 | ~300 |
| reprojekcija | 0.75 px | 1.55 px |

Razlika nije u kalibraciji - provjereno je tako da se Loomu preda COLMAP-ova tocna kalibracija,
ukljucujuci distorziju, i rezultat se nije promijenio. Razlika je u korespondencijama: COLMAP racuna
deskriptore i poklapa svaku sliku s **iducih deset**, dok Loom lanca samo susjedni kadar. Zato mu
trag koji izadje iz kadra nikad ne bude ponovno spojen, i siroka baza ne postoji nigdje u podacima.

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
