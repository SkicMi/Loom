#!/usr/bin/env bash
# Solve snimke COLMAP-om, u obliku koji Loom moze procitati.
#
# ZASTO POSTOJI. Dok se nas solver razvija, ovo je radni put do splatova - i mjerilo s kojim se nas
# solver usporedjuje. Na istom isjecku snimke COLMAP daje 65 od 101 kamere uz 0.91 px, a nas lanac
# 7 od 93 uz 1.553 px; bez tog broja se mjerilo samo sebe.
#
# OGRANICENJE MEMORIJE NIJE OPREZ NEGO ISKUSTVO. COLMAP s 28 dretvi na 4K slikama uzme 29.7 GB. Na
# stroju s 31 GB kernel pozove OOM killer, a kako proces trci unutar cgroupa aplikacije koja ga je
# pokrenula, pod nozem se nadje SVE u tom opsegu - ukljucujuci samu aplikaciju. Zato --scope s
# MemoryMax: kad se limit probije, umire samo COLMAP.
#
# Upotreba:  colmap_solve.sh snimka.mp4 izlazna_mapa [svaki_n_ti_kadar]
set -euo pipefail

VIDEO="${1:?Treba snimka}"
OUT="${2:?Treba izlazna mapa}"
EVERY="${3:-5}"

THREADS="${COLMAP_THREADS:-4}"        # 4 dretve x 1920 px ~ 2.4 GB; 28 x 4K je bilo 29.7 GB
MAXSIZE="${COLMAP_MAXSIZE:-1920}"     # SIFT na smanjenoj slici; kljucne tocke se vracaju u pune koordinate
MEMMAX="${COLMAP_MEMMAX:-8G}"

mkdir -p "$OUT/images" "$OUT/sparse"

echo "== Kadrovi: svaki $EVERY. iz $VIDEO"
ffmpeg -v error -i "$VIDEO" -vf "select='not(mod(n\,$EVERY))'" -vsync 0 -q:v 2 "$OUT/images/%05d.jpg" -y
echo "   $(ls "$OUT/images" | wc -l) slika"

run(){
    if command -v systemd-run >/dev/null 2>&1; then
        systemd-run --user --scope -p MemoryMax="$MEMMAX" -p MemorySwapMax=2G --quiet -- "$@"
    else
        echo "   UPOZORENJE: nema systemd-run, memorija nije ogranicena" >&2
        "$@"
    fi
}

echo "== Znacajke"
run colmap feature_extractor \
    --database_path "$OUT/db.db" --image_path "$OUT/images" \
    --ImageReader.single_camera 1 --ImageReader.camera_model SIMPLE_RADIAL \
    --SiftExtraction.use_gpu 0 --SiftExtraction.num_threads "$THREADS" \
    --SiftExtraction.max_image_size "$MAXSIZE" > "$OUT/log_feat.txt" 2>&1

# Snimka je slijedna, pa se poklapa sa susjedstvom umjesto svaki sa svakim. Preklapanje 10 je
# COLMAP-ova preporuka za video i daje 400 do 1000 poklapanja po paru
echo "== Poklapanje"
run colmap sequential_matcher \
    --database_path "$OUT/db.db" \
    --SiftMatching.use_gpu 0 --SiftMatching.num_threads "$THREADS" \
    --SequentialMatching.overlap 10 > "$OUT/log_match.txt" 2>&1

echo "== Rekonstrukcija"
run colmap mapper \
    --database_path "$OUT/db.db" --image_path "$OUT/images" \
    --output_path "$OUT/sparse" --Mapper.num_threads "$THREADS" > "$OUT/log_map.txt" 2>&1

# U tekst, jer nas ColmapImport cita tekstualni oblik
for model in "$OUT/sparse"/*/; do
    [ -d "$model" ] || continue
    mkdir -p "$OUT/txt"
    colmap model_converter --input_path "$model" --output_path "$OUT/txt" --output_type TXT >/dev/null 2>&1 || true
    colmap model_analyzer --path "$model" 2>&1 | grep -E 'Registered|Points|reprojection' || true
    break
done

echo "== Gotovo: $OUT/txt"
