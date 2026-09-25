"""Rastavljanje boje i procjena svjetla iz istreniranog splata - korak 1: difuzno.

    python tools/splat/relight.py MODEL SPLAT.ply [--out PREFIKS] [--steps 3000] [--downscale 4]

MODEL je izlaz VideoSolvea (cameras.txt, images.txt, images/), SPLAT izlaz train_splats.py (uz njega
<splat>_exposure.json ako postoji). Geometrija splata ostaje kakva jest; uci se:

  - po gaussiani ALBEDO (prava boja povrsine, bez svjetla) i ZASJENJENJE (0-1: kut, pukotina,
    sjena koju globalno svjetlo ne moze objasniti)
  - za cijelu scenu SVJETLO: sunce (smjer i boja) i nebo s gradijentom gore-dolje - ono sto ide u
    Blender. Sjencanje ide kroz sferne harmonike drugog reda (Ramamoorthi i Hanrahan 2001): difuzna
    povrsina vidi svjetlo samo do tog reda
  - ekspozicija po kadru krece od one iz treninga i dotjeruje se

Albedo, svjetlo i sjencanje su u LINEARNOM svjetlu (kao Blender i Loomov PBR); kadrovi i boje
splata su sRGB i prevode se. Jedinice svjetla: piksel_linearno = albedo * E(n) / pi, pa je sunce
irradijancija (Blenderova jakost sunca) a okolina radijancija (boja svijeta uz jakost 1).

Sjencanje je ODGODJENO, po pikselu: albedo i zasjenjenje se nacrtaju kao slika, normala dolazi iz
nacrtane dubine, i piksel = albedo * zasjenjenje * E(normala) / pi. Normala iz dubine je ona koju
geometrija splata stvarno ima - normala pojedine gaussiane (najkraca os) je na MCMC splatu bez
ikakvog poravnanja slaba, pa se ona samo uci prema dubini i zapisuje uz albedo (nx ny nz).

Izlaz (PREFIKS zadano <splat>_svjetlo):
  PREFIKS_albedo.ply  splat s albedom umjesto boje i normalama - u editoru scena bez svjetla
  PREFIKS.json        svjetlo: sunce (smjer, boja), nebo gore/dolje, isto kao SH9, sigurnost smjera
  PREFIKS_okolina.png procijenjeno svjetlo kao panorama (gore je gore)
  PREFIKS_rastav.png  snimka | sjencani model | albedo | sjencanje | normale | novo svjetlo

Sto ovo NE moze: jedno globalno svjetlo ne zna za lokalne izvore ni prave sjene - one zavrse u
zasjenjenju ili albedu. Odsjaji zavrse u albedu (korak 2 je hrapavost i odsjaj).
"""
import argparse, json, math, sys
from pathlib import Path

import numpy as np
import torch
import gsplat
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bench"))
from train_splats import read_cameras, read_images, gaussian_window, ssim_fast
from clean_splats import read_ply
from mjerenja import upisi, snimka_modela

C0 = 0.28209479177387814
#Ramamoorthi-Hanrahan: irradijancija iz SH9 radijancije
K1, K2, K3, K4, K5 = 0.429043, 0.511664, 0.743125, 0.886227, 0.247708


def irradiance(n, L):
    """n (..., 3) jedinicne normale u svijetu, L (9, 3) koeficijenti: L00, L1-1(y), L10(z), L11(x),
    L2-2(xy), L2-1(yz), L20, L21(xz), L22(x^2-y^2). Vraca E (..., 3)."""
    x, y, z = n[..., 0:1], n[..., 1:2], n[..., 2:3]
    return (K1 * L[8] * (x * x - y * y) + K3 * L[6] * z * z + K4 * L[0] - K5 * L[6]
            + 2 * K1 * (L[4] * x * y + L[7] * x * z + L[5] * y * z)
            + 2 * K2 * (L[3] * x + L[1] * y + L[2] * z))


def sh_basis(d):
    """Realni SH do drugog reda u smjeru d (..., 3) -> (..., 9), isti redoslijed kao L."""
    x, y, z = d[..., 0], d[..., 1], d[..., 2]
    return torch.stack([torch.full_like(x, 0.282095), 0.488603 * y, 0.488603 * z, 0.488603 * x,
                        1.092548 * x * y, 1.092548 * y * z, 0.315392 * (3 * z * z - 1),
                        1.092548 * x * z, 0.546274 * (x * x - y * y)], -1)


def depth_normals(depth, K, step=4, sigma=2.0):
    """Normale iz nacrtane dubine (H, W) u sustavu kamere (OpenCV), okrenute prema kameri. Dubina se
    prvo zagladi: bez toga (korak 2, bez zamucenja) je kameni zid na C0257 bio reljef, a stol nije
    bio ravan - tekstura splata ulazi u dubinu, a svjetlo iz takvih normala nema smjera"""
    H, W = depth.shape
    if sigma > 0:
        radius = int(3 * sigma)
        x = torch.arange(-radius, radius + 1, device=depth.device, dtype=depth.dtype)
        line = torch.exp(-x * x / (2 * sigma * sigma)); line = line / line.sum()
        d = torch.nn.functional.pad(depth[None, None], (radius, radius, radius, radius), mode="replicate")
        d = torch.nn.functional.conv2d(d, line.view(1, 1, 1, -1))
        depth = torch.nn.functional.conv2d(d, line.view(1, 1, -1, 1))[0, 0]
    v, u = torch.meshgrid(torch.arange(H, device=depth.device, dtype=depth.dtype),
                          torch.arange(W, device=depth.device, dtype=depth.dtype), indexing="ij")
    P = torch.stack([(u - K[0, 2]) / K[0, 0] * depth, (v - K[1, 2]) / K[1, 1] * depth, depth], -1)
    s = step
    dx = P[s:-s, 2 * s:] - P[s:-s, :-2 * s]
    dy = P[2 * s:, s:-s] - P[:-2 * s, s:-s]
    n = -torch.cross(dx, dy, dim=-1)
    n = n / n.norm(dim=-1, keepdim=True).clamp_min(1e-12)
    n = torch.nn.functional.pad(n.permute(2, 0, 1)[None], (s, s, s, s), mode="replicate")[0].permute(1, 2, 0)
    return n


def to_linear(c):
    """sRGB (kako su kadrovi i boje splata) -> linearno svjetlo"""
    return torch.where(c <= 0.04045, c / 12.92, ((c.clamp_min(0) + 0.055) / 1.055) ** 2.4)


def to_srgb(c):
    c = c.clamp_min(1e-8)
    return torch.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def quat_matrices(q):
    q = q / q.norm(dim=-1, keepdim=True)
    w, x, y, z = q.unbind(-1)
    return torch.stack([1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
                        2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
                        2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)], -1).view(-1, 3, 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("splat")
    ap.add_argument("--out", default="")
    ap.add_argument("--steps", type=int, default=3000)
    ap.add_argument("--downscale", type=int, default=4)
    ap.add_argument("--holdout", type=int, default=6)
    ap.add_argument("--holdout-block", type=int, default=3)
    ap.add_argument("--exposure", default="", help="zadano <splat>_exposure.json ako postoji")
    ap.add_argument("--smooth", type=float, default=0.2, help="glatkoca albeda, ne preko rubova snimke")
    ap.add_argument("--grey", type=float, default=0.5, help="prosjecni albedo je siv (boja svjetla ide u svjetlo)")
    ap.add_argument("--occlusion", type=float, default=0.02, help="zasjenjenje tezi prema 1")
    ap.add_argument("--surface-steps", type=int, default=3000,
                    help="faza 1: koraka dotjerivanja geometrije u povrsinu (0 = splat kakav jest)")
    ap.add_argument("--normals", type=float, default=0.1, help="faza 1: normala gaussiane prema normali iz dubine")
    ap.add_argument("--flat", type=float, default=0.1, help="faza 1: najkraca os gaussiane kratka prema ostalima")
    ap.add_argument("--opis", default="")
    args = ap.parse_args()

    device = "cuda"
    torch.manual_seed(0)
    model = Path(args.model)
    out = args.out or str(Path(args.splat).with_suffix("")) + "_svjetlo"
    exposurePath = Path(args.exposure) if args.exposure else Path(str(Path(args.splat).with_suffix("")) + "_exposure.json")
    exposureFor = json.load(open(exposurePath)) if exposurePath.exists() else {}

    header, names, raw = read_ply(args.splat)
    column = {name: i for i, name in enumerate(names)}
    tensor = lambda cols: torch.from_numpy(np.ascontiguousarray(raw[:, cols])).to(device)
    means = tensor([0, 1, 2])
    scales = torch.exp(tensor([column[f"scale_{i}"] for i in range(3)]))
    opacities = torch.sigmoid(tensor(column["opacity"]))
    quats = tensor([column[f"rot_{i}"] for i in range(4)])
    rgb = (tensor([column["f_dc_0"], column["f_dc_1"], column["f_dc_2"]]) * C0 + 0.5).clamp(0.02, 0.98)
    rest = sorted((c for c in names if c.startswith("f_rest_")), key=lambda c: int(c.split("_")[-1]))
    degree = int(round(math.sqrt(len(rest) // 3 + 1))) - 1
    sh = tensor([column["f_dc_0"], column["f_dc_1"], column["f_dc_2"]])[:, None, :]
    if rest:
        sh = torch.cat([sh, tensor([column[c] for c in rest]).reshape(-1, 3, len(rest) // 3).transpose(1, 2)], 1)
    N = means.shape[0]

    camera = read_cameras(model / "cameras.txt")
    frames = [(n, v) for n, v in read_images(model / "images.txt") if (model / "images" / n).exists()]
    held = set()
    for start in range(0, len(frames), args.holdout * args.holdout_block):
        held.update(range(start, min(start + args.holdout_block, len(frames))))
    train = [i for i in range(len(frames)) if i not in held]
    width, height = camera["width"] // args.downscale, camera["height"] // args.downscale
    K = torch.tensor([[camera["fx"] / args.downscale, 0, camera["cx"] / args.downscale],
                      [0, camera["fy"] / args.downscale, camera["cy"] / args.downscale], [0, 0, 1]], device=device)
    views = torch.from_numpy(np.stack([v for _, v in frames])).float().to(device)
    centres = -(views[:, :3, :3].transpose(1, 2) @ views[:, :3, 3:])[..., 0]
    #GORE iz kamera: OpenCV y ide prema dolje, pa je -y kamere u svijetu gore; prosjek preko snimke
    up = -views[:, 1, :3].mean(0)
    up = up / up.norm()

    def load(i):
        picture = Image.open(model / "images" / frames[i][0]).convert("RGB")
        if picture.size != (width, height): picture = picture.resize((width, height), Image.LANCZOS)
        return torch.from_numpy(np.array(picture, dtype=np.uint8)).to(device)
    pictures = {i: load(i) for i in range(len(frames))}
    print(f"Rastavljanje: {N} gaussiana, {len(train)} kadrova za ucenje, {len(held)} izdvojenih, {width}x{height}")

    exposure = torch.zeros(len(frames), 2, 3, device=device)
    for i, (name, _) in enumerate(frames):
        if name in exposureFor: exposure[i] = torch.tensor(exposureFor[name], device=device)
    exposure.requires_grad_(True)

    window = gaussian_window(11, 1.5, device)
    windowLine = window[0, 0].sum(0)

    def oriented_normals(q, s, m, view):
        """Najkraca os gaussiane, okrenuta prema kameri koja gleda"""
        R = quat_matrices(q)
        axis = torch.argmin(s, dim=1)
        n = R.gather(2, axis[:, None, None].expand(-1, 3, 1))[..., 0]
        centre = -(view[:3, :3].T @ view[:3, 3])
        flip = ((centre[None] - m) * n).sum(-1, keepdim=True) < 0
        return torch.where(flip, -n, n)

    #=====================================================================================
    # FAZA 1: GEOMETRIJA U POVRSINU.
    #
    # Normale iz dubine obicnog splata nisu povrsina: na C0257 je ravni zid i uz zamucenje dubine
    # od 6 px imao normale u svim smjerovima, a vrh stola nije bio jedne boje. Gaussiane MCMC-a
    # nisu ni na ravnini ni plosnate, a ocekivana dubina mijesa ono iza poluprozirnih. Zato se, kao
    # u svim metodama preosvjetljavanja splata (R3DG, GS-IR, 2DGS), geometrija dotjera uz dva
    # uvjeta: normala gaussiane se slaze s normalom nacrtane dubine (gradijent ide u oboje), i
    # gaussiana je plosnata pa joj je normala odredjena. Boja ostaje SH-om kao u treningu, pa
    # fotometrijski gubitak drzi splat vjernim snimci
    #=====================================================================================
    original = (means, quats, scales, opacities, sh)
    if args.surface_steps > 0:
        spread = float((means - means.mean(0)).norm(dim=1).mean())
        g = dict(means=means.clone(), quats=quats.clone(), scales=torch.log(scales),
                 opacities=torch.logit(opacities.clamp(1e-4, 1 - 1e-4)), sh=sh.clone())
        for v in g.values(): v.requires_grad_(True)
        geometry = torch.optim.Adam([dict(params=[g["means"]], lr=1.6e-5 * spread), dict(params=[g["quats"]], lr=1e-3),
                                     dict(params=[g["scales"]], lr=5e-3), dict(params=[g["opacities"]], lr=1e-2),
                                     dict(params=[g["sh"]], lr=5e-4)])
        order = torch.randperm(args.surface_steps * 2, generator=torch.Generator().manual_seed(2)) % len(train)
        for step in range(args.surface_steps):
            i = train[int(order[step])]
            view = views[i]
            s = torch.exp(g["scales"])
            centre = -(view[:3, :3].T @ view[:3, 3])
            colour = (gsplat.spherical_harmonics(degree, g["means"] - centre, g["sh"]) + 0.5).clamp_min(0)
            n = oriented_normals(g["quats"], s, g["means"], view)
            drawn, alpha, _ = gsplat.rasterization(g["means"], g["quats"], s, torch.sigmoid(g["opacities"]),
                                                   torch.cat([colour, n], -1), view[None], K[None], width, height,
                                                   sh_degree=None, render_mode="RGB+ED", packed=True)
            drawn, alpha = drawn[0], alpha[0]
            truth = pictures[i].float() / 255
            shown = drawn[..., :3] * (1.0 + exposure[i, 0].detach()) + exposure[i, 1].detach()
            loss = 0.8 * (shown - truth).abs().mean() + 0.2 * (1 - ssim_fast(shown, truth, windowLine))
            dn = (view[:3, :3].T @ depth_normals(drawn[..., 6], K, step=1, sigma=0.0).reshape(-1, 3).T).T.reshape(height, width, 3)
            gn = drawn[..., 3:6]
            gn = gn / gn.norm(dim=-1, keepdim=True).clamp_min(1e-9)
            solid = (alpha.detach() > 0.5).float()
            consistency = (solid * (1 - (gn * dn).sum(-1, keepdim=True))).sum() / solid.sum().clamp_min(1)
            flatness = (s.min(1).values / s.max(1).values).mean()
            loss = loss + args.normals * consistency + args.flat * flatness
            geometry.zero_grad(set_to_none=True)
            loss.backward()
            geometry.step()
            if step % 500 == 0 or step == args.surface_steps - 1:
                print(f"  povrsina {step:5d}: gubitak {float(loss.detach()):.4f}, neslaganje normala "
                      f"{float(consistency.detach()):.3f}, plosnatost {float(flatness.detach()):.3f}", flush=True)
        with torch.no_grad():
            means, quats, scales = g["means"].detach(), g["quats"].detach(), torch.exp(g["scales"].detach())
            opacities, sh = torch.sigmoid(g["opacities"].detach()), g["sh"].detach()
            rgb = (sh[:, 0] * C0 + 0.5).clamp(0.02, 0.98)
        del g, geometry

    #LINEARNO SVJETLO. Albedo, svjetlo i sjencanje su u linearnom prostoru, a u sRGB se prelazi tek
    #za usporedbu sa snimkom. Prvo je sve bilo u sRGB-u, i tada svjetlo nije znacilo ono sto Blender
    #misli: siva kugla (0.6) pod procijenjenom okolinom 1.19 izasla je gotovo bijela
    albedoLogit = torch.logit(to_linear(rgb).clamp(0.005, 0.98)).clone().requires_grad_(True)
    occlusionLogit = torch.full((N, 1), 3.0, device=device, requires_grad=True)
    #=====================================================================================
    # SVJETLO KAKO GA VFX KORISTI: sunce (smjer, boja) i nebo s gradijentom od gore prema dolje
    # (gore prozor/strop, dolje odbijeno od poda). Prvo se ucio puni SH9 pa se iz njega vadilo sunce
    # i okolina - i to je gubilo: na C0257 je izvadjeno sunce bilo 0.6 dB losije od najboljeg od 48
    # smjerova uz isti albedo, jer je albedo naucen uz svjetlo koje se ne isporucuje. Sad se uci
    # upravo ono sto ide u Blender. Radijancija je nenegativna po gradnji (|gradijent| <= nebo)
    #=====================================================================================
    light = dict(dir=up.clone(), sun=torch.full((3,), -3.0, device=device),
                 sky=torch.full((3,), math.log(math.e - 1), device=device),   #softplus = 1: E / pi = 1
                 grad=torch.zeros(3, device=device))
    for v in light.values(): v.requires_grad_(True)
    upBasis = torch.stack([up[1], up[2], up[0]])       #redoslijed prvog pojasa: y, z, x

    def light_parts():
        a = torch.nn.functional.softplus(light["sky"])
        b = torch.tanh(light["grad"]) * a
        return (torch.nn.functional.normalize(light["dir"], dim=0), torch.nn.functional.softplus(light["sun"]),
                a + b, a - b)

    def light_sh(direction, sun, top, bottom):
        """Sunce boje c iz smjera d: L = c Y(d). Nebo R(w) = a + b (w . gore): L00 = a / Y00,
        L1 = b * 0.488603 * 4pi/3 * gore"""
        a, b = 0.5 * (top + bottom), 0.5 * (top - bottom)
        return sh_basis(direction)[:, None] * sun[None] + torch.cat(
            [(a / 0.282095)[None], 2.046653 * upBasis[:, None] * b[None], torch.zeros(5, 3, device=device)], 0)
    optimiser = torch.optim.Adam([
        dict(params=[albedoLogit], lr=3e-3), dict(params=[occlusionLogit], lr=3e-3),
        dict(params=list(light.values()), lr=2e-2), dict(params=[exposure], lr=1e-3)])
    startLum = float((to_linear(rgb) @ torch.tensor([0.2126, 0.7152, 0.0722], device=device)).mean())
    sphere = torch.nn.functional.normalize(torch.randn(1024, 3, device=device), dim=-1)

    def render(i, albedo, occlusion):
        """Albedo, normala za sjencanje, zasjenjenje, alfa. Normala je normala gaussiana (nakon faze 1
        slozena s dubinom i ostrija od nje na rubovima); bez faze 1 normala iz zagladjene dubine"""
        view = views[i]
        features = torch.cat([albedo, oriented_normals(quats, scales, means, view), occlusion], -1)
        drawn, alpha, _ = gsplat.rasterization(means, quats, scales, opacities, features, view[None], K[None],
                                               width, height, sh_degree=None, render_mode="RGB+ED", packed=True)
        drawn, alpha = drawn[0], alpha[0]
        if args.surface_steps > 0:
            normal = drawn[..., 3:6] / drawn[..., 3:6].norm(dim=-1, keepdim=True).clamp_min(1e-9)
        else:
            normal = (view[:3, :3].T @ depth_normals(drawn[..., 7], K).reshape(-1, 3).T).T.reshape(height, width, 3)
        return drawn[..., :3], normal.detach(), drawn[..., 6:7], alpha

    def shade(albedo, occlusion, normal, light):
        return albedo * occlusion * irradiance(normal, light) / math.pi

    def expose(linear, i):
        """Linearno sjencanje -> sRGB -> ekspozicija kadra (ekspozicija je iz treninga, u sRGB-u)"""
        return to_srgb(linear) * (1.0 + exposure[i, 0]) + exposure[i, 1]

    golden = math.pi * (3 - math.sqrt(5))
    k = torch.arange(48, device=device, dtype=torch.float32)
    yk = 1 - 2 * (k + 0.5) / 48
    sphereDirs = torch.stack([torch.sqrt(1 - yk * yk) * torch.cos(golden * k), yk,
                              torch.sqrt(1 - yk * yk) * torch.sin(golden * k)], -1)

    def sweep(frameList, sun, top, bottom, albedo, occlusion):
        """PSNR po 48 smjerova sunca uz isti albedo, zasjenjenje, boju sunca i nebo"""
        errors = torch.zeros(len(sphereDirs), device=device)
        for i in frameList:
            pa, dn, po, _ = render(i, albedo, occlusion)
            truth = pictures[i].float() / 255
            for j, d in enumerate(sphereDirs):
                errors[j] += ((expose(shade(pa, po, dn, light_sh(d, sun, top, bottom)), i).clamp(0, 1) - truth) ** 2).mean()
        return (-10 * torch.log10(errors / len(frameList))).cpu().numpy()

    def psnr_with(frameList, L, albedo, occlusion):
        error = 0.0
        for i in frameList:
            pa, dn, po, _ = render(i, albedo, occlusion)
            error += float(((expose(shade(pa, po, dn, L), i).clamp(0, 1) - pictures[i].float() / 255) ** 2).mean())
        return -10 * math.log10(error / len(frameList))

    lum = torch.tensor([0.2126, 0.7152, 0.0722], device=device)
    order = torch.randperm(args.steps * 2, generator=torch.Generator().manual_seed(1)) % len(train)
    for step in range(args.steps):
        i = train[int(order[step])]
        albedo = torch.sigmoid(albedoLogit)
        occlusion = torch.sigmoid(occlusionLogit)
        pa, dn, po, alpha = render(i, albedo, occlusion)
        truth = pictures[i].float() / 255
        L = light_sh(*light_parts())
        shown = expose(shade(pa, po, dn, L), i)
        loss = 0.8 * (shown - truth).abs().mean() + 0.2 * (1 - ssim_fast(shown, truth, windowLine))

        solid = (alpha > 0.9).float()
        weight = solid.sum().clamp_min(1)
        #RETINEX: albedo gladak ondje gdje snimci ne mijenja KROMATICNOST. Svjetlo mijenja svjetlinu,
        #materijal boju - pa se glatkoca gasi samo na promjeni boje. Prvo je gasila na promjeni
        #svjetline, i tada je bas svaki rub sjene (vrh stola prema prednjoj strani) otisao u albedo
        a = pa / alpha.clamp_min(1e-3)
        chroma = truth / truth.sum(-1, keepdim=True).clamp_min(0.05)
        for d in (1, 0):
            da = (a.narrow(d, 1, a.shape[d] - 1) - a.narrow(d, 0, a.shape[d] - 1)).abs().sum(-1)
            di = 3 * (chroma.narrow(d, 1, a.shape[d] - 1) - chroma.narrow(d, 0, a.shape[d] - 1)).abs().mean(-1)
            m = solid.narrow(d, 1, a.shape[d] - 1)[..., 0]
            loss = loss + args.smooth * (da * torch.exp(-20 * di) * m).sum() / weight
        #Siv svijet: boja koja je svuda ista pripada svjetlu; a svjetlina albeda ostaje kao na pocetku
        mean = (a * solid).sum((0, 1)) / weight
        loss = loss + args.grey * ((mean - mean.mean()) ** 2).sum() + args.grey * (mean @ lum - startLum) ** 2
        loss = loss + args.occlusion * (solid * (1 - po / alpha.clamp_min(1e-3))).sum() / weight
        loss = loss + 1e-3 * exposure[i].pow(2).sum()
        optimiser.zero_grad(set_to_none=True)
        loss.backward()
        optimiser.step()
        if step % 500 == 0 or step == args.steps - 1:
            direction, sun, top, bottom = (v.detach() for v in light_parts())
            elevation = math.degrees(math.asin(float((direction * up).sum().clamp(-1, 1))))
            print(f"  korak {step:5d}: gubitak {float(loss.detach()):.4f}, sunce {sun.cpu().numpy().round(3)} "
                  f"visina {elevation:+.0f} st, nebo gore {top.cpu().numpy().round(3)} dolje {bottom.cpu().numpy().round(3)}",
                  flush=True)
        #NA POLA: smjer se ne uci samo gradijentom, koji zapne u lokalnom minimumu (na C0257 je sunce
        #odozdo bilo naucen smjer). Uz dosadasnji albedo se probaju 48 smjerova na 12 kadrova
        if step == args.steps // 2:
            with torch.no_grad():
                albedo, occlusion = torch.sigmoid(albedoLogit), torch.sigmoid(occlusionLogit)
                direction, sun, top, bottom = light_parts()
                probe = train[::max(1, len(train) // 12)]
                scores = sweep(probe, sun, top, bottom, albedo, occlusion)
                current = psnr_with(probe, light_sh(direction, sun, top, bottom), albedo, occlusion)
                best = int(scores.argmax())
                if scores[best] > current + 0.02:
                    light["dir"].copy_(sphereDirs[best])
                    print(f"  pretraga smjera: {scores[best]:.3f} dB prema naucenom {current:.3f} - sunce premjesteno")
                else:
                    print(f"  pretraga smjera: nauceni {current:.3f} dB, najbolji od 48 {scores[best]:.3f} - ostaje")

    # --------------------------------------------------------------------------------------
    # Ocjena na izdvojenim: koliko difuzni model objasni prema izvornom splatu
    # --------------------------------------------------------------------------------------
    with torch.no_grad():
        albedo = torch.sigmoid(albedoLogit)
        occlusion = torch.sigmoid(occlusionLogit)
        direction, sun, top, bottom = light_parts()
        L = light_sh(direction, sun, top, bottom)
        scores = []
        for i in sorted(held):
            pa, dn, po, _ = render(i, albedo, occlusion)
            truth = pictures[i].float() / 255
            shown = expose(shade(pa, po, dn, L), i).clamp(0, 1)
            name = frames[i][0]
            psnr = lambda x: -10 * math.log10(max(float(((x - truth) ** 2).mean()), 1e-12))
            row = [psnr(shown)]
            for splat in (original, (means, quats, scales, opacities, sh)):
                drawn, _, _ = gsplat.rasterization(*splat, views[i][None], K[None], width, height,
                                                   sh_degree=degree, packed=True)
                drawn = drawn[0]
                if name in exposureFor:
                    c = torch.tensor(exposureFor[name], device=device)
                    drawn = drawn * (1.0 + c[0]) + c[1]
                row.append(psnr(drawn.clamp(0, 1)))
            scores.append(row)
        scores = np.array(scores)

        #=================================================================================
        # KOLIKO JE SMJER ODREDJEN. Albedo, zasjenjenje, boja sunca i nebo ostanu, a sunce se postavi
        # u 48 smjerova po sferi; PSNR izdvojenih po smjeru kaze razlikuje li snimka smjer uopce. Na
        # sobi s bijelim stolom "svijetao stol" i "jako osvijetljen stol" izgledaju isto
        #=================================================================================
        heldList = sorted(held)
        spherePsnr = sweep(heldList, sun, top, bottom, albedo, occlusion)
        learnedPsnr = psnr_with(heldList, L, albedo, occlusion)
        bestPsnr, worstPsnr = float(spherePsnr.max()), float(spherePsnr.min())
        close = sphereDirs[torch.from_numpy(spherePsnr >= max(bestPsnr, learnedPsnr) - 0.05).to(device)]
        spreadAngle = float(torch.rad2deg(torch.acos((close @ direction).clamp(-1, 1))).max()) if len(close) else 0.0
        print(f"Smjer sunca: naucen {learnedPsnr:.3f} dB, najbolji od 48 {bestPsnr:.3f}, najgori {worstPsnr:.3f} "
              f"(raspon {bestPsnr - worstPsnr:.3f} dB); jednako dobri smjerovi (0.05 dB) do {spreadAngle:.0f} st od naucenog")
        elevation = math.degrees(math.asin(float((direction * up).sum().clamp(-1, 1))))
        print(f"{len(scores)} izdvojenih ({width}x{height}): difuzni model PSNR {scores[:, 0].mean():.2f} dB, "
              f"izvorni splat {scores[:, 1].mean():.2f} dB, povrsinski splat {scores[:, 2].mean():.2f} dB")
        print(f"Svjetlo: sunce {sun.cpu().numpy().round(3)} iz smjera {direction.cpu().numpy().round(3)} "
              f"(visina {elevation:+.0f} st), nebo gore {top.cpu().numpy().round(3)}, dolje {bottom.cpu().numpy().round(3)}")

        # ----------------------------------------------------------------------------------
        # Zapis: svjetlo, albedo splat s normalama, panorama, pregled
        # ----------------------------------------------------------------------------------
        json.dump(dict(
            opis="Procijenjeno difuzno svjetlo scene (tools/splat/relight.py). Sustav svijeta je kamera.usda/"
                 "images.txt, linearno svjetlo: piksel = albedo * E(n) / pi. Sunce je irradijancija okomito na "
                 "njega (Blender: jakost sunca), nebo radijancija koja ide linearno od 'dolje' do 'gore'.",
            sh9=L.cpu().numpy().tolist(),
            sh9_redoslijed="L00, L1-1(y), L10(z), L11(x), L2-2(xy), L2-1(yz), L20(3z^2-1), L21(xz), L22(x^2-y^2)",
            gore=up.cpu().numpy().tolist(),
            sunce_smjer_prema_svjetlu=direction.cpu().numpy().tolist(),
            sunce_boja=sun.cpu().numpy().tolist(),
            sunce_visina_st=elevation,
            nebo_gore=top.cpu().numpy().tolist(),
            nebo_dolje=bottom.cpu().numpy().tolist(),
            okolina_boja=(0.5 * (top + bottom)).cpu().numpy().tolist(),
            izdvojeni_psnr=float(scores[:, 0].mean()), izvorni_psnr=float(scores[:, 1].mean()),
            povrsinski_psnr=float(scores[:, 2].mean()),
            smjer_psnr_raspon_db=bestPsnr - worstPsnr, smjer_nesigurnost_st=spreadAngle,
        ), open(out + ".json", "w"), indent=1)

        #Normale gaussiana okrenute prema najblizoj kameri (soba se snima iznutra)
        R = quat_matrices(quats)
        axis = torch.argmin(scales, dim=1)
        normals = R.gather(2, axis[:, None, None].expand(-1, 3, 1))[..., 0]
        for s in range(0, N, 1 << 18):
            chunk = means[s:s + (1 << 18)]
            nearest = centres[torch.cdist(chunk, centres).argmin(1)]
            flip = ((nearest - chunk) * normals[s:s + (1 << 18)]).sum(-1) < 0
            normals[s:s + (1 << 18)][flip] *= -1
        #Geometrija iz faze 1 u oba zapisa: albedo splat (boja bez svjetla) i povrsinski splat (SH boja
        #kao iz treninga) - isti gaussiani, pa se u editoru mogu zamijeniti
        table = raw.copy()
        table[:, 0:3] = means.cpu().numpy()
        for c in range(3): table[:, column[f"scale_{c}"]] = torch.log(scales[:, c]).cpu().numpy()
        table[:, column["opacity"]] = torch.logit(opacities.clamp(1e-6, 1 - 1e-6)).cpu().numpy()
        for c in range(4): table[:, column[f"rot_{c}"]] = (quats / quats.norm(dim=-1, keepdim=True))[:, c].cpu().numpy()
        outNames = list(names)
        if "nx" in column:
            table[:, [column["nx"], column["ny"], column["nz"]]] = normals.cpu().numpy()
        else:
            table = np.concatenate([table[:, :3], normals.cpu().numpy(), table[:, 3:]], 1)
            outNames = names[:3] + ["nx", "ny", "nz"] + names[3:]
        at = {n: i for i, n in enumerate(outNames)}

        def write(path, dc, restValues):
            copy = table.copy()
            for c in range(3): copy[:, at[f"f_dc_{c}"]] = dc[:, c]
            for k, c in enumerate(rest): copy[:, at[c]] = restValues[:, k]
            with open(path, "wb") as f:
                f.write(("ply\nformat binary_little_endian 1.0\n" + f"element vertex {N}\n" +
                         "".join(f"property float {n}\n" for n in outNames) + "end_header\n").encode())
                f.write(np.ascontiguousarray(copy, dtype="<f4").tobytes())
        write(out + "_albedo.ply", ((to_srgb(albedo) - 0.5) / C0).cpu().numpy(), np.zeros((N, len(rest)), np.float32))
        if args.surface_steps > 0:
            write(out + "_povrsina.ply", sh[:, 0].cpu().numpy(),
                  sh[:, 1:].transpose(1, 2).reshape(N, -1).cpu().numpy())

        #Panorama svjetla: stupac je azimut, redak visina, gore je gore
        east = torch.linalg.cross(up, torch.tensor([0.0, 0.0, 1.0], device=device))
        if east.norm() < 0.1: east = torch.linalg.cross(up, torch.tensor([1.0, 0.0, 0.0], device=device))
        east = east / east.norm(); north = torch.linalg.cross(east, up)
        theta = torch.linspace(0, math.pi, 256, device=device)[:, None]
        phi = torch.linspace(-math.pi, math.pi, 512, device=device)[None]
        d = (torch.cos(theta)[..., None] * up + (torch.sin(theta) * torch.cos(phi))[..., None] * north
             + (torch.sin(theta) * torch.sin(phi))[..., None] * east)
        radiance = (sh_basis(d) @ L).clamp_min(0)
        panorama = (radiance / radiance.max()).pow(1 / 2.2)
        Image.fromarray((panorama.cpu().numpy() * 255).astype(np.uint8)).save(out + "_okolina.png")

        #Pregled na srednjem izdvojenom kadru, i isto s novim svjetlom: sunce zakrenuto 90 st oko gore
        i = sorted(held)[len(held) // 2]
        pa, dn, po, alpha = render(i, albedo, occlusion)
        turned = direction - (direction @ up) * up
        turned = torch.linalg.cross(up, turned) + (direction @ up) * up
        turned = turned / turned.norm()
        relit = shade(pa, po, dn, light_sh(turned, sun, top, bottom))
        #Sjencanje je oko 1 i preko, pa bi u slici bilo bijelo: pokaze se prema svom 99. percentilu
        shading = po * irradiance(dn, L) / math.pi
        shading = shading / torch.quantile(shading[alpha[..., 0] > 0.5].flatten()[::97], 0.99).clamp_min(1e-6)
        tiles = [pictures[i].float() / 255, expose(shade(pa, po, dn, L), i), to_srgb(pa), to_srgb(shading),
                 (dn * 0.5 + 0.5) * alpha, expose(relit, i)]
        tiles = [t.clamp(0, 1).expand(height, width, 3) for t in tiles]
        grid = torch.cat([torch.cat(tiles[:3], 1), torch.cat(tiles[3:], 1)], 0)
        Image.fromarray((grid.cpu().numpy() * 255).astype(np.uint8)).save(out + "_rastav.png")
    print(f"Spremljeno: {out}.json, {out}_albedo.ply, {out}_okolina.png, {out}_rastav.png")
    if args.opis:
        upisi(dict(vrsta="ocjena", snimka=snimka_modela(model), opis=args.opis, razlucivost=f"{width}x{height}",
                   psnr=round(float(scores[:, 0].mean()), 3), kadrova=len(scores), splat=Path(args.splat).name,
                   izvorni_psnr=round(float(scores[:, 1].mean()), 3),
                   povrsinski_psnr=round(float(scores[:, 2].mean()), 3), sunce_visina=round(elevation, 1),
                   smjer_raspon_db=round(bestPsnr - worstPsnr, 3), smjer_nesigurnost_st=round(spreadAngle)))


if __name__ == "__main__":
    main()
