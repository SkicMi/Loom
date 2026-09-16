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

### Sto NIJE rijeseno, i to je sljedeci posao

**Poze se ne slazu s COLMAP-ovima.** Nakon poravnanja slicnoscu (Umeyama, mjerac provjeren na
poznatom odgovoru - daje nulu), na 65 zajednickih kamera:

| | |
|---|---|
| polozaj, medijan | 15.7 % dosega putanje |
| rotacija, medijan | 26.8 st |

Rekonstrukcija koja se sama sa sobom slaze na 1.25 px moze biti posve kriva, i ovdje jest. Niska
reprojekcija nije dokaz geometrije.

**Zasto: baza je preuska.** Kut pod kojim se zrake sijeku:

| | COLMAP | Loom |
|---|---|---|
| medijan | 4.51 st | 2.55 st |
| p10 | 2.42 st | 0.77 st |
| ispod 1 st | 0 od 26 761 | 8 827 od 51 778 (17 %) |

Sedamnaest posto nasih tocaka nema bazu, a dubina iz uske baze ne postoji koliko god tocaka bilo.
COLMAP filtrira ispod 1.5 st (`filter_min_tri_angle`); nas `minParallaxDegrees` stoji na 0.5, a
tijekom gradnje je jos tri puta blazi. Prvo sto treba isprobati je podici ga.

Uz to je duljina traga 3.7 kadra po tocki naspram COLMAP-ovih 8.7 - tocke jos ne zive dovoljno
dugo da bi ih vidjelo dovoljno kamera za siroku bazu.

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
