#!/usr/bin/env python3
"""Dnevnik mjerenja: sto je izmjereno, kad, na cemu, koliko je trajalo i koliko je tocno.

    from mjerenja import upisi                       # iz alata (trener, evaluate_splat)
    upisi({"vrsta": "trening", "snimka": "C0257", "psnr": 24.48, ...})

    tools/bench/mjerenja.py tablica                  # benchmarks/MJERENJA.md iz dnevnika
    tools/bench/mjerenja.py dodaj vrsta=solve snimka=C0257 opis="..." vrijeme_s=2360
    tools/bench/mjerenja.py solve solve.log [solve.time] opis="..."   # procita ispis VideoSolvea

ZASTO. Svaka promjena se mjeri, a bez zapisa se nakon tjedan dana ne zna ni sto je bilo prije ni
koliko je sum. Dnevnik je JSONL (redak = jedno mjerenje) jer se polja dodaju kako se pojavljuju
nove mjere; tablica se iz njega uvijek iznova slozi. VideoSolve pise u isti dnevnik iz C++.
"""
import json, os, re, sys, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DNEVNIK = Path(os.environ.get("LOOM_MJERENJA", ROOT / "benchmarks" / "mjerenja.jsonl"))
TABLICA = DNEVNIK.parent / "MJERENJA.md"


def upisi(zapis):
    """Doda jedno mjerenje; datum i vrijeme se dodaju sami ako ih nema."""
    zapis = dict(zapis)
    zapis.setdefault("datum", datetime.datetime.now().strftime("%Y-%m-%d %H:%M"))
    DNEVNIK.parent.mkdir(parents=True, exist_ok=True)
    with open(DNEVNIK, "a") as f:
        f.write(json.dumps(zapis, ensure_ascii=False) + "\n")


def snimka_modela(model):
    """Ime snimke iz koje je model: VideoSolve u mapu zapise izvor.txt; inace ime mape."""
    izvor = Path(model) / "izvor.txt"
    if izvor.exists():
        return Path(izvor.read_text().strip()).stem
    return Path(model).name


def iz_solvea(log, vrijeme=None):
    """Zapis iz ispisa VideoSolvea (i izlaza `time`, ako ga ima): brzina po fazama i tocnost."""
    text = Path(log).read_text(errors="replace")
    z = {"vrsta": "solve"}
    broj = lambda pattern, cast=float: [cast(m) for m in re.findall(pattern, text)]
    if (v := re.search(r"^\s+(\d+) kadrova, \d+ tragova, .*?, ([\d.]+) s$", text, re.M)):
        z["kadrova"], z["pracenje_s"] = int(v[1]), float(v[2])
    if (v := broj(r"graf poklapanja: .*?, ([\d.]+) s\n")): z["graf_s"] = v[-1]
    if (v := broj(r"ukupno ([\d.]+) s\n")): z["rekonstrukcija_s"] = round(sum(v), 1)
    if (v := broj(r"pune slicice: .*?, ([\d.]+) s\n")): z["pune_slicice_s"] = v[-1]
    konacno = re.findall(r"nakon pune obrade: (\d+) od (\d+) kamera, (\d+) tocaka, reprojekcija ([\d.]+) px", text) or \
              re.findall(r"Najbolje: .*?(\d+) od (\d+) kamera, (\d+) tocaka, reprojekcija ([\d.]+) px", text)
    if konacno:
        a, b, t, r = konacno[-1]
        z["kamera"], z["tocaka"], z["reprojekcija_px"] = f"{a}/{b}", int(t), float(r)
    if (v := re.findall(r"provjera bez istine: .*?reprojekcija ([\d.]+) px .*? omjer ([\d.]+)", text)):
        z["izdvojeni_px"], z["omjer_izdvojenih"] = float(v[-1][0]), float(v[-1][1])
    if (v := broj(r"provjera zapisanog: .*?reprojekcija ([\d.]+) px")): z["zapisano_px"] = v[-1]
    if (v := re.findall(r"Zapisano u (\S+) \(cameras.txt", text)):
        z["izlaz"] = Path(v[-1]).name
        kamere = Path(v[-1]) / "cameras.txt"
        if kamere.exists():
            for line in kamere.read_text().splitlines():
                if line.startswith("#") or not line.strip(): continue
                parts = line.split()
                params = [float(x) for x in parts[4:]]
                z["zarisna_px"] = round(params[0], 1)
                k1 = {"SIMPLE_RADIAL": 3, "RADIAL": 3, "OPENCV": 4, "FULL_OPENCV": 4}.get(parts[1])
                if k1 is not None and k1 < len(params): z["k1"] = round(params[k1], 4)
                break
        izvor = Path(v[-1]) / "izvor.txt"
        if izvor.exists(): z["snimka"] = Path(izvor.read_text().strip()).stem
    if vrijeme and Path(vrijeme).exists() and (v := re.search(r"real\s+(\d+)m([\d.]+)s", Path(vrijeme).read_text())):
        z["vrijeme_s"] = round(int(v[1]) * 60 + float(v[2]))
    return z


def procitaj():
    if not DNEVNIK.exists(): return []
    out = []
    for line in open(DNEVNIK):
        line = line.strip()
        if line:
            try: out.append(json.loads(line))
            except json.JSONDecodeError: pass
    return out


#Stupci po vrsti, redom kojim se prikazuju; ostala polja idu u zadnji stupac "ostalo"
STUPCI = {
    "solve": ["datum", "snimka", "opis", "kadrova", "vrijeme_s", "kamera", "tocaka", "reprojekcija_px",
              "izdvojeni_px", "omjer_izdvojenih", "zarisna_px", "k1", "pracenje_s", "graf_s", "rekonstrukcija_s", "pune_slicice_s", "odluka"],
    "trening": ["datum", "snimka", "opis", "koraka", "razlucivost", "gaussiana", "vrijeme_s", "psnr_medijan", "psnr_najgori", "ssim_medijan", "odluka"],
    "ocjena": ["datum", "snimka", "opis", "razlucivost", "psnr", "ssim", "ostrina", "kadrova", "gaussiana", "odluka"],
    "usporedba": ["datum", "snimka", "opis", "mjera", "razlika", "pogreska", "bolji_kadrova", "odluka"],
    "dekodiranje": ["datum", "snimka", "opis", "vrijeme_s"],
}


def oblik(v):
    if isinstance(v, float):
        if abs(v) >= 1000: return f"{v:.0f}"
        if abs(v) >= 10: return f"{v:.2f}"
        return f"{v:.3f}"
    return str(v).replace("|", "/")


def tablica():
    zapisi = procitaj()
    redovi = ["# Mjerenja", "",
              f"Iz `{DNEVNIK.relative_to(ROOT) if DNEVNIK.is_relative_to(ROOT) else DNEVNIK}` ({len(zapisi)} zapisa). "
              "Slozi se iznova s `tools/bench/mjerenja.py tablica`. Vrijeme je u sekundama; PSNR u dB na "
              "IZDVOJENIM kadrovima (koje trening nije vidio); ostrina je energija detalja nacrtanog prema snimljenom (1 = jednako ostro). "
              "Usporedba je razlika B - A po istim kadrovima s procjenom pogreske; dva ista treninga razlikuju se do oko 0.12 dB, "
              "pa razlika manja od dvije pogreske nije nalaz. Vrijeme treninga i solvea koji su isli istovremeno na kartici nije cisto.", ""]
    vrste = list(STUPCI) + sorted({z.get("vrsta", "ostalo") for z in zapisi} - set(STUPCI))
    for vrsta in vrste:
        ovi = sorted((z for z in zapisi if z.get("vrsta", "ostalo") == vrsta), key=lambda z: z.get("datum", ""))
        if not ovi: continue
        stupci = STUPCI.get(vrsta, ["datum", "snimka", "opis"])
        redovi += [f"## {vrsta}", "", "| " + " | ".join(stupci + ["ostalo"]) + " |", "|" + "---|" * (len(stupci) + 1)]
        for z in ovi:
            ostalo = ", ".join(f"{k}={oblik(v)}" for k, v in z.items() if k not in stupci and k != "vrsta")
            redovi.append("| " + " | ".join(oblik(z.get(s, "")) for s in stupci) + f" | {ostalo} |")
        redovi.append("")
    TABLICA.write_text("\n".join(redovi) + "\n")
    print(f"{TABLICA} ({len(zapisi)} zapisa)")


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "tablica":
        tablica()
    elif len(sys.argv) >= 3 and sys.argv[1] in ("dodaj", "solve"):
        rest = sys.argv[2:]
        zapis = {}
        if sys.argv[1] == "solve":
            datoteke = [a for a in rest if "=" not in a]
            zapis = iz_solvea(datoteke[0], datoteke[1] if len(datoteke) > 1 else None)
            rest = [a for a in rest if "=" in a]
        for arg in rest:
            key, _, value = arg.partition("=")
            try: zapis[key] = float(value) if "." in value or "e" in value.lower() else int(value)
            except ValueError: zapis[key] = value
        upisi(zapis)
        tablica()
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
