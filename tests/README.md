# Loom tests

Every test is one executable that returns 0 only if all of its checks hold. Nothing here
prints a number and leaves the judgement to a human.

```
cmake --build build --target LoomTests
cd build && ctest --output-on-failure
```

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
| `test_vma_memory` | 500 buffers cost no new device memory blocks, each MemoryUsage lands in the kind of memory it asked for, host memory stays mapped, and a CPU write survives the trip back |

Two things worth knowing before reading a failure:

- **They need a GPU and a display.** Each test opens a window; this is not a headless CI suite.
- **`validation` is a check like any other.** The tests assert that the validation layers
  reported zero warnings or errors, counted inside the library rather than grepped out of
  stderr. The first run of this suite failed on a warning that had been filtered by hand
  for a whole day.

Tolerances are not decoration. Where a test allows a delta of 1 it says so, because this
hardware does not round float to unorm exactly at .5 - `shaders/tests/unormprobe.slang`
measures that, and `test_v6c_blur` asserts that every disagreement sits on that boundary
and nowhere else.
