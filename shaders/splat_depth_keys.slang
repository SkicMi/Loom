// Depth key per SPLAT, plus an order array that follows it through the sort.
//
// This sorts 742 thousand splats instead of five million pairs. Counting and expanding then
// follow that order, so pairs are born already sorted front to back - and only the per-tile
// sort remains, which is stable and leaves that order alone. The picture matches the old
// pair-based sort: equal depths still stay ordered by splat index, because this sort is stable
// too.
//
// A splat that is not drawn (zero radius) gets a key like any other. Where it ends up does not
// matter: counting gives it zero tiles, and no pair is ever created.

struct Params{
    uint splatCount;
    uint padding0;
    uint padding1;
    uint padding2;
}

#include "include/SplatTiles.slang"

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<float4> splats;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> keys;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> order;

//Same converter as RadixSort::keyFromFloat on the CPU: positives flip the sign bit, negatives
//flip all bits. Depth is positive, but a function that only works for half its inputs is a trap
uint keyFromFloat(float value){
    const uint bits = asuint(value);
    return (bits & 0x80000000u) != 0 ? ~bits : (bits | 0x80000000u);
}

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint index = threadId.x;
    if(index >= params.splatCount){
        return;
    }

    keys[index] = keyFromFloat(readSplat(splats, index).conicOpacityDepth.z);
    order[index] = index;
}
