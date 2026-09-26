#!/usr/bin/env python3
"""BF16 LoRA SFT for Qwen3.5-4B on the local RTX 5070."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
DEFAULT_MODEL = REPO / ".cache/weaverprocedura/models/qwen3.5-4b"
DEFAULT_OUTPUT = None
BASE_MODEL_ID = "Qwen/Qwen3.5-4B"
BASE_MODEL_REVISION = "851bf6e806efd8d0a36b00ddf55e13ccb7b8cd0a"
DATA = ROOT / "data/v1"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--data-dir", type=Path, default=DATA)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--max-seq-length", type=int, default=512)
    parser.add_argument("--epochs", type=float, default=3.0)
    parser.add_argument("--batch-size", type=int, default=1)
    parser.add_argument("--gradient-accumulation", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=1.5e-4)
    parser.add_argument("--resume-from-checkpoint", type=str, default=None)
    parser.add_argument("--dry-run", action="store_true", help="Load/format data without allocating the model")
    args = parser.parse_args()
    if args.output is None:
        args.output = REPO / f".cache/weaverprocedura/adapters/qwen3.5-4b-loom-{args.data_dir.name}"

    if not (args.model / "config.json").is_file():
        raise SystemExit(f"Base model not found: {args.model}")
    if not (args.data_dir / "train.jsonl").is_file() or not (args.data_dir / "eval.jsonl").is_file():
        raise SystemExit("Run build_dataset.py and validate_dataset.py first")

    if not args.dry_run:
        from unsloth import FastLanguageModel
    import torch
    if not args.dry_run:
        if not torch.cuda.is_available():
            raise SystemExit("CUDA is required for the approved RTX 5070 training run")
        props = torch.cuda.get_device_properties(0)
        print(f"GPU: {props.name}; VRAM {props.total_memory / (1024**3):.2f} GiB; SM {props.major}.{props.minor}", flush=True)
        if props.major < 12:
            print("Note: selected training settings were chosen for RTX 50-series / SM120; this GPU is older.", flush=True)

    from datasets import load_dataset
    from transformers import set_seed
    set_seed(5070)

    if args.dry_run:
        from transformers import AutoTokenizer
        tokenizer = AutoTokenizer.from_pretrained(str(args.model), local_files_only=True)
    else:
        model, tokenizer = FastLanguageModel.from_pretrained(
            model_name=str(args.model),
            max_seq_length=args.max_seq_length,
            load_in_4bit=False,
            load_in_16bit=True,
            full_finetuning=False,
        )
        model = FastLanguageModel.get_peft_model(
            model,
            r=16,
            target_modules=["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"],
            lora_alpha=16,
            lora_dropout=0.0,
            bias="none",
            use_gradient_checkpointing="unsloth",
            random_state=5070,
            max_seq_length=args.max_seq_length,
        )
        model.print_trainable_parameters()

    def render(example: dict) -> dict:
        messages = example["messages"]
        try:
            rendered = tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=False,
                                                     enable_thinking=False)
        except TypeError:
            rendered = tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=False)
        return {"text": rendered}

    raw = load_dataset("json", data_files={"train": str(args.data_dir / "train.jsonl"),
                                            "eval": str(args.data_dir / "eval.jsonl")})
    tokenized_view = {name: raw[name].map(render, remove_columns=raw[name].column_names)
                      for name in ("train", "eval")}
    core_tokenizer = getattr(tokenizer, "tokenizer", tokenizer)
    token_lengths = [len(core_tokenizer.encode(row["text"], add_special_tokens=False)) for row in tokenized_view["train"]]
    over_limit = sum(length > args.max_seq_length for length in token_lengths)
    print(json.dumps({"train": len(tokenized_view["train"]), "eval": len(tokenized_view["eval"]),
                      "max_seq_length": args.max_seq_length, "train_over_limit": over_limit,
                      "max_tokens": max(token_lengths, default=0)}, indent=2), flush=True)

    if args.dry_run:
        print("Dry run passed: data loads and all chat templates render.")
        return 0

    from trl import SFTConfig, SFTTrainer
    from transformers import TrainerCallback
    import gc

    class ClearCudaCacheAtEpochEnd(TrainerCallback):
        def on_epoch_end(self, training_args, state, control, **kwargs):
            gc.collect()
            if torch.cuda.is_available():
                torch.cuda.synchronize()
                torch.cuda.empty_cache()
            print(f"Cleared cached CUDA allocations after epoch {state.epoch:.0f}.", flush=True)
            return control

    args.output.mkdir(parents=True, exist_ok=True)
    expected_steps = max(1, int((len(tokenized_view["train"]) * args.epochs +
                                  args.batch_size * args.gradient_accumulation - 1) //
                                 (args.batch_size * args.gradient_accumulation)))
    warmup_steps = max(1, int(expected_steps * 0.05))
    config = SFTConfig(
        output_dir=str(args.output / "checkpoints"),
        dataset_text_field="text",
        max_length=args.max_seq_length,
        per_device_train_batch_size=args.batch_size,
        per_device_eval_batch_size=1,
        gradient_accumulation_steps=args.gradient_accumulation,
        num_train_epochs=args.epochs,
        learning_rate=args.learning_rate,
        lr_scheduler_type="cosine",
        warmup_steps=warmup_steps,
        weight_decay=0.01,
        optim="adamw_8bit",
        bf16=True,
        fp16=False,
        logging_steps=1,
        # Keep full-vocabulary evaluation off the 12 GB training GPU: it
        # materializes large logits. Evaluate later with the 4-bit loader.
        eval_strategy="no",
        save_strategy="epoch",
        save_total_limit=3,
        load_best_model_at_end=False,
        gradient_checkpointing=True,
        gradient_checkpointing_kwargs={"use_reentrant": False},
        torch_empty_cache_steps=25,
        max_grad_norm=0.3,
        packing=False,
        dataset_num_proc=1,
        report_to="none",
        seed=5070,
        data_seed=5070,
    )
    trainer = SFTTrainer(
        model=model,
        processing_class=tokenizer,
        args=config,
        train_dataset=tokenized_view["train"],
        eval_dataset=None,
        callbacks=[ClearCudaCacheAtEpochEnd()],
    )
    result = trainer.train(resume_from_checkpoint=args.resume_from_checkpoint)
    trainer.save_model(str(args.output))
    tokenizer.save_pretrained(str(args.output))
    metrics = {"train_metrics": result.metrics, "history": trainer.state.log_history,
               "base_model": str(args.model), "base_model_id": BASE_MODEL_ID,
               "base_model_revision": BASE_MODEL_REVISION, "dataset_manifest": json.loads((args.data_dir / "manifest.json").read_text()),
               "config": {"max_seq_length": args.max_seq_length, "epochs": args.epochs,
                          "batch_size": args.batch_size, "gradient_accumulation": args.gradient_accumulation,
                          "learning_rate": args.learning_rate, "lora_rank": 16, "lora_alpha": 16,
                          "precision": "bf16", "quantized_base": False}}
    (args.output / "training_metrics.json").write_text(json.dumps(metrics, indent=2, default=str) + "\n", encoding="utf-8")
    print(f"Adapter saved to {args.output}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
