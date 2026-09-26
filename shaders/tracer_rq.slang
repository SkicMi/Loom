// LOOMTRACER NA KARTICI - hardverske zrake (VK_KHR_ray_query): isti kod kao tracer.slang, samo
// traverse() i zraka sjene idu kroz akceleracijsku strukturu drajvera (RTX, RDNA2+, Arc).
#define LOOM_RAY_QUERY 1
#include "include/TracerCore.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    if((push.flags & 2u) != 0u) restirReuse(id);
    else tracePixel(id);
}
