#!/usr/bin/env python3
"""Trening gaussian splatova iz COLMAP-ovog rjesenja.

ZASTO NAS A NE TUDJI. gsplat nosi ono sto je tesko - rasterizaciju i strategiju zgusnjavanja - ali
njegov primjer vuce pedesetak ovisnosti (nerfstudio, viser, tyro...) i vlastiti oblik podataka.
Ovdje se koristi samo knjiznica, a ulaz je izlaz nase skripte i izlaz je PLY koji Loomov
SplatViewer vec cita. Time cijeli lanac - snimka, poze, splatovi, prikaz - ostaje u Loomu, a tudje
je samo ono sto stvarno ne zelimo pisati sami.

KONVENCIJA. COLMAP rotaciju i pomak vodi iz SVIJETA U KAMERU (+Z naprijed, +Y dolje), a gsplat
trazi bas tu matricu - pa se ovdje, za razliku od ColmapImporta, NE pretvara nista. To je jedino
mjesto gdje se dvije konvencije ne moraju pomiriti, i zato je najlakse promasiti u drugu stranu.
"""
import argparse, math, os, struct, sys
from pathlib import Path

import numpy as np
import torch
from PIL import Image

import gsplat
from gsplat.strategy import DefaultStrategy


# ---------------------------------------------------------------------------------
# Citanje COLMAP-ovog tekstualnog modela
# ---------------------------------------------------------------------------------

def read_lines(path):
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                yield line


def read_cameras(path):
    for line in read_lines(path):
        parts = line.split()
        model, width, height = parts[1], int(parts[2]), int(parts[3])
        params = [float(v) for v in parts[4:]]
        if model in ("SIMPLE_PINHOLE", "SIMPLE_RADIAL", "RADIAL"):
            fx = fy = params[0]; cx, cy = params[1], params[2]
        elif model == "PINHOLE":
            fx, fy, cx, cy = params[0], params[1], params[2], params[3]
        else:
            raise SystemExit(f"Nepoznat model kamere: {model}")
        return dict(width=width, height=height, fx=fx, fy=fy, cx=cx, cy=cy, model=model)
    raise SystemExit("cameras.txt je prazan")


def quat_to_matrix(qw, qx, qy, qz):
    n = math.sqrt(qw*qw + qx*qx + qy*qy + qz*qz)
    qw, qx, qy, qz = qw/n, qx/n, qy/n, qz/n
    return np.array([
        [1-2*(qy*qy+qz*qz), 2*(qx*qy-qz*qw),   2*(qx*qz+qy*qw)],
        [2*(qx*qy+qz*qw),   1-2*(qx*qx+qz*qz), 2*(qy*qz-qx*qw)],
        [2*(qx*qz-qy*qw),   2*(qy*qz+qx*qw),   1-2*(qx*qx+qy*qy)]], dtype=np.float64)


def read_images(path):
    """Vraca [(ime, matrica_svijet_u_kameru 4x4)], poredano po imenu."""
    out, lines = [], list(read_lines(path))
    for i in range(0, len(lines), 2):          # drugi redak su pikseli; ne trebaju nam
        p = lines[i].split()
        qw, qx, qy, qz = (float(v) for v in p[1:5])
        tx, ty, tz = (float(v) for v in p[5:8])
        name = p[9]
        view = np.eye(4, dtype=np.float64)
        view[:3, :3] = quat_to_matrix(qw, qx, qy, qz)
        view[:3, 3] = (tx, ty, tz)
        out.append((name, view))
    out.sort(key=lambda one: one[0])
    return out


def read_points(path):
    positions, colours = [], []
    for line in read_lines(path):
        p = line.split()
        positions.append([float(v) for v in p[1:4]])
        colours.append([int(v) for v in p[4:7]])
    return np.array(positions, dtype=np.float32), np.array(colours, dtype=np.float32) / 255.0


# ---------------------------------------------------------------------------------
# Pocetne gaussiane iz oblaka tocaka
# ---------------------------------------------------------------------------------

def initial_scale(points, neighbours=3):
    """Pocetna velicina svake gaussiane je razmak do najblizih susjeda.

    Gusto podrucje dobije sitne gaussiane, rijetko krupne - sto je jedina pretpostavka koja ne
    trazi da se zna mjerilo scene. Fiksna velicina bi na sobi i na gradu znacila razlicite stvari.
    """
    from torch import cdist
    tensor = torch.from_numpy(points)
    chunk, out = 4096, []
    for start in range(0, len(tensor), chunk):
        block = tensor[start:start+chunk]
        distances = cdist(block, tensor)
        nearest, _ = distances.topk(neighbours + 1, largest=False)
        out.append(nearest[:, 1:].mean(dim=1))          # prvi je tocka sama sa sobom
    return torch.cat(out).clamp(min=1e-6)


def rgb_to_sh0(rgb):
    return (rgb - 0.5) / 0.28209479177387814            # C0 clan sfernih harmonika


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model", help="mapa s cameras.txt, images.txt, points3D.txt")
    ap.add_argument("images", help="mapa sa slikama")
    ap.add_argument("output", help="gdje spremiti PLY")
    ap.add_argument("--steps", type=int, default=7000)
    ap.add_argument("--downscale", type=int, default=2, help="4K je za 12 GB previse; 2 znaci pola")
    ap.add_argument("--sh-degree", type=int, default=3)
    args = ap.parse_args()

    device = "cuda"
    if not torch.cuda.is_available():
        raise SystemExit("Nema kartice - trening bez nje nema smisla")

    model = Path(args.model)
    camera = read_cameras(model / "cameras.txt")
    frames = read_images(model / "images.txt")
    points, colours = read_points(model / "points3D.txt")
    print(f"Model: {len(frames)} kamera, {len(points)} tocaka, {camera['width']}x{camera['height']}")

    # -------------------------------------------------------------------------------
    # Slike
    # -------------------------------------------------------------------------------
    scale = args.downscale
    width, height = camera["width"] // scale, camera["height"] // scale
    K = torch.tensor([[camera["fx"]/scale, 0, camera["cx"]/scale],
                      [0, camera["fy"]/scale, camera["cy"]/scale],
                      [0, 0, 1]], dtype=torch.float32, device=device)

    views, pictures = [], []
    for name, view in frames:
        path = Path(args.images) / name
        if not path.exists():
            continue
        picture = Image.open(path).convert("RGB").resize((width, height), Image.LANCZOS)
        pictures.append(torch.from_numpy(np.asarray(picture, dtype=np.float32) / 255.0))
        views.append(torch.from_numpy(view).float())
    if not pictures:
        raise SystemExit("Nijedna slika se nije nasla")

    #SLIKE OSTAJU NA PROCESORU, na karticu ide samo ona koja se u tom koraku crta. Sve odjednom
    #je za 65 slika 1.6 GB i prolazi, ali za 677 je 17 GB - a kartica ima 12. Poze su sitne pa one
    #smiju ostati gore
    pictures = torch.stack(pictures)
    views = torch.stack(views).to(device)
    gigabytes = pictures.numel() * 4 / (1 << 30)
    print(f"Slike: {len(pictures)} kom, {width}x{height} ({gigabytes:.1f} GB u radnoj memoriji)")

    # -------------------------------------------------------------------------------
    # Parametri
    # -------------------------------------------------------------------------------
    N = len(points)
    scales = torch.log(initial_scale(points)).to(device)[:, None].repeat(1, 3)
    quats = torch.zeros(N, 4, device=device); quats[:, 0] = 1.0
    sh_count = (args.sh_degree + 1) ** 2

    params = torch.nn.ParameterDict({
        "means":     torch.nn.Parameter(torch.from_numpy(points).to(device)),
        "scales":    torch.nn.Parameter(scales),
        "quats":     torch.nn.Parameter(quats),
        "opacities": torch.nn.Parameter(torch.logit(torch.full((N,), 0.1, device=device))),
        "sh0":       torch.nn.Parameter(rgb_to_sh0(torch.from_numpy(colours).to(device))[:, None, :]),
        "shN":       torch.nn.Parameter(torch.zeros(N, sh_count - 1, 3, device=device)),
    })

    #Mjerilo scene ulazi u korak ucenja za polozaje: isti korak na sobi i na gradu znaci razlicito
    spread = float(np.linalg.norm(points - points.mean(axis=0), axis=1).mean())
    rates = {"means": 1.6e-4 * spread, "scales": 5e-3, "quats": 1e-3,
             "opacities": 5e-2, "sh0": 2.5e-3, "shN": 2.5e-3 / 20}
    optimizers = {k: torch.optim.Adam([{"params": params[k], "lr": rates[k], "name": k}],
                                      eps=1e-15, betas=(0.9, 0.999)) for k in params}

    strategy = DefaultStrategy(verbose=False)
    state = strategy.initialize_state(scene_scale=spread)
    strategy.check_sanity(params, optimizers)

    # -------------------------------------------------------------------------------
    # Trening
    # -------------------------------------------------------------------------------
    print(f"Trening: {args.steps} koraka, mjerilo scene {spread:.2f}")
    generator = torch.Generator(device="cpu").manual_seed(20260915)

    for step in range(args.steps):
        index = int(torch.randint(len(pictures), (1,), generator=generator))
        truth = pictures[index].to(device, non_blocking=True)

        colours_sh = torch.cat([params["sh0"], params["shN"]], dim=1)
        rendered, alpha, info = gsplat.rasterization(
            means=params["means"], quats=params["quats"],
            scales=torch.exp(params["scales"]), opacities=torch.sigmoid(params["opacities"]),
            colors=colours_sh, viewmats=views[index:index+1], Ks=K[None],
            width=width, height=height,
            sh_degree=min(args.sh_degree, step // 1000),   #niži redovi prvi, kao u izvornom radu
            packed=True)

        strategy.step_pre_backward(params, optimizers, state, step, info)

        loss = (rendered[0] - truth).abs().mean()
        for optimizer in optimizers.values():
            optimizer.zero_grad(set_to_none=True)
        loss.backward()

        strategy.step_post_backward(params, optimizers, state, step, info, packed=True)
        for optimizer in optimizers.values():
            optimizer.step()

        if step % 500 == 0 or step == args.steps - 1:
            print(f"  {step:5d}  gubitak {loss.item():.4f}  gaussiana {params['means'].shape[0]}")

    # -------------------------------------------------------------------------------
    # Izvoz
    # -------------------------------------------------------------------------------
    gsplat.export_splats(
        means=params["means"], scales=torch.exp(params["scales"]), quats=params["quats"],
        opacities=torch.sigmoid(params["opacities"]),
        sh0=params["sh0"], shN=params["shN"], format="ply", save_to=args.output)
    print(f"Spremljeno: {args.output} ({params['means'].shape[0]} gaussiana)")

    # -------------------------------------------------------------------------------
    # Pogled iz prave kamere, uz fotografiju
    # -------------------------------------------------------------------------------
    #
    # Gubitak pada i kad je rezultat kasa - mjeri prosjek po pikselima, a prosjek prasta mnogo.
    # Ovdje se nacrta ista kamera iz koje je snimljeno i stavi uz izvornik, pa se razlika vidi.
    #
    # ZASTO BAS IZ PRAVE KAMERE. Soba se rekonstruira IZNUTRA, pa je pogled izvana beskoristan -
    # vidi se vanjska strana zidova, jednolicna masa. To se dogodilo u Loomovom pregledniku, koji
    # scenu obilazi izvana jer je pisan za predmete a ne za prostore
    with torch.no_grad():
        which = len(pictures) // 2
        colours_sh = torch.cat([params["sh0"], params["shN"]], dim=1)
        rendered, _, _ = gsplat.rasterization(
            means=params["means"], quats=params["quats"],
            scales=torch.exp(params["scales"]), opacities=torch.sigmoid(params["opacities"]),
            colors=colours_sh, viewmats=views[which:which+1], Ks=K[None],
            width=width, height=height, sh_degree=args.sh_degree, packed=True)

        truth = pictures[which].to(device)
        side = torch.cat([truth, rendered[0].clamp(0, 1)], dim=1)     # lijevo snimljeno, desno nacrtano
        picture = Image.fromarray((side.cpu().numpy() * 255).astype(np.uint8))
        preview = str(Path(args.output).with_suffix("")) + "_usporedba.png"
        picture.save(preview)

        difference = float((truth - rendered[0].clamp(0, 1)).abs().mean())
        print(f"Usporedba: {preview}  (lijevo snimljeno, desno nacrtano; razlika {difference:.4f})")


if __name__ == "__main__":
    main()
