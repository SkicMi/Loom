# Maska subjekta

Ista granica kao kod dubine: **Loom crta, Spool uvozi i izvozi, a model stoji izmedu snimke i
Spoola.** Zato je ovo skripta a ne dio biblioteke, i zato izlaz nije nista sto Loom definira
nego obican **PNG** - 255 gdje je subjekt, 0 gdje nije. Spool ga cita `loadImage`-om kao i
svaku drugu sliku.

## Postavljanje

Nista se ne instalira: `transformers` koji je vec u venvu za dubinu nosi i SAM2
(`Sam2Model`, `Sam2VideoModel`). Venv zato ostaje jedan, i zivi u `tools/depth/.venv` jer je
tamo prvi nastao - torch je preko tri gigabajta i ne kopira se dvaput.

```
tools/depth/.venv/bin/python tools/segment/segment.py --check
```

Prvi poziv skine tezine (`facebook/sam2.1-hiera-small`, ~180 MB). Modeli: `tiny`, `small`,
`base`, `large`.

## Upotreba

```
tools/segment/segment.py portret.jpg                   # prompt u sredini kadra
tools/segment/segment.py portret.jpg --depth p.pfm     # prompt iz karte dubine
tools/segment/segment.py portret.jpg --point 640,400
tools/segment/segment.py portret.jpg --box 300,120,900,800
tools/segment/segment.py kadrovi/ -o maske/            # maska se propagira kroz kadrove
tools/segment/segment.py snimka.mp4 -o maske/
```

Za jednu sliku izlaz je `<ime>_mask.png`, za snimku `mask_0000.png`, `mask_0001.png`, ... -
isto brojanje kao `depth_0000.pfm` i Spoolov `SequenceWriter`.

**Kroz snimku se maska propagira, ne racuna iznova po kadru.** Klik ide samo u prvi kadar;
dalje SAM2 nosi sto je vidio. Maska racunata po kadru dala bi svakom vlastito misljenje o
tome gdje subjekt prestaje, pa bi rub titrao - i mjerenje nad njom mjerilo bi to titranje
umjesto svjetla. Ista zamka kao normalizacija dubine po kadru.

## Prompt iz dubine

`--depth` izvede iz karte **okvir i tri tocke u njemu**: granica subjekta i pozadine trazi se
na pola puta u disparitetu izmedu devetog i prvog decila (disparitet je linearan po
reciprocnoj udaljenosti, pa je sredina izmedu njih sredina u onome sto model zapravo
procjenjuje). Okvir kaze dokle subjekt see, tocke da je sve to jedan predmet.

Dubina time PREDLAZE, a SAM2 ODLUCUJE - i u tome je vrijednost te kombinacije: dvije procjene
koje grijese na razlicitim mjestima (dubina na mekim rubovima, maska na kosi i rukama), pa
njihovo slaganje nije samorazumljivo.

**Prva verzija je davala jednu tocku - teziste najblizeg decila - i to je bilo krivo.** Nad
sintetskim portretom (`tools/verify/`) je tako ispao samo torzo, bez glave. Mjereno protiv
poznate maske:

| prompt                          | IoU    | glave uhvaceno |
|---------------------------------|--------|----------------|
| okvir + `multimask_output=True` | 0.7936 | 5 %            |
| okvir, jedna maska              | 0.9831 | 96 %           |
| 3 tocke po pojasevima           | 0.9866 | 99 %           |
| **okvir + 3 tocke**             | **0.9871** | 98 %       |

Krivac nije bio okvir nego **`multimask_output=True` uz biranje po ocjeni modela**: kad je
prompt jednoznacan, model svojoj vlastitoj ocjeni i dalje rado da DIO. Zato se tri odgovora
sad traze samo od jedne gole tocke, gdje je klik stvarno dvosmislen.

## Cemu maska sluzi

Ne ljepsoj slici, nego **mjerenju**.

Nad sintetskom scenom se sjena da izracunati rukom: zid na sest metara, plocica na tri, i
cetiri ugla su zbrajanje (`tests/test_screen_shadow.cpp`). Nad pravom fotkom nema nicega sto
bi reklo gdje je subjekt a gdje pozadina, pa se o rezultatu moze samo imati dojam. Maska te
dvije regije imenuje, i tek tada se daju izraziti brojem:

1. **Sjena je gdje geometrija kaze.** Maska pomaknuta za `spread * pomak svjetla` je
   predvidena sjena; mjeri se dodano svjetlo unutar nje naspram pozadine drugdje. Zrcali se
   kut i sjena MORA skociti na drugu stranu.
2. **Svjetlo je u prostoru, a ne naljepnica.** Omjer svjetline subjekta i pozadine kroz
   kutove orbite mora se mijenjati; naljepnica bi ga drzala konstantnim.
3. **Koliko je dubina losa na silueti.** Koliki dio ruba maske ima skok dubine unutar par
   piksela, i koliko je prijelazni pojas sirok - broj koji predvida halo i bira `--radius`.
4. **Treperenje kroz snimku.** Srednja dubina unutar maske kroz kadrove, dok maska kaze da se
   subjekt nije pomakao.

**Granica, receno naglas: maska nije istina nego druga procjena.** Cim se njome nesto
ISPRAVI (npr. dubina snapana na njen rub), njome se to vise ne smije provjeravati - to bi
bilo mjerenje istim stapom kojim se i pomelo.

## Samoprovjera

`--check` nacrta scenu ciju masku znamo napamet i usporedi se s njom. Pravokutnik je namjerno
**nesimetrican i pomaknut**: krug u sredini bi prosao i kad bi se x i y zamijenili, i kad bi
se maska zapisala naopako.

Izmjereno na `small`, na ovoj kartici:

```
poznata kutija: (96, 64, 288, 240)
maskina kutija: (96, 64, 288, 240)
IoU: 0.9998   piksela u maski: 33786, u istini: 33792
```

Ne ide u `ctest`: trazi tezine i mrezu, pa test ne bi bio deterministican ni na lavapipeu
bez mreze. Isto pravilo kao dubina - **maska koju neki buduci test koristi mora biti ukoricen
PNG, a ne poziv modela.**
