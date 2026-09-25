"""Preosvjetljavanje snimke novim svjetlom: snimka x E_novo(n) / E_procijenjeno(n).

    python tools/comp/relight_plate.py MODEL SVJETLO.json SPLAT.ply VIDEO IZLAZ
        [--sun-azimuth 0] [--sun-elevation 45] [--sun-intensity 1.0] [--sun-colour 1,1,1]
        [--sky-top 1,1,1] [--sky-bottom 1,1,1] [--width 1920] [--start 0] [--frames 0]

MODEL je izlaz VideoSolvea (kamera.usda s pozom za svaki kadar, cameras.txt), SVJETLO.json i SPLAT.ply
izlaz tools/splat/relight.py (procijenjeno svjetlo i splat s normalama - _albedo.ply ili _povrsina.ply).

ZASTO OMJER. Za svaki kadar se iz splata nacrtaju normale povrsine, iz njih irradijancija pod
procijenjenim i pod novim svjetlom, i snimka se pomnozi njihovim omjerom. Albedo i zasjenjenje se u
omjeru pokrate - pa ostaje SVA tekstura, sum i detalj snimke, a mijenja se samo svjetlo. Gdje splata
nema (nebo, rub) omjer je 1: snimka ostaje kakva jest.

Novo svjetlo je u jedinicama relight.py: sunce (irradijancija, smjer azimut/visina prema "gore"
scene) i nebo gore/dolje. Zadano: nebo kao procijenjeno, a --sun-intensity je umnozak procijenjene
okoline (1 = sunce jako kao cijela okolina). Izlaz IZLAZ/relit_00000.png ..., napredak "napredak I/N".
"""
import argparse, json, math, re, subprocess, sys, time
from pathlib import Path

import numpy as np
import torch
import gsplat
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "splat"))
from train_splats import read_cameras
from clean_splats import read_ply
from relight import irradiance, sh_basis, to_linear, to_srgb, quat_matrices


def usd_views(path):
    """timeCode -> svijet->kamera (OpenCV) iz kamera.usda (kamera gleda -Z, Y gore)"""
    text = Path(path).read_text()
    block = text[text.index("xformOp:transform.timeSamples"):]
    block = block[:block.index("}")]
    flip = np.diag([1.0, -1.0, -1.0, 1.0])
    out = {}
    for line in block.splitlines():
        m = re.match(r"\s*(\d+): \((.*)\),?\s*$", line)
        if m:
            v = np.array([float(x) for x in re.findall(r"[-0-9.e+]+", m.group(2))]).reshape(4, 4)
            out[int(m.group(1))] = np.linalg.inv(v.T @ flip)
    return out


def light_sh(direction, sun, top, bottom, up):
    a, b = 0.5 * (top + bottom), 0.5 * (top - bottom)
    L = sh_basis(direction)[:, None] * sun[None]
    L[0] = L[0] + a / 0.282095
    upBasis = torch.stack([up[1], up[2], up[0]])
    L[1:4] = L[1:4] + 2.046653 * upBasis[:, None] * b[None]
    return L


def triple(text):
    return torch.tensor([float(v) for v in text.split(",")], dtype=torch.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model"); ap.add_argument("light"); ap.add_argument("splat"); ap.add_argument("video"); ap.add_argument("out")
    ap.add_argument("--sun-azimuth", type=float, default=0.0, help="stupnjevi oko osi gore")
    ap.add_argument("--sun-elevation", type=float, default=45.0)
    ap.add_argument("--sun-intensity", type=float, default=1.0, help="umnozak prosjecne procijenjene okoline")
    ap.add_argument("--sun-colour", default="1,1,1")
    ap.add_argument("--sky-scale", type=float, default=1.0, help="umnozak procijenjenog neba")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--start", type=int, default=0)
    ap.add_argument("--frames", type=int, default=0)
    args = ap.parse_args()
    device = "cuda"

    light = json.loads(Path(args.light).read_text())
    up = torch.tensor(light["gore"], dtype=torch.float32, device=device)
    oldL = torch.tensor(light["sh9"], dtype=torch.float32, device=device)
    top = torch.tensor(light.get("nebo_gore", light["okolina_boja"]), device=device) * args.sky_scale
    bottom = torch.tensor(light.get("nebo_dolje", light["okolina_boja"]), device=device) * args.sky_scale

    #Smjer sunca iz azimuta i visine u sustavu scene: gore je "gore", sjever je bilo koja okomica
    east = torch.linalg.cross(up, torch.tensor([0.0, 0.0, 1.0], device=device))
    if east.norm() < 0.1: east = torch.linalg.cross(up, torch.tensor([1.0, 0.0, 0.0], device=device))
    east = east / east.norm(); north = torch.linalg.cross(east, up)
    az, el = math.radians(args.sun_azimuth), math.radians(args.sun_elevation)
    direction = math.cos(el) * (math.cos(az) * north + math.sin(az) * east) + math.sin(el) * up
    ambient = float((0.5 * (top + bottom)).mean())
    sun = triple(args.sun_colour).to(device) * args.sun_intensity * ambient * math.pi
    newL = light_sh(direction, sun, top, bottom, up)

    header, names, raw = read_ply(args.splat)
    column = {n: i for i, n in enumerate(names)}
    t = lambda cols: torch.from_numpy(np.ascontiguousarray(raw[:, cols])).to(device)
    means = t([0, 1, 2]); quats = t([column[f"rot_{i}"] for i in range(4)])
    scales = torch.exp(t([column[f"scale_{i}"] for i in range(3)])); opacities = torch.sigmoid(t(column["opacity"]))
    R = quat_matrices(quats)
    axis = torch.argmin(scales, dim=1)
    normals = R.gather(2, axis[:, None, None].expand(-1, 3, 1))[..., 0]

    camera = read_cameras(Path(args.model) / "cameras.txt")
    views = usd_views(Path(args.model) / "kamera.usda")
    probe = json.loads(subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                                       "stream=width,height,nb_frames", "-of", "json", args.video],
                                      capture_output=True, text=True).stdout)["streams"][0]
    W, H = int(probe["width"]), int(probe["height"])
    outW = min(args.width, W); outH = int(round(H * outW / W / 2)) * 2
    rw, rh = outW // 2, outH // 2               #normale na pola izlaza: omjer je gladak
    scale = rw / camera["width"]
    K = torch.tensor([[camera["fx"] * scale, 0, camera["cx"] * scale], [0, camera["fy"] * scale, camera["cy"] * scale],
                      [0, 0, 1]], device=device, dtype=torch.float32)
    total = int(probe.get("nb_frames", 0) or 0)
    wanted = args.frames if args.frames > 0 else max(0, total - args.start)
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("relit_*.png"): old.unlink()
    (out / "relit.json").write_text(json.dumps(dict(izvor=str(Path(args.video).resolve()), model=str(Path(args.model).resolve()),
        sunce_azimut=args.sun_azimuth, sunce_visina=args.sun_elevation, sunce_jakost=args.sun_intensity,
        sunce_boja=args.sun_colour, nebo=args.sky_scale, sirina=outW, visina=outH, prvi=args.start), indent=1))

    process = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-i", args.video, "-vf",
                                f"select=gte(n\\,{args.start}),scale={outW}:{outH}:in_color_matrix=bt709",
                                "-vsync", "0", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
    started = time.time()
    with torch.no_grad():
        for index in range(wanted):
            data = process.stdout.read(outW * outH * 3)
            if len(data) < outW * outH * 3: break
            source = args.start + index
            plate = torch.from_numpy(np.frombuffer(data, np.uint8).reshape(outH, outW, 3).copy()).to(device).float() / 255
            view = views.get(source + 1)
            if view is None:
                result = plate
            else:
                V = torch.from_numpy(view).float().to(device)
                centre = -(V[:3, :3].T @ V[:3, 3])
                oriented = torch.where(((centre - means) * normals).sum(-1, keepdim=True) < 0, -normals, normals)
                drawn, alpha, _ = gsplat.rasterization(means, quats, scales, opacities, oriented, V[None], K[None],
                                                       rw, rh, sh_degree=None, packed=True)
                n = torch.nn.functional.normalize(drawn[0], dim=-1)
                ratio = irradiance(n, newL).clamp_min(0) / irradiance(n, oldL).clamp_min(1e-3)
                a = alpha[0]
                ratio = a * ratio + (1 - a)                             #gdje splata nema, snimka ostaje
                ratio = torch.nn.functional.interpolate(ratio.permute(2, 0, 1)[None], size=(outH, outW),
                                                        mode="bilinear", align_corners=False)[0].permute(1, 2, 0)
                result = to_srgb(to_linear(plate) * ratio).clamp(0, 1)
            Image.fromarray((result.cpu().numpy() * 255 + 0.5).astype(np.uint8)).save(out / f"relit_{source:05d}.png",
                                                                                        compress_level=1)
            if index % 10 == 0 or index == wanted - 1:
                print(f"napredak {index + 1}/{wanted}  ({(index + 1) / max(1e-6, time.time() - started):.1f} kadrova/s)", flush=True)
    process.kill()
    print(f"Gotovo: {out}")


if __name__ == "__main__":
    main()
