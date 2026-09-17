#!/usr/bin/env bash
# Apsolutna greska solvera preko vise putanja i razina suma, u jednoj tablici.
#
# ZASTO SKRIPTA. Jedna izmjena se dosad mjerila jednim uzorkom i pola dana; ovako se mjeri desetak
# puta i u nekoliko minuta. Sve sto TruthBench javi je greska protiv POZNATE istine - ne protiv
# drugog alata, koji i sam grijesi.
set -u
BENCH=${1:-./build/TruthBench}

printf "%-10s %7s %6s %8s %8s %8s %8s %9s %9s %9s\n" \
  putanja kadrova sum kamera tocaka "baza" "rep px" "polozaj%" "rotac st" "korak st"

run(){
  local out
  out=$("$BENCH" "$1" "$2" "$3" 2>/dev/null)
  local cams pts baza rep pos rot step
  cams=$(sed -n 's/.*rijeseno \([0-9]*\) od \([0-9]*\) kamera.*/\1\/\2/p' <<<"$out")
  pts=$(sed -n 's/.*kamera, \([0-9]*\) tocaka.*/\1/p' <<<"$out")
  baza=$(sed -n 's/.*baza \([0-9.]*\) st.*/\1/p' <<<"$out")
  rep=$(sed -n 's/.*reprojekcija \([0-9.]*\) px.*/\1/p' <<<"$out")
  pos=$(sed -n 's/.*polozaj \(-\?[0-9.]*\) .*/\1/p' <<<"$out")
  rot=$(sed -n 's/.*rotacija \([0-9.]*\) st.*/\1/p' <<<"$out")
  step=$(sed -n 's/.*po koraku \([0-9.]*\) st.*/\1/p' <<<"$out")

  #Putanja koja je pravac ili tocka: poravnanje po polozajima ondje nije odredjeno, pa se broj NE
  #pise. Tiho ispisana besmislica je gora od crtice
  if grep -q "PRAVAC ili TOCKA" <<<"$out"; then pos="degen"; fi

  printf "%-10s %7s %6s %8s %8s %8s %8s %9s %9s %9s\n" \
    "$1" "$2" "$3" "${cams:--}" "${pts:--}" "${baza:--}" "${rep:--}" "${pos:--}" "${rot:--}" "${step:--}"
}

for path in luk drhtaj prolaz zaokret; do run "$path" 30 0; done
for noise in 0.01 0.03 0.08; do run luk 30 "$noise"; done
for count in 15 60; do run luk "$count" 0; done

#Lazna stabilizacija: izoblicenje koje se mijenja po kadru i nije jednoliko preko slike. Postoji
#jer blaga stabilizacija visestruko kvari poze, a nijedna mjera koju imamo to ne prijavi
printf "\n%-10s %7s %6s %8s %8s %8s %8s %9s %9s %9s\n" \
  "izoblic." kadrova sum kamera tocaka "baza" "rep px" "polozaj%" "rotac st" "korak st"
for w in 2 5 10; do
  out=$("$BENCH" luk 30 0 1280 "$w" 2>/dev/null)
  printf "%-10s %7s %6s %8s %8s %8s %8s %9s %9s %9s\n" \
    "${w} px" 30 0 \
    "$(sed -n 's/.*rijeseno \([0-9]*\) od \([0-9]*\) kamera.*/\1\/\2/p' <<<"$out")" \
    "$(sed -n 's/.*kamera, \([0-9]*\) tocaka.*/\1/p' <<<"$out")" \
    "$(sed -n 's/.*baza \([0-9.]*\) st.*/\1/p' <<<"$out")" \
    "$(sed -n 's/.*reprojekcija \([0-9.]*\) px.*/\1/p' <<<"$out")" \
    "$(sed -n 's/.*polozaj \(-\?[0-9.]*\) .*/\1/p' <<<"$out")" \
    "$(sed -n 's/.*rotacija \([0-9.]*\) st.*/\1/p' <<<"$out")" \
    "$(sed -n 's/.*po koraku \([0-9.]*\) st.*/\1/p' <<<"$out")"
done
