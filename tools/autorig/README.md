# WeaverMotion Auto Rig

This local module takes a self-contained, unrigged GLB and uses [UniRig](https://github.com/VAST-AI-Research/UniRig) to predict a skeleton and skin weights, then restores the source mesh and materials. Loom preserves the original and imports the validated result as a model with visible joints.

## Setup

Run `bash tools/autorig/setup.sh` once. It creates a Python 3.11 virtual environment, installs CUDA-enabled PyTorch 2.7.1 for CUDA 12.8, installs UniRig's sparse point-cloud dependencies and a matching FlashAttention wheel, and checks out UniRig at commit `6793c6640ff01c8fb389f3993434124bb43d2933`. The model checkpoint downloads on first use from Hugging Face. UniRig's code is MIT licensed; checkpoint and data terms are described on its [Hugging Face model card](https://huggingface.co/VAST-AI/UniRig).

The current entry point accepts an unrigged, self-contained GLB without required extensions or external image files. It keeps the source intact and creates a timestamped folder under `tools/autorig/outputs/`.
Before inference it runs a real CUDA FlashAttention kernel check and uses UniRig's memory-efficient attention for skinning. Each output folder contains `autorig.log` with streamed subprocess output and the complete failure traceback, including Blender validation output.

## Verification

Before `complete.json` is written, the job reimports the GLB in Blender and checks that each vertex has finite, normalized weights, each mesh is bound to the predicted skeleton, and at least two non-root bones actually deform vertices. A bent-pose GLB is saved as `bend_preview.glb`. These checks measure file structure and deformation; they do not certify anatomical quality. Review the bones and the bend preview before using the result.

The Loom viewport displays the imported mesh at rest and draws the predicted skeleton. It does not yet evaluate GLB skinning during timeline playback; that and retargeting Kimodo onto the imported rig are the next motion-workflow step.

Python checks: `PYTHONPATH=tools/autorig tools/autorig/.venv/bin/python -m unittest test_autorig.InputTests -v`. Blender deformation checks: `blender --background --factory-startup --python-exit-code 1 --python tools/autorig/blender_tests.py`. Loom integration checks: `build/test_autorig`.
