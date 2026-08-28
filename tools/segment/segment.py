#!/usr/bin/env python3
"""SAM2 -> maska subjekta, PNG koji Spool cita.

Zakljucak arhitekture je isti kao kod dubine: model NE zivi u Loomu. Loom crta, Spool uvozi i
izvozi, a model stoji izmedu snimke i Spoola. Zato je ovo skripta a ne biblioteka, i zato
izlaz nije nista sto Loom definira nego obican PNG - 255 gdje je subjekt, 0 gdje nije.

    python3 segment.py portret.jpg                  # prompt u sredini kadra
    python3 segment.py portret.jpg --depth p.pfm    # prompt iz karte dubine
    python3 segment.py portret.jpg --point 640,400
    python3 segment.py portret.jpg --box 300,120,900,800
    python3 segment.py kadrovi/ -o maske/           # maska se propagira kroz kadrove
    python3 segment.py snimka.mp4 -o maske/
    python3 segment.py --check                      # samoprovjera nad poznatom maskom

CEMU MASKA SLUZI: ne ljepsoj slici, nego MJERENJU. Nad sintetskom scenom se sjena da
izracunati rukom - zid na sest metara, plocica na tri, i cetiri ugla su zbrajanje. Nad pravom
fotkom nema nicega sto bi reklo gdje je subjekt a gdje pozadina, pa se o rezultatu moze samo
imati dojam. Maska te dvije regije imenuje, i tek tada se "sjena je pala na pozadinu" i
"svjetlo je u prostoru, a ne naljepnica" daju izraziti brojem.

Maska pritom NIJE istina nego druga procjena - samo takva koja grijesi na drugim mjestima
nego dubina: dubina na mekim rubovima, maska na kosi i rukama. Zato njihovo slaganje nesto
znaci. I zato jedno pravilo: cim se maskom nesto ISPRAVI, njome se to vise ne smije
provjeravati.
"""
import argparse, os, struct, subprocess, sys, tempfile

MODELS = {
    "tiny":  "facebook/sam2.1-hiera-tiny",
    "small": "facebook/sam2.1-hiera-small",
    "base":  "facebook/sam2.1-hiera-base-plus",
    "large": "facebook/sam2.1-hiera-large",
}

IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp")
VIDEO_SUFFIXES = (".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".mts")


def read_pfm(path):
    """Jedan kanal, float. PFM broji retke ODOZDO prema gore, pa se pri citanju vracaju
    natrag - inace bi prompt iz dubine pao na krivu polovicu slike."""
    with open(path, "rb") as f:
        if f.readline().strip() != b"Pf":
            sys.exit("%s nije jednokanalni PFM (Pf)." % path)
        width, height = (int(v) for v in f.readline().split())
        scale = float(f.readline().strip())
        order = "<" if scale < 0 else ">"

        rows = []
        for _ in range(height):
            rows.append(struct.unpack("%s%df" % (order, width), f.read(4 * width)))

    rows.reverse()
    return [v for row in rows for v in row], width, height


def prompt_from_depth(path, size, percentile=90):
    """Gdje je subjekt, po samoj karti dubine: teziste najblize desetine slike.

    Ista definicija kojom LoomApp bira udaljenost subjekta - deveti decil dispariteta. Time
    dubina PREDLAZE, a SAM2 ODLUCUJE: dvije procjene koje grijese na razlicitim mjestima, pa
    njihovo slaganje nije samorazumljivo."""
    values, width, height = read_pfm(path)
    if (width, height) != size:
        sys.exit("karta dubine je %dx%d, a slika %dx%d - moraju biti piksel na piksel"
                 % (width, height, size[0], size[1]))

    threshold = sorted(values)[len(values) * percentile // 100]

    total = sumX = sumY = 0
    for index, value in enumerate(values):
        if value >= threshold:
            total += 1
            sumX += index % width
            sumY += index // width

    if total == 0:
        sys.exit("u karti dubine nema nijedne tocke iznad praga")

    return (sumX / total, sumY / total)


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


def write_mask(path, mask):
    """255 gdje je subjekt, 0 gdje nije. Jedan kanal - Spool ionako svaku sliku prosiri na
    cetiri, pa je maska najmanji file koji nosi istu obavijest."""
    from PIL import Image
    Image.fromarray((mask * 255).astype("uint8"), mode="L").save(path)


def pick_best(masks, scores):
    """SAM2 na jedan klik nudi tri odgovora - "dio", "cjelina", "sve oko toga". Uzima se onaj
    kojem sam model daje najvecu ocjenu preklapanja; bez toga se dobije nasumicno jedan od
    tri, pa maska skace izmedu ruke i cijele osobe."""
    import torch
    best = int(torch.argmax(scores.reshape(-1)))
    return masks.reshape(-1, masks.shape[-2], masks.shape[-1])[best]


def segment_one(model, processor, image, point, box, device):
    import torch

    kwargs = {}
    if box is not None:
        kwargs["input_boxes"] = [[list(box)]]
    else:
        kwargs["input_points"] = [[[[float(point[0]), float(point[1])]]]]
        kwargs["input_labels"] = [[[1]]]

    inputs = processor(images=image, return_tensors="pt", **kwargs).to(device)

    with torch.no_grad():
        outputs = model(**inputs, multimask_output=True)

    masks = processor.post_process_masks(outputs.pred_masks, inputs["original_sizes"],
                                         binarize=True)[0]
    return pick_best(masks, outputs.iou_scores).cpu().numpy().astype("uint8")


def segment_sequence(model, processor, paths, point, box, device, output):
    """Kroz snimku se maska PROPAGIRA, ne racuna iznova po kadru.

    Klik ide samo u prvi kadar; dalje SAM2 nosi sto je vidio. Maska racunata po kadru bi
    svakom dala vlastito misljenje o tome gdje subjekt prestaje, pa bi rub titrao i mjerenje
    nad njom mjerilo bi to titranje umjesto svjetla - ista zamka kao normalizacija dubine po
    kadru."""
    from PIL import Image

    frames = [Image.open(p).convert("RGB") for p in paths]
    frameWidth, frameHeight = frames[0].size
    session = processor.init_video_session(video=frames, inference_device=device)

    if box is not None:
        processor.add_inputs_to_inference_session(
            inference_session=session, frame_idx=0, obj_ids=1, input_boxes=[[list(box)]])
    else:
        processor.add_inputs_to_inference_session(
            inference_session=session, frame_idx=0, obj_ids=1,
            input_points=[[[[float(point[0]), float(point[1])]]]], input_labels=[[[1]]])

    #Pocetni kadar se kaze naglas. Bez njega SAM2 odbija krenuti ("cannot determine the
    #starting frame index") jer sam prompt jos nije prosao kroz model - a prosao bi tek da
    #smo prije ovoga rucno pozvali inferenciju nad nultim kadrom
    written = 0
    for output_frame in model.propagate_in_video_iterator(session, start_frame_idx=0):
        masks = processor.post_process_masks([output_frame.pred_masks],
                                             [[frameHeight, frameWidth]], binarize=True)[0]

        mask = masks.reshape(-1, masks.shape[-2], masks.shape[-1])[0].cpu().numpy().astype("uint8")
        target = os.path.join(output, "mask_%04d.png" % output_frame.frame_idx)
        write_mask(target, mask)

        written += 1
        print("  %d/%d  %s  pokriva %.1f%% kadra"
              % (written, len(paths), os.path.basename(target), 100.0 * mask.mean()))

    return written


def self_check(model, processor, device):
    """Maska protiv maske koja se zna napamet.

    Scena je namjerno NESIMETRICNA - pravokutnik pomaknut ulijevo i prema gore. Krug u
    sredini bi prosao i kad bi se x i y zamijenili, i kad bi se maska zapisala naopako; ovako
    obje greske ispadnu iz kutije koju mjerimo."""
    from PIL import Image
    import numpy

    width, height = 640, 480
    left, top, right, bottom = 96, 64, 288, 240

    pixels = numpy.zeros((height, width, 3), dtype="uint8")
    pixels[:, :] = (30, 30, 40)
    pixels[top:bottom, left:right] = (230, 200, 120)

    #Malo suma, jer je model naviknut na fotografije a ne na plohe jedne boje
    rng = numpy.random.default_rng(7)
    pixels = numpy.clip(pixels.astype("int16") + rng.integers(-6, 7, pixels.shape), 0, 255).astype("uint8")

    image = Image.fromarray(pixels)
    point = ((left + right) / 2.0, (top + bottom) / 2.0)
    mask = segment_one(model, processor, image, point, None, device)

    truth = numpy.zeros((height, width), dtype="uint8")
    truth[top:bottom, left:right] = 1

    intersection = int((mask & truth).sum())
    union = int((mask | truth).sum())
    iou = intersection / union if union else 0.0

    rows = numpy.any(mask, axis=1)
    cols = numpy.any(mask, axis=0)
    got = (int(numpy.argmax(cols)), int(numpy.argmax(rows)),
           int(width - numpy.argmax(cols[::-1])), int(height - numpy.argmax(rows[::-1])))

    print("  poznata kutija: %s" % (str((left, top, right, bottom))))
    print("  maskina kutija: %s" % (str(got),))
    print("  IoU: %.4f   piksela u maski: %d, u istini: %d" % (iou, int(mask.sum()), int(truth.sum())))

    #Kutija se trazi na cetiri piksela tocno, IoU na 0.99. To je vise od "otprilike se
    #poklapa" i uhvatilo bi svaku zamjenu osi ili okrenut zapis
    ok = iou > 0.99 and all(abs(a - b) <= 4 for a, b in zip(got, (left, top, right, bottom)))
    print("  %s" % ("OK" if ok else "PALO"))
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description="SAM2 -> maska subjekta (PNG)")
    parser.add_argument("source", nargs="?", help="slika, mapa sa slikama, ili snimka")
    parser.add_argument("-o", "--output", help="izlazni file ili mapa")
    parser.add_argument("--model", choices=sorted(MODELS), default="small")
    parser.add_argument("--point", help="x,y - tocka koja JEST subjekt")
    parser.add_argument("--box", help="x0,y0,x1,y1 - okvir oko subjekta")
    parser.add_argument("--depth", help="karta dubine iz koje se izvede prompt")
    parser.add_argument("--percentile", type=int, default=90,
                        help="koji decil dubine je subjekt (default 90)")
    parser.add_argument("--check", action="store_true", help="samoprovjera nad poznatom maskom")
    parser.add_argument("--cpu", action="store_true")
    args = parser.parse_args()

    if not args.check and not args.source:
        parser.error("treba izvor, ili --check")

    import torch
    from PIL import Image
    from transformers import Sam2Model, Sam2Processor, Sam2VideoModel, Sam2VideoProcessor

    device = "cpu" if args.cpu or not torch.cuda.is_available() else "cuda"
    name = MODELS[args.model]
    print("model:  %s  (%s)" % (name, device))

    if args.check:
        processor = Sam2Processor.from_pretrained(name)
        model = Sam2Model.from_pretrained(name).to(device).eval()
        return self_check(model, processor, device)

    with tempfile.TemporaryDirectory() as temp:
        paths, many = collect(args.source, temp)

        size = Image.open(paths[0]).size
        box = None
        point = (size[0] / 2.0, size[1] / 2.0)

        if args.box:
            box = [float(v) for v in args.box.split(",")]
            if len(box) != 4:
                sys.exit("--box treba x0,y0,x1,y1")
        elif args.point:
            point = tuple(float(v) for v in args.point.split(","))
        elif args.depth:
            point = prompt_from_depth(args.depth, size, args.percentile)
            print("prompt iz dubine: (%.0f, %.0f) - teziste najblizeg decila" % point)

        if many:
            output = args.output or "masks"
            os.makedirs(output, exist_ok=True)

            processor = Sam2VideoProcessor.from_pretrained(name)
            model = Sam2VideoModel.from_pretrained(name).to(device).eval()
            written = segment_sequence(model, processor, paths, point, box, device, output)
            print("zapisano: %s  (%d maski)" % (output, written))
        else:
            processor = Sam2Processor.from_pretrained(name)
            model = Sam2Model.from_pretrained(name).to(device).eval()

            mask = segment_one(model, processor, Image.open(paths[0]).convert("RGB"),
                               point, box, device)
            output = args.output or (os.path.splitext(args.source)[0] + "_mask.png")
            write_mask(output, mask)
            print("zapisano: %s  (subjekt pokriva %.1f%% kadra)" % (output, 100.0 * mask.mean()))

    return 0


if __name__ == "__main__":
    sys.exit(main())
