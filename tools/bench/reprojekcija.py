#!/usr/bin/env python3
"""Reprojekcija COLMAP modela (VideoSolve izlaz) po opazanju, razdvojeno po izvoru tocke.

    tools/bench/reprojekcija.py model_mapa [--granica N]

Tocke do ID-a N (ukljucivo) su iz prvog grafa (uglovi na smanjenoj slici), ostale iz drugog
(prostor mjerila na punoj) - mergeGraphs ih slaze tim redom. Bez --granica samo ukupno.
Ispisuje medijan, prosjek (RMS) i udio opazanja ispod 0.5 i 1 px.
"""
import sys
import numpy as np


def quat_to_matrix(w, x, y, z):
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
                     [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
                     [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])


def main():
    model = sys.argv[1]
    border = int(sys.argv[sys.argv.index("--granica") + 1]) if "--granica" in sys.argv else None
    camera = [l.split() for l in open(f"{model}/cameras.txt") if l.strip() and not l.startswith("#")][0]
    fx, fy, cx, cy = map(float, camera[4:8]) if camera[1] == "PINHOLE" else (float(camera[4]), float(camera[4]), float(camera[5]), float(camera[6]))
    points = {}
    for line in open(f"{model}/points3D.txt"):
        if line.startswith("#") or not line.strip(): continue
        p = line.split(); points[int(p[0])] = np.array(list(map(float, p[1:4])))
    ids, errors = [], []
    lines = [l for l in open(f"{model}/images.txt") if not l.startswith("#")]
    for header, observations in zip(lines[0::2], lines[1::2]):
        h = header.split()
        R = quat_to_matrix(*map(float, h[1:5])); t = np.array(list(map(float, h[5:8])))
        o = observations.split()
        for k in range(0, len(o) - 2, 3):
            pid = int(o[k + 2])
            if pid < 0 or pid not in points: continue
            c = R @ points[pid] + t
            if c[2] <= 0: continue
            u, v = fx * c[0] / c[2] + cx, fy * c[1] / c[2] + cy
            errors.append(np.hypot(u - float(o[k]), v - float(o[k + 1]))); ids.append(pid)
    ids, errors = np.array(ids), np.array(errors)
    def report(name, e):
        if len(e) == 0: return
        print(f"{name:14s} {len(e):8d} opazanja: medijan {np.median(e):.3f} px, RMS {np.sqrt((e ** 2).mean()):.3f}, "
              f"ispod 0.5 px {100 * (e < 0.5).mean():.1f} %, ispod 1 px {100 * (e < 1).mean():.1f} %")
    report("ukupno", errors)
    if border:
        report("uglovi", errors[ids <= border])
        report("prostor mjerila", errors[ids > border])


if __name__ == "__main__":
    main()
