#!/usr/bin/env python3
"""Je li ubaceno svjetlo stvarno u prostoru - mjereno, a ne pogledano.

Ovo je druga polovica maske: SAM2 imenuje subjekt i pozadinu, a ovdje se nad tim regijama
racunaju brojevi. Bez maske se o rezultatu moze samo imati dojam, jer nad pravom fotkom nista
ne kaze gdje subjekt prestaje.

    measure_relight.py --plate p.png --depth p.pfm --mask p_mask.png --near 1.24 --far 3.20

KONTROLA JE ISTI KUT BEZ TRAGA, a ne drugi kut. Usporedba dvaju kutova mijesa sjenu s padom
svjetla po strani: tamo "sjena" ispadne jednako tamna kao svoja okolina i mjerenje da laznu
nulu. Zato se svaki kut snima dvaput, sa sjenom i bez nje, pa je razlika tocno ono sto je trag
napravio i nista drugo.

Predvidjeni pomak sjene ne racuna se ovdje nego se CITA iz onoga sto app sam ispise - pa se
mjeri slaze li se ono sto app tvrdi s onim sto je nacrtao.

GRANICA TE PROVJERE, izmjerena a ne naslucena: ona je SAMODOSLJEDNA. Pusteno s krivom lecom
(50 stupnjeva na portretu snimljenom s 40) prolazi sve osam provjera jednako dobro - jer app
tada rekonstruira drugu scenu, ispise drugu sjenu (0.87 m umjesto 0.68) i nacrta je dosljedno
toj drugoj sceni. Obje polovice imaju istu manu, pa se ne mogu uhvatiti jedna drugom.

To nije propust skripte nego svojstvo zadatka: svjetlo koje ubacujemo je IZMISLJENO, ne
izmjereno, pa nema vanjske cinjenice s kojom bi se njegova sjena mogla sukobiti. Kut lece se
iz same slike ne da izvesti. Jedino vanjsko sidro koje postoji je --truth, i ono provjerava
ono sto se DA provjeriti izvana: udaljenosti koje je app ocitao iz karte dubine.
"""
import argparse, math, os, re, subprocess, sys, tempfile

import numpy


def run_loom(loom, plate, depth, near, far, fov, angle, shadow, out):
    command = [loom, plate, depth, str(near), str(far), "--fov", str(fov),
               "--angle", str(angle), "--save", out]
    if not shadow:
        command.append("--no-shadow")

    done = subprocess.run(command, capture_output=True, text=True)
    if done.returncode != 0 or not os.path.exists(out):
        sys.exit("LoomApp nije uspio:\n%s\n%s" % (done.stdout[-2000:], done.stderr[-2000:]))
    return done.stdout


def geometry_from(output):
    """Ono sto je app sam ispisao o sceni. Mjeri se protiv NJEGOVIH brojeva, jer se pita slaze
    li se ono sto tvrdi s onim sto je nacrtao."""
    found = re.search(r"subjekt ([\d.]+) m, pozadina ([\d.]+) m; svjetlo na ([\d.]+) m, "
                      r"pomak ([\d.]+) m -> sjena pada ([\d.]+) m u stranu", output)
    if not found:
        sys.exit("u ispisu LoomAppa nema retka o geometriji - je li karta dubine dana?")
    names = ("subjekt", "pozadina", "svjetlo", "pomak", "sjena")
    return dict(zip(names, (float(v) for v in found.groups())))


def grey(path):
    from PIL import Image
    return numpy.asarray(Image.open(path).convert("RGB"), dtype="float64").mean(axis=2)


class Report:
    def __init__(self, title):
        print("== %s ==" % title)
        self.failed = 0

    def check(self, name, passed, detail):
        print("  %-4s %-42s %s" % ("ok" if passed else "FAIL", name, detail))
        if not passed:
            self.failed += 1

    def result(self):
        print("%s\n" % ("sve proslo" if not self.failed else "%d provjera palo" % self.failed))
        return 1 if self.failed else 0


def main():
    parser = argparse.ArgumentParser(description="mjerenje ubacenog svjetla nad maskom")
    parser.add_argument("--plate", required=True)
    parser.add_argument("--depth", required=True)
    parser.add_argument("--mask", required=True, help="maska subjekta (PNG, 255 = subjekt)")
    parser.add_argument("--near", type=float, required=True)
    parser.add_argument("--far", type=float, required=True)
    parser.add_argument("--fov", type=float, default=50.0, help="okomiti kut kamere")
    parser.add_argument("--angles", default="90,270", help="dva suprotna kuta svjetla")
    parser.add_argument("--loom", default="build/LoomApp")
    parser.add_argument("--truth", help="portret_truth.txt, ako scena ima poznat odgovor")
    parser.add_argument("--keep", help="mapa u koju da spremi kadrove")
    args = parser.parse_args()

    first, second = (float(v) for v in args.angles.split(","))

    mask = numpy.asarray(__import__("PIL.Image", fromlist=["Image"]).open(args.mask)) > 127
    height, width = mask.shape
    backdrop = ~mask

    #Gdje je subjekt na ekranu - sve se mjeri kao pomak od njega
    subjectX = float(numpy.nonzero(mask)[1].mean())

    #Koliko metara na pozadini vrijedi jedan piksel, u kameri kojom app racuna
    fy = (height * 0.5) / math.tan(math.radians(args.fov) * 0.5)

    report = Report("svjetlo ubaceno u sliku")
    keep = args.keep or tempfile.mkdtemp(prefix="relight")
    os.makedirs(keep, exist_ok=True)

    shots, geometry = {}, None
    for angle in (first, second):
        for shadow in (True, False):
            name = os.path.join(keep, "kut%.0f_%s.png" % (angle, "sjena" if shadow else "bez"))
            output = run_loom(args.loom, args.plate, args.depth, args.near, args.far,
                              args.fov, angle, shadow, name)
            shots[(angle, shadow)] = grey(name)
            if geometry is None:
                geometry = geometry_from(output)

    predicted = fy * geometry["sjena"] / geometry["pozadina"]
    print("  app tvrdi: subjekt %.2f m, pozadina %.2f m, sjena %.2f m u stranu"
          " -> %.0f px od subjekta\n" % (geometry["subjekt"], geometry["pozadina"],
                                          geometry["sjena"], predicted))

    measured = {}
    for angle in (first, second):
        #Sve sto je trag napravio, i nista drugo
        made = shots[(angle, True)] - shots[(angle, False)]

        dark = (made < -1.0) & backdrop
        onSubject = int(((numpy.abs(made) > 1.0) & mask).sum())

        report.check("kut %.0f: trag baca sjenu" % angle,
            int(dark.sum()) > 2000,
            "zatamnjeno %d px, najdublje %.1f razina" % (int(dark.sum()), -made.min()))

        report.check("kut %.0f: sjena pada na pozadinu" % angle,
            onSubject * 50 < int(dark.sum()),
            "na subjektu promijenjeno %d px naspram %d na pozadini" % (onSubject, int(dark.sum())))

        measured[angle] = float(numpy.nonzero(dark)[1].mean()) - subjectX if dark.sum() else 0.0

    # -------------------------------------------------------------------------------
    # Parna provjera: nesto se MORA promijeniti kad se kut zrcali
    # -------------------------------------------------------------------------------

    report.check("sjena skace na suprotnu stranu",
        measured[first] * measured[second] < 0,
        "pomak %+.0f px na kutu %.0f, %+.0f px na kutu %.0f"
        % (measured[first], first, measured[second], second))

    report.check("obje strane jednako daleko",
        abs(abs(measured[first]) - abs(measured[second])) < 0.15 * predicted,
        "%.0f px naspram %.0f px" % (abs(measured[first]), abs(measured[second])))

    #Sjena je uvijek BLIZE subjektu nego sto geometrija kaze, jer trag zaklon ne vidi kao plohu
    #nego kao kutiju duboku `thickness` - pa pokrije i put od subjekta do svog pravog mjesta.
    #Isto odstupanje je izmjereno i u test_screen_shadow, tamo kao 933 piksela
    ratio = 0.5 * (abs(measured[first]) + abs(measured[second])) / predicted
    report.check("pomak je u redu velicine geometrijskog",
        0.6 < ratio < 1.1,
        "izmjereno %.0f%% predvidjenog (manje od 100%% se ocekuje: kutija debljine)"
        % (100.0 * ratio))

    # -------------------------------------------------------------------------------
    # Svjetlo je u prostoru, a ne naljepnica
    # -------------------------------------------------------------------------------

    #Prva verzija ove provjere usporedivala je SREDNJU svjetlinu subjekta i pozadine kroz
    #kutove, i bila je prazna: kod zrcalnih kutova nad simetricnom scenom se te dvije srednje
    #vrijednosti po simetriji moraju poklopiti, pa je mjerila simetriju a ne dubinu. Izmjereno
    #je davala 0.64 % razlike, sto ne znaci ni da svjetlo jest ni da nije u prostoru.
    #
    #Ono sto se stvarno da tvrditi: subjekt ima normale koje se okrecu od svjetla, a pozadina
    #je ravnina na jednoj dubini. Kad svjetlo prijede na drugu stranu, nagib svjetline preko
    #subjekta se zato mora promijeniti mnogo vise nego preko pozadine - a pozadina je pritom
    #kontrola koja stoji u istoj slici, pod istim svjetlom.
    def gradient(image, region):
        rows, columns = numpy.nonzero(region)
        middle = int(columns.mean())

        left = region.copy();  left[:, middle:] = False
        right = region.copy(); right[:, :middle] = False
        return image[right].mean() - image[left].mean()

    #Bez traga, da sjena ne sudjeluje u ovoj tvrdnji
    subjectSwing = abs(gradient(shots[(first, False)], mask) - gradient(shots[(second, False)], mask))
    backdropSwing = abs(gradient(shots[(first, False)], backdrop) - gradient(shots[(second, False)], backdrop))

    report.check("subjekt se sjenca kao oblik, a ne kao ploha",
        subjectSwing > 2.5 * backdropSwing,
        "nagib preko subjekta se promijenio za %.1f razina, preko pozadine za %.1f (%.1fx)"
        % (subjectSwing, backdropSwing, subjectSwing / max(backdropSwing, 1e-6)))

    # -------------------------------------------------------------------------------
    # Vanjsko sidro, ako ga scena ima
    # -------------------------------------------------------------------------------

    #Sve gore mjeri app protiv njegovih vlastitih brojeva. Ovo je jedino mjesto gdje se
    #usporeduje s necim izvana - i zato jedino koje moze uhvatiti gresku koju obje polovice
    #dijele. Odstupanje NIJE greska Looma nego procjene dubine, pa se ispisuje kao broj, a
    #granica je siroka: hvata pokvarenu kalibraciju, ne pristranost modela
    if args.truth:
        text = open(args.truth).read()
        wanted = {}
        for key, pattern in (("subjekt", r"subjekt \(medijan\)\s+([\d.]+)"),
                             ("pozadina", r"pozadina\s+([\d.]+)")):
            found = re.search(pattern, text)
            if found:
                wanted[key] = float(found.group(1))

        for key in ("subjekt", "pozadina"):
            if key not in wanted:
                continue
            error = abs(geometry[key] - wanted[key]) / wanted[key]
            report.check("%s je tamo gdje uistinu jest" % key,
                error < 0.20,
                "app %.2f m, istina %.2f m (%+.1f %%)"
                % (geometry[key], wanted[key], 100.0 * (geometry[key] - wanted[key]) / wanted[key]))

    if not args.keep:
        print("  kadrovi: %s" % keep)
    return report.result()


if __name__ == "__main__":
    sys.exit(main())
