#!/usr/bin/env python3
"""Sinteticki portret - stalak za mjerenje, dok ne dode prava fotka.

Cemu: nad pravom fotkom ne postoji nista sto bi reklo gdje je subjekt, koliko je dalek, ni
gdje mu je silueta. Nad ovom scenom postoji sve troje, jer je scena izracunata a ne snimljena.
Zato ovaj portret dolazi S ODGOVOROM: uz sliku se zapisuju i tocna dubina i tocna maska, pa
se izlaz modela ima s cime usporediti.

    python3 make_portrait.py -o portret

    portret.png              slika - ono sto model vidi
    portret_truth.pfm        tocna dubina, u istom zapisu kao estimate_depth.py (0..1)
    portret_truth_mask.png   tocna maska subjekta
    portret_truth.txt        geometrija u metrima

STO OVO NE DOKAZUJE, i to treba stajati napisano: da Depth Anything i SAM2 rade na pravoj
kozi, kosi i tkanini. Kugla pred zidom je za oba modela lakSi zadatak od covjeka. Ovime se
dokazuje da LANAC radi i da mjerenje hvata ono sto tvrdi - a to je jedino sto se bez prave
fotke uopce moze dokazati.

Zraka se prati na procesoru, namjerno bez Looma. Da je scena nacrtana istim rendererom koji
je poslije i osvjetljava, greske bi se mogle pokratiti; ovako je plate potpuno drugi kod.
"""
import argparse, math, os, struct, sys

import numpy


#Scena, u metrima i u view prostoru: kamera je u ishodistu i gleda niz -z
HEAD = ((0.0, 0.13, -1.40), 0.115)                      #srediste, polumjer
TORSO = ((0.0, -0.42, -1.44), (0.30, 0.34, 0.20))       #srediste, poluosi

#Vrat postoji zbog SAM2, ne zbog ljepote: bez njega su glava i tijelo dva odvojena predmeta,
#pa jedan klik uhvati samo glavu - i maska bi zaostajala za istinom nasom krivnjom, ne
#modelovom
NECK = ((0.0, -0.03, -1.41), (0.058, 0.10, 0.058))
BACKDROP_Z = -3.20
FOV_Y = math.radians(40.0)

#Kljucno svjetlo koje je scenu vec osvijetlilo. Nije ono koje Loom poslije ubacuje - ovo je
#"kako je snimljeno", a Loom dodaje jos jedno
KEY = (-0.45, 0.55, 0.70)
AMBIENT = 0.28


def sphere_hit(origin, direction, centre, radius):
    """Najbliza pozitivna udaljenost do kugle, ili beskonacno. Vektorizirano po pikselima."""
    offset = origin - numpy.asarray(centre)
    b = 2.0 * (direction * offset).sum(axis=-1)
    c = (offset * offset).sum(axis=-1) - radius * radius

    discriminant = b * b - 4.0 * c
    hit = discriminant >= 0.0

    root = numpy.sqrt(numpy.where(hit, discriminant, 0.0))
    t = numpy.where(hit, (-b - root) * 0.5, numpy.inf)
    return numpy.where(t > 1e-4, t, numpy.inf)


def ellipsoid_hit(origin, direction, centre, radii):
    """Elipsoid je kugla u razvucenom prostoru: podijeli se zraka s poluosima, presjek se
    trazi s jedinicnom kuglom, a dobiveni t vrijedi i u pravom prostoru jer je dijeljenje
    linearno."""
    scale = numpy.asarray(radii)
    o = (origin - numpy.asarray(centre)) / scale
    d = direction / scale

    a = (d * d).sum(axis=-1)
    b = 2.0 * (d * o).sum(axis=-1)
    c = (o * o).sum(axis=-1) - 1.0

    discriminant = b * b - 4.0 * a * c
    hit = discriminant >= 0.0

    root = numpy.sqrt(numpy.where(hit, discriminant, 0.0))
    t = numpy.where(hit, (-b - root) / (2.0 * a), numpy.inf)
    return numpy.where(t > 1e-4, t, numpy.inf)


def write_pfm(path, values, width, height):
    """Jedan kanal, float, little endian. PFM broji retke ODOZDO - isto kao estimate_depth.py,
    inace se dvije karte iste scene ne bi dale usporediti."""
    with open(path, "wb") as f:
        f.write(b"Pf\n%d %d\n-1.0\n" % (width, height))
        for y in range(height - 1, -1, -1):
            f.write(struct.pack("<%df" % width, *values[y].tolist()))


def render(width, height):
    fy = (height * 0.5) / math.tan(FOV_Y * 0.5)
    fx = fy
    cx, cy = width * 0.5, height * 0.5

    xs = (numpy.arange(width) + 0.5 - cx) / fx
    ys = (numpy.arange(height) + 0.5 - cy) / fy

    #Redak 0 je vrh slike, a view prostor ima Y prema gore - zato minus. Bez njega bi portret
    #ispao naopako, a to je greska koju bi kugla u sredini kadra sakrila
    grid = numpy.stack(numpy.meshgrid(xs, -ys), axis=-1)
    direction = numpy.concatenate([grid, numpy.full(grid.shape[:2] + (1,), -1.0)], axis=-1)
    direction /= numpy.linalg.norm(direction, axis=-1, keepdims=True)

    origin = numpy.zeros(3)

    tHead = sphere_hit(origin, direction, HEAD[0], HEAD[1])
    tNeck = ellipsoid_hit(origin, direction, NECK[0], NECK[1])
    tTorso = ellipsoid_hit(origin, direction, TORSO[0], TORSO[1])

    #Pozadina: ravnina z = BACKDROP_Z. Zraka je normirana pa t nije dubina nego duljina
    tWall = BACKDROP_Z / direction[..., 2]

    t = numpy.minimum(numpy.minimum(numpy.minimum(tHead, tNeck), tTorso), tWall)
    point = direction * t[..., None]

    isHead = (t == tHead) & (tHead < tWall)
    isNeck = (t == tNeck) & (tNeck < tWall) & ~isHead
    isTorso = (t == tTorso) & (tTorso < tWall) & ~isHead & ~isNeck
    isSubject = isHead | isNeck | isTorso

    #Normale: kugla i elipsoid analiticki, zid ravno prema kameri
    normal = numpy.zeros_like(point)
    normal[..., 2] = 1.0

    headNormal = point - numpy.asarray(HEAD[0])
    neckNormal = (point - numpy.asarray(NECK[0])) / (numpy.asarray(NECK[1]) ** 2)
    torsoNormal = (point - numpy.asarray(TORSO[0])) / (numpy.asarray(TORSO[1]) ** 2)
    normal = numpy.where(isHead[..., None], headNormal, normal)
    normal = numpy.where(isNeck[..., None], neckNormal, normal)
    normal = numpy.where(isTorso[..., None], torsoNormal, normal)
    normal /= numpy.linalg.norm(normal, axis=-1, keepdims=True)

    #Albedo. Sitni sum posvuda: model dubine i SAM2 su naviknuti na fotografije, a ploha jedne
    #boje im ne daje nista za uhvatiti
    rng = numpy.random.default_rng(11)
    albedo = numpy.zeros_like(point)
    albedo[:] = (0.52, 0.53, 0.56)                                    #zid
    albedo += numpy.linspace(0.06, -0.04, height)[:, None, None]      #blaga vertikala na zidu
    albedo = numpy.where(isHead[..., None], numpy.array([0.78, 0.62, 0.52]), albedo)
    albedo = numpy.where(isNeck[..., None], numpy.array([0.74, 0.58, 0.49]), albedo)
    albedo = numpy.where(isTorso[..., None], numpy.array([0.24, 0.30, 0.40]), albedo)
    albedo += rng.normal(0.0, 0.012, albedo.shape)

    key = numpy.asarray(KEY, dtype=float)
    key /= numpy.linalg.norm(key)
    lambert = numpy.clip((normal * key).sum(axis=-1), 0.0, 1.0)

    colour = albedo * (AMBIENT + 0.85 * lambert[..., None])
    colour = numpy.clip(colour, 0.0, 1.0)

    #Iz linearnog u sRGB, jer je fotografija u sRGB-u i modeli su na tome uceni
    srgb = numpy.where(colour <= 0.0031308, colour * 12.92,
                       1.055 * numpy.power(numpy.maximum(colour, 1e-8), 1.0 / 2.4) - 0.055)

    distance = -point[..., 2]
    return (srgb * 255.0 + 0.5).astype("uint8"), distance, isSubject


def main():
    parser = argparse.ArgumentParser(description="sinteticki portret s tocnom dubinom i maskom")
    parser.add_argument("-o", "--output", default="portret", help="prefiks izlaznih fileova")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    args = parser.parse_args()

    from PIL import Image

    pixels, distance, subject = render(args.width, args.height)

    near = float(distance[subject].min())
    far = float(distance.max())

    #Disparitet 0..1, isti dogovor kao estimate_depth.py bez --raw: 1 je najblize
    inverse = 1.0 / distance
    disparity = (inverse - 1.0 / far) / (1.0 / near - 1.0 / far)

    Image.fromarray(pixels).save(args.output + ".png")
    write_pfm(args.output + "_truth.pfm", disparity.astype("float32"), args.width, args.height)
    Image.fromarray((subject * 255).astype("uint8"), mode="L").save(args.output + "_truth_mask.png")

    subjectDistance = float(numpy.median(distance[subject]))
    lines = [
        "sinteticki portret, %dx%d, fovY %.1f stupnjeva" % (args.width, args.height, math.degrees(FOV_Y)),
        "najblize            %.4f m" % near,
        "najdalje            %.4f m" % far,
        "subjekt (medijan)   %.4f m" % subjectDistance,
        "pozadina            %.4f m" % abs(BACKDROP_Z),
        "subjekt pokriva     %.2f %% kadra" % (100.0 * subject.mean()),
        "kalibracija         DepthMapping::fromRange(%.4f, %.4f)" % (near, far),
    ]
    with open(args.output + "_truth.txt", "w") as f:
        f.write("\n".join(lines) + "\n")

    print("\n".join(lines))
    print("\nzapisano: %s.png, %s_truth.pfm, %s_truth_mask.png, %s_truth.txt"
          % (args.output, args.output, args.output, args.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
