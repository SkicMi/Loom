// Descriptor matching on the GPU, pass 2 of 2: for every feature of frame B in a pair, which
// feature of frame A is nearest to it - for the mutual (A->B and B->A) check. Same tie order as
// Engine::matchSiftNear: distance, then the reversed cell step, then feature index of A.
//
// One thread per (pair, feature of B).
struct Params{
    uint pairCount;
    uint totalThreads;
    uint maxSquared;
    uint padding0;
    float radiusSquared;
    float cell;
    float ratioSquared;
    float apartSquared;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] StructuredBuffer<uint> descriptors;
[[vk::binding(1,0)]] StructuredBuffer<float2> positions;
[[vk::binding(2,0)]] StructuredBuffer<uint> valid;
[[vk::binding(3,0)]] StructuredBuffer<uint4> pairs;         // frame a, frame b, 0, first thread
[[vk::binding(4,0)]] StructuredBuffer<uint2> frames;
[[vk::binding(5,0)]] RWStructuredBuffer<uint> bestFrom;

static const uint NONE = 0xFFFFFFFFu;

int2 cellOf(float2 p){ return int2(floor(p / params.cell)); }

[shader("compute")]
[numthreads(128,1,1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    const uint thread = id.x;
    if(thread >= params.totalThreads) return;

    uint low = 0, high = params.pairCount - 1;
    while(low < high){
        const uint middle = (low + high + 1) / 2;
        if(pairs[middle].w <= thread) low = middle; else high = middle - 1;
    }
    const uint4 pair = pairs[low];
    const uint j = thread - pair.w;
    const uint toFeature = frames[pair.y].x + j;
    const uint2 source = frames[pair.x];

    bestFrom[thread] = NONE;
    if(valid[toFeature] == 0) return;

    uint mine[32];
    for(uint k = 0; k < 32; ++k) mine[k] = descriptors[toFeature * 32 + k];
    const float2 here = positions[toFeature];
    const int2 hereCell = cellOf(here);

    uint best = NONE, bestOrder = NONE, chosen = NONE;
    for(uint i = 0; i < source.y; ++i){
        const uint fromFeature = source.x + i;
        if(valid[fromFeature] == 0) continue;
        const float2 apart = here - positions[fromFeature];
        if(dot(apart, apart) > params.radiusSquared) continue;
        uint d = 0;
        const uint base = fromFeature * 32;
        for(uint k = 0; k < 32; ++k){
            const uint x = descriptors[base + k], y = mine[k];
            for(uint shift = 0; shift < 32; shift += 8){
                const int e = int((x >> shift) & 255u) - int((y >> shift) & 255u);
                d += uint(e * e);
            }
        }
        //Step from A's cell to B's cell, reversed as the CPU does: (-dy + 1) * 3 + (-dx + 1)
        const int2 step = hereCell - cellOf(positions[fromFeature]);
        const uint order = uint((-step.y + 1) * 3 + (-step.x + 1));
        if(d < best || (d == best && order < bestOrder)){ best = d; bestOrder = order; chosen = i; }
    }
    bestFrom[thread] = chosen;
}
