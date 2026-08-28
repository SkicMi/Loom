#!/usr/bin/env python3
"""Depth Anything V2 -> PFM karte dubine koje Spool cita.

Zakljucak arhitekture: procjena dubine NE zivi u Loomu. Loom crta, Spool uvozi i izvozi, a
model je alat koji stoji izmedu snimke i Spoola. Zato je ovo skripta a ne biblioteka - i zato
izlaz nije nista sto Loom definira nego PFM, format kojim se dubina razmjenjuje.

    python3 estimate_depth.py portret.jpg
    python3 estimate_depth.py snimka.mp4 -o dubina/
    python3 estimate_depth.py kadrovi/ -o dubina/ --model base

Sto model daje je DISPARITET: broj proporcionalan reciprocnoj udaljenosti, veci znaci blize.
Ne zna ni razmjer ni pomak - koliko je to u metrima kaze se poslije, Loomovom kalibracijom
(DepthMapping::fromRange ili fromReferences).
"""
import argparse, os, struct, subprocess, sys, tempfile

MODELS = {
    "small": "depth-anything/Depth-Anything-V2-Small-hf",
    "base":  "depth-anything/Depth-Anything-V2-Base-hf",
    "large": "depth-anything/Depth-Anything-V2-Large-hf",
}

IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp")
VIDEO_SUFFIXES = (".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".mts")


def write_pfm(path, values, width, height):
    """Jedan kanal, float, little endian. PFM broji retke ODOZDO prema gore."""
    with open(path, "wb") as f:
        f.write(b"Pf\n%d %d\n-1.0\n" % (width, height))
        for y in range(height - 1, -1, -1):
            f.write(struct.pack("<%df" % width, *values[y * width:(y + 1) * width]))


def frames_from_video(path, into):
    """Kadrovi na disk. ffmpeg je vec ovisnost Spoola, pa nista novo ne uvodimo."""
    pattern = os.path.join(into, "frame_%04d.png")
    subprocess.run(["ffmpeg", "-y", "-i", path, pattern],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return sorted(os.path.join(into, n) for n in os.listdir(into) if n.endswith(".png"))


def collect(source, temp):
    if os.path.isdir(source):
        names = sorted(n for n in os.listdir(source) if n.lower().endswith(IMAGE_SUFFIXES))
        if not names:
            sys.exit("U mapi %s nema slika." % source)
        return [os.path.join(source, n) for n in names], True

    if source.lower().endswith(VIDEO_SUFFIXES):
        return frames_from_video(source, temp), True

    return [source], False


def main():
    parser = argparse.ArgumentParser(description="Depth Anything V2 -> PFM")
    parser.add_argument("source", help="slika, mapa sa slikama, ili snimka")
    parser.add_argument("-o", "--output", help="izlazni file ili mapa")
    parser.add_argument("--model", choices=sorted(MODELS), default="small")
    parser.add_argument("--raw", action="store_true",
                        help="zapisi sirove vrijednosti modela umjesto 0..1")
    parser.add_argument("--cpu", action="store_true")
    args = parser.parse_args()

    import torch
    from PIL import Image
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation

    device = "cpu" if args.cpu or not torch.cuda.is_available() else "cuda"
    name = MODELS[args.model]

    print("model:  %s  (%s)" % (name, device))
    processor = AutoImageProcessor.from_pretrained(name)
    model = AutoModelForDepthEstimation.from_pretrained(name).to(device).eval()

    with tempfile.TemporaryDirectory() as temp:
        paths, many = collect(args.source, temp)

        maps = []
        for index, path in enumerate(paths):
            image = Image.open(path).convert("RGB")
            inputs = processor(images=image, return_tensors="pt").to(device)

            with torch.no_grad():
                predicted = model(**inputs).predicted_depth

            #Model radi u svojoj rezoluciji; vracamo je na velicinu slike, jer karta dubine i
            #snimka moraju biti piksel na piksel
            full = torch.nn.functional.interpolate(
                predicted.unsqueeze(1), size=image.size[::-1],
                mode="bicubic", align_corners=False).squeeze()

            maps.append((os.path.basename(path), image.size, full.float().cpu()))
            print("  %d/%d  %s  %dx%d" % (index + 1, len(paths), os.path.basename(path),
                                          image.size[0], image.size[1]))

        #Raspon se racuna preko SVIH kadrova, ne po kadru. Normalizacija po kadru bi svakom
        #dala vlastito mjerilo, pa bi dubina treperila kroz snimku iako se scena ne mice
        low = min(float(m.min()) for _, _, m in maps)
        high = max(float(m.max()) for _, _, m in maps)
        print("sirovi raspon modela: %.4f .. %.4f%s" %
              (low, high, "" if len(maps) == 1 else "  (preko svih %d kadrova)" % len(maps)))

        if args.output:
            output = args.output
        elif many:
            output = "depth"
        else:
            output = os.path.splitext(args.source)[0] + ".pfm"

        if many:
            os.makedirs(output, exist_ok=True)

        for index, (base, size, values) in enumerate(maps):
            data = values
            if not args.raw:
                data = (data - low) / max(high - low, 1e-9)

            flat = data.reshape(-1).tolist()
            target = (os.path.join(output, "depth_%04d.pfm" % index) if many
                      else output)
            write_pfm(target, flat, size[0], size[1])

        print("zapisano: %s" % (output if many else output))

    if not args.raw:
        print("\nVrijednosti su 0..1 (0 = najdalje, 1 = najblize).\n"
              "Metre kaze Loom: DepthMapping::fromRange(najblize, najdalje)\n"
              "  ./LoomApp %s %s <najblize_m> <najdalje_m>" %
              (args.source, output if not many else output + "/depth_0000.pfm"))


if __name__ == "__main__":
    main()
