#!/usr/bin/env python3
"""Dvije kamere iz kamera.usda (VideoSolve), kadar po kadar: koliko se razlikuju.

    tools/bench/usporedi_kamere.py a/kamera.usda b/kamera.usda

Kut izmedju orijentacija u stupnjevima i razmak polozaja kao dio duljine putanje - mjerilo solvea
je slobodno, pa apsolutni razmak ne znaci nista. Ispis: medijan, 95. percentil i najveca razlika.
Sluzi da se promjena koja NIJE bit-egzaktna (npr. manje tocaka u punim slicicama) izmjeri prema
dosadasnjem rjesenju, umjesto da se procjenjuje.
"""
import re, sys
import numpy as np


def kamere(path):
    text = open(path).read()
    block = text[text.index("xformOp:transform.timeSamples"):]
    block = block[:block.index("}")]
    out = {}
    for time, body in re.findall(r"(\d+(?:\.\d+)?):\s*\(\s*(\(.*?\))\s*\)", block):
        rows = [[float(v) for v in row.split(",")] for row in re.findall(r"\(([^()]*)\)", body)]
        out[float(time)] = np.array(rows)
    return out


def main():
    a, b = kamere(sys.argv[1]), kamere(sys.argv[2])
    times = sorted(set(a) & set(b))
    if not times:
        sys.exit("nema zajednickih kadrova")
    positions = np.array([a[t][3, :3] for t in times])
    length = np.sum(np.linalg.norm(np.diff(positions, axis=0), axis=1))
    angles, shifts = [], []
    for t in times:
        ra, rb = a[t][:3, :3], b[t][:3, :3]
        #Kut iz ||R - I||, ne iz arccos traga: arccos blizu 1 u float32 zapisu daje 0.03 st suma
        difference = np.linalg.norm(ra.T @ rb - np.eye(3))
        angles.append(np.degrees(2.0 * np.arcsin(min(1.0, difference / (2.0 * np.sqrt(2.0))))))
        shifts.append(np.linalg.norm(a[t][3, :3] - b[t][3, :3]) / max(length, 1e-12))
    angles, shifts = np.array(angles), np.array(shifts)
    print(f"{len(times)} zajednickih kadrova ({len(a)} i {len(b)}), duljina putanje {length:.3f}")
    print(f"kut:     medijan {np.median(angles):.4f} st, p95 {np.percentile(angles, 95):.4f}, najvise {angles.max():.4f} (kadar {times[int(angles.argmax())]:.0f})")
    print(f"polozaj: medijan {100 * np.median(shifts):.4f} % putanje, p95 {100 * np.percentile(shifts, 95):.4f}, najvise {100 * shifts.max():.4f}")


if __name__ == "__main__":
    main()
