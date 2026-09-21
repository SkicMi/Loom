#!/usr/bin/env bash
# Solve snimke MapAnything-om (Meta+CMU, Apache 2.0), u obliku koji Loom moze procitati.
#
# ZASTO POSTOJI. Na snimci joysticka (bijeli stol, siromasna tekstura) nas lanac je dao 75/75
# kamera ali omjer izdvojenih opazanja 4.73 - upozorenje da rjesenju ne treba vjerovati. COLMAP na
# ISTOJ snimci nije registrirao gotovo nista: 2 od 197 slika. MapAnything je na istih 75 kadrova
# dao SVE poze u 80 sekundi (nas lanac: 19 minuta; COLMAP: neuspjeh).
#
# STO OVO JEST I STO NIJE. MapAnything je feed-forward mreza: regresira poze, zariste i oblak
# tocaka u jednom prolazu, bez iterativnog bundlea. Izmjereno: baza (paralaksa) 40.11 st medijan -
# sira od COLMAP-ove ili nase na bilo kojoj snimci - ali reprojekcija 50.9 px NA RADNOJ SLICI OD
# 518 PIKSELA (dakle oko 9.8% sirine slike), jer se rjesenje NE dotjeruje nakon regresije.
#
# To je oblik pogreske, ne velicina: mreza dobro pogadja TOPOLOGIJU scene (tko sto vidi, pod kojim
# kutom), a slabo pogadja SUBPIKSELNI polozaj. Za nas svrhu - inicijalna procjena umjesto pogadjanja
# od nule, ondje gdje nasa vlastita geometrija nema signala - to je upravo ono sto treba: dobar
# pocetak koji ionako ide u nas bundle, ne gotovo rjesenje.
#
# LICENCA. --apache bira "facebook/map-anything-apache" checkpoint, jedini komercijalno slobodan.
# Bez te zastavice model je istrazivacki (CC-BY-NC) - NIKAD ga ne pozivati bez nje u ovom projektu.
#
# MEMORIJA KARTICE RASTE S BROJEM KADROVA, ne samo s onim sto radi uz nju. Na kartici od 12 GB:
#
#   kadrova    ishod
#      32      radi, 42 s
#      96      CUDA OOM i sam za sebe (9.75 GB), bez ikog drugog na kartici
#     192      CUDA OOM (dodatno opterecenje samo pogorsa)
#
# Skripta NE dijeli automatski na serije. Za karticu od 12 GB drzati EVERY tako da izlazi tridesetak
# kadrova; za vise treba ili veca kartica ili prava batch podjela u pozivu modela
#
# ZAHTIJEVA odvojenu instalaciju izvan ovog repozitorija (PyTorch model, ne C++):
#   git clone https://github.com/facebookresearch/map-anything.git
#   cd map-anything && python3 -m venv .venv_ma && .venv_ma/bin/pip install -e ".[colmap]"
# Putanje se zadaju MAPANYTHING_HOME i MAPANYTHING_VENV, ili se pretpostavlja ~/Desktop/map-anything
#
# Upotreba:  mapanything_solve.sh snimka.mp4 izlazna_mapa [svaki_n_ti_kadar]
set -euo pipefail

VIDEO="${1:?Treba snimka}"
OUT="${2:?Treba izlazna mapa}"
EVERY="${3:-5}"

MA_HOME="${MAPANYTHING_HOME:-$HOME/Desktop/map-anything}"
MA_VENV="${MAPANYTHING_VENV:-$MA_HOME/.venv_ma}"

if [ ! -x "$MA_VENV/bin/python" ]; then
    echo "GRESKA: nema $MA_VENV/bin/python - vidi upute za instalaciju u zaglavlju skripte" >&2
    exit 1
fi

# RAW_IMAGES JE ODVOJEN OD $OUT/images NAMJERNO. demo_colmap.py PISE svoje obradjene slike u
# <output_dir>/images - da je to ista mapa iz koje cita ulaz, pisanje bi se sudaralo s citanjem
mkdir -p "$OUT/raw_images"

echo "== Kadrovi: svaki $EVERY. iz $VIDEO"
ffmpeg -v error -i "$VIDEO" -vf "select='not(mod(n\,$EVERY))'" -vsync 0 -q:v 2 "$OUT/raw_images/%05d.jpg" -y
echo "   $(ls "$OUT/raw_images" | wc -l) slika"

echo "== MapAnything (Apache 2.0 checkpoint)"
( cd "$MA_HOME" && "$MA_VENV/bin/python" scripts/demo_colmap.py \
    --images_dir="$OUT/raw_images" --output_dir="$OUT" --apache ) > "$OUT/log_mapanything.txt" 2>&1

# U tekst, jer nas ColmapImport cita tekstualni oblik - isti razlog kao u colmap_solve.sh
mkdir -p "$OUT/txt"
colmap model_converter --input_path "$OUT/sparse" --output_path "$OUT/txt" --output_type TXT >/dev/null 2>&1

# POZOR NA RAZLUCIVOST: MapAnything radi na svom RADNOM MJERILU (izmjereno: 518x294 na 4K ulazu),
# ne na izvornom. cameras.txt to ispravno opisuje, ali izvorne 4K slike NE ODGOVARAJU tom modelu -
# za trening splata ili za ModelInfo koristiti "$OUT/images" (mreza ih je vec obradila), ne izvorni
# ulaz u snimka.mp4
echo "== Gotovo: $OUT/txt  (slike za trening: $OUT/images, ne $OUT/images pune snimke)"
