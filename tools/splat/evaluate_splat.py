#!/usr/bin/env python3
"""PSNR gotovog splata na IZDVOJENIM kadrovima, po kadru - za usporedbu dviju inacica PO PAROVIMA.

    tools/splat/evaluate_splat.py model_mapa splat.ply [--camera-model classic|rolling] [--out psnr.npy]
    tools/splat/evaluate_splat.py --compare a.npy b.npy

ZASTO. Medijan PSNR-a jednog treninga na 39 kadrova ima vise suma nego razlike koje se traze: isti
rolling shutter je na tri para treninga izmjeren +1.06, +0.25 i -1.97 dB. Kadrovi se medjusobno
razlikuju tezinom mnogo vise nego inacice, pa se usporedjuje ISTI kadar u objema: razlika po
kadru, njezin prosjek i koliko kadrova je bolje. Izdvojeni kadrovi su isti kao u treneru
(--holdout 6 --holdout-block 3).
"""
import argparse, math, sys
from pathlib import Path

import numpy as np
import torch
from PIL import Image
import gsplat

sys.path.insert(0, str(Path(__file__).resolve().parent))
from train_splats import read_cameras, read_images
from clean_splats import read_ply
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bench"))
from mjerenja import upisi, snimka_modela


def compare(a_path, b_path, opis=""):
    A, B = np.load(a_path), np.load(b_path)
    A = A if A.ndim == 2 else A[:, None]
    B = B if B.ndim == 2 else B[:, None]
    names = ["PSNR dB", "SSIM", "ostrina"]
    for k in range(min(A.shape[1], B.shape[1])):
        a, b = A[:, k], B[:, k]
        d = b - a
        se = d.std(ddof=1) / math.sqrt(len(d))
        print(f"{names[k]:8s} A {a.mean():.4f} (medijan {np.median(a):.4f}), B {b.mean():.4f} (medijan {np.median(b):.4f}); "
              f"B - A po kadru {d.mean():+.4f} (+-{se:.4f}), B bolji na {int((d > 0).sum())} od {len(d)}")
        if opis:
            upisi(dict(vrsta="usporedba", opis=opis, mjera=names[k], razlika=round(float(d.mean()), 4),
                       pogreska=round(float(se), 4), bolji_kadrova=f"{int((d > 0).sum())}/{len(d)}",
                       a=Path(a_path).stem, b=Path(b_path).stem))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model", nargs="?")
    ap.add_argument("splat", nargs="?")
    ap.add_argument("--camera-model", choices=["classic", "rolling"], default="classic")
    ap.add_argument("--downscale", type=int, default=2)
    ap.add_argument("--holdout", type=int, default=6)
    ap.add_argument("--holdout-block", type=int, default=3)
    ap.add_argument("--motion-blur", type=int, default=0, help="kao u treneru: K trenutaka ekspozicije (trazi rolling)")
    ap.add_argument("--out", default="")
    ap.add_argument("--compare", nargs=2, default=None)
    ap.add_argument("--opis", default="",
                    help="sto se mjeri; s njim ocjena i usporedba idu u dnevnik mjerenja (benchmarks/mjerenja.jsonl)")
    args = ap.parse_args()
    if args.compare:
        compare(*args.compare, opis=args.opis)
        return

    device = "cuda"
    model = Path(args.model)
    header, names, raw = read_ply(args.splat)
    column = {name: i for i, name in enumerate(names)}
    tensor = lambda cols: torch.from_numpy(np.ascontiguousarray(raw[:, cols])).to(device)
    means = tensor([0, 1, 2]); quats = tensor([column[f"rot_{i}"] for i in range(4)])
    scales = torch.exp(tensor([column[f"scale_{i}"] for i in range(3)])); opacities = torch.sigmoid(tensor(column["opacity"]))
    rest = sorted((c for c in names if c.startswith("f_rest_")), key=lambda c: int(c.split("_")[-1]))
    degree = int(round(math.sqrt(len(rest) // 3 + 1))) - 1
    sh = tensor([column["f_dc_0"], column["f_dc_1"], column["f_dc_2"]])[:, None, :]
    if rest:
        sh = torch.cat([sh, tensor([column[c] for c in rest]).reshape(-1, 3, len(rest) // 3).transpose(1, 2)], 1)

    camera = read_cameras(model / "cameras.txt")
    frames = [(n, v) for n, v in read_images(model / "images.txt") if (model / "images" / n).exists()]
    held = []
    for start in range(0, len(frames), args.holdout * args.holdout_block):
        held.extend(range(start, min(start + args.holdout_block, len(frames))))
    width, height = camera["width"] // args.downscale, camera["height"] // args.downscale
    K = torch.tensor([[camera["fx"] / args.downscale, 0, camera["cx"] / args.downscale],
                      [0, camera["fy"] / args.downscale, camera["cy"] / args.downscale], [0, 0, 1]], device=device)
    extra = dict(packed=True)
    top = bottom = None
    if args.camera_model == "rolling":
        from gsplat.cuda._wrapper import RollingShutterType
        top = dict(read_images(model / "rs_top" / "images.txt")); bottom = dict(read_images(model / "rs_bottom" / "images.txt"))
        extra = dict(packed=False, with_ut=True, with_eval3d=True, rolling_shutter=RollingShutterType.ROLLING_TOP_TO_BOTTOM)

    #Zamucenje pokretom kao u treneru (train_splats.py): poze duz gibanja gornji->donji redak
    steps = []
    if args.motion_blur > 1 and top is not None:
        def value(file, key):
            for line in open(file) if file.exists() else []:
                parts = line.split()
                if len(parts) == 2 and parts[0] == key: return float(parts[1])
            return 0.0
        exposure = value(model / "camera_metadata.txt", "exposure_frames")
        readout = value(model / "rolling_shutter.txt", "readout_frames")
        steps = [((i + 0.5) / args.motion_blur - 0.5) * exposure / readout for i in range(args.motion_blur)]
    src = open(Path(__file__).resolve().parent / "train_splats.py").read()
    a = src.index("    def rotationLog(R):"); b = src.index("    def draw(view, viewEnd, degree, mode):")
    exec("\n".join(line[4:] for line in src[a:b].split("\n")), globals())

    from train_splats import ssim, gaussian_window
    window = gaussian_window(11, 1.5, device)
    psnrs = []
    with torch.no_grad():
        for i in held:
            name, view = frames[i]
            if top is not None:
                vm = torch.from_numpy(top[name]).float().to(device)[None]
                extra["viewmats_rs"] = torch.from_numpy(bottom[name]).float().to(device)[None]
            else:
                vm = torch.from_numpy(view).float().to(device)[None]
            if steps:
                end = extra["viewmats_rs"]
                drawn = sum(gsplat.rasterization(means, quats, scales, opacities, sh, along(vm, end, t), K[None], width, height,
                                                 sh_degree=degree, **{**extra, "viewmats_rs": along(vm, end, 1.0 + t)})[0]
                            for t in steps) / len(steps)
            else:
                drawn, _, _ = gsplat.rasterization(means, quats, scales, opacities, sh, vm, K[None], width, height,
                                                   sh_degree=degree, **extra)
            picture = Image.open(model / "images" / name).convert("RGB")
            if picture.size != (width, height): picture = picture.resize((width, height), Image.LANCZOS)
            truth = torch.from_numpy(np.array(picture)).to(device).float() / 255
            shown = drawn[0][..., :3].clamp(0, 1)
            mse = float(((shown - truth) ** 2).mean())
            #SSIM (isti kao u treneru) i OSTRINA: energija Laplacea nacrtanog prema snimljenom, po
            #svjetlini. 1 = jednako ostro kao snimka, manje = mutnije
            structure = float(ssim(shown, truth, window, 11))
            #Laplace NAKON Gaussova zamucenja (sigma 1.5 px): mjeri rubove i detalje, ne sum senzora i
            #kompresije, koji na 3x3 Laplaceu snimke nadjaca sve (auto-ISO do 1250)
            def laplace(img):
                g = (img * torch.tensor([0.299, 0.587, 0.114], device=device)).sum(-1)[None, None]
                blur = window.to(g.dtype)
                g = torch.nn.functional.conv2d(g, blur, padding=5)
                k = torch.tensor([[0, 1, 0], [1, -4, 1], [0, 1, 0]], device=device, dtype=g.dtype)[None, None]
                return float(torch.nn.functional.conv2d(g, k)[..., 8:-8, 8:-8].pow(2).mean())
            sharp = laplace(shown) / max(laplace(truth), 1e-12)
            psnrs.append((10 * math.log10(1 / max(mse, 1e-12)), structure, sharp))
    psnrs = np.array(psnrs)
    print(f"{len(psnrs)} izdvojenih kadrova ({width}x{height}): PSNR prosjek {psnrs[:, 0].mean():.3f} dB, "
          f"SSIM {psnrs[:, 1].mean():.4f}, ostrina {psnrs[:, 2].mean():.3f}")
    if args.out: np.save(args.out, psnrs)
    if args.opis:
        upisi(dict(vrsta="ocjena", snimka=snimka_modela(model), opis=args.opis, razlucivost=f"{width}x{height}",
                   psnr=round(float(psnrs[:, 0].mean()), 3), psnr_medijan=round(float(np.median(psnrs[:, 0])), 3),
                   ssim=round(float(psnrs[:, 1].mean()), 4), ostrina=round(float(psnrs[:, 2].mean()), 3),
                   kadrova=len(psnrs), splat=Path(args.splat).name))


if __name__ == "__main__":
    main()
