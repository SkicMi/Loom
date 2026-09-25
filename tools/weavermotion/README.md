# WeaverMotion — NVIDIA Kimodo

Loom runs a small tracked adapter around NVIDIA's official Kimodo CLI as a
separate background job. It exposes all seven installed model variants,
multi-prompt timing, sample count, diffusion steps, CFG, saved JSON constraints,
example export, Kimodo postprocess, initial heading, and the API's root correction
margin. The CLI adapter places the LLM2Vec text encoder on CPU by default to reserve
VRAM for Kimodo; diffusion uses CUDA when PyTorch detects a compatible GPU. Set
TEXT_ENCODER_DEVICE=cuda explicitly only when the GPU has enough free memory.
Kimodo prints missing/unexpected LLM2Vec layer names while loading these adapter
checkpoints; its official quick start marks these messages as expected:
https://github.com/nv-tlabs/kimodo/blob/main/docs/source/getting_started/quick_start.md

## Quality-first generation

Loom defaults to Kimodo-SOMA-RP-v1.1, 200 denoising steps, four candidate samples, separated guidance at 2.0/2.0, and Kimodo postprocess for contacts and constraint enforcement. Use **Use Best Quality Defaults** to restore that profile after changing settings. NVIDIA recommends focused, medium-detail prompts beginning with “A person…” and says 100–200 denoising steps trade speed for quality; keep each prompt to one or two behaviors, then compare the generated candidates in Recent Motions.

The text encoder stays on CPU so the motion model can use CUDA without competing for VRAM. NVIDIA notes that FP32 text embeddings can slightly improve v1.1 accuracy but require extra memory; Loom leaves this off for GPUs with limited VRAM. See the [Kimodo best practices](https://research.nvidia.com/labs/sil/projects/kimodo/docs/key_concepts/limitations.html), [generation parameters](https://research.nvidia.com/labs/sil/projects/kimodo/docs/user_guide/configuration.html), and [CLI guide](https://research.nvidia.com/labs/sil/projects/kimodo/docs/user_guide/cli.html).

## MotionBricks path recording

The Motion panel starts with a curved, editable walk path. Choose a locomotion preset, shape its spline in the viewport, then use **Start Live + Record** and **Stop & Keep Animation**. MotionBricks runs in its own Python process, streams retargeted poses into the character Animator, and writes a complete BVH take to `WeaverMotion/` when stopped. The final take is re-imported so the saved clip contains the full 30 fps sample stream even if the editor rendered fewer preview frames.

Presets use MotionBricks G1 live styles for idle, walk, hands-and-knees crawl, and crouch. Run and jump use Kimodo text generation. Crawl starts with Loom foot-contact IK disabled to preserve hand/elbow contact. For Kimodo generation, postprocess remains enabled to enforce authored constraints and improve contacts; MotionBricks live recording does not use Kimodo postprocess. Jump starts as an in-place action, and the other moving presets start with an editable route. The released G1 motion skeleton has no finger joints, so detailed finger gestures remain a Kimodo rig-retargeting feature rather than a MotionBricks live-control feature.

## Motion Recipe

The Motion panel reads common movement words in the active prompt and selects the matching Idle, Walk, Run, Jump, Crawl, or Crouch recipe. The recipe preview shows the generator, route mode, and MotionBricks style before generation or recording. Crawl prompts automatically turn off Loom rig foot-contact IK while Kimodo postprocess stays enabled. The two controls can be changed independently under Manual contact rules. Phrases such as "no IK" turn off rig foot-contact IK. Set Contact rules to **Manual** to override those defaults. Multi-action prompts expose their transition blend in the recipe controls. The recipe and effective settings are saved in the BVH sidecar for recent-motion history.

## Path + Pose Keys

In Motion, switch **Workflow** to **Path + Pose Keys**. Edit the root route in the viewport or its timeline lane, place the playhead at a time, then add a pose key. Select a bone on the cyan skeleton overlay or by body group, adjust its X/Y/Z rotations, and use mirroring or the pose shortcuts when useful. Repeat at other frames and click **Generate Path + Poses**. Loom writes one Kimodo `constraints.json` containing the root trajectory and all sparse full-body poses, runs SOMA RP v1.1, then imports the resulting BVH into the selected character's Animator. The authored constraint file and pose-key count are kept beside the generated motion.

The directed editor uses Kimodo's 30-joint SOMA constraint skeleton, with the installed model's neutral offsets and joint order. A pose is a target at its marked frame; the generated transition between targets remains stochastic. Keep pose keys sparse (at most 20), and review the generated candidates for contacts and clipping. The editor exposes independent Loom rig foot IK and Kimodo postprocess controls. Turning off Kimodo postprocess also turns off its constraint enforcement.

G1 is available as a separate Kimodo model and as the MotionBricks live skeleton, but Kimodo G1 exports MuJoCo-oriented NPZ/CSV instead of the SOMA BVH that Loom currently imports into its Animator. Its 34-joint robot skeleton is useful for robotics interchange; it is not a measured quality upgrade for a human mesh. An optional G1 base requires a tested G1-to-humanoid import path and a same-prompt quality comparison before making it the default.

## Motion quality comparison

In the Motion panel, expand **Quality Compare**, choose a saved take, and compare the same BVH as **Source BVH**, **Rig no IK**, and **Rig with IK**. The rig options create separate Animator clips; the source preview shows Kimodo's unretargeted skeleton beside the selected character. When a native NPZ exists, **Copy native NPZ path** provides its location. Run `tools/weavermotion/.venv-clean/bin/kimodo_demo`, open its local web UI, and load the NPZ under Load/Save > Motion. Compare the NPZ first, then the source BVH, then both rig clips to locate quality loss.

Loom now retargets joint rotations relative to the BVH T-pose rest instead of treating the first animated frame as neutral. A path-speed warning compares the sampled spline's peak speed against a conservative gait limit inferred from the prompt. Adjust waypoint positions or timing when it warns; **Allow fast path anyway** keeps deliberately stylized or unusual routes possible. The threshold is a UX guard, not a physical validation of the generated motion.

Kimodo postprocess includes both foot-contact cleanup and constraint optimization. Loom rig foot IK is a later, separate step. Crawl defaults to Kimodo postprocess on and Loom rig IK off; switch to Manual to compare either independently.

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

Generated SOMA BVH is automatically imported. With a rigged character selected,
Loom maps source joints by humanoid names or a verified UniRig-52 hierarchy,
fits scale from the imported mesh bounds, and keys the rig joints and root motion.
The GLTF renderer applies up to eight skin influences per vertex, so the mesh
follows the animation during timeline playback. A rigged character receives an
Animator component; generated clips can be previewed separately, and selected
bone transforms can be keyed at the current frame. Each clip has its own Loop
and In place options. One-shot clips hold their final pose; an in-place loop
repeats the pose while keeping the character origin stationary. Root-motion
clips move the character object together with its skinned mesh. Playback Check
reports mapped bones, valid keys, live mesh deformation, pose orientation, and
measured origin travel. Unrigged GLB/GLTF assets can be sent through Auto Rig
from the media browser or imported as a humanoid.

The Along a Path editor lets you add several timed points on the timeline,
insert a point after the selected one, and drag them in the viewport. Smooth
path mode previews a spline through those points and samples the same curve
into Kimodo root constraints. Additional pose and end-effector
constraints can be loaded from Kimodo JSON. Without a target rig, a BVH remains
available as a skeleton preview. SOMA BVH is the only Kimodo motion format Loom
currently imports; G1 CSV and SMPL-X NPZ outputs remain in the output folder.

## MotionBricks locomotion

The motion panel can also generate a separate MotionBricks G1 locomotion clip
using NVIDIA's released G1 checkpoint. Run `bash tools/motionbricks/setup.sh`
first. The bridge exports a Y-up BVH with humanoid control bones, then Loom's
existing Animator retargets it onto a rigged character, including UE5 Manny.
A smooth Loom path guides direction and speed. MotionBricks G1 has no finger
joints, so use Kimodo clips for gestures and detailed hand animation. These
are separate clips; this integration does not blend both neural models within
a single generated clip.
