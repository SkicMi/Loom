"""Skup snimaka: cijeli lanac (VideoSolve + trening + ocjena) na svim testnim snimkama, jedna tablica.

    python tools/bench/skup_snimaka.py OZNAKA [--snimke C0255,C0257] [--solve "..."] [--trener "..."]
                                              [--usporedi DRUGA_OZNAKA] [--izlaz MAPA]

Zasto: C0255 je pokazao da promjena koja na C0257 ne mijenja nista moze drugu snimku srusiti za 7 dB
(samokalibracija, pocetni par). Svaka promjena u solveu ili treneru ide zato kroz sve snimke.

Za svaku snimku iz ~/Desktop/loomTestClips (ili --snimke): VideoSolve svaki 10. kadar s treningom
preko --then (izdvojeni kadrovi 6/3, kao u svim mjerenjima), zatim ocjena na pola razlucivosti s
ekspozicijom. Rezultat ide u MAPA/OZNAKA/sazetak.json i u dnevnik mjerenja. S --usporedi se po
snimci usporedi s drugom oznakom kadar po kadar - ali samo ako su izdvojeni kadrovi isti (solve moze
dati drugi skup kamera, i tada se ispise samo razlika prosjeka).
"""
import argparse, json, shlex, subprocess, sys, time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
CLIPS = Path.home() / "Desktop" / "loomTestClips"
sys.path.insert(0, str(Path(__file__).resolve().parent))
from mjerenja import upisi


def run(command, log):
    with open(log, "w") as f:
        started = time.time()
        code = subprocess.call(command, shell=True, stdout=f, stderr=subprocess.STDOUT, cwd=ROOT)
    return code, time.time() - started


def held_names(model, holdout=6, block=3):
    names = []
    for line in (model / "images.txt").read_text().splitlines()[4::2]:
        parts = line.split()
        if len(parts) >= 10 and (model / "images" / parts[9]).exists():
            names.append(parts[9])
    names.sort()
    held = []
    for start in range(0, len(names), holdout * block):
        held.extend(names[start:start + block])
    return held


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("oznaka", help="ime ovog prolaza, npr. zadano ili e15_povrsina")
    ap.add_argument("--snimke", default="", help="zarezom odvojena imena bez .MP4; zadano sve u loomTestClips")
    ap.add_argument("--solve", default="", help="dodatni argumenti VideoSolvea")
    ap.add_argument("--trener", default="", help="dodatni argumenti train_splats.py")
    ap.add_argument("--usporedi", default="", help="oznaka prethodnog prolaza za usporedbu")
    ap.add_argument("--izlaz", default=str(Path.home() / "Desktop" / "loomTestClips" / "skup"))
    args = ap.parse_args()

    names = args.snimke.split(",") if args.snimke else sorted(p.stem for p in CLIPS.glob("*.MP4"))
    base = Path(args.izlaz) / args.oznaka
    base.mkdir(parents=True, exist_ok=True)
    python = ROOT / ".venv" / "bin" / "python"
    env = f'PATH="{ROOT}/.venv/bin:$PATH"'
    summary = {}
    for name in names:
        model = base / name
        trainer = (f'cd "{ROOT}" && {env} {python} tools/splat/train_splats.py "{model}" "{model}/images" '
                   f'"{model}/scena.ply" --holdout 6 --holdout-block 3 --opis "skup {args.oznaka}: {name}" {args.trener}')
        solve = (f'{ROOT}/build/VideoSolve "{CLIPS / (name + ".MP4")}" 10 100000 0 "{model}" '
                 f'--then {shlex.quote(trainer)} {args.solve}')
        print(f"== {name}: lanac ...", flush=True)
        code, seconds = run(solve, base / f"{name}.log")
        splat = model / "scena.ply"
        if code != 0 or not splat.exists():
            print(f"   PAO (izlaz {code}), vidi {base / (name + '.log')}")
            summary[name] = dict(pao=True, vrijeme_s=round(seconds))
            continue
        exposure = model / "scena_exposure.json"
        scores = base / f"{name}_d2.npy"
        evaluate = (f'{env} {python} tools/splat/evaluate_splat.py "{model}" "{splat}" --downscale 2 '
                    f'--out "{scores}" --opis "skup {args.oznaka}: {name}"'
                    + (f' --exposure "{exposure}"' if exposure.exists() else ""))
        run(evaluate, base / f"{name}_ocjena.log")
        values = np.load(scores)
        log = (base / f"{name}.log").read_text(errors="replace")
        cameras = next((l.split("nakon pune obrade:")[1].split(",")[0].strip() for l in log.splitlines()
                        if "nakon pune obrade:" in l), "?")
        summary[name] = dict(psnr=round(float(values[:, 0].mean()), 3), ssim=round(float(values[:, 1].mean()), 4),
                             ostrina=round(float(values[:, 2].mean()), 3), vrijeme_s=round(seconds),
                             kamera=cameras, izdvojeni=held_names(model))
        print(f"   PSNR {summary[name]['psnr']:.2f} dB, SSIM {summary[name]['ssim']:.3f}, "
              f"ostrina {summary[name]['ostrina']:.3f}, {cameras}, {seconds / 60:.1f} min", flush=True)
    (base / "sazetak.json").write_text(json.dumps(summary, indent=1, ensure_ascii=False))

    print(f"\n{'snimka':16s} {'PSNR':>7s} {'SSIM':>7s} {'ostrina':>8s} {'min':>6s}  kamere")
    for name, row in summary.items():
        if row.get("pao"):
            print(f"{name:16s} {'PAO':>7s}"); continue
        print(f"{name:16s} {row['psnr']:7.2f} {row['ssim']:7.3f} {row['ostrina']:8.3f} {row['vrijeme_s'] / 60:6.1f}  {row['kamera']}")

    if args.usporedi:
        other = Path(args.izlaz) / args.usporedi
        before = json.loads((other / "sazetak.json").read_text())
        print(f"\nUsporedba s '{args.usporedi}':")
        for name, row in summary.items():
            old = before.get(name)
            if not old or old.get("pao") or row.get("pao"):
                print(f"  {name}: nema para"); continue
            if old["izdvojeni"] == row["izdvojeni"]:
                subprocess.call(f'{python} tools/splat/evaluate_splat.py --compare "{other / (name + "_d2.npy")}" '
                                f'"{base / (name + "_d2.npy")}" --opis "skup {args.oznaka} prema {args.usporedi}: {name}"',
                                shell=True, cwd=ROOT)
            else:
                print(f"  {name}: izdvojeni kadrovi razliciti (solve je dao drugi skup kamera) - samo prosjek: "
                      f"PSNR {row['psnr'] - old['psnr']:+.2f} dB, ostrina {row['ostrina'] - old['ostrina']:+.3f}")
    upisi(dict(vrsta="skup", opis=f"skup snimaka: {args.oznaka}", solve=args.solve, trener=args.trener,
               rezultat={k: {kk: vv for kk, vv in v.items() if kk != "izdvojeni"} for k, v in summary.items()}))


if __name__ == "__main__":
    main()
