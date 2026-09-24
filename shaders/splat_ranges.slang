// Binning, pass 4: where each tile starts and ends in the sorted array.
//
// After the second sort, pairs are ordered by tile, then by depth within the tile. A boundary
// is found by comparing with the neighbor: where the key changes, one tile ends and another
// begins. The first and last elements are boundaries by definition.
//
// Tiles that no splat touches stay at zero, so their range is empty - which is why the array
// must be cleared before this pass.
[[vk::binding(0,0)]] RWStructuredBuffer<uint> sortedTiles;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> ranges;   //two uints per tile: start, end
[[vk::binding(2,0)]] RWStructuredBuffer<uint> pairSizes;   //[0] is the pair count written by the GPU - see splat_pair_sizes.slang

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint index = threadId.x;
    if(index >= pairSizes[0]){
        return;
    }

    const uint tile = sortedTiles[index];

    if(index == 0 || sortedTiles[index - 1] != tile){
        ranges[tile * 2 + 0] = index;
    }
    if(index + 1 == pairSizes[0] || sortedTiles[index + 1] != tile){
        ranges[tile * 2 + 1] = index + 1;
    }
}
