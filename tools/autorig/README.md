# WeaverMotion Auto Rig

This local module takes an unrigged GLB or glTF model and uses [UniRig](https://github.com/VAST-AI-Research/UniRig) to predict skeleton positions and skin weights, then fits the resulting rig to the UE5 Manny bone names and hierarchy. It restores the source mesh and materials. Loom preserves the original and imports the validated result as a model with visible joints.

## Setup

Run `bash tools/autorig/setup.sh` once. It creates a Python 3.11 virtual environment, installs CUDA-enabled PyTorch 2.7.1 for CUDA 12.8, installs UniRig's sparse point-cloud dependencies and a matching FlashAttention wheel, and checks out UniRig at commit `6793c6640ff01c8fb389f3993434124bb43d2933`. The model checkpoint downloads on first use from Hugging Face. UniRig's code is MIT licensed; checkpoint and data terms are described on its [Hugging Face model card](https://huggingface.co/VAST-AI/UniRig).

The current entry point accepts unrigged GLB or glTF files. For glTF it uses Blender to pack referenced buffers and images into a temporary self-contained GLB before inference. Required extensions and compressed geometry are not supported. It keeps the source intact and creates a timestamped folder under `tools/autorig/outputs/`.
Before inference it runs a real CUDA FlashAttention kernel check and uses UniRig's memory-efficient attention for skinning. Each output folder contains `autorig.log` with streamed subprocess output and the complete failure traceback, including Blender validation output.

## Verification

Before `complete.json` is written, the job reimports the GLB in Blender and checks that each vertex has finite, normalized weights, each mesh is bound to the predicted skeleton, and at least two non-root bones actually deform vertices. A bent-pose GLB is saved as `bend_preview.glb`. These checks measure file structure and deformation; they do not certify anatomical quality. Review the bones and the bend preview before using the result.

The Loom viewport imports GLTF skin weights and inverse bind matrices, deforms the mesh from the imported joint transforms, and keeps the joint links when a project is reopened. Generate Kimodo motion with this rig selected to retarget the clip onto its bones and play the deformed mesh on the timeline. The bend preview remains a separate offline quality check.

Python checks: `PYTHONPATH=tools/autorig tools/autorig/.venv/bin/python -m unittest test_autorig.InputTests -v`. Blender deformation checks: `blender --background --factory-startup --python-exit-code 1 --python tools/autorig/blender_tests.py`. Loom integration checks: `build/test_autorig`.

## Default Unreal Mannequin profile

Auto Rig now emits the 88-bone UE5 Manny hierarchy by default. The compact
`manny_template.json` was extracted from the Manny FBX supplied for this Loom
workspace. Its positions are fitted to UniRig's predicted 52 joint landmarks;
existing skin weights remain attached to corresponding Manny deform bones.
Extra spine, twist, metacarpal and IK bones are present for compatibility.
Twist and IK bones have no inferred vertex weights. The original predicted rig
is retained as `rigged_unirig.glb` in each output folder. Import the final
`rigged.glb` in Loom for Animator and Kimodo retargeting.
