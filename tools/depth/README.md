# Procjena dubine

Loom crta, Spool uvozi i izvozi, a **model stoji izmedu snimke i Spoola**. Zato je ovo
skripta a ne dio biblioteke, i zato izlaz nije nista sto Loom definira nego **PFM** - format
kojim se dubina razmjenjuje i koji `Spool::loadDepthImage` cita.

## Postavljanje

Torch se posuduje sa sustava ako je vec instaliran; venv dodaje samo ono sto fali.

```
python3 -m venv --system-site-packages tools/depth/.venv
tools/depth/.venv/bin/pip install transformers
```

Bez sistemskog torcha:

```
tools/depth/.venv/bin/pip install torch transformers
```

## Upotreba

```
tools/depth/.venv/bin/python tools/depth/estimate_depth.py portret.jpg
tools/depth/.venv/bin/python tools/depth/estimate_depth.py snimka.mp4 -o dubina/
tools/depth/.venv/bin/python tools/depth/estimate_depth.py kadrovi/ -o dubina/ --model base
```

Modeli: `small` (najbrzi), `base`, `large` (najtocniji). Svi su Depth Anything V2.

## Sto izlazi

**Disparitet**: broj proporcionalan *reciprocnoj* udaljenosti - vece znaci blize. Model ne
zna ni razmjer ni pomak, pa se metri kazu poslije:

```cpp
DepthMapping::fromRange(1.2f, 6.0f);                    // "najblize 1.2 m, najdalje 6"
DepthMapping::fromReferences(v1, 2.4f, v2, 17.0f);      // dvije poznate udaljenosti
```

Za snimke se raspon racuna **preko svih kadrova**, ne po kadru. Normalizacija po kadru bi
svakom dala vlastito mjerilo, pa bi dubina treperila iako se scena ne mice.

`--raw` zapisuje sirove vrijednosti modela; tada se kalibrira `fromReferences`.
