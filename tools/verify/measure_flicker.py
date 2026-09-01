#!/usr/bin/env python3
"""Titranje kroz kadrove: koliko se izlaz mijenja vise nego sto se scena stvarno promijenila.

Cemu: sve dosad izmjereno o ubacenom svjetlu je JEDAN kadar. Dubina se procjenjuje po kadru,
pa ako model titra, titraju i normale, i sjena, i okluzija - a to se na jednoj slici ne vidi.

Mjeri se na dvije regije koje maska daje besplatno, i one su ovdje cijeli trik:

    zid       piksel koji je pozadina u OBA susjedna kadra. Zid se ne mice, pa je svaka
              promjena tocne dubine tamo nula - i sve sto model tamo pokaze je izmisljeno
    subjekt   piksel koji je subjekt u oba. Tu se scena stvarno mijenja, pa se ne pita ima li
              promjene nego koliko je veca od istinite

Pojas koji subjekt otkriva dok se mice namjerno ispada iz obje regije: tamo se i tocna dubina
mijenja, pa bi mjerenje mijesalo otkrivanje s titranjem.

    measure_flicker.py --plates 'niz/p_0*.png' --masks 'niz/p_*_truth_mask.png' \
        --depth tocna='niz/p_*_truth.pfm' --depth model='niz/dubina/depth_*.pfm' \
        --relight tocna='niz/out/r_*.png' --relight model='niz/est/e_*.png' \
        --near 1.21 --far 3.20
"""
import argparse, glob, sys

import numpy


REC709 = numpy.array([0.2126, 0.7152, 0.0722])


def read_pfm(path):
    with open(path, "rb") as f:
        if f.readline().strip() != b"Pf":
            raise ValueError("%s nije jednokanalni PFM" % path)
        width, height = map(int, f.readline().split())
        scale = float(f.readline())
        order = "<f4" if scale < 0 else ">f4"
        data = numpy.frombuffer(f.read(width * height * 4), dtype=order).reshape(height, width)
        return data[::-1].copy()


def read_luma(path):
    from PIL import Image
    srgb = numpy.asarray(Image.open(path).convert("RGB"), dtype=numpy.float64) / 255.0

    #U linearno prije ikakvog oduzimanja: razlika bajtova nije razlika svjetla, a titranje se
    #mjeri u svjetlu
    linear = numpy.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
    return linear @ REC709


def read_mask(path):
    from PIL import Image
    return numpy.asarray(Image.open(path).convert("L")) > 127


#Globovi se siruju rukom jer maske i dubine stoje uz same kadrove: 'p_*.png' bi pokupio i
#p_0000_truth_mask.png, sto bi tiho udvostrucilo niz
def sequence(pattern, drop="_truth"):
    files = sorted(f for f in glob.glob(pattern) if drop is None or drop not in f)
    if not files:
        raise SystemExit("nijedan file ne odgovara: %s" % pattern)
    return files


def named(values):
    out = []
    for value in values or []:
        if "=" not in value:
            raise SystemExit("ocekujem naziv=glob, dobio: %s" % value)
        name, pattern = value.split("=", 1)
        out.append((name, pattern))
    return out


#Srednja razlika izmedu susjednih kadrova, samo na pikselima koje regija pusta
def stepwise(frames, region):
    return numpy.array([numpy.abs(frames[i + 1] - frames[i])[region(i)].mean()
                        for i in range(len(frames) - 1)])


def metres(disparity, offset, near, far):
    inverse = lambda d: 1.0 / far + d * (1.0 / near - 1.0 / far)
    return abs(1.0 / inverse(offset + disparity) - 1.0 / inverse(offset))


def main():
    parser = argparse.ArgumentParser(description="koliko izlaz titra iznad onoga sto se stvarno mijenja")
    parser.add_argument("--plates", required=True, help="glob kadrova snimke")
    parser.add_argument("--masks", required=True, help="glob maski subjekta (tocnih)")
    parser.add_argument("--depth", action="append", help="naziv=glob niza dubina (PFM)")
    parser.add_argument("--relight", action="append", help="naziv=glob osvijetljenih kadrova")
    parser.add_argument("--near", type=float, default=0.0, help="za pretvorbu dispariteta u metre")
    parser.add_argument("--far", type=float, default=0.0)
    args = parser.parse_args()

    plates = [read_luma(f) for f in sequence(args.plates)]
    masks = [read_mask(f) for f in sequence(args.masks, drop=None)]
    if len(plates) != len(masks):
        raise SystemExit("kadrova %d, maski %d" % (len(plates), len(masks)))
    if len(plates) < 2:
        raise SystemExit("titranje trazi barem dva kadra")

    #Jedini piksel na kojem se smije tvrditi "ovdje se nista nije promijenilo" je onaj koji je
    #pozadina u oba kadra. Sve ostalo je subjekt, ili pojas koji je otkrio
    wall = lambda i: (~masks[i]) & (~masks[i + 1])
    person = lambda i: masks[i] & masks[i + 1]
    regions = (("zid (stoji)", wall), ("subjekt (mice se)", person))

    print("kadrova: %d\n" % len(plates))

    base = {name: stepwise(plates, region) for name, region in regions}
    print("SNIMKA - koliko se sam ulaz mijenja")
    for name, _ in regions:
        print("  %-18s %.6f" % (name, base[name].mean()))

    for label, pattern in named(args.depth):
        frames = [read_pfm(f) for f in sequence(pattern, drop=None)]
        print("\nDUBINA '%s' - razlika susjednih kadrova, u disparitetu 0..1" % label)
        for name, region in regions:
            step = stepwise(frames, region)
            line = "  %-18s %.6f   (najgori kadar %.6f)" % (name, step.mean(), step.max())
            if args.near > 0.0 and args.far > args.near:
                #Zid stoji na dnu raspona, subjekt na vrhu - isti disparitet tamo znaci
                #sasvim druge metre, pa se pretvara na mjestu gdje regija stvarno jest
                offset = 0.0 if region is wall else 1.0 - step.mean()
                line += "  = %.1f cm po kadru" % (100.0 * metres(step.mean(), offset, args.near, args.far))
            print(line)

    for label, pattern in named(args.relight):
        frames = [read_luma(f) for f in sequence(pattern, drop=None)]
        print("\nOSVIJETLJENO '%s' - razlika susjednih kadrova, u luminanciji" % label)
        for name, region in regions:
            step = stepwise(frames, region)
            print("  %-18s %.6f   (snimka %.6f)" % (name, step.mean(), base[name].mean()))

    print("\nZid je kontrola: tamo se ulaz ne mijenja, pa je sve iznad snimkine nule ili prava\n"
          "sjena koja se pomakla, ili izmisljeno.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
