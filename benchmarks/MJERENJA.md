# Mjerenja

Iz `benchmarks/mjerenja.jsonl` (376 zapisa). Slozi se iznova s `tools/bench/mjerenja.py tablica`. Vrijeme je u sekundama; PSNR u dB na IZDVOJENIM kadrovima (koje trening nije vidio); ostrina je energija detalja nacrtanog prema snimljenom (1 = jednako ostro). Usporedba je razlika B - A po istim kadrovima s procjenom pogreske; dva ista treninga razlikuju se do oko 0.12 dB, pa razlika manja od dvije pogreske nije nalaz. Vrijeme treninga i solvea koji su isli istovremeno na kartici nije cisto.

## solve

| datum | snimka | opis | kadrova | vrijeme_s | kamera | tocaka | reprojekcija_px | izdvojeni_px | omjer_izdvojenih | zarisna_px | k1 | pracenje_s | graf_s | rekonstrukcija_s | pune_slicice_s | odluka | ostalo |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-09-24 15:54 | C0257 | polazno stanje (231 kadar) | 231 | 2533 | 229/229 | 265981 | 1.202 | 2.987 | 2.460 | 4650 |  | 229.60 | 683.20 | 303.20 | 425.20 | polaziste | zapisano_px=1.474, izlaz=solve_prije |
| 2026-09-24 16:34 | C0257 | subpikselni uglovi, samokalibracija | 231 | 2384 | 229/229 | 247060 | 1.166 | 2.787 | 2.410 | 4754 |  | 178.60 | 656.90 | 240.90 | 395.60 | odbaceno: k1 0.061, splat -3.6 dB | zapisano_px=1.390, izlaz=solve_subpix |
| 2026-09-24 17:59 | C0257 | subpikselni uglovi + zadana kalibracija; RS citanje 0.60 | 231 | 2618 | 229/229 | 246634 | 1.146 | 2.689 | 2.410 | 4650 |  | 180.10 | 620.00 | 749.50 | 392.90 | odbaceno: splat -0.53+-0.23 dB | zapisano_px=1.362, izlaz=solve_subpix_kal |
| 2026-09-24 18:59 | C0257 | paralelna lokalizacija + dekodiranje s nitima | 231 | 2360 | 229/229 | 265981 | 1.202 | 2.987 | 2.460 | 4650 |  | 198.90 | 693.90 | 359.90 | 331.90 | zadrzano: isti izlaz bit za bit, -3 min | zapisano_px=1.474, izlaz=solve_brzi |
| 2026-09-24 19:18 | C0257 | brzi uzorak 60 kadrova, samokalibracija | 60 | 464 | 59/59 | 72817 | 1.277 | 4.263 | 3.210 | 7542 |  | 28.40 | 158.50 | 66.70 | 82.00 | samokalibracija pala (f 7542) - mjeriti sa zadanom | zapisano_px=1.605, izlaz=q_base |
| 2026-09-24 19:24 | C0257 | 60 kadrova, --track-scale 2, samokalibracija | 60 | 355 | 59/59 | 17334 | 1.379 | 3.899 | 2.830 | 8512 |  | 11.30 | 145.00 | 16.10 | 36.20 | samokalibracija pala - ponovljeno sa zadanom | zapisano_px=2.301, izlaz=q_D |
| 2026-09-24 19:35 | C0257 | 60 kadrova, zadana kalibracija (uz trening na kartici) | 60 | 568 | 59/59 | 79913 | 1.225 | 4.549 | 3.460 | 4650 |  | 48.20 | 171.30 | 166.60 | 86.90 | polaziste brzog uzorka | zapisano_px=1.483, izlaz=k_base |
| 2026-09-24 19:58 | C0257 | 60 kadrova, zadana kalibracija, --track-scale 2 | 60 | 531 | 59/59 | 25588 | 1.361 | 4.220 | 3.350 | 4650 |  | 11.80 | 163.90 | 228.80 | 39.90 | -37 s; pune slicice 2x brze, ali rekonstrukcija sporija i 3x manje tocaka - provjeriti | zapisano_px=2.249, izlaz=k_D |
| 2026-09-24 19:58 | C0257 | 60 kadrova, zadana kalibracija, --track-scale 4 | 60 | 458 | 56/56 | 34164 | 1.329 | 2.935 | 2.390 | 4650 |  | 9.800 | 148.10 | 166.50 | 50.00 | -110 s, izdvojeni 2.94 px (osnova 4.55) - obecavajuce, potvrditi na punoj snimci | zapisano_px=2.150, izlaz=k_E |
| 2026-09-24 20:01 | C0257 | 60 kadrova, zadana kalibracija, --keyframes-only | 60 | 463 | 59/59 | 79913 | 1.225 | 4.549 | 3.460 | 4650 |  | 29.40 | 158.50 | 188.80 |  | -105 s, isto rjesenje; preskace pune slicice - za splat dovoljno, za VFX kameru po kadru ne | zapisano_px=1.483, izlaz=k_F |
| 2026-09-24 20:10 | C0257 | 60 kadrova, zadana kalibracija: jedan kandidat vidnog polja umjesto 7 | 60 | 547 | 59/59 | 79913 | 1.225 | 4.549 | 3.460 | 4650 |  | 44.90 | 219.10 | 73.70 | 89.90 | zadrzano: isti izlaz bit za bit, rekonstrukcija 167->74 s (ukupno zasumljeno: isao uz solve G i trening) | zapisano_px=1.483, izlaz=k_H |
| 2026-09-24 20:11 | C0257 | 60 kadrova, zadana kalibracija, --gpu-match | 60 | 696 | 59/59 | 79913 | 1.225 | 4.549 | 3.460 | 4650 |  | 27.90 | 173.10 | 251.60 | 87.40 | zadrzano: isti izlaz bit za bit, poklapanje prostora mjerila 63->9.5 s; ukupno zasumljeno (isao uz k_H i trening) | zapisano_px=1.483, izlaz=k_G |
| 2026-09-24 20:25 | C0257 | 60 kadrova, zadana kalibracija: znacajke po kadrovima usporedo (12 dretvi) | 60 | 403 | 59/59 | 79913 | 1.225 | 4.549 | 3.460 | 4650 |  | 29.20 | 139.20 | 61.00 | 86.80 | zadrzano: isti izlaz bit za bit, znacajke 80->45 s, graf 219->139 s | zapisano_px=1.483, izlaz=k_I |
| 2026-09-24 20:42 | C0257 | 60 kadrova, zadana kalibracija: potpisi prostora mjerila i poklapanje na kartici | 60 | 246 | 59/59 | 25386 | 1.344 | 4.610 | 3.500 | 4650 |  | 27.10 | 41.70 | 54.20 | 35.70 | graf 139->42 s, ukupno 6m43->4m06; puna obrada skrenula (baza 7.6->1.6 st, 80k->25k tocaka) iako je graf gotovo isti - provjera na drugim kadrovima | zapisano_px=2.220, izlaz=k_J |
| 2026-09-24 20:52 | C0257 | kontrola: svaki 7. kadar, 60, zadana kalibracija, sve na procesoru | 60 | 308 | 57/57 | 22716 | 1.400 | 4.577 | 3.400 | 4650 |  | 22.70 | 116.60 | 60.90 | 28.50 | i procesor zavrsi u uskoj bazi (1.63 st) - nestabilnost pune obrade, ne kartice | zapisano_px=2.666, izlaz=k7_cpu |
| 2026-09-24 20:52 | C0257 | kontrola: svaki 7. kadar, 60, zadana kalibracija, potpisi i poklapanje na kartici | 60 | 235 | 57/57 | 22840 | 1.386 | 4.560 | 3.320 | 4650 |  | 23.40 | 41.70 | 61.30 | 28.60 | zadrzano: isto kao procesor (izdvojeni 4.56 prema 4.58 px), 5m08 -> 3m55 | zapisano_px=2.630, izlaz=k7_gpu |
| 2026-09-24 21:40 | C0257 | graf s kartice iz cachea, 12 pocetnih parova (stari izbor) |  |  | 59/59 | 79798 | 1.224 | 4.610 | 3.500 | 4650 |  |  |  | 132.70 |  | 11 od 12 pokusaja u krivom rjesenju (baza 1.6 st, 25k tocaka); pocetni parovi 0.5-0.7 st | zapisano_px=1.485, izlaz=p_gpu_12, napomena=izdvojeni_px je s brzog kandidata, ne s isporucenog rjesenja |
| 2026-09-24 21:40 | C0257 | graf s kartice: kandidati po razmaku kadrova + izbor po tockama |  |  | 59/59 | 79897 | 1.227 | 4.745 | 3.680 | 4650 |  |  |  | 54.30 |  | dobro rjesenje (80k tocaka, 1.227 px) | zapisano_px=1.483, izlaz=s_gpu, napomena=izdvojeni_px je s brzog kandidata, ne s isporucenog rjesenja |
| 2026-09-24 21:40 | C0257 | graf s procesora: kandidati po razmaku kadrova + izbor po tockama |  |  | 59/59 | 79807 | 1.192 | 2.863 | 2.400 | 4650 |  |  |  | 21.90 |  | brzi kandidat zadrzan (79.8k, 1.192 px, izdvojeni omjer 2.40); losija puna obrada odbacena | zapisano_px=1.500, izlaz=s_cpu, napomena=izdvojeni_px je s brzog kandidata, ne s isporucenog rjesenja |
| 2026-09-24 21:40 | C0257 | kontrola svaki 7. kadar: kandidati po razmaku + izbor po tockama | 60 | 277 | 57/57 | 75839 | 1.258 | 4.707 | 3.460 | 4650 |  | 22.20 | 43.90 | 59.40 | 72.00 | zadrzano: 22.7k -> 75.8k tocaka, baza 1.63 -> 8.39 st, 1.40 -> 1.258 px | zapisano_px=1.599, izlaz=s7, napomena=izdvojeni_px je s brzog kandidata, ne s isporucenog rjesenja |
| 2026-09-24 22:03 | C0257 | cijela snimka (231), sva ubrzanja i novi izbor pocetnog para | 231 | 1320 | 229/229 | 266449 | 1.209 | 4.452 | 3.550 | 4259 | -0.025 | 100.30 | 161.30 | 206.10 | 272.90 | 42m13 -> 21m59, isto rjesenje po kamerama/tockama/reprojekciji; samokalibracija f 4259 (prije 4650) | zapisano_px=1.482, izlaz=solve_novi, izdvojeni_od=brzog kandidata (90/229 kamera) - ne vrijedi |
| 2026-09-24 22:06 | C0257 | 60 kadrova iz cachea grafa: paralelni zapis slika + --measure-held-out |  |  | 59/59 | 79897 | 1.227 | 2.864 | 2.400 | 4650 |  |  |  | 104.20 |  | zadrzano: slike iste do bajta, zapis 59 slika 13.1 s; izdvojeni na isporucenom 2.864 px (omjer 2.40) umjesto brzog 4.745 | zapisano_px=1.483, izlaz=w_gpu |
| 2026-09-24 22:35 | C0257 | pune slicice: odsjecci serijski iznutra, 12 u letu (60 kadrova iz cachea) |  |  |  |  |  |  |  |  |  |  |  |  | 166.50 | odbaceno: isti izlaz, ali 166 s prema 94 s (mjereno uz pokuse) |  |
| 2026-09-24 22:45 | C0257 | cijela snimka, zadana f 4650 (stara samokalibracija) | 231 | 1175 | 229/229 | 265968 | 1.205 | 2.974 | 2.530 | 4650 |  | 125.70 | 230.70 | 262.90 | 290.80 | solve jednak kao f 4259 (2.974 prema 2.981 px) - samokalibracija ovdje luta | zapisano_px=1.474, izlaz=e_f4650 |
| 2026-09-24 22:54 | C0257 | pokusaji pocetnog para: svaki svoj dio jezgri (60 kadrova iz cachea) |  |  |  |  |  |  |  |  |  |  |  | 56.80 |  | zadrzano: puna obrada 75.8->56.8 i 57.9->47.6 s, izlaz isti do bita (mjereno uz pokuse) |  |
| 2026-09-24 23:05 | C0257 | cijela snimka, zadana f 4259 k1 -0.025 (nova samokalibracija) | 231 | 1210 | 229/229 | 266441 | 1.209 | 2.981 | 2.530 | 4259 |  | 167.20 | 220.90 | 1525 | 279.80 |  | zapisano_px=1.482, izlaz=e_f4259, faze_s=pracenje i graf 437.3, brzi kandidat 125.2, puna obrada 286.8, provjere 0.0, slike kadrova 78.0, pune slicice i USD 280.0, COLMAP i provjera 1.9 |
| 2026-09-24 23:23 | C0257 | cijela snimka, f 4259, subpikselni uglovi | 231 | 1050 | 229/229 | 247002 | 1.152 | 2.705 | 2.410 | 4259 |  | 138.50 | 176.10 | 1301 | 266.90 | solve bolji (izdvojeni 2.705 prema 2.981 px), splat nije - ostaje iskljuceno | zapisano_px=1.371, izlaz=e_subpix, faze_s=pracenje i graf 365.0, brzi kandidat 84.4, puna obrada 251.9, provjere 0.0, slike kadrova 77.5, pune slicice i USD 267.2, COLMAP i provjera 3.4 |
| 2026-09-24 23:43 | C0257 | cijela snimka, f 4259, rolling shutter | 231 | 1216 | 229/229 | 266441 | 1.209 | 2.981 | 2.530 | 4259 |  | 148.10 | 183.20 | 1516 | 275.20 |  | zapisano_px=1.482, izlaz=e_rs, faze_s=pracenje i graf 365.6, brzi kandidat 102.4, puna obrada 298.1, provjere 0.0, slike kadrova 76.6, pune slicice i USD 275.4, COLMAP i provjera 97.6 |
| 2026-09-25 00:10 | C0257 | cijela snimka, f 3925 iz metapodataka objektiva (18 mm, ekv. 36.8 mm) | 231 | 996 | 229/229 | 266124 | 1.256 | 3.254 | 2.660 | 3925 |  | 107.80 | 191.20 | 1236 | 273.40 | solve slican (izdvojeni 3.254 px), splat -1.1 dB - --focal-from-metadata ostaje iskljucen | zapisano_px=1.571, izlaz=e_f3925, faze_s=pracenje i graf 332.7, brzi kandidat 99.5, puna obrada 240.0, provjere 0.0, slike kadrova 48.1, pune slicice i USD 273.6, COLMAP i provjera 1.6 |
| 2026-09-25 00:54 | C0257 | CISTO mjerenje: cijeli lanac, zadane postavke, sam na stroju | 231 | 1062 | 229/229 | 266449 | 1.209 | 4.452 | 3.550 | 4259 |  | 84.00 | 163.30 | 1275 | 327.30 |  | izdvojeni_od=brzog kandidata, zapisano_px=1.482, izlaz=cist, faze_s=pracenje i graf 264.9, brzi kandidat 223.4, puna obrada 180.9, provjere 0.0, slike kadrova 63.2, pune slicice i USD 327.5, COLMAP i provjera 1.8 |
| 2026-09-25 01:36 | C0257 | CISTO: solve sa zadanim postavkama, sam na stroju | 231 | 1062 | 229/229 | 266449 | 1.209 | 4.452 | 3.550 | 4259 |  | 84.00 | 163.30 | 1275 | 327.30 | 17m42; trening zadano 29m05 (3.4M gaussiana); ukupno 46.8 min | izdvojeni_od=brzog kandidata, zapisano_px=1.482, izlaz=cist, faze_s=pracenje i graf 264.9, brzi kandidat 223.4, puna obrada 180.9, provjere 0.0, slike kadrova 63.2, pune slicice i USD 327.5, COLMAP i provjera 1.8 |
| 2026-09-25 02:06 | C0257 | --coarse-weight 1.0 (60 kadrova iz cachea, f 4259) | 60 |  |  |  |  | 2.888 | 2.420 |  |  |  |  |  |  | polaziste | prostor_mjerila_medijan_px=0.877 |
| 2026-09-25 02:06 | C0257 | --coarse-weight 0.5 (60 kadrova iz cachea, f 4259) | 60 |  |  |  |  | 2.845 | 2.110 |  |  |  |  |  |  | izdvojeni -1.5 %, omjer bolji; provjera splatom na cijeloj snimci (E10) | prostor_mjerila_medijan_px=0.868 |
| 2026-09-25 02:06 | C0257 | --coarse-weight 0.3 (60 kadrova iz cachea, f 4259) | 60 |  |  |  |  | 2.843 | 2.140 |  |  |  |  |  |  | izdvojeni -1.5 %, omjer bolji; provjera splatom na cijeloj snimci (E10) | prostor_mjerila_medijan_px=0.865 |
| 2026-09-25 02:24 | C0257 | E10 cijela snimka, f 4259, --coarse-weight 0.5 | 231 | 1089 | 229/229 | 267086 | 1.367 | 2.948 | 2.200 | 4259 |  | 82.80 | 165.80 | 1384 | 352.80 | omjer 2.53 -> 2.20; precizne 0.876 -> 0.867 px | zapisano_px=1.639, izlaz=e_w05, faze_s=pracenje i graf 265.5, brzi kandidat 110.0, puna obrada 277.9, provjere 0.0, slike kadrova 81.5, pune slicice i USD 352.9, COLMAP i provjera 1.0, prostor_mjerila_medijan_px=0.867 |
| 2026-09-25 02:33 | C0257 | analiza: izoblicenje po kadru na ostatku preciznih znacajki |  |  |  |  |  |  |  |  |  |  |  |  |  | stabilizacija/RS objasne ~0.1 px; vecina je sum polozaja znacajki - 0.5 px nije dostizno podesavanjem | prostor_mjerila_medijan_px=0.844, nakon_afinog_px=0.806, nakon_kvadratnog_px=0.751 |
| 2026-09-25 15:53 | C0257 | CISTO: cijeli lanac (solve + trening --then), zadano | 231 | 1846 | 229/229 | 267253 | 1.359 | 4.415 | 3.170 | 4402 | -0.000 | 82.00 | 158.60 | 1240 | 343.40 | cijeli lanac 30m46 (solve 17m25, trening uz njega jos 13m20); samokalibracija f 4402, k1 -0.0004 | izdvojeni_od=brzog kandidata, zapisano_px=1.625, izlaz=lanac, faze_s=pracenje i graf 257.3, brzi kandidat 224.9, puna obrada 157.8, provjere 0.0, slike kadrova 60.8, pune slicice i USD 343.6, COLMAP i provjera 1.1 |
| 2026-09-25 16:47 | C0257 | samokalibracija A: tezina 1.0, sva opazanja |  |  |  |  |  |  |  | 4259 | -0.025 |  |  |  |  | potvrdjeno splatom |  |
| 2026-09-25 16:47 | C0257 | samokalibracija B: tezina 0.5, sva opazanja |  |  |  |  |  |  |  | 4402 | -0.000 |  |  |  |  | SSIM 4K -0.03 u cistom mjerenju |  |
| 2026-09-25 16:47 | C0257 | samokalibracija C: tezina 0.5, samo precizna |  |  |  |  |  |  |  | 4599 | -0.000 |  |  |  |  | nestabilno, brzi kandidat pokvaren |  |
| 2026-09-25 16:47 | C0257 | samokalibracija D: tezina 1.0, samo precizna |  |  |  |  |  |  |  | 4354 | -0.000 |  |  |  |  | nestabilno, brzi kandidat pokvaren |  |
| 2026-09-25 16:47 | C0257 | samokalibracija E: samokalibracija 1.0, obrada 0.5 (zadano) |  |  |  |  |  |  |  | 4259 | -0.025 |  |  |  |  | zadano: kalibracija A, rekonstrukcija kao E10 (izdvojeni 2.947, omjer 2.20) |  |
| 2026-09-25 17:18 | C0257 | CISTO 2: cijeli lanac, popravljena samokalibracija | 231 | 1871 | 229/229 | 267089 | 1.367 | 4.452 | 3.550 | 4259 | -0.025 | 79.90 | 154.20 | 1266 | 343.90 | cijeli lanac 31m11 (solve 17m33 + trening jos 13m37); f 4259, k1 -0.025 | izdvojeni_od=brzog kandidata, zapisano_px=1.639, izlaz=lanac2, faze_s=pracenje i graf 250.9, brzi kandidat 214.3, puna obrada 181.8, provjere 0.0, slike kadrova 61.3, pune slicice i USD 344.0, COLMAP i provjera 1.0 |
| 2026-09-25 18:10 | C0255 | C0255: cijeli lanac sada (--then) | 197 |  | 119/189 | 23856 | 1.962 | 5.897 | 4.580 | 2774 |  | 62.10 | 58.10 | 277.00 | 46.50 |  | izdvojeni_od=brzog kandidata, zapisano_px=2.792, izlaz=c55_sada, faze_s=pracenje i graf 134.9, brzi kandidat 24.0, puna obrada 16.5, provjere 0.0, slike kadrova 34.6, pune slicice i USD 46.5, COLMAP i provjera 0.3 |
| 2026-09-25 18:10 |  | C0255: cijeli lanac jutro 24.9. (fad6408) | 197 |  | 120/189 | 21798 | 1.321 | 5.404 | 4.230 | 3851 |  | 75.00 | 236.00 | 96.50 | 57.20 |  | zapisano_px=2.183, izlaz=c55_jutro |
| 2026-09-25 18:27 |  | C0255: cijeli lanac, ograda distorzije + ponovljeno sjeme | 197 |  | 120/189 | 21026 | 1.988 | 5.459 | 2.910 |  |  | 64.10 | 61.50 | 182.40 |  |  | izdvojeni_od=brzog kandidata |
| 2026-09-25 18:46 | C0255 | C0255: cijeli lanac, ograde + kamere pa tocke | 197 |  | 119/189 | 26644 | 1.889 | 5.162 | 2.730 | 3851 |  | 64.90 | 61.80 | 563.80 | 49.50 |  | zapisano_px=2.526, izlaz=c55_fix, faze_s=pracenje i graf 142.2, brzi kandidat 172.9, puna obrada 20.8, provjere 0.0, slike kadrova 25.7, pune slicice i USD 49.5, COLMAP i provjera 0.3 |

## trening

| datum | snimka | opis | koraka | razlucivost | gaussiana | vrijeme_s | psnr_medijan | psnr_najgori | ssim_medijan | odluka | ostalo |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-09-24 03:49 | C0257 | polazni trening (A) | 7000 | 1920x1080 | 3782367 |  | 24.86 | 17.69 | 0.774 |  |  |
| 2026-09-24 03:58 | C0257 | --opacity-reg + --scale-reg | 7000 | 1920x1080 | 3723168 |  | 17.99 | 15.25 | 0.714 | odbaceno: -6.9 dB |  |
| 2026-09-24 15:13 | C0257 | A' - classic na RS solveu | 7000 | 1920x1080 | 3663419 |  | 24.46 | 12.60 | 0.764 |  |  |
| 2026-09-24 15:22 | C0257 | 3DGUT bez rolling shuttera | 7000 | 1920x1080 | 3670796 |  | 23.43 | 12.21 | 0.762 | odbaceno |  |
| 2026-09-24 15:32 | C0257 | rolling shutter (3DGUT, rs_top/rs_bottom) | 7000 | 1920x1080 | 3659959 |  | 25.52 | 17.62 | 0.778 | sum; vidi usporedbe |  |
| 2026-09-24 16:49 | C0257 | polazni solve, classic | 7000 | 1920x1080 | 3754760 |  | 24.98 | 17.76 | 0.781 | polaziste usporedbi | kamera=classic |
| 2026-09-24 16:58 | C0257 | polazni solve, rolling shutter | 7000 | 1920x1080 | 3754760 |  | 25.23 | 17.29 | 0.782 | odbaceno: -0.08+-0.14 dB | kamera=rolling |
| 2026-09-24 17:06 | C0257 | subpix solve (samokalibracija) | 7000 | 1920x1080 | 3756662 |  | 21.37 | 17.44 | 0.768 | odbaceno | kamera=classic |
| 2026-09-24 17:14 | C0257 | novi solve s RS (samokalibracija) | 7000 | 1920x1080 | 3754760 |  | 21.90 | 17.92 | 0.747 | odbaceno | kamera=rolling |
| 2026-09-24 18:07 | C0257 | subpix + zadana kalibracija | 7000 | 1920x1080 | 3756616 |  | 24.53 | 17.04 | 0.778 | odbaceno: -0.53 dB | kamera=classic |
| 2026-09-24 18:16 | C0257 | subpix + kalibracija + RS | 7000 | 1920x1080 | 3754508 |  | 22.56 | 13.01 | 0.773 | odbaceno: -0.55 dB | kamera=rolling |
| 2026-09-24 18:32 | C0257 | zamucenje pokretom (--motion-blur) + RS | 7000 | 1920x1080 | 3076125 | 926 | 25.05 | 17.07 | 0.780 | odbaceno: -0.33 dB | kamera=rolling |
| 2026-09-24 18:42 | C0257 | ponovljen t_1 - mjerenje suma | 7000 | 1920x1080 | 3713019 |  | 24.79 | 18.63 | 0.774 | sum: -0.006+-0.127 | kamera=classic |
| 2026-09-24 18:53 | C0257 | ponovljen t_1 - mjerenje suma | 7000 | 1920x1080 | 3749720 |  | 25.36 | 17.45 | 0.778 | sum: -0.117+-0.130 | kamera=classic |
| 2026-09-24 19:03 | C0257 | --max-gaussians 1000000 | 7000 | 1920x1080 | 1000000 | 543 | 25.19 | 17.73 | 0.770 | zadrzivo: isto kao 3.75M, 4x manji | kamera=classic |
| 2026-09-24 19:10 | C0257 | --max-gaussians 1500000 | 7000 | 1920x1080 | 1500000 | 432 | 24.74 | 17.29 | 0.775 | -0.17+-0.12 dB (sum) | kamera=classic |
| 2026-09-24 19:41 | C0257 | 15000 koraka, granica 1.5M | 15000 | 1920x1080 | 1500000 | 843 |  |  |  | ostrije (+19 % na 1080p, +22 % na 4K), PSNR isti | kamera=classic |
| 2026-09-24 19:57 | C0257 | trening na punoj 4K (--downscale 1), granica 1.5M | 7000 | 3840x2160 | 1500000 | 1065 | 24.48 | 17.29 | 0.778 |  | kamera=classic, izlaz=r4k.ply, izdvojenih=39 |
| 2026-09-24 20:27 | C0257 | 30000 koraka, granica 1.5M | 30000 | 1920x1080 | 1500000 | 1626 | 24.29 | 17.31 | 0.757 | ostrije (+32 % 1080p, +49 % 4K), ali PSNR -0.25 i SSIM -0.004 - pocinje se prilagodjavati snimljenim kadrovima | kamera=classic, izlaz=k30000.ply, izdvojenih=39 |
| 2026-09-24 22:18 | C0257 | cijela snimka, novi solve, 15000 koraka | 15000 | 1920x1080 | 1500000 | 957 | 25.34 | 20.91 | 0.790 | najbolje dosad: izdvojeni PSNR +1.0 dB (1080p) / +1.2 dB (4K), SSIM bolji na 39/39, najgori kadar 17.8 -> 20.9 dB | kamera=classic, izlaz=t_novi.ply, izdvojenih=39 |
| 2026-09-24 22:55 | C0257 | E1 f 4650, 7000 koraka | 7000 | 1920x1080 | 1500000 | 568 | 24.87 | 19.78 | 0.794 |  | kamera=classic, izlaz=e_f4650.ply, izdvojenih=39 |
| 2026-09-24 23:15 | C0257 | E2 f 4259, 7000 koraka | 7000 | 1920x1080 | 1500000 | 547 | 25.20 | 21.07 | 0.809 |  | kamera=classic, izlaz=e_f4259.ply, izdvojenih=39 |
| 2026-09-24 23:32 | C0257 | E3 f 4259 + subpiksel, 7000 koraka | 7000 | 1920x1080 | 1500000 | 540 | 24.83 | 21.16 | 0.798 |  | kamera=classic, izlaz=e_subpix.ply, izdvojenih=39 |
| 2026-09-24 23:53 | C0257 | E4 f 4259 + rolling shutter, 7000 koraka | 7000 | 1920x1080 | 1500000 | 570 | 25.72 | 21.08 | 0.802 |  | kamera=rolling, izlaz=e_rs.ply, izdvojenih=39 |
| 2026-09-25 00:18 | C0257 | E5 f 3925 metapodaci, 7000 koraka | 7000 | 1920x1080 | 1500000 | 508 | 23.54 | 18.95 | 0.779 |  | kamera=classic, izlaz=e_f3925.ply, izdvojenih=39 |
| 2026-09-25 00:29 | C0257 | E6 f 4259, 3DGUT bez rolling shuttera, 7000 koraka | 7000 | 1920x1080 | 1500000 | 578 | 24.86 | 20.94 | 0.803 |  | kamera=ut, izlaz=e_ut.ply, izdvojenih=39 |
| 2026-09-25 01:23 | C0257 | CISTO mjerenje: zadani trening (15000 koraka, ciscenje) | 15000 | 1920x1080 | 1270402 | 1741 | 25.21 | 21.45 | 0.796 |  | kamera=classic, izlaz=cist.ply, izdvojenih=39 |
| 2026-09-25 01:33 | C0257 | E7 f 4259, poze iz RS bundlea (sredina kadra), obicno crtanje, 7000 koraka | 7000 | 1920x1080 | 1500000 | 586 | 26.03 | 21.92 | 0.795 |  | kamera=rsmid, izlaz=e_rsmid.ply, izdvojenih=39 |
| 2026-09-25 01:56 | C0257 | E8 f 4259, 7000 koraka 1080p + 3000 na 4K | 10000 | 3840x2160 | 1500000 | 1000 | 25.16 | 21.88 | 0.827 |  | kamera=classic, izlaz=e_fine.ply, izdvojenih=39 |
| 2026-09-25 02:05 | C0257 | E9 f 4259, ekspozicija po kadru, 7000 koraka | 7000 | 1920x1080 | 1500000 | 468 | 25.60 | 20.68 | 0.809 |  | kamera=classic, izlaz=e_exp.ply, izdvojenih=39 |
| 2026-09-25 02:33 | C0257 | E10 f 4259, coarse 0.5, 7000 koraka | 7000 | 1920x1080 | 1500000 | 477 | 25.29 | 21.33 | 0.805 |  | kamera=classic, izlaz=e_w05.ply, izdvojenih=39 |
| 2026-09-25 02:43 | C0257 |  | 6001 | 1920x1080 | 3755219 | 450 | 25.18 | 20.57 | 0.798 |  | kamera=classic, izlaz=prof.ply, izdvojenih=39 |
| 2026-09-25 02:50 | C0257 |  | 6001 | 1920x1080 | 3655125 | 380 | 25.10 | 21.03 | 0.801 |  | kamera=classic, izlaz=prof.ply, izdvojenih=39 |
| 2026-09-25 03:08 | C0257 | E11 visible-adam + brzi SSIM, kao cisto mjerenje | 15000 | 1920x1080 | 1575873 | 1047 | 25.50 | 21.41 | 0.798 |  | kamera=classic, izlaz=vadam.ply, izdvojenih=39 |
| 2026-09-25 04:22 | C0257 | profil koraka pri 3.75M gaussiana (1080p) |  |  |  |  |  |  |  | Adam 25.9 -> 3.8 ms (SelectiveAdam), SSIM 8.3 -> 6.4 ms; ostaje unatrag 37, crtanje 16, MCMC 9 | ms_prije=99.30, ms_poslije=73.00 |
| 2026-09-25 04:38 | C0257 |  | 8001 | 1920x1080 | 2649927 | 471 | 25.19 | 20.68 | 0.807 |  | kamera=classic, izlaz=prof.ply, izdvojenih=39 |
| 2026-09-25 04:54 | C0257 | E12 brisanje nevidljivih svakih 1000 + granica 2.5M | 15000 | 1920x1080 | 2094359 | 952 | 25.38 | 19.36 | 0.802 |  | kamera=classic, izlaz=prune.ply, izdvojenih=39 |
| 2026-09-25 05:08 | C0257 | E13 12000 koraka (brisanje + 2.5M zadano) | 12000 | 1920x1080 | 2020453 | 745 | 25.23 | 21.34 | 0.810 |  | kamera=classic, izlaz=k12.ply, izdvojenih=39 |
| 2026-09-25 15:53 | C0257 | CISTO: cijeli lanac sa zadanim postavkama (--then) | 15000 | 1920x1080 | 2148945 | 1143 | 26.34 | 21.08 | 0.801 |  | kamera=classic, izlaz=scena.ply, izdvojenih=39 |
| 2026-09-25 17:18 | C0257 | CISTO 2: cijeli lanac sa zadanim postavkama (--then), popravljena samokalibracija | 15000 | 1920x1080 | 2156223 | 1161 | 25.91 | 21.44 | 0.802 |  | kamera=classic, izlaz=scena.ply, izdvojenih=39 |
| 2026-09-25 18:10 | C0255 | C0255: cijeli lanac sada | 15000 | 1920x1080 | 223175 | 739 | 14.95 | 10.24 | 0.792 |  | kamera=classic, izlaz=scena.ply, izdvojenih=21 |
| 2026-09-25 18:46 | C0255 | C0255: cijeli lanac, ograde + kamere pa tocke | 15000 | 1920x1080 | 243656 | 738 | 26.71 | 12.81 | 0.925 |  | kamera=classic, izlaz=scena.ply, izdvojenih=21 |
| 2026-09-25 20:47 | C0257 | E14: ispravak poza u treningu | 15000 | 1920x1080 | 1866479 | 1713 | 26.87 | 21.96 | 0.802 |  | kamera=classic, izlaz=e14_pose.ply, izdvojenih=39 |
| 2026-09-25 21:09 | C0257 | E15: povrsina u treningu (0.1, plosnatost 0.1, od 7000) | 15000 | 1920x1080 | 1930114 | 1262 | 25.27 | 20.44 | 0.781 |  | kamera=classic, izlaz=e15_surface.ply, izdvojenih=39 |
| 2026-09-25 21:33 | C0257 | E16: bilateralna mreza umjesto ekspozicije | 15000 | 1920x1080 | 2206230 | 1422 | 25.67 | 20.97 | 0.806 |  | kamera=classic, izlaz=e16_bilagrid.ply, izdvojenih=39 |
| 2026-09-25 22:56 | C0257 | E14b: ispravak poza, rijetki Adam 1e-5 od 3000 | 15000 | 1920x1080 | 2155145 | 2400 | 26.16 | 21.45 | 0.809 |  | kamera=classic, izlaz=e14b_pose.ply, izdvojenih=39 |
| 2026-09-25 23:15 | C0257 | E17: trening kadrovi zamijenjeni najostrijim susjedom (+-4) s pozom iz punih slicica | 15000 | 1920x1080 | 2174762 | 1022 | 25.63 | 21.33 | 0.805 |  | kamera=classic, izlaz=e17_ostri.ply, izdvojenih=39 |

## ocjena

| datum | snimka | opis | razlucivost | psnr | ssim | ostrina | kadrova | gaussiana | odluka | ostalo |
|---|---|---|---|---|---|---|---|---|---|---|
| 2026-09-24 03:37 | C0257 | prije ciscenja floatera | 1920x1080 | 24.37 |  |  | 39 | 3631276 |  | datoteka_mb=857 |
| 2026-09-24 03:37 | C0257 | poslije ciscenja floatera (clean_splats) | 1920x1080 | 24.70 |  |  | 39 | 848672 | zadrzano (--clean zadano) | datoteka_mb=200 |
| 2026-09-24 19:25 | C0257 | t_1_stari (polaziste ostrine) | 1920x1080 | 24.48 | 0.774 | 0.297 | 39 |  |  |  |
| 2026-09-24 19:25 | C0257 | t_1_stari (polaziste ostrine) | 3840x2160 | 24.19 | 0.779 | 0.087 | 39 |  |  |  |
| 2026-09-24 19:40 | C0257 | 15000 koraka | 1920x1080 | 24.54 | 0.773 | 0.354 | 39 | 1500000 |  |  |
| 2026-09-24 19:40 | C0257 | 15000 koraka | 3840x2160 | 24.23 | 0.778 | 0.107 | 39 | 1500000 |  |  |
| 2026-09-24 20:05 | C0257 | 4K trening, 7000 koraka | 3840x2160 | 24.00 | 0.778 | 0.070 | 39 | 1500000 |  |  |
| 2026-09-24 20:27 | C0257 | 30000 koraka | 1920x1080 | 24.23 | 0.771 | 0.393 | 39 |  |  | psnr_medijan=24.31, splat=k30000.ply |
| 2026-09-24 20:27 | C0257 | 30000 koraka | 3840x2160 | 23.92 | 0.776 | 0.130 | 39 |  |  | psnr_medijan=24.07, splat=k30000.ply |
| 2026-09-24 22:19 | C0257 | novi solve, 15000 koraka | 1920x1080 | 25.48 | 0.802 | 0.379 | 39 |  |  | psnr_medijan=25.34, splat=t_novi.ply |
| 2026-09-24 22:19 | C0257 | novi solve, 15000 koraka | 3840x2160 | 25.40 | 0.826 | 0.129 | 39 |  |  | psnr_medijan=25.29, splat=t_novi.ply |
| 2026-09-24 22:55 | C0257 | E1 f 4650 | 1920x1080 | 24.88 | 0.784 | 0.310 | 39 |  |  | psnr_medijan=24.87, splat=e_f4650.ply |
| 2026-09-24 22:55 | C0257 | E1 f 4650 | 3840x2160 | 24.75 | 0.786 | 0.091 | 39 |  |  | psnr_medijan=24.79, splat=e_f4650.ply |
| 2026-09-24 23:15 | C0257 | E2 f 4259 | 1920x1080 | 25.24 | 0.801 | 0.324 | 39 |  |  | psnr_medijan=25.20, splat=e_f4259.ply |
| 2026-09-24 23:15 | C0257 | E2 f 4259 | 3840x2160 | 25.16 | 0.826 | 0.103 | 39 |  |  | psnr_medijan=25.15, splat=e_f4259.ply |
| 2026-09-24 23:32 | C0257 | E3 f 4259 + subpiksel | 1920x1080 | 25.16 | 0.798 | 0.306 | 39 |  |  | psnr_medijan=24.83, splat=e_subpix.ply |
| 2026-09-24 23:33 | C0257 | E3 f 4259 + subpiksel | 3840x2160 | 25.09 | 0.824 | 0.098 | 39 |  |  | psnr_medijan=24.78, splat=e_subpix.ply |
| 2026-09-24 23:53 | C0257 | E4 f 4259 + rolling shutter | 1920x1080 | 25.63 | 0.802 | 0.295 | 39 |  |  | psnr_medijan=25.72, splat=e_rs.ply |
| 2026-09-24 23:53 | C0257 | E4 f 4259 + rolling shutter | 3840x2160 | 25.55 | 0.827 | 0.082 | 39 |  |  | psnr_medijan=25.66, splat=e_rs.ply |
| 2026-09-25 00:19 | C0257 | E5 f 3925 | 1920x1080 | 24.15 | 0.775 | 0.276 | 39 |  |  | psnr_medijan=23.54, splat=e_f3925.ply |
| 2026-09-25 00:19 | C0257 | E5 f 3925 | 3840x2160 | 24.04 | 0.782 | 0.080 | 39 |  |  | psnr_medijan=23.46, splat=e_f3925.ply |
| 2026-09-25 00:29 | C0257 | E6 3DGUT bez RS | 1920x1080 | 25.16 | 0.800 | 0.310 | 39 |  |  | psnr_medijan=24.86, splat=e_ut.ply |
| 2026-09-25 00:30 | C0257 | E6 3DGUT bez RS | 3840x2160 | 25.09 | 0.825 | 0.092 | 39 |  |  | psnr_medijan=24.81, splat=e_ut.ply |
| 2026-09-25 01:34 | C0257 | E7 rsmid | 1920x1080 | 25.70 | 0.796 | 0.265 | 39 |  |  | psnr_medijan=26.03, splat=e_rsmid.ply |
| 2026-09-25 01:34 | C0257 | E7 rsmid | 3840x2160 | 25.62 | 0.824 | 0.076 | 39 |  |  | psnr_medijan=25.97, splat=e_rsmid.ply |
| 2026-09-25 01:35 | C0257 | CISTO mjerenje, zadano (3.4M -> 1.27M nakon ciscenja) | 1920x1080 | 25.75 | 0.803 | 0.398 | 39 |  |  | psnr_medijan=25.21, splat=cist.ply |
| 2026-09-25 01:35 | C0257 | CISTO mjerenje, zadano (3.4M -> 1.27M nakon ciscenja) | 3840x2160 | 25.66 | 0.827 | 0.137 | 39 |  |  | psnr_medijan=25.11, splat=cist.ply |
| 2026-09-25 01:56 | C0257 | E8 grubo pa fino | 1920x1080 | 25.38 | 0.799 | 0.298 | 39 |  |  | psnr_medijan=25.20, splat=e_fine.ply |
| 2026-09-25 01:57 | C0257 | E8 grubo pa fino | 3840x2160 | 25.31 | 0.826 | 0.091 | 39 |  |  | psnr_medijan=25.16, splat=e_fine.ply |
| 2026-09-25 02:05 | C0257 | E9 ekspozicija | 1920x1080 | 25.57 | 0.802 | 0.322 | 39 |  |  | psnr_medijan=25.60, splat=e_exp.ply |
| 2026-09-25 02:05 | C0257 | E9 ekspozicija | 3840x2160 | 25.48 | 0.827 | 0.104 | 39 |  |  | psnr_medijan=25.53, splat=e_exp.ply |
| 2026-09-25 02:33 | C0257 | E10 coarse 0.5 | 1920x1080 | 25.39 | 0.800 | 0.320 | 39 |  |  | psnr_medijan=25.29, splat=e_w05.ply |
| 2026-09-25 02:33 | C0257 | E10 coarse 0.5 | 3840x2160 | 25.32 | 0.825 | 0.102 | 39 |  |  | psnr_medijan=25.23, splat=e_w05.ply |
| 2026-09-25 03:08 | C0257 | E11 visible-adam | 1920x1080 | 25.57 | 0.805 | 0.437 | 39 |  |  | psnr_medijan=25.50, splat=vadam.ply |
| 2026-09-25 03:09 | C0257 | E11 visible-adam | 3840x2160 | 25.49 | 0.827 | 0.172 | 39 |  |  | psnr_medijan=25.43, splat=vadam.ply |
| 2026-09-25 04:55 | C0257 | E12 brisanje + 2.5M | 1920x1080 | 25.48 | 0.806 | 0.436 | 39 |  |  | psnr_medijan=25.38, splat=prune.ply |
| 2026-09-25 04:55 | C0257 | E12 brisanje + 2.5M | 3840x2160 | 25.40 | 0.828 | 0.168 | 39 |  |  | psnr_medijan=25.30, splat=prune.ply |
| 2026-09-25 05:09 | C0257 | E13 12000 koraka | 1920x1080 | 25.36 | 0.804 | 0.388 | 39 |  |  | psnr_medijan=25.23, splat=k12.ply |
| 2026-09-25 05:09 | C0257 | E13 12000 koraka | 3840x2160 | 25.28 | 0.827 | 0.145 | 39 |  |  | psnr_medijan=25.15, splat=k12.ply |
| 2026-09-25 15:53 | C0257 | CISTO: cijeli lanac, zadano | 1920x1080 | 26.10 | 0.799 | 0.445 | 39 |  |  | psnr_medijan=26.34, splat=scena.ply |
| 2026-09-25 15:53 | C0257 | CISTO: cijeli lanac, zadano | 3840x2160 | 25.93 | 0.798 | 0.165 | 39 |  |  | psnr_medijan=26.23, splat=scena.ply |
| 2026-09-25 17:19 | C0257 | CISTO 2: cijeli lanac | 1920x1080 | 25.90 | 0.807 | 0.443 | 39 |  |  | psnr_medijan=25.91, splat=scena.ply |
| 2026-09-25 17:19 | C0257 | CISTO 2: cijeli lanac | 3840x2160 | 25.81 | 0.828 | 0.172 | 39 |  |  | psnr_medijan=25.87, splat=scena.ply |
| 2026-09-25 18:11 | c55_jutro | C0255 jutro | 1920x1080 | 21.61 | 0.886 | 0.286 | 21 |  |  | psnr_medijan=21.30, splat=scena.ply |
| 2026-09-25 18:11 | C0255 | C0255 sada | 1920x1080 | 14.46 | 0.780 | 0.036 | 21 |  |  | psnr_medijan=14.95, splat=scena.ply |
| 2026-09-25 18:11 | c55_jutro | C0255 jutro | 3840x2160 | 21.56 | 0.876 | 0.093 | 21 |  |  | psnr_medijan=21.28, splat=scena.ply |
| 2026-09-25 18:11 | C0255 | C0255 sada | 3840x2160 | 14.45 | 0.793 | 0.009 | 21 |  |  | psnr_medijan=14.93, splat=scena.ply |
| 2026-09-25 18:46 | C0255 | C0255 ograde + kamere pa tocke | 1920x1080 | 25.78 | 0.908 | 0.430 | 21 |  |  | psnr_medijan=26.71, splat=scena.ply |
| 2026-09-25 18:46 | C0255 | C0255 ograde + kamere pa tocke | 3840x2160 | 25.70 | 0.893 | 0.154 | 21 |  |  | psnr_medijan=26.64, splat=scena.ply |
| 2026-09-25 19:02 | C0257 | C0257: prvo rastavljanje (povrsina 3000 + svjetlo 3000) | 960x540 | 24.07 |  |  | 39 |  |  | splat=cist.ply, izvorni_psnr=25.91, povrsinski_psnr=26.10, sunce_visina=-18.70 |
| 2026-09-25 19:16 | C0257 | C0257: rastavljanje u linearnom svjetlu | 960x540 | 23.59 |  |  | 39 |  |  | splat=cist.ply, izvorni_psnr=25.91, povrsinski_psnr=26.10, sunce_visina=-43.20 |
| 2026-09-25 19:21 | C0257 | C0257: rastavljanje linearno + sigurnost smjera | 960x540 | 23.58 |  |  | 39 |  |  | splat=cist.ply, izvorni_psnr=25.91, povrsinski_psnr=26.10, sunce_visina=-42.90, smjer_raspon_db=1.069, smjer_nesigurnost_st=173 |
| 2026-09-25 19:26 | C0257 | C0257: rastavljanje, sunce + nebo gore/dolje | 960x540 | 23.57 |  |  | 39 |  |  | splat=cist.ply, izvorni_psnr=25.91, povrsinski_psnr=26.10, sunce_visina=4.400, smjer_raspon_db=0.137, smjer_nesigurnost_st=164 |
| 2026-09-25 19:30 | C0255 | C0255: rastavljanje, sunce + nebo gore/dolje | 960x540 | 21.96 |  |  | 21 |  |  | splat=scena.ply, izvorni_psnr=25.79, povrsinski_psnr=26.12, sunce_visina=27.40, smjer_raspon_db=0.124, smjer_nesigurnost_st=169 |
| 2026-09-25 20:47 | C0257 | E14 ispravak poza | 1920x1080 | 26.58 | 0.790 | 0.135 | 39 |  |  | psnr_medijan=26.87, splat=e14_pose.ply |
| 2026-09-25 20:47 | C0257 | E14 ispravak poza | 3840x2160 | 26.49 | 0.824 | 0.028 | 39 |  |  | psnr_medijan=26.81, splat=e14_pose.ply |
| 2026-09-25 21:09 | C0257 | E15: povrsina u treningu (0.1, plosnatost 0.1, od 7000) | 1920x1080 | 24.87 | 0.788 | 0.329 | 39 |  |  | psnr_medijan=25.27, splat=e15_surface.ply |
| 2026-09-25 21:09 | C0257 | E15: povrsina u treningu (0.1, plosnatost 0.1, od 7000) | 3840x2160 | 24.81 | 0.819 | 0.143 | 39 |  |  | psnr_medijan=25.22, splat=e15_surface.ply |
| 2026-09-25 21:33 | C0257 | E16: bilateralna mreza umjesto ekspozicije | 1920x1080 | 25.88 | 0.809 | 0.425 | 39 |  |  | psnr_medijan=25.67, splat=e16_bilagrid.ply |
| 2026-09-25 21:33 | C0257 | E16: bilateralna mreza umjesto ekspozicije | 3840x2160 | 25.79 | 0.829 | 0.168 | 39 |  |  | psnr_medijan=25.63, splat=e16_bilagrid.ply |
| 2026-09-25 22:57 | C0257 | E14b: ispravak poza, rijetki Adam 1e-5 od 3000 | 1920x1080 | 26.14 | 0.806 | 0.438 | 39 |  |  | psnr_medijan=26.16, splat=e14b_pose.ply |
| 2026-09-25 22:57 | C0257 | E14b: ispravak poza, rijetki Adam 1e-5 od 3000 | 3840x2160 | 26.05 | 0.828 | 0.172 | 39 |  |  | psnr_medijan=26.06, splat=e14b_pose.ply |
| 2026-09-25 23:15 | C0257 | E17: trening kadrovi zamijenjeni najostrijim susjedom (+-4) s pozom iz punih slicica | 1920x1080 | 25.96 | 0.807 | 0.566 | 39 |  |  | psnr_medijan=25.63, splat=e17_ostri.ply |
| 2026-09-25 23:15 | C0257 | E17: trening kadrovi zamijenjeni najostrijim susjedom (+-4) s pozom iz punih slicica | 3840x2160 | 25.86 | 0.828 | 0.255 | 39 |  |  | psnr_medijan=25.59, splat=e17_ostri.ply |

## usporedba

| datum | snimka | opis | mjera | razlika | pogreska | bolji_kadrova | odluka | ostalo |
|---|---|---|---|---|---|---|---|---|
| 2026-09-24 18:18 | C0257 | rolling shutter prema classic (polazni solve) | PSNR dB | -0.077 | 0.137 |  |  |  |
| 2026-09-24 18:19 | C0257 | subpix+kalibracija prema polaznom solveu | PSNR dB | -0.529 | 0.233 |  |  |  |
| 2026-09-24 18:19 | C0257 | RS na subpix solveu | PSNR dB | -0.552 | 0.314 |  |  |  |
| 2026-09-24 18:34 | C0257 | zamucenje pokretom prema RS | PSNR dB | -0.254 | 0.098 |  |  |  |
| 2026-09-24 18:34 | C0257 | zamucenje pokretom prema classic | PSNR dB | -0.331 | 0.160 |  |  |  |
| 2026-09-24 18:43 | C0257 | isti trening ponovljen (sum) | PSNR dB | -0.006 | 0.127 |  |  |  |
| 2026-09-24 18:53 | C0257 | isti trening ponovljen (sum) | PSNR dB | -0.117 | 0.130 |  |  |  |
| 2026-09-24 19:03 | C0257 | granica 1M gaussiana prema bez granice | PSNR dB | 0.001 | 0.149 |  |  |  |
| 2026-09-24 19:10 | C0257 | granica 1.5M gaussiana prema bez granice | PSNR dB | -0.167 | 0.115 |  |  |  |
| 2026-09-24 19:40 | C0257 | 15000 prema 7000 koraka (1080p) | PSNR dB | 0.053 | 0.178 | 24/39 | sum |  |
| 2026-09-24 19:40 | C0257 | 15000 prema 7000 koraka (1080p) | SSIM | -0.001 | 0.002 | 23/39 | sum |  |
| 2026-09-24 19:40 | C0257 | 15000 prema 7000 koraka (1080p) | ostrina | 0.057 | 0.010 | 32/39 | ostrina stvarna (>5 pogresaka) |  |
| 2026-09-24 19:40 | C0257 | 15000 prema 7000 koraka (4K) | PSNR dB | 0.034 | 0.167 | 24/39 | sum |  |
| 2026-09-24 19:40 | C0257 | 15000 prema 7000 koraka (4K) | SSIM | -0.001 | 0.001 | 21/39 | sum |  |
| 2026-09-24 19:40 | C0257 | 15000 prema 7000 koraka (4K) | ostrina | 0.019 | 0.003 | 34/39 | ostrina stvarna (>5 pogresaka) |  |
| 2026-09-24 20:05 | C0257 | 4K trening 7000 koraka prema 1080p (1080p) | SSIM | -0.004 | 0.001 | 6/39 | odbaceno: mutnije - 7000 koraka premalo za 4K |  |
| 2026-09-24 20:05 | C0257 | 4K trening 7000 koraka prema 1080p (1080p) | ostrina | -0.037 | 0.006 | 5/39 | odbaceno: mutnije - 7000 koraka premalo za 4K |  |
| 2026-09-24 20:05 | C0257 | 4K trening 7000 koraka prema 1080p (4K) | PSNR dB | -0.197 | 0.097 | 8/39 | odbaceno: mutnije - 7000 koraka premalo za 4K |  |
| 2026-09-24 20:05 | C0257 | 4K trening 7000 koraka prema 1080p (4K) | SSIM | -0.001 | 0.001 | 12/39 | odbaceno: mutnije - 7000 koraka premalo za 4K |  |
| 2026-09-24 20:05 | C0257 | 4K trening 7000 koraka prema 1080p (4K) | ostrina | -0.017 | 0.003 | 3/39 | odbaceno: mutnije - 7000 koraka premalo za 4K |  |
| 2026-09-24 20:27 | C0257 | 30000 koraka prema 7000 koraka (d2) | PSNR dB | -0.255 | 0.242 | 18/39 |  | a=q1_d2, b=q_k30000_d2 |
| 2026-09-24 20:27 | C0257 | 30000 koraka prema 7000 koraka (d2) | SSIM | -0.004 | 0.002 | 16/39 |  | a=q1_d2, b=q_k30000_d2 |
| 2026-09-24 20:27 | C0257 | 30000 koraka prema 7000 koraka (d2) | ostrina | 0.096 | 0.013 | 36/39 |  | a=q1_d2, b=q_k30000_d2 |
| 2026-09-24 20:27 | C0257 | 30000 koraka prema 7000 koraka (d1) | PSNR dB | -0.273 | 0.228 | 17/39 |  | a=q1_d1, b=q_k30000_d1 |
| 2026-09-24 20:27 | C0257 | 30000 koraka prema 7000 koraka (d1) | SSIM | -0.003 | 0.002 | 13/39 |  | a=q1_d1, b=q_k30000_d1 |
| 2026-09-24 20:27 | C0257 | 30000 koraka prema 7000 koraka (d1) | ostrina | 0.043 | 0.004 | 38/39 |  | a=q1_d1, b=q_k30000_d1 |
| 2026-09-24 22:19 | C0257 | novi solve 15000 prema polaznom 7000 (d2) | PSNR dB | 0.999 | 0.258 | 28/39 | stvarno (>3.5 pogreske) | a=q1_d2, b=q_novi_d2 |
| 2026-09-24 22:19 | C0257 | novi solve 15000 prema polaznom 7000 (d2) | SSIM | 0.027 | 0.003 | 37/39 | stvarno (>3.5 pogreske) | a=q1_d2, b=q_novi_d2 |
| 2026-09-24 22:19 | C0257 | novi solve 15000 prema polaznom 7000 (d2) | ostrina | 0.083 | 0.017 | 30/39 |  | a=q1_d2, b=q_novi_d2 |
| 2026-09-24 22:19 | C0257 | novi solve 15000 prema polaznom 7000 (d1) | PSNR dB | 1.207 | 0.244 | 34/39 | stvarno (>3.5 pogreske) | a=q1_d1, b=q_novi_d1 |
| 2026-09-24 22:19 | C0257 | novi solve 15000 prema polaznom 7000 (d1) | SSIM | 0.047 | 0.002 | 39/39 | stvarno (>3.5 pogreske) | a=q1_d1, b=q_novi_d1 |
| 2026-09-24 22:19 | C0257 | novi solve 15000 prema polaznom 7000 (d1) | ostrina | 0.042 | 0.006 | 37/39 |  | a=q1_d1, b=q_novi_d1 |
| 2026-09-24 22:19 | C0257 | novi solve prema starom, oba 15000 koraka (d2) | PSNR dB | 0.946 | 0.240 | 28/39 | stvarno (>3.5 pogreske) | a=q_k15000_d2, b=q_novi_d2 |
| 2026-09-24 22:19 | C0257 | novi solve prema starom, oba 15000 koraka (d2) | SSIM | 0.029 | 0.003 | 39/39 | stvarno (>3.5 pogreske) | a=q_k15000_d2, b=q_novi_d2 |
| 2026-09-24 22:19 | C0257 | novi solve prema starom, oba 15000 koraka (d2) | ostrina | 0.026 | 0.012 | 23/39 |  | a=q_k15000_d2, b=q_novi_d2 |
| 2026-09-24 22:19 | C0257 | novi solve prema starom, oba 15000 koraka (d1) | PSNR dB | 1.173 | 0.218 | 30/39 | stvarno (>3.5 pogreske) | a=q_k15000_d1, b=q_novi_d1 |
| 2026-09-24 22:19 | C0257 | novi solve prema starom, oba 15000 koraka (d1) | SSIM | 0.048 | 0.002 | 39/39 | stvarno (>3.5 pogreske) | a=q_k15000_d1, b=q_novi_d1 |
| 2026-09-24 22:19 | C0257 | novi solve prema starom, oba 15000 koraka (d1) | ostrina | 0.022 | 0.006 | 28/39 |  | a=q_k15000_d1, b=q_novi_d1 |
| 2026-09-24 23:15 | C0257 | f 4259 prema f 4650, isti lanac, 7000 koraka (d2) | PSNR dB | 0.362 | 0.141 | 22/39 | zarisna je uzrok: 4259 bolja (solve je ne razlikuje) | a=q_e_f4650_d2, b=q_e_f4259_d2 |
| 2026-09-24 23:15 | C0257 | f 4259 prema f 4650, isti lanac, 7000 koraka (d2) | SSIM | 0.017 | 0.002 | 38/39 | zarisna je uzrok: 4259 bolja (solve je ne razlikuje) | a=q_e_f4650_d2, b=q_e_f4259_d2 |
| 2026-09-24 23:15 | C0257 | f 4259 prema f 4650, isti lanac, 7000 koraka (d2) | ostrina | 0.014 | 0.006 | 26/39 | zarisna je uzrok: 4259 bolja (solve je ne razlikuje) | a=q_e_f4650_d2, b=q_e_f4259_d2 |
| 2026-09-24 23:15 | C0257 | f 4259 prema f 4650, isti lanac, 7000 koraka (d1) | PSNR dB | 0.414 | 0.138 | 22/39 | zarisna je uzrok: 4259 bolja (solve je ne razlikuje) | a=q_e_f4650_d1, b=q_e_f4259_d1 |
| 2026-09-24 23:15 | C0257 | f 4259 prema f 4650, isti lanac, 7000 koraka (d1) | SSIM | 0.040 | 0.001 | 39/39 | zarisna je uzrok: 4259 bolja (solve je ne razlikuje) | a=q_e_f4650_d1, b=q_e_f4259_d1 |
| 2026-09-24 23:15 | C0257 | f 4259 prema f 4650, isti lanac, 7000 koraka (d1) | ostrina | 0.012 | 0.003 | 27/39 | zarisna je uzrok: 4259 bolja (solve je ne razlikuje) | a=q_e_f4650_d1, b=q_e_f4259_d1 |
| 2026-09-24 23:33 | C0257 | subpikselni uglovi prema bez, f 4259, 7000 koraka (d2) | PSNR dB | -0.080 | 0.073 | 16/39 | odbaceno: SSIM losiji na 30/39, ostrina -5 %, PSNR u sumu | a=q_e_f4259_d2, b=q_e_subpix_d2 |
| 2026-09-24 23:33 | C0257 | subpikselni uglovi prema bez, f 4259, 7000 koraka (d2) | SSIM | -0.003 | 0.001 | 9/39 | odbaceno: SSIM losiji na 30/39, ostrina -5 %, PSNR u sumu | a=q_e_f4259_d2, b=q_e_subpix_d2 |
| 2026-09-24 23:33 | C0257 | subpikselni uglovi prema bez, f 4259, 7000 koraka (d2) | ostrina | -0.018 | 0.008 | 10/39 | odbaceno: SSIM losiji na 30/39, ostrina -5 %, PSNR u sumu | a=q_e_f4259_d2, b=q_e_subpix_d2 |
| 2026-09-24 23:33 | C0257 | subpikselni uglovi prema bez, f 4259, 7000 koraka (d1) | PSNR dB | -0.075 | 0.072 | 16/39 | odbaceno: SSIM losiji na 30/39, ostrina -5 %, PSNR u sumu | a=q_e_f4259_d1, b=q_e_subpix_d1 |
| 2026-09-24 23:33 | C0257 | subpikselni uglovi prema bez, f 4259, 7000 koraka (d1) | SSIM | -0.002 | 0.001 | 11/39 | odbaceno: SSIM losiji na 30/39, ostrina -5 %, PSNR u sumu | a=q_e_f4259_d1, b=q_e_subpix_d1 |
| 2026-09-24 23:33 | C0257 | subpikselni uglovi prema bez, f 4259, 7000 koraka (d1) | ostrina | -0.005 | 0.002 | 15/39 | odbaceno: SSIM losiji na 30/39, ostrina -5 %, PSNR u sumu | a=q_e_f4259_d1, b=q_e_subpix_d1 |
| 2026-09-24 23:53 | C0257 | rolling shutter prema bez, f 4259, 7000 koraka (d2) | PSNR dB | 0.388 | 0.151 | 25/39 | PSNR +0.39 (stvarno), ali ostrina -9 %/-20 % - nije zadano; provjeriti 3DGUT bez RS | a=q_e_f4259_d2, b=q_e_rs_d2 |
| 2026-09-24 23:53 | C0257 | rolling shutter prema bez, f 4259, 7000 koraka (d2) | SSIM | 0.001 | 0.001 | 23/39 | PSNR +0.39 (stvarno), ali ostrina -9 %/-20 % - nije zadano; provjeriti 3DGUT bez RS | a=q_e_f4259_d2, b=q_e_rs_d2 |
| 2026-09-24 23:53 | C0257 | rolling shutter prema bez, f 4259, 7000 koraka (d2) | ostrina | -0.029 | 0.005 | 9/39 | PSNR +0.39 (stvarno), ali ostrina -9 %/-20 % - nije zadano; provjeriti 3DGUT bez RS | a=q_e_f4259_d2, b=q_e_rs_d2 |
| 2026-09-24 23:53 | C0257 | rolling shutter prema bez, f 4259, 7000 koraka (d1) | PSNR dB | 0.386 | 0.147 | 25/39 | PSNR +0.39 (stvarno), ali ostrina -9 %/-20 % - nije zadano; provjeriti 3DGUT bez RS | a=q_e_f4259_d1, b=q_e_rs_d1 |
| 2026-09-24 23:53 | C0257 | rolling shutter prema bez, f 4259, 7000 koraka (d1) | SSIM | 0.001 | 0.001 | 24/39 | PSNR +0.39 (stvarno), ali ostrina -9 %/-20 % - nije zadano; provjeriti 3DGUT bez RS | a=q_e_f4259_d1, b=q_e_rs_d1 |
| 2026-09-24 23:53 | C0257 | rolling shutter prema bez, f 4259, 7000 koraka (d1) | ostrina | -0.021 | 0.002 | 0/39 | PSNR +0.39 (stvarno), ali ostrina -9 %/-20 % - nije zadano; provjeriti 3DGUT bez RS | a=q_e_f4259_d1, b=q_e_rs_d1 |
| 2026-09-25 00:19 | C0257 | f 3925 (metapodaci) prema f 4259 (d2) | PSNR dB | -1.092 | 0.189 | 6/39 | odbaceno: ekvivalent iz metapodataka nije pravi kadar (-1.1 dB, SSIM 0/39) | a=q_e_f4259_d2, b=q_e_f3925_d2 |
| 2026-09-25 00:19 | C0257 | f 3925 (metapodaci) prema f 4259 (d2) | SSIM | -0.025 | 0.001 | 0/39 | odbaceno: ekvivalent iz metapodataka nije pravi kadar (-1.1 dB, SSIM 0/39) | a=q_e_f4259_d2, b=q_e_f3925_d2 |
| 2026-09-25 00:19 | C0257 | f 3925 (metapodaci) prema f 4259 (d2) | ostrina | -0.048 | 0.010 | 6/39 | odbaceno: ekvivalent iz metapodataka nije pravi kadar (-1.1 dB, SSIM 0/39) | a=q_e_f4259_d2, b=q_e_f3925_d2 |
| 2026-09-25 00:19 | C0257 | f 3925 (metapodaci) prema f 4259 (d1) | PSNR dB | -1.126 | 0.185 | 6/39 | odbaceno: ekvivalent iz metapodataka nije pravi kadar (-1.1 dB, SSIM 0/39) | a=q_e_f4259_d1, b=q_e_f3925_d1 |
| 2026-09-25 00:19 | C0257 | f 3925 (metapodaci) prema f 4259 (d1) | SSIM | -0.044 | 0.001 | 0/39 | odbaceno: ekvivalent iz metapodataka nije pravi kadar (-1.1 dB, SSIM 0/39) | a=q_e_f4259_d1, b=q_e_f3925_d1 |
| 2026-09-25 00:19 | C0257 | f 3925 (metapodaci) prema f 4259 (d1) | ostrina | -0.023 | 0.004 | 6/39 | odbaceno: ekvivalent iz metapodataka nije pravi kadar (-1.1 dB, SSIM 0/39) | a=q_e_f4259_d1, b=q_e_f3925_d1 |
| 2026-09-25 00:29 | C0257 | 3DGUT bez RS prema classic, f 4259 (d2) | PSNR dB | -0.080 | 0.079 | 14/39 | 3DGUT sam: PSNR u sumu, ostrina -4 %/-10 % - dobitak E4 je od rolling shuttera, a polovica zamucenja od 3DGUT | a=q_e_f4259_d2, b=q_e_ut_d2 |
| 2026-09-25 00:29 | C0257 | 3DGUT bez RS prema classic, f 4259 (d2) | SSIM | -0.001 | 0.000 | 13/39 | 3DGUT sam: PSNR u sumu, ostrina -4 %/-10 % - dobitak E4 je od rolling shuttera, a polovica zamucenja od 3DGUT | a=q_e_f4259_d2, b=q_e_ut_d2 |
| 2026-09-25 00:29 | C0257 | 3DGUT bez RS prema classic, f 4259 (d2) | ostrina | -0.014 | 0.005 | 16/39 | 3DGUT sam: PSNR u sumu, ostrina -4 %/-10 % - dobitak E4 je od rolling shuttera, a polovica zamucenja od 3DGUT | a=q_e_f4259_d2, b=q_e_ut_d2 |
| 2026-09-25 00:30 | C0257 | 3DGUT bez RS prema classic, f 4259 (d1) | PSNR dB | -0.077 | 0.078 | 15/39 | 3DGUT sam: PSNR u sumu, ostrina -4 %/-10 % - dobitak E4 je od rolling shuttera, a polovica zamucenja od 3DGUT | a=q_e_f4259_d1, b=q_e_ut_d1 |
| 2026-09-25 00:30 | C0257 | 3DGUT bez RS prema classic, f 4259 (d1) | SSIM | -0.001 | 0.000 | 15/39 | 3DGUT sam: PSNR u sumu, ostrina -4 %/-10 % - dobitak E4 je od rolling shuttera, a polovica zamucenja od 3DGUT | a=q_e_f4259_d1, b=q_e_ut_d1 |
| 2026-09-25 00:30 | C0257 | 3DGUT bez RS prema classic, f 4259 (d1) | ostrina | -0.011 | 0.002 | 2/39 | 3DGUT sam: PSNR u sumu, ostrina -4 %/-10 % - dobitak E4 je od rolling shuttera, a polovica zamucenja od 3DGUT | a=q_e_f4259_d1, b=q_e_ut_d1 |
| 2026-09-25 01:34 | C0257 | rsmid prema classic, f 4259 (d2) | PSNR dB | 0.460 | 0.150 | 27/39 | PSNR +0.46, ali ostrina -18 %/-26 % (0/39) - mutnoca je od RS poza, ne od 3DGUT; nije zadano | a=q_e_f4259_d2, b=q_e_rsmid_d2 |
| 2026-09-25 01:34 | C0257 | rsmid prema classic, f 4259 (d2) | SSIM | -0.004 | 0.001 | 11/39 | PSNR +0.46, ali ostrina -18 %/-26 % (0/39) - mutnoca je od RS poza, ne od 3DGUT; nije zadano | a=q_e_f4259_d2, b=q_e_rsmid_d2 |
| 2026-09-25 01:34 | C0257 | rsmid prema classic, f 4259 (d2) | ostrina | -0.059 | 0.006 | 0/39 | PSNR +0.46, ali ostrina -18 %/-26 % (0/39) - mutnoca je od RS poza, ne od 3DGUT; nije zadano | a=q_e_f4259_d2, b=q_e_rsmid_d2 |
| 2026-09-25 01:34 | C0257 | rsmid prema classic, f 4259 (d1) | PSNR dB | 0.455 | 0.146 | 27/39 | PSNR +0.46, ali ostrina -18 %/-26 % (0/39) - mutnoca je od RS poza, ne od 3DGUT; nije zadano | a=q_e_f4259_d1, b=q_e_rsmid_d1 |
| 2026-09-25 01:34 | C0257 | rsmid prema classic, f 4259 (d1) | SSIM | -0.002 | 0.001 | 18/39 | PSNR +0.46, ali ostrina -18 %/-26 % (0/39) - mutnoca je od RS poza, ne od 3DGUT; nije zadano | a=q_e_f4259_d1, b=q_e_rsmid_d1 |
| 2026-09-25 01:34 | C0257 | rsmid prema classic, f 4259 (d1) | ostrina | -0.027 | 0.002 | 0/39 | PSNR +0.46, ali ostrina -18 %/-26 % (0/39) - mutnoca je od RS poza, ne od 3DGUT; nije zadano | a=q_e_f4259_d1, b=q_e_rsmid_d1 |
| 2026-09-25 01:35 | C0257 | cijeli novi lanac (zadano) prema jutarnjem (d2) | PSNR dB | 1.267 | 0.255 | 32/39 | najbolje dosad | a=q1_d2, b=q_cist_d2 |
| 2026-09-25 01:35 | C0257 | cijeli novi lanac (zadano) prema jutarnjem (d2) | SSIM | 0.029 | 0.004 | 38/39 | najbolje dosad | a=q1_d2, b=q_cist_d2 |
| 2026-09-25 01:35 | C0257 | cijeli novi lanac (zadano) prema jutarnjem (d2) | ostrina | 0.101 | 0.017 | 33/39 | najbolje dosad | a=q1_d2, b=q_cist_d2 |
| 2026-09-25 01:35 | C0257 | zadano (bez granice, ciscenje) prema granici 1.5M bez ciscenja (d2) | PSNR dB | 0.268 | 0.127 | 26/39 | bez granice +0.27 dB i ostrina +5 %, ali trening 29 prema 16 min - zadano ostaje bez granice | a=q_novi_d2, b=q_cist_d2 |
| 2026-09-25 01:35 | C0257 | zadano (bez granice, ciscenje) prema granici 1.5M bez ciscenja (d2) | SSIM | 0.001 | 0.001 | 24/39 | bez granice +0.27 dB i ostrina +5 %, ali trening 29 prema 16 min - zadano ostaje bez granice | a=q_novi_d2, b=q_cist_d2 |
| 2026-09-25 01:35 | C0257 | zadano (bez granice, ciscenje) prema granici 1.5M bez ciscenja (d2) | ostrina | 0.019 | 0.003 | 32/39 | bez granice +0.27 dB i ostrina +5 %, ali trening 29 prema 16 min - zadano ostaje bez granice | a=q_novi_d2, b=q_cist_d2 |
| 2026-09-25 01:35 | C0257 | cijeli novi lanac (zadano) prema jutarnjem (d1) | PSNR dB | 1.468 | 0.242 | 35/39 | najbolje dosad | a=q1_d1, b=q_cist_d1 |
| 2026-09-25 01:35 | C0257 | cijeli novi lanac (zadano) prema jutarnjem (d1) | SSIM | 0.048 | 0.002 | 39/39 | najbolje dosad | a=q1_d1, b=q_cist_d1 |
| 2026-09-25 01:35 | C0257 | cijeli novi lanac (zadano) prema jutarnjem (d1) | ostrina | 0.050 | 0.007 | 39/39 | najbolje dosad | a=q1_d1, b=q_cist_d1 |
| 2026-09-25 01:35 | C0257 | zadano (bez granice, ciscenje) prema granici 1.5M bez ciscenja (d1) | PSNR dB | 0.262 | 0.124 | 26/39 | bez granice +0.27 dB i ostrina +5 %, ali trening 29 prema 16 min - zadano ostaje bez granice | a=q_novi_d1, b=q_cist_d1 |
| 2026-09-25 01:35 | C0257 | zadano (bez granice, ciscenje) prema granici 1.5M bez ciscenja (d1) | SSIM | 0.001 | 0.001 | 21/39 | bez granice +0.27 dB i ostrina +5 %, ali trening 29 prema 16 min - zadano ostaje bez granice | a=q_novi_d1, b=q_cist_d1 |
| 2026-09-25 01:35 | C0257 | zadano (bez granice, ciscenje) prema granici 1.5M bez ciscenja (d1) | ostrina | 0.008 | 0.002 | 34/39 | bez granice +0.27 dB i ostrina +5 %, ali trening 29 prema 16 min - zadano ostaje bez granice | a=q_novi_d1, b=q_cist_d1 |
| 2026-09-25 01:56 | C0257 | 7000+3000 na 4K prema 7000, f 4259 (d2) | PSNR dB | 0.142 | 0.161 | 25/39 | odbaceno uz granicu 1.5M: PSNR u sumu, ostrina -8 %/-12 %, 2x dulje; probati bez granice | a=q_e_f4259_d2, b=q_e_fine_d2 |
| 2026-09-25 01:56 | C0257 | 7000+3000 na 4K prema 7000, f 4259 (d2) | SSIM | -0.002 | 0.002 | 19/39 | odbaceno uz granicu 1.5M: PSNR u sumu, ostrina -8 %/-12 %, 2x dulje; probati bez granice | a=q_e_f4259_d2, b=q_e_fine_d2 |
| 2026-09-25 01:56 | C0257 | 7000+3000 na 4K prema 7000, f 4259 (d2) | ostrina | -0.026 | 0.008 | 16/39 | odbaceno uz granicu 1.5M: PSNR u sumu, ostrina -8 %/-12 %, 2x dulje; probati bez granice | a=q_e_f4259_d2, b=q_e_fine_d2 |
| 2026-09-25 01:57 | C0257 | 7000+3000 na 4K prema 7000, f 4259 (d1) | PSNR dB | 0.145 | 0.158 | 25/39 | odbaceno uz granicu 1.5M: PSNR u sumu, ostrina -8 %/-12 %, 2x dulje; probati bez granice | a=q_e_f4259_d1, b=q_e_fine_d1 |
| 2026-09-25 01:57 | C0257 | 7000+3000 na 4K prema 7000, f 4259 (d1) | SSIM | -0.000 | 0.001 | 20/39 | odbaceno uz granicu 1.5M: PSNR u sumu, ostrina -8 %/-12 %, 2x dulje; probati bez granice | a=q_e_f4259_d1, b=q_e_fine_d1 |
| 2026-09-25 01:57 | C0257 | 7000+3000 na 4K prema 7000, f 4259 (d1) | ostrina | -0.012 | 0.003 | 18/39 | odbaceno uz granicu 1.5M: PSNR u sumu, ostrina -8 %/-12 %, 2x dulje; probati bez granice | a=q_e_f4259_d1, b=q_e_fine_d1 |
| 2026-09-25 02:05 | C0257 | ekspozicija po kadru prema bez, f 4259 (d2) | PSNR dB | 0.325 | 0.072 | 31/39 | zadano: +0.33 dB (4.5 pogreske), SSIM bolji, ostrina ista, vrijeme isto | a=q_e_f4259_d2, b=q_e_exp_d2 |
| 2026-09-25 02:05 | C0257 | ekspozicija po kadru prema bez, f 4259 (d2) | SSIM | 0.001 | 0.001 | 30/39 | zadano: +0.33 dB (4.5 pogreske), SSIM bolji, ostrina ista, vrijeme isto | a=q_e_f4259_d2, b=q_e_exp_d2 |
| 2026-09-25 02:05 | C0257 | ekspozicija po kadru prema bez, f 4259 (d2) | ostrina | -0.002 | 0.007 | 23/39 | zadano: +0.33 dB (4.5 pogreske), SSIM bolji, ostrina ista, vrijeme isto | a=q_e_f4259_d2, b=q_e_exp_d2 |
| 2026-09-25 02:05 | C0257 | ekspozicija po kadru prema bez, f 4259 (d1) | PSNR dB | 0.321 | 0.071 | 31/39 | zadano: +0.33 dB (4.5 pogreske), SSIM bolji, ostrina ista, vrijeme isto | a=q_e_f4259_d1, b=q_e_exp_d1 |
| 2026-09-25 02:05 | C0257 | ekspozicija po kadru prema bez, f 4259 (d1) | SSIM | 0.001 | 0.000 | 26/39 | zadano: +0.33 dB (4.5 pogreske), SSIM bolji, ostrina ista, vrijeme isto | a=q_e_f4259_d1, b=q_e_exp_d1 |
| 2026-09-25 02:05 | C0257 | ekspozicija po kadru prema bez, f 4259 (d1) | ostrina | 0.000 | 0.002 | 18/39 | zadano: +0.33 dB (4.5 pogreske), SSIM bolji, ostrina ista, vrijeme isto | a=q_e_f4259_d1, b=q_e_exp_d1 |
| 2026-09-25 02:33 | C0257 | coarse-weight 0.5 prema 1.0, f 4259 (d2) | PSNR dB | 0.153 | 0.050 | 27/39 | zadano 0.5: PSNR +0.15 (3 pogreske), SSIM -0.001, ostrina ista | a=q_e_f4259_d2, b=q_e_w05_d2 |
| 2026-09-25 02:33 | C0257 | coarse-weight 0.5 prema 1.0, f 4259 (d2) | SSIM | -0.001 | 0.001 | 15/39 | zadano 0.5: PSNR +0.15 (3 pogreske), SSIM -0.001, ostrina ista | a=q_e_f4259_d2, b=q_e_w05_d2 |
| 2026-09-25 02:33 | C0257 | coarse-weight 0.5 prema 1.0, f 4259 (d2) | ostrina | -0.004 | 0.004 | 18/39 | zadano 0.5: PSNR +0.15 (3 pogreske), SSIM -0.001, ostrina ista | a=q_e_f4259_d2, b=q_e_w05_d2 |
| 2026-09-25 02:33 | C0257 | coarse-weight 0.5 prema 1.0, f 4259 (d1) | PSNR dB | 0.152 | 0.049 | 27/39 | zadano 0.5: PSNR +0.15 (3 pogreske), SSIM -0.001, ostrina ista | a=q_e_f4259_d1, b=q_e_w05_d1 |
| 2026-09-25 02:33 | C0257 | coarse-weight 0.5 prema 1.0, f 4259 (d1) | SSIM | -0.001 | 0.000 | 17/39 | zadano 0.5: PSNR +0.15 (3 pogreske), SSIM -0.001, ostrina ista | a=q_e_f4259_d1, b=q_e_w05_d1 |
| 2026-09-25 02:33 | C0257 | coarse-weight 0.5 prema 1.0, f 4259 (d1) | ostrina | -0.001 | 0.001 | 18/39 | zadano 0.5: PSNR +0.15 (3 pogreske), SSIM -0.001, ostrina ista | a=q_e_f4259_d1, b=q_e_w05_d1 |
| 2026-09-25 03:08 | C0257 | visible-adam + brzi SSIM prema obicnom Adamu (d2) | PSNR dB | -0.177 | 0.081 | 15/39 | zadano: 99 -> 73 ms/korak, ostrina +10 %/+25 %, PSNR prosjek -0.18 (medijan +0.3) | a=q_cist_d2, b=q_vadam_d2 |
| 2026-09-25 03:08 | C0257 | visible-adam + brzi SSIM prema obicnom Adamu (d2) | SSIM | 0.002 | 0.001 | 22/39 | zadano: 99 -> 73 ms/korak, ostrina +10 %/+25 %, PSNR prosjek -0.18 (medijan +0.3) | a=q_cist_d2, b=q_vadam_d2 |
| 2026-09-25 03:08 | C0257 | visible-adam + brzi SSIM prema obicnom Adamu (d2) | ostrina | 0.039 | 0.007 | 33/39 | zadano: 99 -> 73 ms/korak, ostrina +10 %/+25 %, PSNR prosjek -0.18 (medijan +0.3) | a=q_cist_d2, b=q_vadam_d2 |
| 2026-09-25 03:09 | C0257 | visible-adam + brzi SSIM prema obicnom Adamu (d1) | PSNR dB | -0.174 | 0.080 | 15/39 | zadano: 99 -> 73 ms/korak, ostrina +10 %/+25 %, PSNR prosjek -0.18 (medijan +0.3) | a=q_cist_d1, b=q_vadam_d1 |
| 2026-09-25 03:09 | C0257 | visible-adam + brzi SSIM prema obicnom Adamu (d1) | SSIM | 0.000 | 0.001 | 19/39 | zadano: 99 -> 73 ms/korak, ostrina +10 %/+25 %, PSNR prosjek -0.18 (medijan +0.3) | a=q_cist_d1, b=q_vadam_d1 |
| 2026-09-25 03:09 | C0257 | visible-adam + brzi SSIM prema obicnom Adamu (d1) | ostrina | 0.035 | 0.004 | 39/39 | zadano: 99 -> 73 ms/korak, ostrina +10 %/+25 %, PSNR prosjek -0.18 (medijan +0.3) | a=q_cist_d1, b=q_vadam_d1 |
| 2026-09-25 04:55 | C0257 | brisanje nevidljivih + 2.5M prema E11 (d2) | PSNR dB | -0.091 | 0.072 | 22/39 | zadano: 17.5 -> 15.9 min, SSIM +0.0015 (31/39), PSNR/ostrina u sumu | a=q_vadam_d2, b=q_prune_d2 |
| 2026-09-25 04:55 | C0257 | brisanje nevidljivih + 2.5M prema E11 (d2) | SSIM | 0.002 | 0.000 | 31/39 | zadano: 17.5 -> 15.9 min, SSIM +0.0015 (31/39), PSNR/ostrina u sumu | a=q_vadam_d2, b=q_prune_d2 |
| 2026-09-25 04:55 | C0257 | brisanje nevidljivih + 2.5M prema E11 (d2) | ostrina | -0.000 | 0.002 | 18/39 | zadano: 17.5 -> 15.9 min, SSIM +0.0015 (31/39), PSNR/ostrina u sumu | a=q_vadam_d2, b=q_prune_d2 |
| 2026-09-25 04:55 | C0257 | brisanje nevidljivih + 2.5M prema E11 (d1) | PSNR dB | -0.089 | 0.071 | 23/39 | zadano: 17.5 -> 15.9 min, SSIM +0.0015 (31/39), PSNR/ostrina u sumu | a=q_vadam_d1, b=q_prune_d1 |
| 2026-09-25 04:55 | C0257 | brisanje nevidljivih + 2.5M prema E11 (d1) | SSIM | 0.001 | 0.000 | 28/39 | zadano: 17.5 -> 15.9 min, SSIM +0.0015 (31/39), PSNR/ostrina u sumu | a=q_vadam_d1, b=q_prune_d1 |
| 2026-09-25 04:55 | C0257 | brisanje nevidljivih + 2.5M prema E11 (d1) | ostrina | -0.004 | 0.001 | 13/39 | zadano: 17.5 -> 15.9 min, SSIM +0.0015 (31/39), PSNR/ostrina u sumu | a=q_vadam_d1, b=q_prune_d1 |
| 2026-09-25 05:09 | C0257 | 12000 prema 15000 koraka (d2) | PSNR dB | -0.117 | 0.121 | 19/39 | odbaceno: -3.5 min, ali ostrina -11 %/-14 % (8/39), SSIM losiji | a=q_prune_d2, b=q_k12_d2 |
| 2026-09-25 05:09 | C0257 | 12000 prema 15000 koraka (d2) | SSIM | -0.003 | 0.002 | 9/39 | odbaceno: -3.5 min, ali ostrina -11 %/-14 % (8/39), SSIM losiji | a=q_prune_d2, b=q_k12_d2 |
| 2026-09-25 05:09 | C0257 | 12000 prema 15000 koraka (d2) | ostrina | -0.049 | 0.009 | 8/39 | odbaceno: -3.5 min, ali ostrina -11 %/-14 % (8/39), SSIM losiji | a=q_prune_d2, b=q_k12_d2 |
| 2026-09-25 05:09 | C0257 | 12000 prema 15000 koraka (d1) | PSNR dB | -0.114 | 0.119 | 19/39 | odbaceno: -3.5 min, ali ostrina -11 %/-14 % (8/39), SSIM losiji | a=q_prune_d1, b=q_k12_d1 |
| 2026-09-25 05:09 | C0257 | 12000 prema 15000 koraka (d1) | SSIM | -0.001 | 0.001 | 10/39 | odbaceno: -3.5 min, ali ostrina -11 %/-14 % (8/39), SSIM losiji | a=q_prune_d1, b=q_k12_d1 |
| 2026-09-25 05:09 | C0257 | 12000 prema 15000 koraka (d1) | ostrina | -0.023 | 0.004 | 8/39 | odbaceno: -3.5 min, ali ostrina -11 %/-14 % (8/39), SSIM losiji | a=q_prune_d1, b=q_k12_d1 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema jutru 24.9. (d2) | PSNR dB | 1.615 | 0.227 | 35/39 | +1.62/+1.74 dB, ostrina +50/+89 % | a=q1_d2, b=q_lanac_d2 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema jutru 24.9. (d2) | SSIM | 0.024 | 0.003 | 35/39 | +1.62/+1.74 dB, ostrina +50/+89 % | a=q1_d2, b=q_lanac_d2 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema jutru 24.9. (d2) | ostrina | 0.148 | 0.016 | 39/39 | +1.62/+1.74 dB, ostrina +50/+89 % | a=q1_d2, b=q_lanac_d2 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema cistom mjerenju prije ubrzanja treninga (d2) | PSNR dB | 0.348 | 0.143 | 20/39 | PSNR +0.35/+0.27, ostrina +12/+21 %, ali SSIM -0.005/-0.029 (4K 0/39) - provjeriti (samokalibracija f 4402 k1 0 prema 4259 -0.025) | a=q_cist_d2, b=q_lanac_d2 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema cistom mjerenju prije ubrzanja treninga (d2) | SSIM | -0.005 | 0.002 | 13/39 | PSNR +0.35/+0.27, ostrina +12/+21 %, ali SSIM -0.005/-0.029 (4K 0/39) - provjeriti (samokalibracija f 4402 k1 0 prema 4259 -0.025) | a=q_cist_d2, b=q_lanac_d2 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema cistom mjerenju prije ubrzanja treninga (d2) | ostrina | 0.047 | 0.005 | 36/39 | PSNR +0.35/+0.27, ostrina +12/+21 %, ali SSIM -0.005/-0.029 (4K 0/39) - provjeriti (samokalibracija f 4402 k1 0 prema 4259 -0.025) | a=q_cist_d2, b=q_lanac_d2 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema jutru 24.9. (d1) | PSNR dB | 1.738 | 0.212 | 36/39 | +1.62/+1.74 dB, ostrina +50/+89 % | a=q1_d1, b=q_lanac_d1 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema jutru 24.9. (d1) | SSIM | 0.019 | 0.002 | 38/39 | +1.62/+1.74 dB, ostrina +50/+89 % | a=q1_d1, b=q_lanac_d1 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema jutru 24.9. (d1) | ostrina | 0.078 | 0.007 | 39/39 | +1.62/+1.74 dB, ostrina +50/+89 % | a=q1_d1, b=q_lanac_d1 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema cistom mjerenju prije ubrzanja treninga (d1) | PSNR dB | 0.270 | 0.139 | 20/39 | PSNR +0.35/+0.27, ostrina +12/+21 %, ali SSIM -0.005/-0.029 (4K 0/39) - provjeriti (samokalibracija f 4402 k1 0 prema 4259 -0.025) | a=q_cist_d1, b=q_lanac_d1 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema cistom mjerenju prije ubrzanja treninga (d1) | SSIM | -0.029 | 0.001 | 0/39 | PSNR +0.35/+0.27, ostrina +12/+21 %, ali SSIM -0.005/-0.029 (4K 0/39) - provjeriti (samokalibracija f 4402 k1 0 prema 4259 -0.025) | a=q_cist_d1, b=q_lanac_d1 |
| 2026-09-25 15:53 | C0257 | cijeli lanac sada prema cistom mjerenju prije ubrzanja treninga (d1) | ostrina | 0.028 | 0.003 | 39/39 | PSNR +0.35/+0.27, ostrina +12/+21 %, ali SSIM -0.005/-0.029 (4K 0/39) - provjeriti (samokalibracija f 4402 k1 0 prema 4259 -0.025) | a=q_cist_d1, b=q_lanac_d1 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema jutru 24.9. (d2) | PSNR dB | 1.418 | 0.245 | 32/39 | +1.42/+1.62 dB, SSIM 39/39, ostrina +49/+97 % | a=q1_d2, b=q_lanac2_d2 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema jutru 24.9. (d2) | SSIM | 0.032 | 0.003 | 39/39 | +1.42/+1.62 dB, SSIM 39/39, ostrina +49/+97 % | a=q1_d2, b=q_lanac2_d2 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema jutru 24.9. (d2) | ostrina | 0.146 | 0.018 | 37/39 | +1.42/+1.62 dB, SSIM 39/39, ostrina +49/+97 % | a=q1_d2, b=q_lanac2_d2 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema cistom mjerenju prije ubrzanja (d2) | PSNR dB | 0.150 | 0.099 | 24/39 | sve tri mjere bolje, 46.8 -> 31.2 min | a=q_cist_d2, b=q_lanac2_d2 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema cistom mjerenju prije ubrzanja (d2) | SSIM | 0.004 | 0.001 | 26/39 | sve tri mjere bolje, 46.8 -> 31.2 min | a=q_cist_d2, b=q_lanac2_d2 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema cistom mjerenju prije ubrzanja (d2) | ostrina | 0.045 | 0.005 | 38/39 | sve tri mjere bolje, 46.8 -> 31.2 min | a=q_cist_d2, b=q_lanac2_d2 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema jutru 24.9. (d1) | PSNR dB | 1.620 | 0.228 | 36/39 | +1.42/+1.62 dB, SSIM 39/39, ostrina +49/+97 % | a=q1_d1, b=q_lanac2_d1 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema jutru 24.9. (d1) | SSIM | 0.049 | 0.002 | 39/39 | +1.42/+1.62 dB, SSIM 39/39, ostrina +49/+97 % | a=q1_d1, b=q_lanac2_d1 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema jutru 24.9. (d1) | ostrina | 0.085 | 0.008 | 39/39 | +1.42/+1.62 dB, SSIM 39/39, ostrina +49/+97 % | a=q1_d1, b=q_lanac2_d1 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema cistom mjerenju prije ubrzanja (d1) | PSNR dB | 0.152 | 0.097 | 24/39 | sve tri mjere bolje, 46.8 -> 31.2 min | a=q_cist_d1, b=q_lanac2_d1 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema cistom mjerenju prije ubrzanja (d1) | SSIM | 0.002 | 0.001 | 23/39 | sve tri mjere bolje, 46.8 -> 31.2 min | a=q_cist_d1, b=q_lanac2_d1 |
| 2026-09-25 17:19 | C0257 | cijeli lanac (popravljen) prema cistom mjerenju prije ubrzanja (d1) | ostrina | 0.035 | 0.003 | 39/39 | sve tri mjere bolje, 46.8 -> 31.2 min | a=q_cist_d1, b=q_lanac2_d1 |
| 2026-09-25 18:11 |  | C0255: sada prema jutru (d2) | PSNR dB | -7.141 | 0.973 | 0/21 |  | a=q_c55j_d2, b=q_c55s_d2 |
| 2026-09-25 18:11 |  | C0255: sada prema jutru (d2) | SSIM | -0.106 | 0.009 | 0/21 |  | a=q_c55j_d2, b=q_c55s_d2 |
| 2026-09-25 18:11 |  | C0255: sada prema jutru (d2) | ostrina | -0.250 | 0.058 | 7/21 |  | a=q_c55j_d2, b=q_c55s_d2 |
| 2026-09-25 18:11 |  | C0255: sada prema jutru (d1) | PSNR dB | -7.112 | 0.956 | 0/21 |  | a=q_c55j_d1, b=q_c55s_d1 |
| 2026-09-25 18:11 |  | C0255: sada prema jutru (d1) | SSIM | -0.083 | 0.008 | 0/21 |  | a=q_c55j_d1, b=q_c55s_d1 |
| 2026-09-25 18:11 |  | C0255: sada prema jutru (d1) | ostrina | -0.084 | 0.017 | 5/21 |  | a=q_c55j_d1, b=q_c55s_d1 |
| 2026-09-25 18:47 |  | C0255: popravljeni solve prema jutru (d2) | PSNR dB | 4.178 | 0.831 | 16/21 |  | a=q_c55j_d2, b=q_c55f_d2 |
| 2026-09-25 18:47 |  | C0255: popravljeni solve prema jutru (d2) | SSIM | 0.022 | 0.005 | 17/21 |  | a=q_c55j_d2, b=q_c55f_d2 |
| 2026-09-25 18:47 |  | C0255: popravljeni solve prema jutru (d2) | ostrina | 0.144 | 0.060 | 13/21 |  | a=q_c55j_d2, b=q_c55f_d2 |
| 2026-09-25 18:47 |  | C0255: popravljeni solve prema jutru (d1) | PSNR dB | 4.138 | 0.821 | 16/21 |  | a=q_c55j_d1, b=q_c55f_d1 |
| 2026-09-25 18:47 |  | C0255: popravljeni solve prema jutru (d1) | SSIM | 0.017 | 0.004 | 17/21 |  | a=q_c55j_d1, b=q_c55f_d1 |
| 2026-09-25 18:47 |  | C0255: popravljeni solve prema jutru (d1) | ostrina | 0.061 | 0.017 | 16/21 |  | a=q_c55j_d1, b=q_c55f_d1 |
| 2026-09-25 20:47 |  | E14 ispravak poza prema zadanom (d2) | PSNR dB | 0.683 | 0.249 | 24/39 |  | a=q_lanac2_d2, b=q_e14_d2 |
| 2026-09-25 20:47 |  | E14 ispravak poza prema zadanom (d2) | SSIM | -0.017 | 0.004 | 9/39 |  | a=q_lanac2_d2, b=q_e14_d2 |
| 2026-09-25 20:47 |  | E14 ispravak poza prema zadanom (d2) | ostrina | -0.308 | 0.020 | 0/39 |  | a=q_lanac2_d2, b=q_e14_d2 |
| 2026-09-25 20:47 |  | E14 ispravak poza prema zadanom (d1) | PSNR dB | 0.674 | 0.244 | 24/39 |  | a=q_lanac2_d1, b=q_e14_d1 |
| 2026-09-25 20:47 |  | E14 ispravak poza prema zadanom (d1) | SSIM | -0.005 | 0.002 | 12/39 |  | a=q_lanac2_d1, b=q_e14_d1 |
| 2026-09-25 20:47 |  | E14 ispravak poza prema zadanom (d1) | ostrina | -0.144 | 0.010 | 0/39 |  | a=q_lanac2_d1, b=q_e14_d1 |
| 2026-09-25 21:09 |  | E15: povrsina u treningu (0.1, plosnatost 0.1, od 7000) prema zadanom (d2) | PSNR dB | -1.032 | 0.092 | 1/39 |  | a=q_lanac2_d2, b=q_e15_surface_d2 |
| 2026-09-25 21:09 |  | E15: povrsina u treningu (0.1, plosnatost 0.1, od 7000) prema zadanom (d2) | SSIM | -0.018 | 0.001 | 0/39 |  | a=q_lanac2_d2, b=q_e15_surface_d2 |
| 2026-09-25 21:09 |  | E15: povrsina u treningu (0.1, plosnatost 0.1, od 7000) prema zadanom (d2) | ostrina | -0.114 | 0.013 | 2/39 |  | a=q_lanac2_d2, b=q_e15_surface_d2 |
| 2026-09-25 21:09 |  | E15: povrsina u treningu (0.1, plosnatost 0.1, od 7000) prema zadanom (d1) | PSNR dB | -1.003 | 0.092 | 1/39 |  | a=q_lanac2_d1, b=q_e15_surface_d1 |
| 2026-09-25 21:09 |  | E15: povrsina u treningu (0.1, plosnatost 0.1, od 7000) prema zadanom (d1) | SSIM | -0.009 | 0.001 | 1/39 |  | a=q_lanac2_d1, b=q_e15_surface_d1 |
| 2026-09-25 21:09 |  | E15: povrsina u treningu (0.1, plosnatost 0.1, od 7000) prema zadanom (d1) | ostrina | -0.030 | 0.008 | 8/39 |  | a=q_lanac2_d1, b=q_e15_surface_d1 |
| 2026-09-25 21:33 |  | E16: bilateralna mreza umjesto ekspozicije prema zadanom (d2) | PSNR dB | -0.019 | 0.128 | 20/39 |  | a=q_lanac2_d2, b=q_e16_bilagrid_d2 |
| 2026-09-25 21:33 |  | E16: bilateralna mreza umjesto ekspozicije prema zadanom (d2) | SSIM | 0.002 | 0.001 | 23/39 |  | a=q_lanac2_d2, b=q_e16_bilagrid_d2 |
| 2026-09-25 21:33 |  | E16: bilateralna mreza umjesto ekspozicije prema zadanom (d2) | ostrina | -0.018 | 0.009 | 18/39 |  | a=q_lanac2_d2, b=q_e16_bilagrid_d2 |
| 2026-09-25 21:33 |  | E16: bilateralna mreza umjesto ekspozicije prema zadanom (d1) | PSNR dB | -0.020 | 0.126 | 20/39 |  | a=q_lanac2_d1, b=q_e16_bilagrid_d1 |
| 2026-09-25 21:33 |  | E16: bilateralna mreza umjesto ekspozicije prema zadanom (d1) | SSIM | 0.000 | 0.001 | 20/39 |  | a=q_lanac2_d1, b=q_e16_bilagrid_d1 |
| 2026-09-25 21:33 |  | E16: bilateralna mreza umjesto ekspozicije prema zadanom (d1) | ostrina | -0.004 | 0.004 | 20/39 |  | a=q_lanac2_d1, b=q_e16_bilagrid_d1 |
| 2026-09-25 22:57 |  | E14b: ispravak poza, rijetki Adam 1e-5 od 3000 prema zadanom (d2) | PSNR dB | 0.243 | 0.082 | 29/39 |  | a=q_lanac2_d2, b=q_e14b_pose_d2 |
| 2026-09-25 22:57 |  | E14b: ispravak poza, rijetki Adam 1e-5 od 3000 prema zadanom (d2) | SSIM | -0.000 | 0.001 | 22/39 |  | a=q_lanac2_d2, b=q_e14b_pose_d2 |
| 2026-09-25 22:57 |  | E14b: ispravak poza, rijetki Adam 1e-5 od 3000 prema zadanom (d2) | ostrina | -0.005 | 0.004 | 20/39 |  | a=q_lanac2_d2, b=q_e14b_pose_d2 |
| 2026-09-25 22:57 |  | E14b: ispravak poza, rijetki Adam 1e-5 od 3000 prema zadanom (d1) | PSNR dB | 0.235 | 0.080 | 29/39 |  | a=q_lanac2_d1, b=q_e14b_pose_d1 |
| 2026-09-25 22:57 |  | E14b: ispravak poza, rijetki Adam 1e-5 od 3000 prema zadanom (d1) | SSIM | 0.000 | 0.000 | 23/39 |  | a=q_lanac2_d1, b=q_e14b_pose_d1 |
| 2026-09-25 22:57 |  | E14b: ispravak poza, rijetki Adam 1e-5 od 3000 prema zadanom (d1) | ostrina | -0.000 | 0.002 | 20/39 |  | a=q_lanac2_d1, b=q_e14b_pose_d1 |
| 2026-09-25 23:15 |  | E17: trening kadrovi zamijenjeni najostrijim susjedom (+-4) s pozom iz punih slicica prema zadanom (d2) | PSNR dB | 0.057 | 0.099 | 23/39 |  | a=q_lanac2_d2, b=q_e17_ostri_d2 |
| 2026-09-25 23:15 |  | E17: trening kadrovi zamijenjeni najostrijim susjedom (+-4) s pozom iz punih slicica prema zadanom (d2) | SSIM | 0.001 | 0.002 | 20/39 |  | a=q_lanac2_d2, b=q_e17_ostri_d2 |
| 2026-09-25 23:15 |  | E17: trening kadrovi zamijenjeni najostrijim susjedom (+-4) s pozom iz punih slicica prema zadanom (d2) | ostrina | 0.123 | 0.013 | 39/39 |  | a=q_lanac2_d2, b=q_e17_ostri_d2 |
| 2026-09-25 23:15 |  | E17: trening kadrovi zamijenjeni najostrijim susjedom (+-4) s pozom iz punih slicica prema zadanom (d1) | PSNR dB | 0.049 | 0.098 | 23/39 |  | a=q_lanac2_d1, b=q_e17_ostri_d1 |
| 2026-09-25 23:15 |  | E17: trening kadrovi zamijenjeni najostrijim susjedom (+-4) s pozom iz punih slicica prema zadanom (d1) | SSIM | -0.001 | 0.001 | 16/39 |  | a=q_lanac2_d1, b=q_e17_ostri_d1 |
| 2026-09-25 23:15 |  | E17: trening kadrovi zamijenjeni najostrijim susjedom (+-4) s pozom iz punih slicica prema zadanom (d1) | ostrina | 0.083 | 0.010 | 39/39 |  | a=q_lanac2_d1, b=q_e17_ostri_d1 |

## dekodiranje

| datum | snimka | opis | vrijeme_s | ostalo |
|---|---|---|---|---|
| 2026-09-24 18:30 | C0257 | ffmpeg, 1 nit | 96.70 |  |
| 2026-09-24 18:30 | C0257 | ffmpeg, niti | 12.40 |  |
| 2026-09-24 18:30 | C0257 | NVDEC | 13.40 |  |
| 2026-09-24 18:30 | C0257 | Spool s nitima | 31.20 |  |

## pune_slicice

| datum | snimka | opis | vrijeme_s | drift_kut_medijan | drift_kut_p90 | drift_polozaj_medijan_posto | drift_polozaj_p90_posto | odluka | ostalo |
|---|---|---|---|---|---|---|---|---|---|
| 2026-09-25 00:35 | C0257 | --dense-points 0 (60 kljucnih, 532 medjukadra) | 104.30 | 0.049 | 0.103 | 2.270 | 9.080 | polaziste (izlaz isti do bajta) |  |
| 2026-09-25 00:35 | C0257 | --dense-points 4000 (60 kljucnih, 532 medjukadra) | 38.30 | 0.049 | 0.120 | 2.350 | 10.73 | medijan isti, p90 +16 %; 2.7x brze - opcija |  |
| 2026-09-25 00:35 | C0257 | --dense-points 2000 (60 kljucnih, 532 medjukadra) | 22.20 | 0.051 | 0.131 | 2.410 | 10.80 | p90 +27 % |  |
| 2026-09-25 00:35 | C0257 | --dense-points 1000 (60 kljucnih, 532 medjukadra) | 13.40 | 0.062 | 0.146 | 3.600 | 12.78 | losije |  |

## odluka

| datum | snimka | opis | ostalo |
|---|---|---|---|
| 2026-09-25 20:48 | C0257 | E14 ispravak poza (Adam 1e-4 od 500): odbaceno - PSNR +0.68 dB ali ostrina 0.44->0.14 (1080p), 0.17->0.03 (4K); zakret medijan 0.42 st, poze odlutale |  |
| 2026-09-25 21:10 | C0257 | E15 povrsina u treningu (normale 0.1, plosnatost 0.1 od 7000): odbaceno - PSNR -1.03 dB (38/39 losije), ostrina 0.44->0.33, 21 min umjesto 16 |  |
| 2026-09-25 21:34 | C0257 | E16 bilateralna mreza umjesto ekspozicije: odbaceno - PSNR -0.02+-0.13 (sum), ostrina -0.018, trening 24 min umjesto 16 |  |
| 2026-09-25 21:45 | C0255 | pune slicice s 6 razina piramide: isto 1121/1772 (63.3 %), drift p90 1.14->1.98 st - odbaceno; neuspjeli medjukadrovi su odsjecci od 70 neriješenih kljucnih kadrova, ne raspon pracenja |  |
| 2026-09-25 22:08 | C0255 | pune slicice: lazne poze nerijesenih kljucnih kadrova uklonjene (70 na C0255); promasaj lanca raspodijeljen po odsjecku - skok na granici najveci 1.45->0.145, p90 0.098->0.036 (unutar odsjecka 0.023) |  |
| 2026-09-25 22:13 | C0255 | makro dio (1250-1960) sam za sebe: 35/70 kamera, omjer izdvojenih 2.88, samokalibracija 84 st odbijena - snimka preslaba za klasicni solve (bijelo bez teksture, mala dubinska ostrina); treba nauceni prior |  |
| 2026-09-25 23:01 | C0257 | E14b ispravak poza (rijetki Adam 1e-5 od 3000): PSNR +0.24+-0.08 dB (29/39) na 1080p i 4K, SSIM i ostrina isti - ZADANO |  |
| 2026-09-25 23:16 | C0257 | E17 najostriji susjed (+-4) s pozom iz punih slicica: ostrina +28 % (1080p) i +48 % (4K), bolje na 39/39, PSNR +0.05 (sum) - ugradjuje se u VideoSolve |  |
| 2026-09-26 00:10 |  | MotionQuality: najbolja od Kimodovih varijanti bira se sama (klizanje, propadanje, lebdenje, trzaj, skok kukova, ruta); na 7 generiranja najgora varijanta je 1.3-5.5x losija od najbolje, a 3 od 4 u motion_1790364325023 lebde 12-27 cm - ZADANO u editoru |  |

## pokret

| datum | snimka | opis | ostalo |
|---|---|---|---|
| 2026-09-26 00:10 | motion_1790364325023 | A person walks along the path | varijanti=4, najbolja=02, ocjena_najbolje=3.110, ocjena_najgore=17.11, skate_cm_s=11.40, propada_cm=0.000, trzaj=183.20, lebdi_cm=1.880, mjera=MotionScore (Engine/MotionQuality) |
| 2026-09-26 00:10 | motion_1790369311054 | A person runs forward with an athletic stride and coordinated arm swing / A person pulls o | varijanti=4, najbolja=00, ocjena_najbolje=3.100, ocjena_najgore=6.430, skate_cm_s=11.69, propada_cm=1.190, trzaj=179.80, lebdi_cm=0.000, mjera=MotionScore (Engine/MotionQuality) |
| 2026-09-26 00:10 | motion_1790368235350 | A person crawls forward on hands and knees close to the floor with steady contacts | varijanti=4, najbolja=02, ocjena_najbolje=3.210, ocjena_najgore=4.140, skate_cm_s=15.58, propada_cm=2.080, trzaj=66.50, lebdi_cm=0.030, mjera=MotionScore (Engine/MotionQuality) |
| 2026-09-26 00:10 | motion_1790357086676 | A person runs forward with an athletic stride and coordinated arm swing | varijanti=4, najbolja=03, ocjena_najbolje=3.260, ocjena_najgore=5.360, skate_cm_s=11.78, propada_cm=0.010, trzaj=253.40, lebdi_cm=0.690, mjera=MotionScore (Engine/MotionQuality) |
| 2026-09-26 00:10 | motion_1790295931355 | A person dances happily | varijanti=4, najbolja=00, ocjena_najbolje=1.820, ocjena_najgore=3.390, skate_cm_s=9.810, propada_cm=0.020, trzaj=100.20, lebdi_cm=0.000, mjera=MotionScore (Engine/MotionQuality) |
| 2026-09-26 00:10 | motion_1790367917330 | A person walks backward along the path | varijanti=4, najbolja=01, ocjena_najbolje=2.170, ocjena_najgore=4.590, skate_cm_s=8.920, propada_cm=0.630, trzaj=80.20, lebdi_cm=1.060, mjera=MotionScore (Engine/MotionQuality) |
| 2026-09-26 00:10 | motion_1790358021365 | A person walks forward at a relaxed pace with a natural arm swing | varijanti=4, najbolja=03, ocjena_najbolje=3.710, ocjena_najgore=5.620, skate_cm_s=31.26, propada_cm=0.050, trzaj=60.10, lebdi_cm=0.000, mjera=MotionScore (Engine/MotionQuality) |
| 2026-09-26 01:06 | motion_1790364325023_02 na Mascot_Manny | pose blend: upperarm_r +40 st u kadrovima 20 i 60, 25 fps scena, 30 Hz klip | mjera=realblend (FK prema originalu, korak 0.1 kadra), prije_zglobova=61, prije_noga_m=0.246, prije_stopalo_m=0.566, prije_skok_ruke_st=38.22, poslije_zglobova=1, poslije_noga_m=0.000, poslije_stopalo_m=0.000, poslije_skok_ruke_st=0.000, odluka=zadrzano: aditivni ispravak samo diranih zglobova, na vremenima postojecih kljuceva |
| 2026-09-26 01:19 | motion_1790364325023_02 na Mascot_Manny | pose blend, desna ruka (upperarm +35 st, lowerarm -25 st) u kadrovima 20 i 60: odstupanje sake od crte izmedju kljuceva u sustavu prsa | mjera=realblend2, samo_kutovi_mm=383.50, ik_sake_mm=5.210, ik_na_kljucevima=0 mm osim kadra 55.17 (ruka ispruzena, cilj izvan dosega), noga_mm=0.000, odluka=zadrzano: sake po polozaju u sustavu prsa + IK |
| 2026-09-26 01:41 | motion_1790364325023_02 -> motion_1790379449333 | dio takea iznova (kadrovi 36-82 od 120), iz editora, 4 varijante, 200 koraka | mjera=kimodo_range + MotionScore, vrijeme_s=43, izvan_raspona_m=0.000, rub_korak_m=0.137, izvor_najveci_korak_m=0.243, sredina_promjena_medijan_m=0.270, ocjena_izvor=3.110, ocjena_najbolja=2.430, odluka=zadrzano: fullbody ogranicenja izvan raspona + spoj s pretapanjem 4 kadra |
| 2026-09-26 05:03 |  | Kimodo servis (modeli u memoriji): 4 varijante, 200 koraka, 120 kadrova | mjera=zidni sat, izravno_s=44, servis_prvi_s=49.90, servis_sljedeci_s=20.30, iz_editora_s=19.9 / 17.9, isti_izlaz=0.00 m razlike uz isto sjeme, ram_gb=14.90, odluka=zadrzano: editor generira kroz kimodo_service.py, radnik se gasi nakon 20 min bez posla |
| 2026-09-26 05:03 |  | foot IK: lijepljenje stopala u skoku (stopalo > 5 cm iznad poda bez IK-a, < 2 cm s IK-om) | mjera=glue probe na Mascot_Manny, prije=6 od 8 commitanih varijanti skoka, najvise 13 kadrova do 10 cm (motion_1790369311054_00); tvoj take motion_1790382707996_01: 27 kadrova, poslije=0 na svih 8 i na tvom takeu, klizanje_hod_prije_poslije=5.7 -> 4.0 cm/s, odluka=zadrzano: zakljucava se samo vodoravno, prag dodira 2 % visine, pustanje kroz 3 kadra |
| 2026-09-26 13:22 | motion_1790419879047_03 | Replace dijela takea (backflip, kadrovi 137-232 od 266) s 'Person points a gun while crouched' | mjera=kimodo_range + posed_joints na SOMA77, 4 varijante, jedan_opis_kukovi_m=0.99 (lik stoji), tri_opisa_kukovi_m=0.42-0.61 (cucne), izvan_raspona_m=0.000, rub_kraj_fiksno_m=0.303-0.387, rub_kraj_prilagodjeno_m=0.101-0.105, izvor_najveci_korak_uz_rub_m=0.092, napomena=sirovo generirano rekonstruirano iz cistog dijela raspona (cucanj miruje ~1 mm/kadar), odluka=zadrzano: opis samo na raspon, izvorni opisi prije/poslije; pretapanje ruba po razmaku (~3 cm/kadar, do trecine raspona) |

