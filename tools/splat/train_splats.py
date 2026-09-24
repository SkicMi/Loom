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
    ap.add_argument("--camera-model", choices=["auto", "classic", "ut", "rolling"], default="classic",
                    help="auto: rolling kad postoje rs_top/rs_bottom, inace classic")
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
    args = ap.parse_args()
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
    points, colours = read_points(model / ("rs_top" if args.camera_model == "rolling" else ".") / "points3D.txt")
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
    if args.camera_model == "rolling":
        for which in ("rs_top", "rs_bottom"):
            listed = model / which / "images.txt"
            if not listed.exists():
                raise SystemExit(f"{listed} ne postoji - napravi ga s RollingShutterProbe (ROLLING_WRITE)")
            rowViews[which] = {name: view for name, view in read_images(listed)}

    depthMaps = []
    views, pictures, viewsEnd = [], [], []
    for name, view in frames:
        path = Path(args.images) / name
        if not path.exists():
            continue
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
    optimizers = {k: torch.optim.Adam([{"params": params[k], "lr": rates[k], "name": k}],
                                      eps=1e-15, betas=(0.9, 0.999)) for k in params}

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
    usesUt = args.camera_model != "classic"

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

    windowSize = 11
    window = gaussian_window(windowSize, 1.5, device)
    lastDepthTerm = 0.0
    lastNeedles = 0.0
    print(f"Trening: {args.steps} koraka, mjerilo scene {spread:.2f}, gubitak {args.loss}, "
          f"rasterizacija {args.rasterize}, zasicenje od {args.saturation}")
    generator = torch.Generator(device="cpu").manual_seed(20260915)

    for step in range(args.steps):
        index = int(torch.randint(len(pictures), (1,), generator=generator))
        truth = pictures[index].to(device, non_blocking=True).float() / 255.0

        rendered, alpha, info = draw(views[index:index+1], viewsEnd[index:index+1],
                                     min(args.sh_degree, step // 1000),     #niži redovi prvi, kao u izvornom radu
                                     "RGB+ED" if len(depthMaps) else "RGB")

        strategy.step_pre_backward(params, optimizers, state, step, info)

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
            absolute = ((rendered[0][..., :3] - truth).abs().mean(dim=-1) * weight).sum() / weight.sum().clamp(min=1.0)
        else:
            absolute = (rendered[0][..., :3] - truth).abs().mean()

        if args.loss == "ssim":
            structure = 1.0 - ssim(rendered[0][..., :3], truth, window, windowSize)
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
        for optimizer in optimizers.values():
            optimizer.zero_grad(set_to_none=True)
        loss.backward()

        if args.strategy == "mcmc":
            strategy.step_post_backward(params, optimizers, state, step, info, lr=rates["means"])
        else:
            strategy.step_post_backward(params, optimizers, state, step, info, packed=not usesUt)
        for optimizer in optimizers.values():
            optimizer.step()

        #Kad se granica dosegne, zgusnjavanje staje a ucenje ide dalje - preostali koraci jos
        #popravljaju polozaj, boju i neprozirnost onoga sto vec postoji
        if not capped and args.strategy == "default" and params["means"].shape[0] >= budget:
            strategy.refine_stop_iter = step
            capped = True
            print(f"  granica dosegnuta u {step}. koraku: {params['means'].shape[0]} gaussiana, "
                  f"zgusnjavanje staje")

        if step % 500 == 0 or step == args.steps - 1:
            extra = f"  dubina {lastDepthTerm:.4f} (tezina {args.depth_weight})" if len(depthMaps) else ""
            if args.anisotropy_weight > 0.0: extra += f"  iglice {lastNeedles:.3f} (tezina {args.anisotropy_weight})"
            print(f"  {step:5d}  gubitak {loss.item():.4f}  gaussiana {params['means'].shape[0]}{extra}")

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

    if heldOut:
        with torch.no_grad():
            colours_sh = torch.cat([params["sh0"], params["shN"]], dim=1)
            psnrs, ssims = [], []
            for i in range(len(heldPictures)):
                shown, _, _ = draw(heldViews[i:i+1], heldViewsEnd[i:i+1], args.sh_degree,
                                   "RGB+ED" if len(depthMaps) else "RGB")

                truth = heldPictures[i].to(device).float() / 255.0
                shown = shown[0]
                shown = shown[..., :3]
                mse = float(((shown - truth) ** 2).mean())
                psnrs.append(10.0 * math.log10(1.0 / max(mse, 1e-12)))
                ssims.append(float(ssim(shown, truth, window, windowSize)))

            psnrs.sort(); ssims.sort()
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


if __name__ == "__main__":
    main()
