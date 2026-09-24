# WeaverMotion — NVIDIA Kimodo

Loom runs the official Kimodo CLI as a separate background job. Text-to-motion
uses Kimodo-SOMA-RP-v1.1, exports BVH in standard T-pose, then imports that
BVH into Loom's animated skeletal preview. The text encoder runs on CPU to
leave VRAM for Kimodo's motion model; diffusion uses CUDA when PyTorch detects
a compatible GPU.

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

The first real generation downloads the Kimodo checkpoint and LLM2Vec encoder.
The gated Llama repository is large (the complete Hub snapshot is about 32 GB);
allow network time and disk space. Downloads are cached by Hugging Face outside
the repository in ~/.cache/huggingface.

## Current MVP boundary

Generated BVH is automatically imported and the motion is visible as animated
joint/bone geometry in Loom's viewport and timeline. The current renderer does
not skin/deform the WeaverMascott FBX mesh or retarget SOMA's 77-joint rig onto
the mascot's 34-bone rig. The UI reports this explicitly; it does not claim
that the FBX character is already being animated.
