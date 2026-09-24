# Mjerenja

Iz `benchmarks/mjerenja.jsonl` (52 zapisa). Slozi se iznova s `tools/bench/mjerenja.py tablica`. Vrijeme je u sekundama; PSNR u dB na IZDVOJENIM kadrovima (koje trening nije vidio); ostrina je energija detalja nacrtanog prema snimljenom (1 = jednako ostro). Usporedba je razlika B - A po istim kadrovima s procjenom pogreske; dva ista treninga razlikuju se do oko 0.12 dB, pa razlika manja od dvije pogreske nije nalaz. Vrijeme treninga i solvea koji su isli istovremeno na kartici nije cisto.

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

## ocjena

| datum | snimka | opis | razlucivost | psnr | ssim | ostrina | kadrova | gaussiana | odluka | ostalo |
|---|---|---|---|---|---|---|---|---|---|---|
| 2026-09-24 03:37 | C0257 | prije ciscenja floatera | 1920x1080 | 24.37 |  |  | 39 | 3631276 |  | datoteka_mb=857 |
| 2026-09-24 03:37 | C0257 | poslije ciscenja floatera (clean_splats) | 1920x1080 | 24.70 |  |  | 39 | 848672 | zadrzano (--clean zadano) | datoteka_mb=200 |
| 2026-09-24 19:25 | C0257 | t_1_stari (polaziste ostrine) | 1920x1080 | 24.48 | 0.774 | 0.297 | 39 |  |  |  |
| 2026-09-24 19:25 | C0257 | t_1_stari (polaziste ostrine) | 3840x2160 | 24.19 | 0.779 | 0.087 | 39 |  |  |  |
| 2026-09-24 19:40 | C0257 | 15000 koraka | 1920x1080 | 24.54 | 0.773 | 0.354 | 39 | 1500000 |  |  |
| 2026-09-24 19:40 | C0257 | 15000 koraka | 3840x2160 | 24.23 | 0.778 | 0.107 | 39 | 1500000 |  |  |

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

## dekodiranje

| datum | snimka | opis | vrijeme_s | ostalo |
|---|---|---|---|---|
| 2026-09-24 18:30 | C0257 | ffmpeg, 1 nit | 96.70 |  |
| 2026-09-24 18:30 | C0257 | ffmpeg, niti | 12.40 |  |
| 2026-09-24 18:30 | C0257 | NVDEC | 13.40 |  |
| 2026-09-24 18:30 | C0257 | Spool s nitima | 31.20 |  |

