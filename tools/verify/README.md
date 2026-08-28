# Sinteticki portret

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

## Sto je ovime izmjereno

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

## Sto ovo NE dokazuje

Da Depth Anything i SAM2 rade na pravoj kozi, kosi i tkanini - kugla pred zidom je za oba
modela laksi zadatak od covjeka. Ovime se dokazuje da **lanac radi i da mjerenje hvata ono
sto tvrdi**, a to je jedino sto se bez prave fotke uopce moze dokazati.
