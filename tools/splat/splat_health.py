#!/usr/bin/env python3
"""Koliko se splat razisao izvan scene koju je solver rijesio.

ZASTO OVO POSTOJI. Sve nase mjere kvalitete poza racuna ISTI program koji je poze i nasao -
reprojekcija, baza, izdvojena opazanja, polje ostataka. Ovo je prva koja dolazi IZVANA: mjeri se
sto je TRENER splatova napravio s tim pozama, a on o nasem solveru ne zna nista.

Ideja je jednostavna. Kad se poze medjusobno slazu, trener nadje dosljedno 3D objasnjenje i
gaussiani ostanu ondje gdje je scena. Kad se ne slazu, nijedan polozaj ne zadovoljava sve poglede,
pa ih trener sije na sve strane u sve vecim dubinama da svaki kadar posebno izgleda tocno.

MJERI SE OMJER RASPONA, ne apsolutna velicina - mjerilo rekonstrukcije je slobodno, pa bi svaka
apsolutna brojka bila besmislena. Omjer je neovisan o mjerilu.

IZMJERENO na cetiri prava slucaja:

    slucaj                        tocaka   gaussiana   omjer splat/model   u 3x scene
    soba, nase poze (oba_boja)     81639      655247              23.1x        90.0%
    soba, nase poze (rast2)        59879      655247               1.3x        99.5%
    joystick, nase poze            17640      400331            7515.3x        10.8%
    joystick, MapAnythingove       232677     3775883              1.4x        98.9%

Dvije stvari koje te brojke iskljucuju:

  - NIJE mjerilo scene: omjer je bezdimenzijski.
  - NIJE rijetka inicijalizacija. Soba "rast2" ima MANJE pocetnih tocaka od "oba_boja" (59879
    naspram 81639) a bitno BOLJI omjer (1.3x naspram 23.1x). Da je kriva rijetkost, bilo bi
    obrnuto.

Ostaje kvaliteta samog rjesenja - i to se slaze sa svime drugim sto o tim snimkama znamo:
joystick je ona snimka na kojoj je omjer izdvojenih opazanja bio 4.73, COLMAP registrirao 2 od 197
slika, a splat vidljivo krivo izgledao.

ZDRAVO JE OKO 1 DO 2. Preko desetak znaci da trener nije nasao dosljedno objasnjenje, i tada
gledanje splata nije test solvera nego test strpljenja.

    tools/splat/splat_health.py scena.ply model_mapa
"""
import argparse
import sys

import numpy as np


def read_centres(path):
    """Centri gaussiana iz .ply. Cita se samo zaglavlje pa x, y, z - ostalih pedesetak polja
    (sferni harmonici, kovarijanca) ovdje ne trebaju, a nose devedeset posto datoteke."""
    with open(path, "rb") as handle:
        header = b""
        while b"end_header" not in header:
            line = handle.readline()
            if not line:
                raise ValueError(f"{path}: nema end_header, nije .ply")
            header += line

        count = 0
        fields = []
        for line in header.decode("ascii", "ignore").split("\n"):
            if line.startswith("element vertex"):
                count = int(line.split()[-1])
            elif line.startswith("property"):
                parts = line.split()
                if parts[1] != "float":
                    raise ValueError(f"{path}: polje '{parts[-1]}' nije float, citac to ne podrzava")
                fields.append(parts[-1])

        raw = np.frombuffer(handle.read(count * len(fields) * 4), dtype=np.float32)
        table = raw.reshape(count, len(fields))
        return table[:, [fields.index(axis) for axis in ("x", "y", "z")]].astype(np.float64)


def read_points(path):
    """Tocke iz COLMAP-ovog points3D.txt."""
    points = []
    with open(path) as handle:
        for line in handle:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.split()
            points.append([float(parts[1]), float(parts[2]), float(parts[3])])
    if not points:
        raise ValueError(f"{path}: nijedna tocka")
    return np.array(points)


def main():
    parser = argparse.ArgumentParser(description="koliko se splat razisao izvan rijesene scene")
    parser.add_argument("ply", help="scena.ply iz treninga")
    parser.add_argument("model", help="mapa s points3D.txt, ona iz koje je trening krenuo")
    parser.add_argument("--prag", type=float, default=10.0,
                        help="omjer iznad kojeg se javlja upozorenje")
    args = parser.parse_args()

    centres = read_centres(args.ply)
    points = read_points(args.model.rstrip("/") + "/points3D.txt")

    #Krajnji postotci, ne min/max: rekonstrukcija redovito ima pokoju tocku trianguliranu iz
    #gotovo paralelnih zraka, i ona sama pomakne min/max za nekoliko redova velicine
    low, high = np.percentile(points, [1, 99], axis=0)
    centre = (low + high) / 2.0
    half = (high - low) / 2.0

    splatLow, splatHigh = np.percentile(centres, [1, 99], axis=0)
    splatHalf = (splatHigh - splatLow) / 2.0

    ratio = float(np.linalg.norm(splatHalf) / np.linalg.norm(half))
    inside = float(np.all(np.abs(centres - centre) <= half * 3.0, axis=1).mean())

    print(f"  model      {len(points)} tocaka, polustranica {np.round(half, 3)}")
    print(f"  splat      {len(centres)} gaussiana, polustranica {np.round(splatHalf, 3)}")
    print(f"  OMJER      {ratio:.1f}x raspon splata naspram scene  (zdravo je 1 do 2)")
    print(f"  u 3x scene {100.0 * inside:.1f}% gaussiana")

    if ratio > args.prag:
        print(f"             UPOZORENJE: trener nije nasao dosljedno 3D objasnjenje za ove poze.")
        print(f"             Gaussiani su rasprseni {ratio:.0f}x izvan scene - gledanje takvog "
              f"splata ne govori o solveru.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
