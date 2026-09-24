// Descriptor matching on the GPU, pass 1 of 2: for every feature of frame A in a pair, the
// nearest descriptor in frame B among the features within 'radius' pixels, the second nearest
// that is NOT a neighbour of the nearest, and whether the nearest passes the distance ceiling and
// the ratio test. Same rules and the same tie order as Engine::matchSiftNear (Sift.cpp):
//
//   ties on distance go to the candidate met first when the CPU walks the 3x3 cells around the
//   feature (dy -1..1, then dx -1..1) and, inside a cell, by feature index
//
// One thread per (pair, feature of A); the pair is found by binary search over pair starts.
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
[[vk::binding(0,0)]] StructuredBuffer<uint> descriptors;    // 32 uints (128 bytes) per feature
[[vk::binding(1,0)]] StructuredBuffer<float2> positions;
[[vk::binding(2,0)]] StructuredBuffer<uint> valid;
[[vk::binding(3,0)]] StructuredBuffer<uint4> pairs;         // frame a, frame b, first thread, 0
[[vk::binding(4,0)]] StructuredBuffer<uint2> frames;        // first feature, count
[[vk::binding(5,0)]] RWStructuredBuffer<uint> bestTo;
[[vk::binding(6,0)]] RWStructuredBuffer<uint> bestSquared;
[[vk::binding(7,0)]] RWStructuredBuffer<uint> accepted;

static const uint NONE = 0xFFFFFFFFu;

uint distanceSquared(uint mine[32], uint other){
    uint sum = 0;
    const uint base = other * 32;
    for(uint k = 0; k < 32; ++k){
        const uint x = mine[k], y = descriptors[base + k];
        for(uint shift = 0; shift < 32; shift += 8){
            const int d = int((x >> shift) & 255u) - int((y >> shift) & 255u);
            sum += uint(d * d);
        }
    }
    return sum;
}

int2 cellOf(float2 p){ return int2(floor(p / params.cell)); }

[shader("compute")]
[numthreads(128,1,1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    const uint thread = id.x;
    if(thread >= params.totalThreads) return;

    uint low = 0, high = params.pairCount - 1;
    while(low < high){
        const uint middle = (low + high + 1) / 2;
        if(pairs[middle].z <= thread) low = middle; else high = middle - 1;
    }
    const uint4 pair = pairs[low];
    const uint i = thread - pair.z;
    const uint fromFeature = frames[pair.x].x + i;
    const uint2 target = frames[pair.y];

    bestTo[thread] = NONE;
    bestSquared[thread] = NONE;
    accepted[thread] = 0;
    if(valid[fromFeature] == 0) return;

    uint mine[32];
    for(uint k = 0; k < 32; ++k) mine[k] = descriptors[fromFeature * 32 + k];
    const float2 here = positions[fromFeature];
    const int2 hereCell = cellOf(here);

    uint best = NONE, bestOrder = NONE, chosen = 0;
    for(uint j = 0; j < target.y; ++j){
        const uint toFeature = target.x + j;
        if(valid[toFeature] == 0) continue;
        const float2 apart = positions[toFeature] - here;
        if(dot(apart, apart) > params.radiusSquared) continue;
        const uint d = distanceSquared(mine, toFeature);
        const int2 step = cellOf(positions[toFeature]) - hereCell;
        const uint order = uint((step.y + 1) * 3 + (step.x + 1));
        if(d < best || (d == best && order < bestOrder)){ best = d; bestOrder = order; chosen = j; }
    }
    if(best == NONE) return;

    uint second = NONE;
    const float2 chosenPixel = positions[target.x + chosen];
    for(uint j = 0; j < target.y; ++j){
        if(j == chosen) continue;
        const uint toFeature = target.x + j;
        if(valid[toFeature] == 0) continue;
        const float2 apart = positions[toFeature] - here;
        if(dot(apart, apart) > params.radiusSquared) continue;
        if(params.apartSquared > 0.0){
            const float2 fromBest = positions[toFeature] - chosenPixel;
            if(dot(fromBest, fromBest) < params.apartSquared) continue;
        }
        second = min(second, distanceSquared(mine, toFeature));
    }

    bestTo[thread] = chosen;
    bestSquared[thread] = best;
    bool ok = best <= params.maxSquared;
    if(ok && second != NONE && float(best) > params.ratioSquared * float(second)) ok = false;
    accepted[thread] = ok ? 1u : 0u;
}
