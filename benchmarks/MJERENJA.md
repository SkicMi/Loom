# Mjerenja

Iz `benchmarks/mjerenja.jsonl` (78 zapisa). Slozi se iznova s `tools/bench/mjerenja.py tablica`. Vrijeme je u sekundama; PSNR u dB na IZDVOJENIM kadrovima (koje trening nije vidio); ostrina je energija detalja nacrtanog prema snimljenom (1 = jednako ostro). Usporedba je razlika B - A po istim kadrovima s procjenom pogreske; dva ista treninga razlikuju se do oko 0.12 dB, pa razlika manja od dvije pogreske nije nalaz. Vrijeme treninga i solvea koji su isli istovremeno na kartici nije cisto.

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

## dekodiranje

| datum | snimka | opis | vrijeme_s | ostalo |
|---|---|---|---|---|
| 2026-09-24 18:30 | C0257 | ffmpeg, 1 nit | 96.70 |  |
| 2026-09-24 18:30 | C0257 | ffmpeg, niti | 12.40 |  |
| 2026-09-24 18:30 | C0257 | NVDEC | 13.40 |  |
| 2026-09-24 18:30 | C0257 | Spool s nitima | 31.20 |  |

