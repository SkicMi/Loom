// Prefix sum, pass 3 of 3: add what comes before a block to every block.
//
// Pass 1 already produced the correct within-block sum, pass 2 said where each block starts.
// Here the two are just added up - which is why this is the only pass without a barrier.
struct Params{
    uint count;
    uint blockCount;
    uint padding0;
    uint padding1;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> values;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> blockTotals;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> total;

static const uint THREADS = 256;
static const uint PER_THREAD = 4;

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_GroupThreadID, uint3 groupId : SV_GroupID){
    const uint blockStart = groupId.x * THREADS * PER_THREAD;
    const uint offset = blockTotals[groupId.x];

    for(uint e = 0; e < PER_THREAD; ++e){
        const uint index = blockStart + threadId.x * PER_THREAD + e;
        if(index < params.count){
            values[index] += offset;
        }
    }

    //Total sum: the last block's offset plus what the last block carries. Pass 1 left that sum
    //in total[0] before pass 2 overwrote it with the offset
    if(groupId.x == params.blockCount - 1 && threadId.x == 0){
        total[0] += offset;
    }
}
