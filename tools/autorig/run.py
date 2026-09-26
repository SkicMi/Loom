#!/usr/bin/env python3
"""Local Auto Rig job (UniRig, or the direct geometric Manny fit in direct_rig.py).
Never replaces the source asset or an existing output directory."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
VENDOR = ROOT / "vendor/UniRig"
COMMIT = "6793c6640ff01c8fb389f3993434124bb43d2933"


def inspect_input(path: Path) -> dict:
    """Validate the normalized, self-contained GLB that UniRig will process."""
    if path.suffix.lower() != ".glb":
        raise ValueError("Expected a normalized GLB input.")
    with path.open("rb") as file:
        header = file.read(20)
        if len(header) != 20:
            raise ValueError("Truncated GLB")
        magic, version, size, length, kind = struct.unpack("<5I", header)
        if magic != 0x46546C67 or version != 2 or kind != 0x4E4F534A:
            raise ValueError("Expected a GLB v2 with a JSON chunk")
        if size != path.stat().st_size or length > size - 20:
            raise ValueError("Invalid GLB chunk length")
        document = json.loads(file.read(length))
    if document.get("skins") or any("skin" in node for node in document.get("nodes", [])):
        raise ValueError("This model already contains a rig; use the unrigged source model.")
    if not document.get("meshes"):
        raise ValueError("The file contains no mesh")
    for section in ("buffers", "images"):
        if any("uri" in item and not item["uri"].startswith("data:") for item in document.get(section, [])):
            raise ValueError("External resources are not supported; export a self-contained GLB.")
    if document.get("extensionsRequired"):
        raise ValueError("Compressed/extended GLB input is not supported yet; export an uncompressed GLB.")
    return document


def run_step(label: str, arguments: list[str], work: Path) -> None:
    """Use argv, not upstream eval-based shell scripts (spaces and shell characters are safe)."""
    print(f"AutoRig: {label}", flush=True)
    env = os.environ.copy()
    env["PYTHONPATH"] = str(VENDOR)
    env["PYTHONUNBUFFERED"] = "1"
    env["WANDB_MODE"] = "disabled"
    env["OMP_NUM_THREADS"] = "4"
    run_process([sys.executable, *arguments], work, env, work.parent / "autorig.log")


def run_process(arguments: list[str], cwd: Path, env: dict[str, str], log_path: Path) -> None:
    """Stream subprocess output to Loom and a durable log, preserving failure traces."""
    with log_path.open("a", encoding="utf-8") as log:
        log.write(f"\n$ {' '.join(arguments)}\n")
        log.flush()
        process = subprocess.Popen(arguments, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                   errors="replace", bufsize=1)
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            log.write(line)
        process.stdout.close()
        status = process.wait()
    if status:
        raise subprocess.CalledProcessError(status, arguments)


def require_file(path: Path) -> None:
    # Some upstream steps catch errors and return status 0. Never treat that as success alone.
    if not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError(f"The backend did not produce {path.name}; see the preceding log.")
def configure_model_configs(work: Path) -> None:
    """Use the official SDPA skeleton path and memory-efficient skinning attention."""
    import yaml

    ar_config = work / "configs/model/unirig_ar_350m_1024_81920_float32.yaml"
    ar = yaml.safe_load(ar_config.read_text())
    ar["llm"]["_attn_implementation"] = "sdpa"
    ar_config.write_text(yaml.safe_dump(ar))
    skin_config = work / "configs/model/unirig_skin.yaml"
    skin = yaml.safe_load(skin_config.read_text())
    skin["mesh_encoder"]["enable_flash"] = True
    skin_config.write_text(yaml.safe_dump(skin))


def verify_flash_attention() -> str:
    """Run a real tiny CUDA kernel early so incompatible wheels fail clearly."""
    try:
        import flash_attn
        import torch

        if not torch.cuda.is_available():
            raise RuntimeError("CUDA is unavailable")
        qkv = torch.randn((8, 3, 1, 16), device="cuda", dtype=torch.float16)
        cu_seqlens = torch.tensor([0, 8], device="cuda", dtype=torch.int32)
        result = flash_attn.flash_attn_varlen_qkvpacked_func(
            qkv, cu_seqlens, max_seqlen=8, dropout_p=0.0, softmax_scale=16 ** -0.5
        )
        torch.cuda.synchronize()
        if result.shape != (8, 1, 16) or not torch.isfinite(result).all().item():
            raise RuntimeError("FlashAttention returned an invalid result")
        device = torch.cuda.get_device_name()
        del result, qkv, cu_seqlens
        torch.cuda.empty_cache()
        return device
    except Exception as error:
        raise RuntimeError(f"FlashAttention CUDA kernel is unavailable: {error}") from error




def finish_manny(source: Path, output: Path, work: Path, predicted: Path, log_path: Path,
                 manifest: dict, started: float) -> None:
    """UniRig-52 layout -> UE5 Manny (88 bones, hand rig) -> independent Blender validation."""
    result = output / "rigged.glb"
    print("AutoRig: Fit UE5 Manny skeleton to the 52 joints", flush=True)
    run_process(["blender", "--background", "--factory-startup", "--python-exit-code", "1",
                 "--python", str(ROOT / "manny_rig.py"), "--", str(predicted),
                 str(result), str(ROOT / "manny_template.json")], work, os.environ.copy(), log_path)
    require_file(result)
    blender_command = ["blender", "--background", "--factory-startup", "--python-exit-code", "1",
                       "--python", str(ROOT / "validate.py"), "--", str(result),
                       "--report", str(output / "validation.json"),
                       "--preview", str(output / "bend_preview.glb")]
    print("AutoRig: independent Blender deformation validation", flush=True)
    run_process(blender_command, work, os.environ.copy(), log_path)
    require_file(output / "validation.json")
    manifest.update({"source": str(source), "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                     "rig_profile": "UE5 Manny", "seconds": round(time.monotonic() - started, 2),
                     "result": str(result)})
    # This marker is written only after all steps and independent deformation validation passed.
    (output / "complete.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"AutoRig complete: {result}", flush=True)


def generate_direct(source: Path, output: Path) -> None:
    """Joints measured from the mesh (direct_rig.py), no neural network, no GPU."""
    source = source.resolve(strict=True)
    if source.suffix.lower() not in {".glb", ".gltf"}:
        raise ValueError("Auto Rig accepts GLB or glTF models.")
    if source.suffix.lower() == ".glb":
        inspect_input(source)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    log_path = output / "autorig.log"
    log_path.write_text(f"AutoRig start (direct)\nSource: {source}\n", encoding="utf-8")
    work = output / "work"
    work.mkdir()
    predicted = output / "rigged_direct52.glb"
    print("AutoRig: measure joints and bind the skin (direct)", flush=True)
    run_process(["blender", "--background", "--factory-startup", "--python-exit-code", "1",
                 "--python", str(ROOT / "direct_rig.py"), "--", str(source), str(predicted)],
                work, os.environ.copy(), log_path)
    require_file(predicted)
    finish_manny(source, output, work, predicted, log_path,
                 {"backend": "direct", "hand_rig": "hand_rig.py (+X curls into the palm)"}, started)


def generate(source: Path, output: Path, seed: int) -> None:

    source = source.resolve(strict=True)
    source_suffix = source.suffix.lower()
    if source_suffix not in {".glb", ".gltf"}:
        raise ValueError("Auto Rig accepts GLB or glTF models.")
    if source_suffix == ".glb":
        inspect_input(source)
    revision = subprocess.check_output(["git", "-C", str(VENDOR), "rev-parse", "HEAD"], text=True).strip()
    if revision != COMMIT:
        raise RuntimeError("Unexpected UniRig version. Run tools/autorig/setup.sh.")
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    log_path = output / "autorig.log"
    with log_path.open("w", encoding="utf-8") as log:
        log.write(f"AutoRig start\nSource: {source}\nUniRig revision: {revision}\nSeed: {seed}\n")
    device = verify_flash_attention()
    print(f"AutoRig: FlashAttention CUDA kernel verified on {device}", flush=True)
    with log_path.open("a", encoding="utf-8") as log:
        log.write(f"FlashAttention CUDA kernel verified on {device}\n")
    # Isolated working directory/configuration also prevents concurrent jobs sharing cached meshes.
    work = output / "work"
    work.mkdir()
    shutil.copytree(VENDOR / "configs", work / "configs")
    normalized = work / "input.glb"
    if source_suffix == ".gltf":
        print("Auto Rig: packing glTF assets into a self-contained GLB", flush=True)
        converter = ROOT / "gltf_to_glb.py"
        blender_command = ["blender", "--background", "--factory-startup", "--python-exit-code", "1",
                           "--python", str(converter), "--", str(source), str(normalized)]
        run_process(blender_command, work, os.environ.copy(), log_path)
    else:
        shutil.copy2(source, normalized)
    inspect_input(normalized)
    configure_model_configs(work)
    base_extract = ["-m", "src.data.extract", "--config=configs/data/quick_inference.yaml",
                    "--require_suffix=glb,fbx", "--faces_target_count=50000", "--num_runs=1",
                    "--id=0", "--time=loom", "--force_override=true", "--output_dir=cache"]
    run_step("1/5 Read mesh", [*base_extract, "--input=input.glb"], work)
    raw = list((work / "cache").rglob("raw_data.npz"))
    if len(raw) != 1:
        raise RuntimeError("Mesh extraction failed or produced an ambiguous result")
    run_step("2/5 Predict skeleton", [str(VENDOR / "run.py"),
             "--task=configs/task/quick_inference_skeleton_articulationxl_ar_256.yaml",
             f"--seed={seed}", "--input=input.glb", "--output=skeleton.fbx", "--npz_dir=cache"], work)
    require_file(work / "skeleton.fbx")
    run_step("3/5 Read predicted skeleton", [*base_extract, "--input=skeleton.fbx"], work)
    run_step("4/5 Predict skin weights", [str(VENDOR / "run.py"),
             "--task=configs/task/quick_inference_unirig_skin.yaml", f"--seed={seed}",
             "--input=skeleton.fbx", "--output=skin.fbx", "--npz_dir=cache", "--data_name=raw_data.npz"], work)
    require_file(work / "skin.fbx")
    unirig_result = output / "rigged_unirig.glb"
    run_step("5/6 Restore materials", ["-m", "src.inference.merge",
             "--require_suffix=glb,fbx", "--num_runs=1", "--id=0", "--source=skin.fbx",
             "--target=input.glb", f"--output={unirig_result}"], work)
    require_file(unirig_result)
    finish_manny(source, output, work, unirig_result, log_path,
                 {"backend": "UniRig", "revision": revision, "seed": seed}, started)


def generate_auto(source: Path, output: Path, seed: int) -> None:
    """Direct first (no GPU, exact hands); UniRig only when direct cannot rig the model."""
    try:
        generate_direct(source, output)
        return
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        if not (VENDOR / "run.py").is_file():
            raise
        print(f"AutoRig: direct failed ({error}); trying UniRig", flush=True)
        failed = output.resolve().with_name(output.resolve().name + "-direct-failed")
        if failed.exists():
            shutil.rmtree(failed)
        if output.exists():
            output.rename(failed)
    generate(source, output, seed)
    with (output.resolve() / "autorig.log").open("a", encoding="utf-8") as log:
        log.write(f"Direct backend failed first; its log is in {failed.name}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--backend", choices=("auto", "unirig", "direct"), default="auto",
                        help="direct: joints measured from a standing A/T-posed humanoid, no GPU; "
                             "auto: direct first, UniRig when direct fails and UniRig is installed")
    args = parser.parse_args()
    try:
        if args.backend == "direct":
            generate_direct(args.input, args.output)
        elif args.backend == "unirig":
            generate(args.input, args.output, args.seed)
        else:
            generate_auto(args.input, args.output, args.seed)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        if args.output.is_dir():
            with (args.output / "autorig.log").open("a", encoding="utf-8") as log:
                log.write(f"\nAutoRig FAILED: {type(error).__name__}: {error}\n")
        print(f"AutoRig FAILED: {error}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
