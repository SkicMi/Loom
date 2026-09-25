"""Osoba iz videa bez zelenog platna: detektor nadje ljude, SAM2 ih prati kroz video, izlaz je alfa maska.

    python tools/comp/person_matte.py VIDEO IZLAZNA_MAPA [--width 1920] [--start 0] [--frames 0]
                                      [--every 15] [--model small] [--device cuda]

Izlaz: IZLAZNA_MAPA/matte_00000.png ... (8-bit siva, 255 = osoba) u sirini --width (visina po omjeru
snimke) i IZLAZNA_MAPA/matte.json (izvor, velicina, fps, koliko kadrova, tko je nadjen). Editorov
Compositor (cvor Person Matte) ih cita po kadru; Nuke/Resolve ih uzimaju kao sekvencu.

KAKO:
  - RT-DETR (COCO, klasa "person") na svakom --every. kadru nadje ljude; osoba koja se jos ne prati
    (okvir se malo preklapa s dosadasnjim maskama) dobije svoj SAM2 objekt s okvirom kao upitom
  - SAM2.1 u nacinu toka (streaming): kadar po kadar, bez ucitavanja cijelog videa u memoriju -
    4K snimka od 2000 kadrova ne stane ni u RAM ni na karticu
  - maska je SAM2-ova vjerojatnost (sigmoid logita) povecana na izlaznu velicinu, pa rub ostaje mek;
    vise osoba se spoji najvecom vrijednoscu

Modeli s Hugging Facea (prvi put se skinu u ~/.cache/huggingface): facebook/sam2.1-hiera-small
(~180 MB) ili -tiny/-base-plus/-large, i PekingU/rtdetr_r18vd_coco_o365 (~80 MB).
Napredak ide na izlaz kao "napredak I/N" da ga editor moze pokazati.
"""
import argparse, json, subprocess, sys, time
from pathlib import Path

import numpy as np
import torch
from PIL import Image

SAM_MODELS = {"tiny": "facebook/sam2.1-hiera-tiny", "small": "facebook/sam2.1-hiera-small",
              "base": "facebook/sam2.1-hiera-base-plus", "large": "facebook/sam2.1-hiera-large"}
DETECTOR = "PekingU/rtdetr_r18vd_coco_o365"


def probe(video):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "stream=width,height,r_frame_rate,nb_frames:stream_side_data=rotation",
                          "-of", "json", video], capture_output=True, text=True).stdout
    stream = json.loads(out)["streams"][0]
    num, den = (int(x) for x in stream["r_frame_rate"].split("/"))
    width, height = int(stream["width"]), int(stream["height"])
    rotation = 0
    for side in stream.get("side_data_list", []):
        rotation = int(side.get("rotation", 0))
    if abs(rotation) in (90, 270): width, height = height, width
    return width, height, num / max(den, 1), int(stream.get("nb_frames", 0) or 0)


def frames(video, width, height, start, count):
    """Kadrovi kao RGB uint8 (height, width, 3), redom, od start. BT.709 kao i Loom (Spool)"""
    command = ["ffmpeg", "-loglevel", "error"]
    command += ["-i", video, "-vf", f"select=gte(n\\,{start}),scale={width}:{height}:in_color_matrix=bt709",
                "-vsync", "0", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    process = subprocess.Popen(command, stdout=subprocess.PIPE)
    size = width * height * 3
    produced = 0
    while count <= 0 or produced < count:
        raw = process.stdout.read(size)
        if len(raw) < size: break
        yield np.frombuffer(raw, np.uint8).reshape(height, width, 3)
        produced += 1
    process.kill()


def box_iou(a, b):
    x1, y1 = max(a[0], b[0]), max(a[1], b[1])
    x2, y2 = min(a[2], b[2]), min(a[3], b[3])
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    union = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / max(union, 1e-9)


def mask_box(mask, threshold=0.5):
    ys, xs = np.nonzero(mask > threshold)
    if len(xs) == 0: return None
    return [float(xs.min()), float(ys.min()), float(xs.max()), float(ys.max())]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("out")
    ap.add_argument("--width", type=int, default=1920, help="sirina izlazne maske (visina po omjeru)")
    ap.add_argument("--work-width", type=int, default=1280, help="sirina na kojoj se trazi i prati")
    ap.add_argument("--start", type=int, default=0)
    ap.add_argument("--frames", type=int, default=0, help="koliko kadrova (0 = do kraja)")
    ap.add_argument("--every", type=int, default=15, help="svakih koliko kadrova detektor trazi nove ljude")
    ap.add_argument("--score", type=float, default=0.6, help="najmanja sigurnost detektora")
    ap.add_argument("--model", choices=list(SAM_MODELS), default="small")
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    args = ap.parse_args()

    from transformers import (AutoImageProcessor, RTDetrForObjectDetection,
                              Sam2VideoModel, Sam2VideoProcessor)
    device = args.device
    dtype = torch.bfloat16 if device == "cuda" else torch.float32
    width, height, fps, total = probe(args.video)
    workW = min(args.work_width, width); workH = int(round(height * workW / width / 2)) * 2
    outW = min(args.width, width); outH = int(round(height * outW / width / 2)) * 2
    wanted = args.frames if args.frames > 0 else max(0, total - args.start)
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("matte_*.png"): old.unlink()

    print(f"Ucitavam modele ({SAM_MODELS[args.model]}, {DETECTOR}) na {device}...", flush=True)
    detectorProcessor = AutoImageProcessor.from_pretrained(DETECTOR)
    detector = RTDetrForObjectDetection.from_pretrained(DETECTOR).to(device).eval()
    person = next(i for i, name in detector.config.id2label.items() if name.lower() == "person")
    processor = Sam2VideoProcessor.from_pretrained(SAM_MODELS[args.model])
    model = Sam2VideoModel.from_pretrained(SAM_MODELS[args.model]).to(device, dtype=dtype).eval()
    session = processor.init_video_session(inference_device=device, dtype=dtype)

    tracked = []          #id-evi SAM2 objekata
    found = []            #kad je koja osoba nadjena i gdje
    previous = None
    started = time.time()
    written = 0
    with torch.inference_mode():
        for index, frame in enumerate(frames(args.video, workW, workH, args.start, wanted)):
            inputs = processor(images=frame, device=device, return_tensors="pt")

            #Novi ljudi: detektor na ovom kadru; osoba ciji se okvir malo preklapa s maskama prethodnog
            #kadra je nova. Upit se dodaje PRIJE nego model obradi kadar (nacin toka)
            if index % args.every == 0:
                detection = detectorProcessor(images=frame, return_tensors="pt").to(device)
                result = detectorProcessor.post_process_object_detection(
                    detector(**detection), threshold=args.score, target_sizes=[(workH, workW)])[0]
                boxes = [b.tolist() for b, l in zip(result["boxes"], result["labels"]) if int(l) == person]
                known = [mask_box(m) for m in previous] if previous is not None else []
                fresh = [b for b in boxes if all(k is None or box_iou(b, k) < 0.3 for k in known)]
                if fresh:
                    ids = list(range(len(tracked) + 1, len(tracked) + 1 + len(fresh)))
                    processor.add_inputs_to_inference_session(
                        inference_session=session, frame_idx=index, obj_ids=ids,
                        input_boxes=[fresh], original_size=inputs.original_sizes[0])
                    tracked += ids
                    found += [dict(id=i, kadar=args.start + index, okvir=[round(v, 1) for v in b])
                              for i, b in zip(ids, fresh)]
                    print(f"  kadar {args.start + index}: {len(fresh)} nova osoba, ukupno {len(tracked)}", flush=True)

            current = None
            if tracked:
                output = model(inference_session=session, frame=inputs.pixel_values[0].to(dtype))
                current = processor.post_process_masks([output.pred_masks], original_sizes=inputs.original_sizes,
                                                       binarize=False)[0]
                current = torch.sigmoid(current.float())[:, 0].cpu().numpy()      #(objekata, h, w)
            previous = current

            alpha = current.max(0) if current is not None and len(current) else np.zeros((workH, workW), np.float32)
            matte = Image.fromarray((np.clip(alpha, 0, 1) * 255 + 0.5).astype(np.uint8))
            if (outW, outH) != (workW, workH): matte = matte.resize((outW, outH), Image.BILINEAR)
            matte.save(out / f"matte_{args.start + index:05d}.png", compress_level=1)
            written += 1
            if index % 10 == 0 or index == wanted - 1:
                rate = written / max(1e-6, time.time() - started)
                print(f"napredak {written}/{wanted}  ({rate:.1f} kadrova/s)", flush=True)

    info = dict(izvor=str(Path(args.video).resolve()), sirina=outW, visina=outH, fps=fps, prvi=args.start,
                kadrova=written, model=SAM_MODELS[args.model], detektor=DETECTOR, osobe=found,
                trajanje_s=round(time.time() - started, 1))
    (out / "matte.json").write_text(json.dumps(info, indent=1, ensure_ascii=False))
    print(f"Gotovo: {written} kadrova, {len(tracked)} osoba, {info['trajanje_s']} s -> {out}")


if __name__ == "__main__":
    main()
