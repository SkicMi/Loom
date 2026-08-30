#!/usr/bin/env python3
"""Curi li tekstura snimke u kartu dubine.

Pitanje koje odlucuje ima li smisla filtrirati dubinu. Ako model prugice na tkanini ili sare
na tapeti pretvara u reljef, u karti dubine postoji sitni uzorak koji se POKLAPA s uzorkom u
snimci - i tada bilateralno glacanje dubine ima sto raditi. Ako ne postoji, filtriranje je
cista steta: mijenja geometriju, a ono sto se na slici vidi dolazi iz same snimke.

    tools/verify/depth_texture.py --plate p.png --depth p.pfm --mask p_mask.png

Izmjereno na fotografiji covjeka u prugastoj majici, Depth Anything V2 base:

    visoke frekvencije dubine    0.06 % raspona scene
    korelacija s HF snimke       0.040

Dakle NE curi. Prugice koje se na slici vide dolaze iz snimke i tamo im je mjesto - novo
svjetlo ih mnozi, kao sto bi ih mnozilo i pravo svjetlo. Bilateralno glacanje dubine je nad
tom slikom svejedno probano, i mjerenjem odbaceno: visoke frekvencije su pale 8.24 -> 8.08
(dva posto), a mrlje samosjencanja skocile sa 613 na 7137.
"""
import argparse, sys

import numpy


def highFrequency(image, radius=2):
    """Koliko piksel odstupa od prosjeka svoje okoline. Sitni uzorak prezivi, oblik ne."""
    from numpy.lib.stride_tricks import sliding_window_view
    padded = numpy.pad(image, radius, mode="edge")
    window = sliding_window_view(padded, (2 * radius + 1, 2 * radius + 1))
    return numpy.abs(image - window.mean(axis=(-1, -2)))


def erode(mask, radius):
    out = mask.copy()
    for dx in range(-radius, radius + 1):
        for dy in range(-radius, radius + 1):
            out &= numpy.roll(numpy.roll(mask, dx, axis=1), dy, axis=0)
    return out


def main():
    parser = argparse.ArgumentParser(description="curi li tekstura u dubinu")
    parser.add_argument("--plate", required=True)
    parser.add_argument("--depth", required=True)
    parser.add_argument("--mask", help="gdje mjeriti; bez nje se mjeri cijeli kadar")
    parser.add_argument("--radius", type=int, default=2)
    args = parser.parse_args()

    from PIL import Image
    sys.path.insert(0, "tools/segment")
    from segment import read_pfm

    values, width, height = read_pfm(args.depth)
    depth = numpy.array(values, dtype="float64").reshape(height, width)

    plate = numpy.asarray(Image.open(args.plate).convert("RGB"), dtype="float64").mean(axis=2) / 255.0
    if plate.shape != depth.shape:
        sys.exit("snimka je %s, dubina %s - moraju biti piksel na piksel"
                 % (plate.shape, depth.shape))

    where = numpy.ones_like(depth, dtype=bool)
    if args.mask:
        #Rub se erodira: tamo je skok dubine stvaran i nosio bi cijelu mjeru
        where = erode(numpy.array(Image.open(args.mask)) > 127, 8)

    depthHigh = highFrequency(depth, args.radius)
    plateHigh = highFrequency(plate, args.radius)

    span = float(depth.max() - depth.min())
    correlation = float(numpy.corrcoef(depthHigh[where], plateHigh[where])[0, 1])

    print("mjereno na %d piksela" % int(where.sum()))
    print("  visoke frekvencije dubine   %.5f   (%.2f %% raspona scene)"
          % (depthHigh[where].mean(), 100.0 * depthHigh[where].mean() / max(span, 1e-9)))
    print("  visoke frekvencije snimke   %.5f" % plateHigh[where].mean())
    print("  korelacija                  %.3f" % correlation)
    print()

    if correlation > 0.25:
        print("TEKSTURA CURI U DUBINU. Bilateralno glacanje dubine ima sto raditi.")
    else:
        print("NE CURI. Ono sto se na slici vidi dolazi iz same snimke, i filtriranje")
        print("dubine bi samo pomaknulo geometriju bez razloga.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
