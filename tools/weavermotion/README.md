# WeaverMotion — NVIDIA Kimodo

Loom runs a small tracked adapter around NVIDIA's official Kimodo CLI as a
separate background job. It exposes all seven installed model variants,
multi-prompt timing, sample count, diffusion steps, CFG, saved JSON constraints,
example export, foot cleanup, initial heading, and the API's root correction
margin. The text encoder runs on CPU to leave VRAM for Kimodo's motion model;
diffusion uses CUDA when PyTorch detects a compatible GPU.

## Local setup

Run ./tools/weavermotion/setup.sh from the Loom checkout. It creates the
ignored local environment at tools/weavermotion/.venv-clean and installs the
NVIDIA repository at the verified commit
58e781898b3d7e328a676a75d3e338c45dce3ad9.

Hugging Face access to meta-llama/Meta-Llama-3-8B-Instruct is gated. Accept
the model terms on Hugging Face, then authenticate locally with
tools/weavermotion/.venv-clean/bin/hf auth login. Never put the token in the
project, command-line arguments, or source control. Confirm login with
tools/weavermotion/.venv-clean/bin/hf auth whoami (it should print your
account name, not your token).

The first real generation downloads about 16.1 GB of Llama safetensors, the
1.1 GB Kimodo checkpoint, and roughly 0.34 GB of LLM2Vec adapters. The complete
Llama Hub repository lists about 32 GB because it also contains a separate
legacy checkpoint that this runtime does not need. Downloads are cached by
Hugging Face outside the repository in ~/.cache/huggingface.

## Current MVP boundary

Generated BVH is automatically imported and the motion is visible as animated
joint/bone geometry in Loom's viewport and timeline. The UI lists rigged scene
characters and anchors that skeleton preview to the selected character, but
the current renderer does not skin/deform its mesh or retarget SOMA's 77-joint
rig onto the mascot's 34-bone rig. Constraints can be loaded from Kimodo JSON;
Loom does not yet author/edit pose, end-effector, or root-path constraints on
its own timeline. SOMA BVH is the only Kimodo format Loom currently previews;
G1 CSV and SMPL-X NPZ outputs are retained in the output folder.
