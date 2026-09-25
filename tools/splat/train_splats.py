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
import argparse, json, math, os, struct, sys, time
from pathlib import Path

#Fragmentacija je odnijela 3.16 GB od 11.49 pri prvom punom treningu - memorija je bila rezervirana
#ali neiskoristiva, jer zgusnjavanje stalno trazi sve vece susjedne blokove. Mora stajati PRIJE
#uvoza torcha, jer se cita jednom pri pokretanju
os.environ.setdefault("PYTORCH_CUDA_ALLOC_CONF", "expandable_segments:True")

import numpy as np
import torch
from PIL import Image

import gsplat
from gsplat.strategy import DefaultStrategy, MCMCStrategy

sys.path.insert(0, str(Path(__file__).resolve().parent))      #floaters.py uz ovu skriptu


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


#=============================================================================================
# SSIM: mjera strukture, ne prosjeka
#
# ZASTO. L1 mjeri prosjecnu razliku po pikselima, a prosjek ne razlikuje ostro od mutnog - mutna
# mrlja preko svjetiljke ima isti prosjek kao ostra zarulja, pa optimizacija nema razloga popraviti
# je. Izmjereno: udvostrucena razlucivost i udvostruceni broj koraka dali su LOSIJI rezultat
# (0.0142 -> 0.0169), sto je simptom bas toga - plato nije u broju koraka nego u tome sto se mjeri.
#
# SSIM usporedjuje lokalnu srednju vrijednost, raspon i suodnos dvaju prozora, pa mutnocu kaznjava
# izravno. Izvorni rad o 3DGS koristi 0.8*L1 + 0.2*(1-SSIM), i to je omjer koji je ovdje zadan.
#=============================================================================================

def gaussian_window(size, sigma, device):
    coords = torch.arange(size, dtype=torch.float32, device=device) - (size - 1) / 2
    line = torch.exp(-(coords ** 2) / (2 * sigma ** 2))
    line = line / line.sum()
    return (line[:, None] @ line[None, :])[None, None, ...]


def ssim(a, b, window, size):
    """a i b su (H, W, 3) u rasponu 0..1."""
    x = a.permute(2, 0, 1)[None]
    y = b.permute(2, 0, 1)[None]
    kernel = window.expand(3, 1, size, size)

    pad = size // 2
    mx = torch.nn.functional.conv2d(x, kernel, padding=pad, groups=3)
    my = torch.nn.functional.conv2d(y, kernel, padding=pad, groups=3)

    mxx, myy, mxy = mx * mx, my * my, mx * my
    vx = torch.nn.functional.conv2d(x * x, kernel, padding=pad, groups=3) - mxx
    vy = torch.nn.functional.conv2d(y * y, kernel, padding=pad, groups=3) - myy
    vxy = torch.nn.functional.conv2d(x * y, kernel, padding=pad, groups=3) - mxy

    #Konstante iz izvornog rada o SSIM-u; drze racun stabilnim kad su srednja vrijednost ili
    #raspon blizu nule, sto se na tamnim plohama dogadja stalno
    c1, c2 = 0.01 ** 2, 0.03 ** 2
    return (((2 * mxy + c1) * (2 * vxy + c2)) / ((mxx + myy + c1) * (vx + vy + c2))).mean()


def ssim_fast(a, b, line):
    """Isti SSIM kao ssim(), za gubitak u treningu: Gaussov prozor je umnozak dvaju 1D, pa se pet
    zamucivanja (x, y, x^2, y^2, xy) radi kao JEDNA grupna konvolucija vodoravno i jedna okomito -
    22 umjesto 5 x 121 mnozenja po pikselu. Rub s nulama se razdvaja isto, pa je rezultat isti do
    zaokruzivanja. Na 1080p s 3.75M gaussiana gubitak je bio 8.3 ms od 99 ms koraka"""
    x = a.permute(2, 0, 1)
    y = b.permute(2, 0, 1)
    stack = torch.cat([x, y, x * x, y * y, x * y], 0)[None]
    size = line.numel(); pad = size // 2
    across = torch.nn.functional.conv2d(stack, line.view(1, 1, 1, size).expand(15, 1, 1, size), padding=(0, pad), groups=15)
    blurred = torch.nn.functional.conv2d(across, line.view(1, 1, size, 1).expand(15, 1, size, 1), padding=(pad, 0), groups=15)[0]
    mx, my, sxx, syy, sxy = blurred.split(3)
    mxx, myy, mxy = mx * mx, my * my, mx * my
    vx, vy, vxy = sxx - mxx, syy - myy, sxy - mxy
    c1, c2 = 0.01 ** 2, 0.03 ** 2
    return (((2 * mxy + c1) * (2 * vxy + c2)) / ((mxx + myy + c1) * (vx + vy + c2))).mean()


#=============================================================================================
# Dubina iz modela kao uporiste ondje gdje geometrija nema nikakvo
#
# ZASTO. Rekonstrukcija daje tocke samo gdje ima teksture: izmjereno na ovoj snimci, 313 tocaka po
# kadru na tamnom parketu i 20499 na kamenom zidu. Ondje gdje tocaka nema, gaussiane nemaju sto
# drzati na mjestu - pa lebde, i iz novog kuta se raspadnu. Bas to strogi test i kaznjava.
#
# Monokularni model dubine (Depth Anything) ne zna mjerilo ni gdje je kamera, ali zna sto je BLIZE
# a sto DALJE - i to zna i na praznom zidu. To je uporiste koje geometrija ondje nema.
#
# MJERILO I POMAK SE NE ZNAJU, pa se ne smiju ni pretpostaviti: model daje dispariter do na linearnu
# preobrazbu. Zato se po slici najprije NAJMANJIM KVADRATIMA namjesti a*d + b na ono sto scena
# stvarno crta, pa se kaznjava tek ostatak. Bez toga bi se kaznjavala razlika u mjerilu, koja nije
# greska nego svojstvo modela.
#
# IZMJERENO: NE POMAZE, i zato je zadano iskljuceno (--depth prazno). Na 634 kadra, 30000 koraka,
# strogi test s izdvojenim odsjeccima:
#
#   bez dubine          PSNR 29.07 dB, najgori 25.06
#   dubina tezina 0.05  PSNR 29.01 dB, najgori 25.20
#   dubina tezina 1.0   PSNR 28.95 dB, najgori 24.79
#
# Prva tezina je bila isuvise mala da bi se uopce cula - clan dubine je oko 0.01 uz gubitak od 0.04,
# dakle jedan posto - pa je "nema razlike" tada znacilo "nije ni ukljuceno". Uz tezinu koja se cuje
# rezultat je neznatno GORI. Ne znaci da dubina opcenito ne radi; znaci da na ovoj sceni i s ovom
# izvedbom ne daje nista. Moguce je da je normalizacija po slici pregruba, ili da model dubine u
# tamnim dijelovima grijesi taman koliko i geometrija - a bas su ti dijelovi bili razlog.
#=============================================================================================

def read_pfm(path):
    with open(path, "rb") as f:
        header = f.readline().decode().strip()
        if header not in ("Pf", "PF"):
            raise SystemExit(f"{path}: nije PFM")
        channels = 1 if header == "Pf" else 3
        w, h = (int(v) for v in f.readline().decode().split())
        scale = float(f.readline().decode().strip())
        data = np.frombuffer(f.read(w * h * channels * 4),
                             dtype="<f4" if scale < 0 else ">f4")
        image = data.reshape(h, w, channels)[::-1]      #PFM ide odozdo prema gore
        return image[..., 0].copy()


def pose_correction(delta, spread):
    """Ispravak poze kadra iz 6 brojeva (os-kut zakreta u radijanima, pomak u jedinicama mjerila
    scene) -> 4x4 koji se mnozi s lijeve strane matrice svijet->kamera: ispravlja kameru, ne scenu.
    Isti racun koristi evaluate_splat.py za <splat>_poses.json"""
    w = delta[..., :3]
    zero = torch.zeros_like(w[..., 0])
    skew = torch.stack([zero, -w[..., 2], w[..., 1], w[..., 2], zero, -w[..., 0],
                        -w[..., 1], w[..., 0], zero], -1).reshape(*w.shape[:-1], 3, 3)
    M = torch.eye(4, device=delta.device, dtype=delta.dtype).expand(*w.shape[:-1], 4, 4).clone()
    M[..., :3, :3] = torch.linalg.matrix_exp(skew)
    M[..., :3, 3] = delta[..., 3:] * spread
    return M


def depth_normals(depth, K, step=1):
    """Normale iz nacrtane dubine (H, W) u sustavu kamere (OpenCV), okrenute prema kameri"""
    H, W = depth.shape
    v, u = torch.meshgrid(torch.arange(H, device=depth.device, dtype=depth.dtype),
                          torch.arange(W, device=depth.device, dtype=depth.dtype), indexing="ij")
    P = torch.stack([(u - K[0, 2]) / K[0, 0] * depth, (v - K[1, 2]) / K[1, 1] * depth, depth], -1)
    s = step
    dx = P[s:-s, 2 * s:] - P[s:-s, :-2 * s]
    dy = P[2 * s:, s:-s] - P[:-2 * s, s:-s]
    n = torch.nn.functional.normalize(-torch.cross(dx, dy, dim=-1), dim=-1)
    return torch.nn.functional.pad(n.permute(2, 0, 1)[None], (s, s, s, s), mode="replicate")[0].permute(1, 2, 0)


def bilagrid_identity(count, device):
    """Bilateralna mreza po kadru: 12 brojeva (afina 3x4 boje) u mrezi 8 (svjetlina) x 16 x 16"""
    grid = torch.zeros(count, 12, 8, 16, 16, device=device)
    grid[:, 0] = grid[:, 5] = grid[:, 10] = 1.0
    return grid


def bilagrid_apply(grid, image):
    """Wang i sur. 2024: afina boje po pikselu iz mreze (x, y, svjetlina slike). grid (12, 8, 16, 16),
    image (h, w, 3). Svjetlina vodilja se ne derivira"""
    h, w = image.shape[:2]
    grey = (image.detach() * torch.tensor([0.299, 0.587, 0.114], device=image.device)).sum(-1).clamp(0, 1)
    ys, xs = torch.meshgrid(torch.linspace(-1, 1, h, device=image.device),
                            torch.linspace(-1, 1, w, device=image.device), indexing="ij")
    coords = torch.stack([xs, ys, grey * 2 - 1], -1)[None, None]
    affine = torch.nn.functional.grid_sample(grid[None], coords, align_corners=True)[0, :, 0]
    affine = affine.permute(1, 2, 0).reshape(h, w, 3, 4)
    return (affine[..., :3] @ image[..., None])[..., 0] + affine[..., 3]


def bilagrid_tv(grid):
    return sum(((grid.narrow(d, 1, grid.shape[d] - 1) - grid.narrow(d, 0, grid.shape[d] - 1)) ** 2).mean()
               for d in (-1, -2, -3))


def fill_between(trained, names):
    """Vrijednosti po kadru za kadrove koji nisu ucili (izdvojeni): linearno izmedju najblizeg
    ucenog prije i poslije po imenu (redoslijed snimke)"""
    order = sorted(set(trained) | set(names))
    out = {}
    for position, name in enumerate(order):
        if name in trained: continue
        before = next((order[j] for j in range(position - 1, -1, -1) if order[j] in trained), None)
        after = next((order[j] for j in range(position + 1, len(order)) if order[j] in trained), None)
        if before and after:
            a, b = order.index(before), order.index(after)
            t = (position - a) / max(1, b - a)
            out[name] = (1 - t) * trained[before] + t * trained[after]
        elif before or after:
            out[name] = trained[before or after]
    return out


def rgb_to_sh0(rgb):
    return (rgb - 0.5) / 0.28209479177387814            # C0 clan sfernih harmonika


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model", help="mapa s cameras.txt, images.txt, points3D.txt")
    ap.add_argument("images", help="mapa sa slikama")
    ap.add_argument("output", help="gdje spremiti PLY")
    ap.add_argument("--steps", type=int, default=15000,
                    help="15000: ostrina +19 %% prema 7000 uz isti PSNR na izdvojenim kadrovima; 30000 ostrije, ali pocinje pamtiti snimljene kadrove (benchmarks/MJERENJA.md)")
    ap.add_argument("--downscale", type=int, default=2, help="4K je za 12 GB previse; 2 znaci pola")
    ap.add_argument("--sh-degree", type=int, default=3)
    ap.add_argument("--loss", choices=["l1", "ssim"], default="ssim",
                    help="l1 je samo prosjek po pikselima; ssim dodaje mjeru strukture")
    ap.add_argument("--max-gaussians", type=int, default=0,
                    help="0 znaci izvedi iz slobodne memorije kartice")
    #OBA SU ZADANO ISKLJUCENA, i to je mjereno a ne pretpostavka - vidi komentare uz njih
    ap.add_argument("--rasterize", choices=["classic", "antialiased"], default="classic",
                    help="antialiased obraduje gaussiane manje od piksela drukcije; izmjereno bez ucinka")
    ap.add_argument("--anisotropy", type=float, default=10.0,
                    help="od ovog omjera najduze/najkrace osi pa navise gaussiana se kaznjava; 0 iskljucuje")
    ap.add_argument("--anisotropy-weight", type=float, default=0.0,
                    help="koliko ta kazna tezi u gubitku")
    ap.add_argument("--depth", default="",
                    help="mapa s PFM kartama dubine (tools/depth/estimate_depth.py); prazno iskljucuje")
    #TEZINA 0.05 JE BILA ISKLJUCENO, a ne oprezno: clan dubine je oko 0.01, gubitak oko 0.04, pa je
    #doprinos bio jedan posto. Izmjereno je i s 1.0 - ni tada ne pomaze, vidi komentar uz read_pfm
    ap.add_argument("--depth-weight", type=float, default=1.0,
                    help="koliko dubina tezi u gubitku")
    #MCMC JE ZADANO, i to je mjereno: 29.07 dB naspram 26.70, a najgori kadar 25.06 naspram 18.42
    ap.add_argument("--strategy", choices=["default", "mcmc"], default="mcmc",
                    help="mcmc PREMJESTA gaussiane umjesto da ih dodaje po gradijentu")
    ap.add_argument("--holdout", type=int, default=0,
                    help="svaki N-ti kadar se IZDVAJA iz treninga i sluzi za ocjenu; 0 iskljucuje")
    ap.add_argument("--holdout-block", type=int, default=1,
                    help="koliko UZASTOPNIH kadrova se izdvaja odjednom; 1 znaci pojedinacno")
    ap.add_argument("--saturation", type=float, default=1.0,
                    help="od ove svjetline pa navise piksel se manje broji; 1.0 iskljucuje")
    #KAZNE IZ RADA O MCMC SPLATOVIMA, i IZMJERENO: STETE, pa su zadano iskljucene. C0257, 7000
    #koraka, 39 izdvojenih kadrova: bez njih PSNR 24.86 dB, s 0.01/0.01 17.99 dB. Adam normira
    #gradijent po gaussiani, a u videu je svaka vidljiva u malo kadrova - u svim ostalim koracima
    #dobije SAMO kaznu, i to punim korakom, pa se gasi i ono sto kadrovi trebaju. Floatere zato
    #mice --clean na kraju, a ne kazna usput
    ap.add_argument("--opacity-reg", type=float, default=0.0,
                    help="L1 kazna na neprozirnost (MCMC rad: 0.01); tjera prozirne koprene da nestanu")
    ap.add_argument("--scale-reg", type=float, default=0.0,
                    help="L1 kazna na velicinu u mjerilu scene (MCMC rad: 0.01); krupne mrlje se smanje")
    #Izmjereno na izdvojenim kadrovima (floaters.py): PSNR prosjek 24.37 -> 24.70 dB, a splat
    #cetiri puta manji. --no-clean ga iskljucuje
    #MODEL KAMERE PRI CRTANJU. classic je dosadasnji (EWA, jedna poza po kadru). ut je gsplatov
    #3DGUT s istom jednom pozom - kontrola, da se vidi koliko mijenja sam nacin projekcije. rolling
    #je 3DGUT s ROLLING SHUTTEROM: gornji redak u pozi iz model/rs_top, donji iz model/rs_bottom
    #(RollingShutterProbe ih zapise), a izmedju se poza mijenja redak po redak
    #ZADANO auto: rolling kad VideoSolve uz rezultat zapise rs_top i rs_bottom (izmjerio je rolling
    #shutter), inace classic. Izmjereno na C0257, 39 izdvojenih kadrova: rolling 25.52 dB, ista
    #scena bez njega (ut) 23.43, classic 24.46
    #ZADANO classic: rolling je izmjeren +1.06, +0.25 i -1.97 dB na tri para treninga - dakle unutar
    #suma izmedju treninga, dok se ne izmjeri s vise sjemena i parno po kadru (evaluate_splat.py)
    ap.add_argument("--camera-model", choices=["auto", "classic", "ut", "rolling", "rsmid"], default="classic",
                    help="auto: rolling kad postoje rs_top/rs_bottom, inace classic; rsmid: poze iz bundlea s "
                         "rolling shutterom (sredina kadra), a crta se obicno - bez mutnoce 3DGUT-a")
    ap.add_argument("--seed", type=int, default=0,
                    help="sjeme za torch (MCMC premjestanje i sum); isti seed smanjuje razliku izmedju treninga")
    #ZAMUCENJE POKRETOM: kadar skuplja svjetlo cijelu ekspoziciju dok se kamera mice (C0257: 1/100 s
    #uz ~10 st/s, dakle ~8 px na 4K). Crta se kao prosjek K trenutaka ekspozicije, svaki redak po
    #redak. Trazi rolling shutter (rs_top/rs_bottom daju gibanje) i vrijeme ekspozicije
    #(camera_metadata.txt iz VideoSolvea, ili --exposure-frames). 0 iskljucuje; cijena je K puta
    #vise crtanja po koraku
    ap.add_argument("--motion-blur", type=int, default=0,
                    help="koliko trenutaka ekspozicije se crta po kadru (0/1 iskljucuje, npr. 3)")
    ap.add_argument("--exposure-frames", type=float, default=0.0,
                    help="ekspozicija u kadrovima (1/100 s pri 50 fps = 0.5); 0 cita camera_metadata.txt")
    ap.add_argument("--clean", action=argparse.BooleanOptionalAction, default=True,
                    help="na kraju makni floatere (floaters.py): nevidljive i mrlje uz kameru")
    #ZADANO UKLJUCENO: na C0257 (auto-ISO 500-1250) izdvojeni PSNR +0.33 +- 0.07 dB, SSIM bolji na
    #30/39, ostrina ista, vrijeme isto
    ap.add_argument("--exposure", action=argparse.BooleanOptionalAction, default=True,
                    help="naucena korekcija boje po kadru (pojacanje i pomak po kanalu) za auto-ISO; "
                         "izdvojeni kadrovi dobiju interpoliranu od susjeda, zapise se u <izlaz>_exposure.json")
    #ZADANO UKLJUCENO: na C0257 (15000 koraka, bez granice) korak 99 -> 73 ms pri 3.75M gaussiana,
    #ostrina +10 % (1080p) i +25 % (4K, bolja na 39/39), PSNR prosjek -0.18 +- 0.08 ali medijan +0.3
    ap.add_argument("--visible-adam", action=argparse.BooleanOptionalAction, default=True,
                    help="optimizator gaussiana azurira samo one vidljive u kadru (gsplat SelectiveAdam)")
    #ZADANO 1000, uz granicu 2.5M: na C0257 trening 17.5 -> 15.9 min, SSIM bolji na 31/39,
    #PSNR i ostrina u sumu; budzet ide na vidljive (na kraju 2.09M korisnih prema 1.58M)
    ap.add_argument("--prune-every", type=int, default=1000,
                    help="svakih N koraka (od 3000.) ukloni gaussiane nevidljive u svim trening kadrovima; 0 iskljuci")
    ap.add_argument("--prune-budget", type=int, default=2500000,
                    help="uz --prune-every i bez --max-gaussians: granica je manja od ove i one iz memorije kartice")
    ap.add_argument("--profile", action="store_true",
                    help="svakih 500 koraka prosjecno vrijeme po dijelu koraka (sinkronizira karticu - samo za mjerenje)")
    ap.add_argument("--finish-full-res", type=int, default=0,
                    help="jos toliko koraka NA PUNOJ RAZLUCIVOSTI nakon --steps (postupno: grubo pa fino)")
    #=============================================================================================
    # POZE SE DOTJERUJU U TRENINGU (--pose-opt). Solve ima 0.87 px na 4K preciznim znacajkama, a
    # pola do jednog piksela krive poze je zamucenje koje nijedan broj gaussiana ne moze skinuti:
    # svaki kadar vidi scenu malo pomaknutu, pa splat nauci prosjek. Svaki kadar dobije mali
    # ispravak (zakret + pomak) koji se uci s gradijentom zajedno sa splatom; izdvojeni kadrovi
    # dobiju ispravak interpoliran od susjeda (isto kao ekspozicija) i zapisu se u <izlaz>_poses.json
    #=============================================================================================
    ap.add_argument("--pose-opt", action=argparse.BooleanOptionalAction, default=False,
                    help="uci ispravak poze po kadru; zapisuje <izlaz>_poses.json")
    #PRVI POKUSAJ (E14, obicni Adam, 1e-4 od 500. koraka) je poze pustio da odlutaju: zakret
    #medijan 0.42 st umjesto ocekivanih stotinki, PSNR +0.68 dB ali ostrina 0.44 -> 0.14. Adam je
    #zamahom micao i kadrove koji u tom koraku nisu ni crtani. Sad: rijetki Adam (samo crtani kadar),
    #deset puta manji korak, od 3000. koraka kad je raspored nadjen
    ap.add_argument("--pose-lr", type=float, default=1e-5, help="korak ucenja ispravka poze (radijani / mjerilo scene)")
    ap.add_argument("--pose-from", type=int, default=3000, help="od kojeg koraka se poze uce")
    #=============================================================================================
    # POVRSINA U TRENINGU (--surface W). Normala gaussiane (najkraca os) se slaze s normalom nacrtane
    # dubine, a gaussiana je plosnata (--surface-flat). Na gotovom C0257 splatu je isto dotjerivanje
    # (relight.py faza 1) dalo +0.2 dB na izdvojenim i normale koje su povrsina; ovdje ide od koraka
    # --surface-from, kad je raspored vec nadjen (kao 2DGS od 7000)
    #=============================================================================================
    ap.add_argument("--surface", type=float, default=0.0, help="tezina slaganja normale gaussiane i dubine (0 = iskljuceno)")
    ap.add_argument("--surface-flat", type=float, default=0.1, help="tezina plosnatosti (najkraca / najdulja os)")
    ap.add_argument("--surface-from", type=int, default=7000)
    #=============================================================================================
    # BILATERALNA MREZA (--bilagrid) umjesto pojacanja i pomaka po kadru: afina boje koja ovisi o
    # mjestu u kadru i svjetlini (vinjeta, lokalno tonsko mapiranje, bijela ravnoteza koja se mijenja).
    # Wang i sur. 2024 (Bilateral Guided Radiance Field Processing). Izdvojeni kadrovi dobiju mrezu
    # interpoliranu od susjeda; zapis <izlaz>_bilagrid.npz
    #=============================================================================================
    ap.add_argument("--bilagrid", action=argparse.BooleanOptionalAction, default=False)
    ap.add_argument("--bilagrid-tv", type=float, default=10.0, help="glatkoca mreze")
    ap.add_argument("--opis", default="",
                    help="sto se ovim treningom mjeri; ide u dnevnik mjerenja (benchmarks/mjerenja.jsonl)")
    args = ap.parse_args()
    if args.surface > 0 and args.depth:
        raise SystemExit("--surface i --depth zajedno nisu podrzani: oba citaju dodatne kanale istog crtanja")
    started = time.time()
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    device = "cuda"
    if not torch.cuda.is_available():
        raise SystemExit("Nema kartice - trening bez nje nema smisla")

    model = Path(args.model)
    camera = read_cameras(model / "cameras.txt")
    frames = read_images(model / "images.txt")
    if args.camera_model == "auto":
        args.camera_model = "rolling" if (model / "rs_top" / "images.txt").exists() and \
                                         (model / "rs_bottom" / "images.txt").exists() else "classic"
    #Uz rolling shutter i pocetne tocke iz bundlea s njim (rs_top ih nosi), da poze i tocke budu iz
    #istog rjesenja
    points, colours = read_points(model / ("rs_top" if args.camera_model in ("rolling", "rsmid") else ".") / "points3D.txt")
    print(f"Model kamere: {args.camera_model}")
    print(f"Model: {len(frames)} kamera, {len(points)} tocaka, {camera['width']}x{camera['height']}")

    # -------------------------------------------------------------------------------
    # Slike
    # -------------------------------------------------------------------------------
    scale = args.downscale
    width, height = camera["width"] // scale, camera["height"] // scale
    K = torch.tensor([[camera["fx"]/scale, 0, camera["cx"]/scale],
                      [0, camera["fy"]/scale, camera["cy"]/scale],
                      [0, 0, 1]], dtype=torch.float32, device=device)

    #SLIKE SE DRZE KAO BAJTOVI, ne kao float. Piksel je u datoteci osam bita po kanalu i pretvorba
    #u float ga ne cini tocnijim - samo cetiri puta vecim. Na 634 slike u punoj polovici razlucivosti
    #to je razlika izmedju 15.8 GB i 3.9 GB, dakle izmedju "ne stane" i "stane".
    #
    #Pretvorba u 0..1 se radi na kartici, nad jednom slikom po koraku, i tamo je besplatna.
    needed = len(frames) * width * height * 3 / (1 << 30)
    if needed > 12.0:
        raise SystemExit(
            f"Slike bi uzele {needed:.1f} GB radne memorije ({len(frames)} kom, {width}x{height}).\n"
            f"Povecaj --downscale: svaki korak dijeli s cetiri.")

    #KARTE SE POVEZUJU SA SLIKAMA PO REDOSLIJEDU, ne po imenu. estimate_depth.py ih zove
    #depth_0000.pfm a kadrovi su 00001.jpg - pravilo prevodjenja bi bilo tocno danas i krivo cim
    #se ijedna od dvije skripte preimenuje. Obje mape dolaze iz istog niza, pa je redoslijed ono
    #sto ih stvarno povezuje
    depthFor = {}
    if args.depth:
        allImages = sorted(p.name for p in Path(args.images).iterdir() if p.suffix.lower() in (".jpg", ".png"))
        allDepths = sorted(Path(args.depth).glob("*.pfm"))
        #BROJEVI SE MORAJU POKLAPATI TOCNO. Prva verzija je trazila samo "barem toliko" i time tiho
        #uparila 101 sliku jednog isjecka s prvih 101 karata cijele snimke - dakle krive kadrove, bez
        #ijedne poruke. Trening je prosao i dao broj koji ne znaci nista.
        #
        #Kad se povezuje po redoslijedu, jednak broj je jedino sto potvrdjuje da su to isti kadrovi
        if len(allDepths) != len(allImages):
            raise SystemExit(
                f"Karata dubine je {len(allDepths)}, a slika {len(allImages)} - to nisu isti kadrovi.\n"
                f"Karte se povezuju po redoslijedu, pa moraju biti napravljene bas iz ove mape slika.")
        depthFor = {name: path for name, path in zip(allImages, allDepths)}

    #Rolling shutter: poze gornjeg i donjeg retka, po imenu slike
    rowViews = {}
    if args.camera_model in ("rolling", "rsmid"):
        for which in ("rs_top", "rs_bottom"):
            listed = model / which / "images.txt"
            if not listed.exists():
                raise SystemExit(f"{listed} ne postoji - napravi ga s RollingShutterProbe (ROLLING_WRITE)")
            rowViews[which] = {name: view for name, view in read_images(listed)}

    depthMaps = []
    views, pictures, viewsEnd = [], [], []
    pictureNames = []
    for name, view in frames:
        path = Path(args.images) / name
        if not path.exists():
            continue
        pictureNames.append(name)
        picture = Image.open(path).convert("RGB").resize((width, height), Image.LANCZOS)
        pictures.append(torch.from_numpy(np.asarray(picture, dtype=np.uint8)))
        if rowViews:
            views.append(torch.from_numpy(rowViews["rs_top"][name]).float())
            viewsEnd.append(torch.from_numpy(rowViews["rs_bottom"][name]).float())
        else:
            views.append(torch.from_numpy(view).float())
            viewsEnd.append(torch.from_numpy(view).float())

        if args.depth:
            pfm = depthFor.get(name)
            if pfm is None:
                raise SystemExit(f"Nema karte dubine za {name}")
            prior = read_pfm(str(pfm))
            small = np.asarray(Image.fromarray(prior).resize((width, height), Image.BILINEAR),
                               dtype=np.float32)
            depthMaps.append(torch.from_numpy(small))
    if not pictures:
        raise SystemExit("Nijedna slika se nije nasla")

    #SLIKE OSTAJU NA PROCESORU, na karticu ide samo ona koja se u tom koraku crta. Sve odjednom
    #je za 65 slika 1.6 GB i prolazi, ali za 677 je 17 GB - a kartica ima 12. Poze su sitne pa one
    #smiju ostati gore
    #IZDVOJENI KADROVI. Ocjena na kadru na kojem je model treniran mjeri koliko ga je zapamtio, ne
    #koliko je scenu razumio - a bas se ta razlika i trazi. Objavljene brojke za 3DGS mjere se na
    #kadrovima koje model nikad nije vidio, pa se i nase moraju tako mjeriti da bi se usporedile.
    #
    #Uzima se svaki N-ti, ne nasumicnih N posto: susjedni kadrovi na snimci su gotovo isti, pa bi
    #nasumican izbor izdvojio kadar cijeli susjed kojega je u treningu - i ocjena bi opet bila
    #precijenjena, samo neprimjetnije
    #IZDVAJANJE U ODSJECCIMA, ne pojedinacno. Prva verzija je uzimala svaki N-ti kadar i to nije
    #bilo dovoljno: model uzima svaki peti kadar snimke, pa je izdvojeni kadar od svojih susjeda u
    #treningu udaljen desetinku sekunde gibanja kamere - prakticki isti pogled. Ocjena je time
    #mjerila gotovo isto sto i ocjena na kadru iz treninga.
    #
    #Kad se izdvoji odsjecak od K uzastopnih kadrova, sredina tog odsjecka je od najblizeg kadra iz
    #treninga udaljena K/2 koraka - i tek tada se mjeri sto model zna o pogledu koji nije vidio
    heldOut = []
    if args.holdout > 1:
        block = max(1, args.holdout_block)
        for start in range(0, len(pictures), args.holdout * block):
            heldOut.extend(range(start, min(start + block, len(pictures))))
        keep = [i for i in range(len(pictures)) if i not in set(heldOut)]
        heldPictures = [pictures[i] for i in heldOut]
        heldNames = [pictureNames[i] for i in heldOut]
        pictureNames = [pictureNames[i] for i in keep]
        heldViews = [views[i] for i in heldOut]
        heldViewsEnd = [viewsEnd[i] for i in heldOut]
        pictures = [pictures[i] for i in keep]
        views = [views[i] for i in keep]
        viewsEnd = [viewsEnd[i] for i in keep]
        if len(depthMaps): depthMaps = [depthMaps[i] for i in keep]
        print(f"Izdvojeno {len(heldPictures)} kadrova za ocjenu, trenira se na {len(pictures)}")

    if depthMaps:
        depthMaps = torch.stack(depthMaps)
        print(f"Karte dubine: {len(depthMaps)} kom")

    pictures = torch.stack(pictures)
    views = torch.stack(views).to(device)
    viewsEnd = torch.stack(viewsEnd).to(device)
    if heldOut:
        heldPictures = torch.stack(heldPictures)
        heldViews = torch.stack(heldViews).to(device)
        heldViewsEnd = torch.stack(heldViewsEnd).to(device)
    gigabytes = pictures.numel() / (1 << 30)
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
    #VIDLJIVI ADAM (--visible-adam): obicni Adam svaki korak prode kroz SVE gaussiane (26 ms od 99 pri
    #3.75M), a u jednom kadru vidi se tek dio njih. SelectiveAdam azurira momente samo vidljivima
    if args.visible_adam:
        from gsplat.optimizers import SelectiveAdam
        optimizers = {k: SelectiveAdam([{"params": params[k], "lr": rates[k], "name": k}],
                                       eps=1e-15, betas=(0.9, 0.999)) for k in params}
    else:
        optimizers = {k: torch.optim.Adam([{"params": params[k], "lr": rates[k], "name": k}],
                                          eps=1e-15, betas=(0.9, 0.999)) for k in params}

    #EKSPOZICIJA PO KADRU (--exposure). Kamera snima na auto-ISO (C0257: 500-1250, 27 promjena),
    #pa ista ploha u razlicitim kadrovima ima razlicitu svjetlinu - a bez ovoga je trener mora
    #objasniti bojom i geometrijom gaussiana, tj. mrljama i floaterima. Svaki kadar dobije pojacanje i
    #pomak po kanalu; splat uci prosjecnu, pravu boju scene
    poseDelta, poseOptimizer = None, None
    if args.pose_opt:
        poseTable = torch.nn.Embedding(len(pictures), 6, sparse=True).to(device)
        torch.nn.init.zeros_(poseTable.weight)
        poseDelta = poseTable.weight
        poseOptimizer = torch.optim.SparseAdam(poseTable.parameters(), lr=args.pose_lr)
    bilagrid, bilagridOptimizer = None, None
    if args.bilagrid:
        bilagrid = bilagrid_identity(len(pictures), device).requires_grad_(True)
        bilagridOptimizer = torch.optim.Adam([bilagrid], lr=2e-3)
        args.exposure = False
    exposure, exposureOptimizer = None, None
    if args.exposure:
        exposure = torch.zeros(len(pictures), 2, 3, device=device, requires_grad=True)
        exposureOptimizer = torch.optim.Adam([exposure], lr=1e-3)

    # -------------------------------------------------------------------------------
    # Koliko gaussiana kartica podnosi
    # -------------------------------------------------------------------------------
    #
    # Zgusnjavanje raste dok ga nesto ne zaustavi, a zaustavila ga je kartica: prvi puni trening s
    # SSIM-om je pukao na 4 064 257 gaussiana u 13000. koraku, na kartici od 11.49 GiB.
    #
    # Po gaussiani se drzi parametar, dva Adamova stanja i gradijent - dakle cetiri primjerka - a
    # pri dijeljenju se polja nakratko udvostruce. Zato se granica racuna iz SLOBODNE memorije, a ne
    # zadaje brojem: ista scena na drugoj kartici ima drugu granicu.
    floatsPer = 3 + 3 + 4 + 1 + 3 + 3 * (sh_count - 1)
    bytesPer = floatsPer * 4 * 4                      #parametar + dva stanja + gradijent

    if args.max_gaussians > 0:
        budget = args.max_gaussians
    else:
        free, total = torch.cuda.mem_get_info()
        #Trecina slobodne memorije: ostatak trosi rasterizacija, unatrazni prolaz i vrhunac pri
        #dijeljenju. Izmjereno da granica tako ispadne blizu one na kojoj je stvarno puklo
        budget = int(free * 0.33 / bytesPer)
        if args.prune_every > 0 and args.prune_budget > 0: budget = min(budget, args.prune_budget)
        print(f"Kartica: {free / (1 << 30):.1f} GB slobodno, {bytesPer} B po gaussiani "
              f"-> granica {budget / 1e6:.1f} M")

    capped = False

    #DVIJE STRATEGIJE ZGUSNJAVANJA, i razlika je u vrsti. Default DODAJE gaussiane ondje gdje je
    #gradijent velik, pa raste dok ga nesto ne zaustavi - kod nas kartica. MCMC drzi FIKSAN broj i
    #premjesta ih: mrtvu gaussianu preseli tamo gdje fali. Na neravnomjernoj pokrivenosti - a nasa
    #snimka daje 313 tocaka po kadru na parketu i 20499 na kamenom zidu - to bi trebalo biti
    #otpornije, jer broj gaussiana ne odlucuje gdje ce zavrsiti
    if args.strategy == "mcmc":
        strategy = MCMCStrategy(cap_max=budget, verbose=False)
    else:
        strategy = DefaultStrategy(verbose=False)
    #MCMC ne uzima mjerilo scene - njegovo premjestanje radi u prostoru parametara, ne u metrima
    state = (strategy.initialize_state() if args.strategy == "mcmc"
             else strategy.initialize_state(scene_scale=spread))
    strategy.check_sanity(params, optimizers)



    # -------------------------------------------------------------------------------
    # Trening
    # -------------------------------------------------------------------------------
    #Jedno mjesto koje crta, za trening, ocjenu i pregled - pa sva tri crtaju istim modelom kamere
    from gsplat.cuda._wrapper import RollingShutterType
    usesUt = args.camera_model not in ("classic", "rsmid")

    #ZAMUCENJE POKRETOM: pomak u vremenu delta (u kadrovima) je pomak duz gibanja izmedju gornjeg i
    #donjeg retka, alfa = delta / citanje. Poze se interpoliraju u prostoru kamere (sredista
    #linearno, rotacija po osi relativnog zakreta), pa se vrate u svijet-u-kameru
    blurSteps = []
    if args.motion_blur > 1 and args.camera_model == "rolling":
        def readValue(file, key):
            if not file.exists(): return 0.0
            for line in open(file):
                parts = line.split()
                if len(parts) == 2 and parts[0] == key: return float(parts[1])
            return 0.0
        exposure = args.exposure_frames or readValue(model / "camera_metadata.txt", "exposure_frames")
        readout = readValue(model / "rolling_shutter.txt", "readout_frames")
        if exposure > 0.0 and readout > 0.0:
            k = args.motion_blur
            blurSteps = [((i + 0.5) / k - 0.5) * exposure / readout for i in range(k)]
            print(f"Zamucenje pokretom: {k} trenutaka, ekspozicija {exposure:.3f} kadra, citanje {readout:.3f}")
        else:
            print("Zamucenje pokretom trazi ekspoziciju (camera_metadata.txt) i citanje (rolling_shutter.txt) - iskljuceno")

    def rotationLog(R):
        cosine = ((R[..., 0, 0] + R[..., 1, 1] + R[..., 2, 2] - 1.0) * 0.5).clamp(-1.0, 1.0)
        angle = torch.acos(cosine)
        axis = torch.stack([R[..., 2, 1] - R[..., 1, 2], R[..., 0, 2] - R[..., 2, 0], R[..., 1, 0] - R[..., 0, 1]], -1)
        scale = torch.where(angle > 1e-6, angle / (2.0 * torch.sin(angle).clamp(min=1e-9)), torch.full_like(angle, 0.5))
        return axis * scale[..., None]
    def rotationExp(w):
        angle = w.norm(dim=-1, keepdim=True).clamp(min=1e-12)
        k = w / angle
        K = torch.zeros(w.shape[:-1] + (3, 3), device=w.device, dtype=w.dtype)
        K[..., 0, 1], K[..., 0, 2], K[..., 1, 0] = -k[..., 2], k[..., 1], k[..., 2]
        K[..., 1, 2], K[..., 2, 0], K[..., 2, 1] = -k[..., 0], -k[..., 1], k[..., 0]
        a = angle[..., None]
        eye = torch.eye(3, device=w.device, dtype=w.dtype).expand_as(K)
        return eye + torch.sin(a) * K + (1.0 - torch.cos(a)) * (K @ K)
    def along(top, bottom, alpha):
        #svijet-u-kameru -> kamera-u-svijet, interpolacija, natrag
        Rt, Rb = top[..., :3, :3].transpose(-1, -2), bottom[..., :3, :3].transpose(-1, -2)
        Ct = -(Rt @ top[..., :3, 3:])[..., 0]
        Cb = -(Rb @ bottom[..., :3, 3:])[..., 0]
        R = Rt @ rotationExp(alpha * rotationLog(Rt.transpose(-1, -2) @ Rb))
        C = Ct + alpha * (Cb - Ct)
        out = torch.eye(4, device=top.device, dtype=top.dtype).expand_as(top).clone()
        out[..., :3, :3] = R.transpose(-1, -2)
        out[..., :3, 3] = -(R.transpose(-1, -2) @ C[..., None])[..., 0]
        return out

    def drawSurface(view, degree):
        """Kao drawOnce za obicnu kameru, ali uz boju crta i normalu gaussiane (6 kanala + dubina).
        SH se racuna ovdje (isto sto rasterizacija radi iznutra), da sve ide u jednom prolazu"""
        centre = torch.linalg.inv(view[0])[:3, 3]
        colours_sh = torch.cat([params["sh0"], params["shN"]], dim=1)
        colour = (gsplat.spherical_harmonics(degree, params["means"] - centre, colours_sh) + 0.5).clamp_min(0.0)
        scales = torch.exp(params["scales"])
        q = torch.nn.functional.normalize(params["quats"], dim=-1)
        w, x, y, z = q.unbind(-1)
        R = torch.stack([1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
                         2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
                         2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)], -1).view(-1, 3, 3)
        axis = torch.argmin(scales, dim=1)
        normal = R.gather(2, axis[:, None, None].expand(-1, 3, 1))[..., 0]
        normal = torch.where(((centre - params["means"]) * normal).sum(-1, keepdim=True) < 0, -normal, normal)
        return gsplat.rasterization(
            means=params["means"], quats=params["quats"], scales=scales,
            opacities=torch.sigmoid(params["opacities"]), colors=torch.cat([colour, normal], -1),
            viewmats=view, Ks=K[None], width=width, height=height, sh_degree=None,
            rasterize_mode=args.rasterize, render_mode="RGB+ED", packed=True)

    def draw(view, viewEnd, degree, mode):
        if blurSteps:
            total, alpha, info = None, None, None
            for step in blurSteps:
                shown, alpha, info = drawOnce(along(view, viewEnd, step), along(view, viewEnd, 1.0 + step), degree, mode)
                total = shown if total is None else total + shown
            return total / len(blurSteps), alpha, info
        return drawOnce(view, viewEnd, degree, mode)

    def drawOnce(view, viewEnd, degree, mode):
        extra = dict(packed=True)
        if usesUt:
            extra = dict(packed=False, with_ut=True, with_eval3d=True)
            if args.camera_model == "rolling":
                extra.update(rolling_shutter=RollingShutterType.ROLLING_TOP_TO_BOTTOM, viewmats_rs=viewEnd)
        colours_sh = torch.cat([params["sh0"], params["shN"]], dim=1)
        return gsplat.rasterization(
            means=params["means"], quats=params["quats"],
            scales=torch.exp(params["scales"]), opacities=torch.sigmoid(params["opacities"]),
            colors=colours_sh, viewmats=view, Ks=K[None], width=width, height=height,
            sh_degree=degree, rasterize_mode=args.rasterize, render_mode=mode, **extra)

    #RSMID: rolling shutter samo za POZE. Bundle s njim daje tocnije poze (+0.39 dB na C0257), ali
    #crtanje redak po redak ide kroz 3DGUT, a on sam splat zamuti (ostrina -10 % na 4K). Poza
    #sredine kadra (pola izmedju gornjeg i donjeg retka) s obicnim crtanjem zadrzava prvo bez drugoga
    if args.camera_model == "rsmid":
        views = along(views, viewsEnd, 0.5); viewsEnd = views
        if heldOut:
            heldViews = along(heldViews, heldViewsEnd, 0.5); heldViewsEnd = heldViews

    windowSize = 11
    window = gaussian_window(windowSize, 1.5, device)
    windowLine = window[0, 0].sum(0)            #1D prozor: redak 2D prozora zbrojen po stupcima
    lastDepthTerm = 0.0
    lastNeedles = 0.0
    print(f"Trening: {args.steps} koraka, mjerilo scene {spread:.2f}, gubitak {args.loss}, "
          f"rasterizacija {args.rasterize}, zasicenje od {args.saturation}")
    generator = torch.Generator(device="cpu").manual_seed(20260915)

    #POSTUPNO PO RAZLUCIVOSTI (--finish-full-res). Na pola razlucivosti geometrija i boje nadju
    #mjesto brzo, ali 4K pogled iz takvog splata je razvucena slika od 1080p (ostrina na 4K 0.10
    #od snimke). Izravno na 4K od pocetka 7000 koraka je premalo (ostrina 0.070) - pa se prvo
    #trenira grubo, a zadnji koraci idu na punoj razlucivosti i dodaju fini detalj
    def loadAt(names, factor):
        out = []
        for name in names:
            picture = Image.open(Path(args.images) / name).convert("RGB")
            if factor > 1: picture = picture.resize((camera["width"] // factor, camera["height"] // factor), Image.LANCZOS)
            out.append(torch.from_numpy(np.asarray(picture, dtype=np.uint8)))
        return torch.stack(out)

    phaseTimes = {}
    phaseMark = [time.time()]
    def tick(name):
        if not args.profile: return
        torch.cuda.synchronize()
        now = time.time()
        phaseTimes[name] = phaseTimes.get(name, 0.0) + now - phaseMark[0]
        phaseMark[0] = now

    for step in range(args.steps + args.finish_full_res):
        tick("ostalo")
        if step == args.steps and args.finish_full_res > 0 and scale > 1:
            scale = 1
            width, height = camera["width"], camera["height"]
            K = torch.tensor([[camera["fx"], 0, camera["cx"]], [0, camera["fy"], camera["cy"]], [0, 0, 1]],
                             dtype=torch.float32, device=device)
            pictures = loadAt(pictureNames, 1)
            if heldOut: heldPictures = loadAt(heldNames, 1)
            print(f"  puna razlucivost od koraka {step}: {width}x{height}, jos {args.finish_full_res} koraka", flush=True)
        index = int(torch.randint(len(pictures), (1,), generator=generator))
        truth = pictures[index].to(device, non_blocking=True).float() / 255.0
        tick("slika")

        view, viewEnd = views[index:index+1], viewsEnd[index:index+1]
        if poseDelta is not None and step >= args.pose_from:
            correction = pose_correction(poseTable(torch.tensor([index], device=device))[0], spread)
            view, viewEnd = correction @ view, correction @ viewEnd
        surfaceNow = args.surface > 0 and step >= args.surface_from and not usesUt and not blurSteps
        if surfaceNow:
            rendered, alpha, info = drawSurface(view, min(args.sh_degree, step // 1000))
        else:
            rendered, alpha, info = draw(view, viewEnd,
                                         min(args.sh_degree, step // 1000),     #niži redovi prvi, kao u izvornom radu
                                         "RGB+ED" if len(depthMaps) else "RGB")

        tick("crtanje")
        strategy.step_pre_backward(params, optimizers, state, step, info)
        image = rendered[0][..., :3]
        if exposure is not None:
            image = image * (1.0 + exposure[index, 0]) + exposure[index, 1]
        if bilagrid is not None:
            image = bilagrid_apply(bilagrid[index], image)

        #ZASICENI PIKSEL NE NOSI PODATAK. Gdje je senzor u zasicenju - zarulja, odsjaj - prava
        #vrijednost je "barem ovoliko", ne "tocno ovoliko", pa optimizacija pokusava pogoditi broj
        #koji u snimci ne postoji.
        #
        #ZADANO ISKLJUCENO, JER MJERENJE NIJE POTVRDILO KORIST. Na sceni s 1.16 posto zasicenih
        #piksela, 7000 koraka: crne mrlje unutar zarulje NESTANU, ali cijela zarulja postane
        #zamucena i razlivena, uz sivi oreol. Razlika od fotografije 0.0167 -> 0.0177. Jedan kvar
        #zamijenjen drugim. Ostaje kao prekidac jer na drugoj snimci moze ispasti drukcije - ali ne
        #kao zadano, dok se ne nadje oblik koji ne zamuti
        if args.saturation < 1.0:
            brightest = truth.max(dim=-1).values
            weight = (1.0 - (brightest - args.saturation).clamp(min=0.0) / (1.0 - args.saturation)).clamp(0.0, 1.0)
            absolute = ((image - truth).abs().mean(dim=-1) * weight).sum() / weight.sum().clamp(min=1.0)
        else:
            absolute = (image - truth).abs().mean()

        if args.loss == "ssim":
            structure = 1.0 - ssim_fast(image, truth, windowLine)
            loss = 0.8 * absolute + 0.2 * structure
        else:
            loss = absolute

        #IGLICE. Gaussiana koja se izduzi u gotovo ravnu crtu s boka je nevidljiva, pa je nista u
        #gubitku ne kaznjava - a cim se kut promijeni, pojavi se kao krhotina preko pola slike.
        #Izmjereno na sceni: 19.65 posto gaussiana je izduzeno preko 10 puta, 0.90 posto preko sto,
        #a najgora ima omjer od dvadeset milijuna - dakle ravnina bez debljine.
        #
        #Kaznjava se tek ono PREKO praga: izduzenost sama po sebi nije greska, ravna ploha i rub
        #stola se njome i opisuju. Greska je kad omjer pobjegne toliko da gaussiana prestane biti
        #tijelo
        if args.anisotropy_weight > 0.0 and args.anisotropy > 0.0:
            #LOGARITAM OMJERA, ne sam omjer. U linearnom obliku 100 gaussiana od 3.85 milijuna nosi
            #52.6 posto kazne - najgora ima omjer od dvadeset milijuna - pa bi gradijent otisao
            #gotovo iskljucivo na njih, a ostalih 750 tisuca izduzenih ostalo bi netaknuto.
            #Logaritam izjednacava: kazna raste s REDOM VELICINE izduzenosti, ne s njezinim brojem
            sizes = torch.exp(params["scales"])
            ratio = sizes.max(dim=1).values / sizes.min(dim=1).values.clamp(min=1e-9)
            needles = torch.relu(torch.log(ratio) - math.log(args.anisotropy)).mean()
            loss = loss + args.anisotropy_weight * needles
            lastNeedles = float(needles)

        #KOPRENE I MRLJE. MCMC premjesta samo gaussiane ispod min_opacity (0.005), a svima dodaje
        #sum to jaci sto su prozirnije - pa poluprozirne odlutaju po sobi i ondje ostanu, jer ih kadar
        #iz kojeg se ne vide ne vraca. Izmjereno na C0257 bez ovoga: 76 posto gaussiana ni u jednom
        #kadru ne doprinosi ni pola piksela, a krupne mrlje uz kameru popravljaju po dva kadra.
        #L1 na neprozirnost i velicinu (rad o MCMC splatovima) daje im razlog da nestanu ili se
        #skupe; velicina se dijeli mjerilom scene, jer solver nema metre
        if args.opacity_reg > 0.0:
            loss = loss + args.opacity_reg * torch.sigmoid(params["opacities"]).mean()
        if args.scale_reg > 0.0:
            loss = loss + args.scale_reg * (torch.exp(params["scales"]) / spread).mean()

        if len(depthMaps):
            #Nacrtana dubina se pretvara u dispariter, jer model daje dispariter - a i zato sto je
            #on ravnomjerniji: u metrima daleki zid nosi tisucu puta vise tezine nego bliski stol
            drawn = rendered[0][..., 3]
            near = 1.0 / drawn.clamp(min=1e-3)
            prior = depthMaps[index].to(device, non_blocking=True)

            #Gleda se samo ono sto je stvarno nacrtano: gdje nema gaussiana dubina je besmislena
            mask = drawn > 1e-3
            if mask.any():
                x = prior[mask]
                y = near[mask]
                #Najmanji kvadrati za a i b u a*x + b ~ y, zatvorenog oblika
                mx, my = x.mean(), y.mean()
                cov = ((x - mx) * (y - my)).mean()
                var = ((x - mx) ** 2).mean().clamp(min=1e-8)
                a = cov / var
                b = my - a * mx
                depthTerm = (a * x + b - y).abs().mean()
                loss = loss + args.depth_weight * depthTerm
                lastDepthTerm = float(depthTerm)
        if surfaceNow:
            gaussianNormal = torch.nn.functional.normalize(rendered[0][..., 3:6], dim=-1)
            depthNormal = depth_normals(rendered[0][..., 6], K)
            solid = (alpha[0].detach() > 0.5).float()
            consistency = (solid * (1 - (gaussianNormal * depthNormal).sum(-1, keepdim=True))).sum() / solid.sum().clamp(min=1.0)
            gaussianScales = torch.exp(params["scales"])
            flatness = (gaussianScales.min(1).values / gaussianScales.max(1).values).mean()
            loss = loss + args.surface * consistency + args.surface_flat * flatness
            lastSurface = (float(consistency), float(flatness))
        if bilagrid is not None:
            loss = loss + args.bilagrid_tv * bilagrid_tv(bilagrid[index])
            bilagridOptimizer.zero_grad(set_to_none=True)
        if exposure is not None:
            #Blago prema nuli: korekcija smije objasniti ekspoziciju, ne boju scene
            loss = loss + 1e-3 * exposure[index].pow(2).sum()
            exposureOptimizer.zero_grad(set_to_none=True)
        for optimizer in optimizers.values():
            optimizer.zero_grad(set_to_none=True)
        tick("gubitak")
        loss.backward()
        tick("unatrag")

        if args.strategy == "mcmc":
            strategy.step_post_backward(params, optimizers, state, step, info, lr=rates["means"])
        else:
            strategy.step_post_backward(params, optimizers, state, step, info, packed=not usesUt)
        tick("zgusnjavanje")
        if args.visible_adam:
            if "gaussian_ids" in info:
                visible = torch.zeros(params["means"].shape[0], dtype=torch.bool, device=device)
                visible[info["gaussian_ids"]] = True
            else:
                visible = (info["radii"] > 0).all(-1).any(0)
            for optimizer in optimizers.values():
                optimizer.step(visible)
        else:
            for optimizer in optimizers.values():
                optimizer.step()
        tick("optimizator")

        #NEVIDLJIVI VAN USRED TRENINGA (--prune-every). Na kraju ih ciscenje ionako baci - na C0257
        #2.17M od 3.74M - a dotad svaki korak placa njihovo crtanje, povratni prolaz i MCMC.
        #Nevidljiv u svim trening kadrovima ne dobiva gradijent, pa ga nista ni ne treba
        total = args.steps + args.finish_full_res
        if args.prune_every > 0 and step >= 3000 and step % args.prune_every == 0 and step < total - 500:
            import floaters
            from gsplat.strategy import ops
            #measure mjeri vidljivost gradijentom, pa bez no_grad; na odvojenim kopijama parametara
            viewList = [views[i] for i in range(len(views))]
            if "pruneDepths" not in locals():
                pruneDepths = floaters.view_depths(torch.from_numpy(points).to(device), viewList)
            most, _, _ = floaters.measure(
                params["means"].detach(), params["quats"].detach(), torch.exp(params["scales"].detach()),
                torch.sigmoid(params["opacities"].detach()), viewList, K, width, height, pruneDepths)
            invisible = most < 0.5
            if int(invisible.sum()) > 0:
                before = params["means"].shape[0]
                #MCMC-ovo stanje (binoms) nije po gaussianu i ne smije se rezati; zadana strategija ga ima po gaussianu
                ops.remove(params=params, optimizers=optimizers, state=state if args.strategy != "mcmc" else {}, mask=invisible)
                print(f"  {step:5d}  nevidljivih van: {int(invisible.sum())} od {before}", flush=True)
        if exposure is not None:
            exposureOptimizer.step()
        if bilagrid is not None:
            bilagridOptimizer.step()
        if poseDelta is not None and step >= args.pose_from:
            poseOptimizer.step()
            poseOptimizer.zero_grad(set_to_none=True)

        #Kad se granica dosegne, zgusnjavanje staje a ucenje ide dalje - preostali koraci jos
        #popravljaju polozaj, boju i neprozirnost onoga sto vec postoji
        if not capped and args.strategy == "default" and params["means"].shape[0] >= budget:
            strategy.refine_stop_iter = step
            capped = True
            print(f"  granica dosegnuta u {step}. koraku: {params['means'].shape[0]} gaussiana, "
                  f"zgusnjavanje staje")

        if step % 500 == 0 or step == args.steps + args.finish_full_res - 1:
            extra = f"  dubina {lastDepthTerm:.4f} (tezina {args.depth_weight})" if len(depthMaps) else ""
            if args.anisotropy_weight > 0.0: extra += f"  iglice {lastNeedles:.3f} (tezina {args.anisotropy_weight})"
            if args.surface > 0 and step >= args.surface_from and "lastSurface" in locals():
                extra += f"  normale {lastSurface[0]:.3f} plosnatost {lastSurface[1]:.3f}"
            print(f"  {step:5d}  gubitak {loss.item():.4f}  gaussiana {params['means'].shape[0]}{extra}")
            if args.profile and phaseTimes:
                total = sum(phaseTimes.values())
                print("         ms po koraku: " + ", ".join(f"{k} {1000 * v / 500:.1f}" for k, v in phaseTimes.items())
                      + f"; ukupno {1000 * total / 500:.1f}", flush=True)
                phaseTimes.clear()

    # -------------------------------------------------------------------------------
    # Ciscenje (floaters.py)
    # -------------------------------------------------------------------------------
    #Na KADROVIMA TRENINGA i u razlucivosti treninga: izdvojeni kadrovi su za ocjenu, pa ne smiju
    #odlucivati sto ostaje. Ocjena nize je tako posteno mjerenje i samog ciscenja
    if args.clean:
        import floaters
        viewList = [views[i] for i in range(len(views))]
        depths = floaters.view_depths(torch.from_numpy(points).to(device), viewList)
        most, seen, nearest = floaters.measure(
            params["means"], params["quats"], torch.exp(params["scales"]), torch.sigmoid(params["opacities"]),
            viewList, K, width, height, depths,
            views_end=[viewsEnd[i] for i in range(len(viewsEnd))] if args.camera_model == "rolling" else None)
        keep, invisible, byCamera = floaters.keep_mask(most, seen, nearest)
        before = params["means"].shape[0]
        for key in list(params.keys()):
            params[key] = torch.nn.Parameter(params[key].detach()[keep])
        print(f"Ciscenje: nevidljivih {invisible}, mrlja uz kameru {byCamera}; "
              f"ostaje {params['means'].shape[0]} od {before}")

    # -------------------------------------------------------------------------------
    # Izvoz
    # -------------------------------------------------------------------------------
    #SIROVE VRIJEDNOSTI, ne aktivirane. Standardni 3DGS .ply drzi mjerilo kao LOGARITAM a
    #neprozirnost kao LOGIT, i svaki preglednik na njih primijeni exp i sigmoid pri citanju.
    #export_splats zapisuje sto mu se preda, pa sam mu prvo predao exp(scales) i sigmoid(opacities)
    #- i onda je Loom aktivirao drugi put: exp(0.02) = 1.02 umjesto 0.02, splatovi pedeset puta
    #preveliki, scena jednolicna ploha. Traceno je na kameru, a bilo je ovdje
    gsplat.export_splats(
        means=params["means"], scales=params["scales"], quats=params["quats"],
        opacities=params["opacities"],
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
    # -------------------------------------------------------------------------------
    # Ocjena na izdvojenim kadrovima
    # -------------------------------------------------------------------------------

    #Ekspozicija izdvojenih kadrova: interpolirana od najblizeg trening kadra prije i poslije
    heldExposure = {}
    if exposure is not None:
        trained = {name: exposure[i].detach().cpu().numpy() for i, name in enumerate(pictureNames)}
        heldExposure = fill_between(trained, heldNames if heldOut else [])
        with open(str(Path(args.output).with_suffix("")) + "_exposure.json", "w") as f:
            json.dump({name: value.tolist() for name, value in {**trained, **heldExposure}.items()}, f)
        spread_gain = np.array([v[0] for v in trained.values()])
        print(f"Ekspozicija po kadru: pojacanje {spread_gain.min():+.3f} do {spread_gain.max():+.3f}")

    heldGrid = {}
    if bilagrid is not None:
        trainedGrid = {name: bilagrid[i].detach().cpu().numpy() for i, name in enumerate(pictureNames)}
        heldGrid = fill_between(trainedGrid, heldNames if heldOut else [])
        allGrids = {**trainedGrid, **heldGrid}
        np.savez_compressed(str(Path(args.output).with_suffix("")) + "_bilagrid.npz",
                            names=np.array(list(allGrids)), grids=np.stack(list(allGrids.values())))

    heldPose = {}
    if poseDelta is not None:
        trainedPose = {name: poseDelta[i].detach().cpu().numpy() for i, name in enumerate(pictureNames)}
        heldPose = fill_between(trainedPose, heldNames if heldOut else [])
        with open(str(Path(args.output).with_suffix("")) + "_poses.json", "w") as f:
            json.dump(dict(mjerilo=spread, poze={name: value.tolist() for name, value in {**trainedPose, **heldPose}.items()}), f)
        size = np.array([np.linalg.norm(v[:3]) for v in trainedPose.values()]) * 180 / math.pi
        shift = np.array([np.linalg.norm(v[3:]) for v in trainedPose.values()]) * spread
        print(f"Ispravak poza: zakret medijan {np.median(size):.4f} st (najveci {size.max():.4f}), "
              f"pomak medijan {np.median(shift):.5f} (najveci {shift.max():.5f}; mjerilo scene {spread:.3f})")

    if heldOut:
        with torch.no_grad():
            colours_sh = torch.cat([params["sh0"], params["shN"]], dim=1)
            psnrs, ssims = [], []
            for i in range(len(heldPictures)):
                heldView, heldViewEnd = heldViews[i:i+1], heldViewsEnd[i:i+1]
                if heldNames[i] in heldPose:
                    correction = pose_correction(torch.from_numpy(heldPose[heldNames[i]]).float().to(device), spread)
                    heldView, heldViewEnd = correction @ heldView, correction @ heldViewEnd
                shown, _, _ = draw(heldView, heldViewEnd, args.sh_degree,
                                   "RGB+ED" if len(depthMaps) else "RGB")

                truth = heldPictures[i].to(device).float() / 255.0
                shown = shown[0]
                shown = shown[..., :3]
                if heldNames[i] in heldExposure:
                    correction = torch.from_numpy(heldExposure[heldNames[i]]).float().to(device)
                    shown = shown * (1.0 + correction[0]) + correction[1]
                if heldNames[i] in heldGrid:
                    shown = bilagrid_apply(torch.from_numpy(heldGrid[heldNames[i]]).float().to(device), shown)
                mse = float(((shown - truth) ** 2).mean())
                psnrs.append(10.0 * math.log10(1.0 / max(mse, 1e-12)))
                ssims.append(float(ssim(shown, truth, window, windowSize)))

            psnrs.sort(); ssims.sort()
            heldScore = dict(psnr_medijan=round(psnrs[len(psnrs)//2], 3), psnr_najgori=round(psnrs[0], 3),
                             ssim_medijan=round(ssims[len(ssims)//2], 4), izdvojenih=len(psnrs))
            print(f"OCJENA na {len(psnrs)} izdvojenih kadrova: "
                  f"PSNR medijan {psnrs[len(psnrs)//2]:.2f} dB (najgori {psnrs[0]:.2f}, najbolji {psnrs[-1]:.2f}), "
                  f"SSIM medijan {ssims[len(ssims)//2]:.3f}")

    with torch.no_grad():
        which = len(pictures) // 2
        colours_sh = torch.cat([params["sh0"], params["shN"]], dim=1)
        rendered, _, _ = draw(views[which:which+1], viewsEnd[which:which+1], args.sh_degree,
                              "RGB+ED" if len(depthMaps) else "RGB")

        truth = pictures[which].to(device).float() / 255.0
        side = torch.cat([truth, rendered[0][..., :3].clamp(0, 1)], dim=1)     # lijevo snimljeno, desno nacrtano
        picture = Image.fromarray((side.cpu().numpy() * 255).astype(np.uint8))
        preview = str(Path(args.output).with_suffix("")) + "_usporedba.png"
        picture.save(preview)

        #I sam prikaz zasebno, da se dva trcanja mogu staviti jedno uz drugo
        alone = Image.fromarray((rendered[0][..., :3].clamp(0, 1).cpu().numpy() * 255).astype(np.uint8))
        alone.save(str(Path(args.output).with_suffix("")) + "_prikaz.png")
        Image.fromarray((truth.cpu().numpy() * 255).astype(np.uint8)).save(
            str(Path(args.output).with_suffix("")) + "_snimljeno.png")

        difference = float((truth - rendered[0][..., :3].clamp(0, 1)).abs().mean())
        print(f"Usporedba: {preview}  (lijevo snimljeno, desno nacrtano; razlika {difference:.4f})")

    #Dnevnik mjerenja: svaki trening ostavi redak, da se kroz vrijeme vidi kamo se ide
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bench"))
    from mjerenja import upisi, snimka_modela
    upisi(dict(vrsta="trening", snimka=snimka_modela(model), opis=args.opis, koraka=args.steps + args.finish_full_res,
               razlucivost=f"{width}x{height}", gaussiana=int(params["means"].shape[0]),
               vrijeme_s=round(time.time() - started), kamera=args.camera_model,
               izlaz=Path(args.output).name, **(heldScore if heldOut else {})))


if __name__ == "__main__":
    main()
