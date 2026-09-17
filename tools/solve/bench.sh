#!/usr/bin/env bash
# Apsolutna greska solvera preko vise putanja i razina suma, u jednoj tablici.
#
# ZASTO SKRIPTA. Jedna izmjena se dosad mjerila jednim uzorkom i pola dana; ovako se mjeri desetak
# puta i u nekoliko minuta. Sve sto TruthBench javi je greska protiv POZNATE istine - ne protiv
# drugog alata, koji i sam grijesi.
set -u
BENCH=${1:-./build/TruthBench}

printf "%-10s %7s %6s %8s %9s %10s %10s %9s %9s\n" \
  putanja kadrova sum kamera "baza st" "rep px" "polozaj%" "rotac st" "smjer st"

run(){
  local out
  out=$("$BENCH" "$1" "$2" "$3" 2>/dev/null)
  local cams baza rep pos rot dir
  cams=$(sed -n 's/.*rijeseno \([0-9]*\) od \([0-9]*\) kamera.*/\1\/\2/p' <<<"$out")
  baza=$(sed -n 's/.*baza \([0-9.]*\) st.*/\1/p' <<<"$out")
  rep=$(sed -n 's/.*reprojekcija \([0-9.]*\) px.*/\1/p' <<<"$out")
  pos=$(sed -n 's/.*polozaj \([0-9.]*\) %%.*/\1/p' <<<"$out")
  [ -z "$pos" ] && pos=$(sed -n 's/.*polozaj \([0-9.]*\) %.*/\1/p' <<<"$out")
  rot=$(sed -n 's/.*rotacija \([0-9.]*\) st.*/\1/p' <<<"$out")
  dir=$(sed -n 's/.*smjer \([0-9.]*\) st.*/\1/p' <<<"$out")
  printf "%-10s %7s %6s %8s %9s %10s %10s %9s %9s\n" \
    "$1" "$2" "$3" "${cams:--}" "${baza:--}" "${rep:--}" "${pos:--}" "${rot:--}" "${dir:--}"
}

for path in luk drhtaj prolaz zaokret; do run "$path" 30 0; done
for noise in 0.01 0.03 0.08; do run luk 30 "$noise"; done
for count in 15 60; do run luk "$count" 0; done
