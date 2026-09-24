#!/usr/bin/env python3
"""Ciscenje floatera iz gotovog splata, bez novog treninga (floaters.py kaze sto se mice i zasto).

    tools/splat/clean_splats.py model_mapa ulaz.ply izlaz.ply [--downscale 2]

Mjeri se na kadrovima iz kojih je splat treniran i u razlucivosti u kojoj je treniran (trener
zadano uzima pola), jer "pola piksela" znaci pola piksela te slike. Izlaz je isti PLY, samo bez
maknutih redaka - svaki bajt ostalih gaussiana je netaknut. Na kraju se PSNR na kadrovima snimke
izmjeri prije i poslije, pa se odmah vidi ako je maknuto nesto sto kadar treba.
"""
import argparse, re, sys, time
from pathlib import Path

import numpy as np
import torch
from PIL import Image

import gsplat

sys.path.insert(0, str(Path(__file__).resolve().parent))
from train_splats import read_cameras, read_images, read_points
import floaters


def read_ply(path):
    with open(path, "rb") as f:
        header = b""
        while True:
            line = f.readline()
            if not line:
                raise SystemExit(f"{path}: PLY bez end_header")
            header += line
            if line.strip() == b"end_header":
                break
        body = f.read()
    text = header.decode()
    if "binary_little_endian" not in text:
        raise SystemExit(f"{path}: cita se samo binary_little_endian PLY")
    names = [l.split()[-1] for l in text.splitlines() if l.startswith("property")]
    if any(l.split()[1] != "float" for l in text.splitlines() if l.startswith("property")):
        raise SystemExit(f"{path}: sva svojstva moraju biti float")
    count = int(re.search(r"element vertex (\d+)", text).group(1))
    raw = np.frombuffer(body, dtype="<f4", count=count * len(names)).reshape(count, len(names))
    return header, names, raw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model", help="mapa s cameras.txt, images.txt, points3D.txt")
    ap.add_argument("input", help="splat (.ply)")
    ap.add_argument("output", help="gdje spremiti ocisceni splat")
    ap.add_argument("--images", default="", help="mapa sa slikama za provjeru PSNR-a (zadano: model/images)")
    ap.add_argument("--downscale", type=int, default=2, help="razlucivost treninga; trener zadano 2")
    ap.add_argument("--visible", type=float, default=0.5, help="najmanje piksela u barem jednom kadru")
    ap.add_argument("--few-views", type=int, default=10, help="mrlja uz kameru: vidi se u najvise ovoliko kadrova")
    ap.add_argument("--near", type=float, default=0.3, help="... i blize je od ovog udjela dubine scene")
    args = ap.parse_args()

    device = "cuda"
    model = Path(args.model)
    header, names, raw = read_ply(args.input)
    column = {name: i for i, name in enumerate(names)}
    tensor = lambda cols: torch.from_numpy(np.ascontiguousarray(raw[:, cols])).to(device)
    means = tensor([column["x"], column["y"], column["z"]])
    quats = tensor([column[f"rot_{i}"] for i in range(4)])
    scales = torch.exp(tensor([column[f"scale_{i}"] for i in range(3)]))
    opacities = torch.sigmoid(tensor(column["opacity"]))
    print(f"Splat: {len(raw)} gaussiana")

    camera = read_cameras(model / "cameras.txt")
    frames = read_images(model / "images.txt")
    points, _ = read_points(model / "points3D.txt")
    scale = args.downscale
    width, height = camera["width"] // scale, camera["height"] // scale
    K = torch.tensor([[camera["fx"]/scale, 0, camera["cx"]/scale],
                      [0, camera["fy"]/scale, camera["cy"]/scale], [0, 0, 1]], dtype=torch.float32, device=device)
    views = [torch.from_numpy(view).float().to(device) for _, view in frames]
    depths = floaters.view_depths(torch.from_numpy(points).to(device), views)

    started = time.time()
    most, seen, nearest = floaters.measure(means, quats, scales, opacities, views, K, width, height, depths, args.visible)
    keep, invisible, byCamera = floaters.keep_mask(most, seen, nearest, args.visible, args.few_views, args.near)
    kept = int(keep.sum())
    print(f"Izmjereno na {len(views)} kadrova ({width}x{height}) za {time.time() - started:.1f} s")
    print(f"  nevidljive u svim kadrovima: {invisible}")
    print(f"  mrlje uz kameru:             {byCamera}")
    print(f"  ostaje: {kept} od {len(raw)} ({100.0 * kept / len(raw):.1f} %)")

    mask = keep.cpu().numpy()
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(re.sub(rb"element vertex \d+", b"element vertex %d" % kept, header))
        f.write(np.ascontiguousarray(raw[mask]).tobytes())
    print(f"Spremljeno: {args.output}")

    #PROVJERA: PSNR na svakom cetvrtom kadru, samo osnovnom bojom (bez smjera) - ista mjera prije
    #i poslije, pa je razlika posteno usporediva i kad nije PSNR punog prikaza
    images = Path(args.images) if args.images else model / "images"
    colour = tensor([column[f"f_dc_{i}"] for i in range(3)]) * 0.28209479177387814 + 0.5
    def psnr(subset):
        errors = []
        with torch.no_grad():
            for (name, _), view in list(zip(frames, views))[::4]:
                path = images / name
                if not path.exists():
                    continue
                truth = torch.from_numpy(np.asarray(Image.open(path).convert("RGB").resize((width, height), Image.BILINEAR))).to(device).float() / 255.0
                drawn, _, _ = gsplat.rasterization(means[subset], quats[subset], scales[subset], opacities[subset],
                                                   colour[subset], view[None], K[None], width, height, sh_degree=None, packed=True)
                errors.append(float(((drawn[0].clamp(0, 1) - truth) ** 2).mean()))
        return 10.0 * np.log10(1.0 / np.mean(errors)) if errors else float("nan")
    everything = torch.ones_like(keep)
    print(f"PSNR na kadrovima snimke (osnovna boja): prije {psnr(everything):.3f} dB, poslije {psnr(keep):.3f} dB")


if __name__ == "__main__":
    main()
