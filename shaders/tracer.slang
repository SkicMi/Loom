// LOOMTRACER NA KARTICI - obilazak vlastitog BVH-a (radi na svakoj Vulkan kartici i na llvmpipeu).
// Sav kod je u include/TracerCore.slang; tracer_rq.slang je isto s hardverskim zrakama.
#include "include/TracerCore.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID){ tracePixel(id); }
