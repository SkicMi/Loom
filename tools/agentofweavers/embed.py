"""Frozen text encoder for AgentOfWeavers: every description once, stored, never on the GPU during training.

    .venv/bin/python embed.py DATA_DIR              # DATA_DIR/houses.jsonl from procedura-gen
    .venv/bin/python embed.py --prompts FILE.jsonl  # held-out prompts ({"prompt": ...} per line)

Writes next to the input:
    embeddings.f16.npy   [N, 768] float16, L2-normalised
    texts.jsonl          one line per row: {"row", "id", "which", "split", "text"}
    encoder.json         model name, revision, pooling, dimension, input hash

Encoder: BAAI/bge-base-en-v1.5 (English, CLS pooling, normalised), the model and descriptions
are English only. Change ENCODER and re-run to compare encoders; the data set stays the same.
"""

import argparse
import hashlib
import json
import pathlib
import sys

import numpy as np
import torch
from transformers import AutoModel, AutoTokenizer

ENCODER = "BAAI/bge-base-en-v1.5"


def read_texts(path: pathlib.Path, prompts: bool):
    rows = []
    with path.open() as source:
        for line in source:
            record = json.loads(line)
            if prompts:
                rows.append({"id": record.get("id", len(rows)), "which": 0, "split": "heldout", "text": record["prompt"]})
                continue
            if record.get("status") != "pass":
                continue
            for which, text in enumerate(record.get("descriptions", [])):
                rows.append({"id": record["id"], "which": which, "split": record["split"], "text": text})
    return rows


@torch.no_grad()
def encode(texts, batch_size=256):
    tokenizer = AutoTokenizer.from_pretrained(ENCODER)
    model = AutoModel.from_pretrained(ENCODER, torch_dtype=torch.float16).cuda().eval()
    out = np.zeros((len(texts), model.config.hidden_size), dtype=np.float16)
    for start in range(0, len(texts), batch_size):
        batch = tokenizer(texts[start:start + batch_size], padding=True, truncation=True, max_length=128, return_tensors="pt").to("cuda")
        cls = model(**batch).last_hidden_state[:, 0]
        out[start:start + len(cls)] = torch.nn.functional.normalize(cls.float(), dim=-1).cpu().numpy().astype(np.float16)
        if start // batch_size % 50 == 0:
            print(f"{min(start + batch_size, len(texts))} / {len(texts)}", flush=True)
    return out, model.config


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("data", help="DATA_DIR with houses.jsonl, or a prompts .jsonl with --prompts")
    parser.add_argument("--prompts", action="store_true")
    args = parser.parse_args()
    source = pathlib.Path(args.data)
    path = source if args.prompts else source / "houses.jsonl"
    out_dir = path.parent if not args.prompts else path.with_suffix("")
    out_dir.mkdir(parents=True, exist_ok=True)
    rows = read_texts(path, args.prompts)
    if not rows:
        sys.exit(f"no texts in {path}")
    vectors, config = encode([row["text"] for row in rows])
    np.save(out_dir / "embeddings.f16.npy", vectors)
    with (out_dir / "texts.jsonl").open("w") as target:
        for index, row in enumerate(rows):
            target.write(json.dumps({"row": index, **row}) + "\n")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    (out_dir / "encoder.json").write_text(json.dumps({
        "model": ENCODER, "revision": getattr(config, "_commit_hash", None), "pooling": "cls, l2-normalised",
        "dimension": int(vectors.shape[1]), "rows": len(rows), "input": str(path), "input_sha256": digest}, indent=2) + "\n")
    print(f"{len(rows)} texts -> {out_dir / 'embeddings.f16.npy'}")


if __name__ == "__main__":
    main()
