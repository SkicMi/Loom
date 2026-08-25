# Loom tests

The suite covers both libraries in this repository - `Loom`, which draws, and `Spool`, which
reads and writes files - a PNG in, a PNG out, and an image sequence. The two do not depend on each other in either direction; a test is
allowed to link both, and that is the only place they meet.

Every test is one executable that returns 0 only if all of its checks hold. Nothing here
prints a number and leaves the judgement to a human.

```
cmake --build build --target LoomTests
cd build && ctest --output-on-failure
```

Two of them are not executables. `tier1_header_is_clean` and `tier1_leak_detector`
preprocess a translation unit and search the result, because a leaked include is never in
the file you are looking at - it is in something that file included, and the only honest
way to see it is to look at all of it at once. Measured today: `<Loom/Loom.h>` comes to
1622367 characters with **0** `vk::` and **0** `vulkan` in them, while the tier 2 control
has 29719 and 47986. The control exists because a detector that finds nothing looks
exactly like a detector that does not work.

What each one covers:

| test | claim |
|---|---|
| `test_v5a_fullscreen` | a fullscreen pass reproduces its source byte for byte, covers every pixel, and does not flip the image |
| `test_v5b_tint` | the post pass multiplies linear light, not stored bytes, and a material can be changed while frames are in flight |
| `test_v6a_buffer` | a dispatch fills a buffer with the right values and writes nothing past the count it was given |
| `test_v6b_image` | a dispatch writes an image, the graphics side reads what it wrote, and both storage-image guards fire |
| `test_v6c_blur` | compute reads one image and writes another, through a sampler and as a storage image, and a rendered scene survives the round trip |
| `test_v6d_histogram` | atomics lose no increment, a workgroup reduction matches the CPU, and the window can be read back |
| `test_api_contracts` | a pipeline is not tied to the swapchain format, a material carries any payload, a push constant range is the pipeline's business, and a material survives its source being resized |
| `test_v7_shadowmap` | a pass can render depth with no colour attachment, that depth survives the pass and matches the CPU's own projection to the last bit, and a pass driven by a light writes what the light's matrices say it should |
| `test_v7c_shadow` | a shadow map is sampled, a shadowed surface keeps exactly its ambient term and loses the rest, depth bias removes the acne a tilted plane inflicts on itself, and too much of it detaches the shadow entirely |
| `test_v7e_pointshadow` | the shadow box fits itself to the camera's frustum, keeps its size through a full turn of the camera, lands on whole texels, and a point light casts through all six faces of a cube map |
| `test_shapes` | every primitive is one unit across, wound so its faces agree with their own normals, and one call draws it with a material built once and cached |
| `test_headless` | a whole frame runs with no window, no surface and no swapchain, and its picture is byte for byte the one the windowed path draws; the same frame number gives the same frame twice |
| `test_lifetime` | GLFW is initialised by the first window and terminated by the last, so destroying one window leaves the others alive; a headless Loom never touches GLFW at all, and a Loom built after a full shutdown draws the same picture |
| `test_spool_image` | a real PNG decodes to exactly the pixels that were written into it, three channels come back as four, alpha survives, and those bytes become a Loom texture without either library knowing the other exists |
| `test_spool_export` | pixels written as a PNG read back as exactly the same pixels, BGRA becomes RGBA, and five headless frames become five numbered files that hold what was drawn |
| `test_tier1_preset` | a program written against `<Loom/Loom.h>` draws the same image, byte for byte, as the tier 2 program that does the same thing by hand; `Loom::Transform` produces the same matrix as the glm written out longhand |
| `test_vma_memory` | 500 buffers cost no new device memory blocks, each MemoryUsage lands in the kind of memory it asked for, host memory stays mapped, and a CPU write survives the trip back |

Two things worth knowing before reading a failure:

- **They need a GPU. Most of them still need a display**, because they open a window - but
  that is now a property of each test rather than of the library. `LoomConfig::headless`
  builds Loom with no window, no surface and no swapchain, and `test_headless` proves the
  picture that comes out is byte identical to the windowed one. A test written against the
  headless path runs with no display at all, on a software rasterizer:

  ```
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ./build/some_headless_test
  ```

  Measured on this machine: the same cube covers 2704 pixels on the GTX 1650 and 2704 on
  llvmpipe, both with `DISPLAY` unset. Coverage is geometry and agrees; **shaded bytes are
  not promised to agree across devices**, so the exact pixel tests in this suite stay on a
  reference device until each one has been checked.
- **`validation` is a check like any other.** The tests assert that the validation layers
  reported zero warnings or errors, counted inside the library rather than grepped out of
  stderr. The first run of this suite failed on a warning that had been filtered by hand
  for a whole day.

One thing the hardware here refuses to do: **two VkInstances alive at once, with the
validation layer loaded, crash this NVIDIA driver when one of them is destroyed** - inside
the driver, on a null function pointer, after the loader reports unloading the layer that
the surviving instance is still using. The same code passes on llvmpipe. `test_lifetime`
therefore proves the window lifetime at the `Window` level, which is where the bug it
guards actually was, and does not claim two whole Looms can coexist on every driver.

Tolerances are not decoration. Where a test allows a delta of 1 it says so, because this
hardware does not round float to unorm exactly at .5 - `shaders/tests/unormprobe.slang`
measures that, and `test_v6c_blur` asserts that every disagreement sits on that boundary
and nowhere else.
