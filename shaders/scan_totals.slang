// Prefiksni zbroj, prolaz 2 od 3: zbroj preko zbrojeva blokova.
//
// Jedna grupa od 256 dretvi kroz polje blockCount brojeva, u komadima. Za scenu od 741883
// splatova to je 725 brojeva - jedan komad i nesto. Isti oblik kao radix_scan.
struct Params{
    uint count;
    uint blockCount;
    uint padding0;
    uint padding1;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> blockTotals;

static const uint THREADS = 256;

groupshared uint temp[THREADS];
groupshared uint carry;

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_GroupThreadID){
    const uint thread = threadId.x;

    if(thread == 0){
        carry = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for(uint base = 0; base < params.blockCount; base += THREADS){
        const uint index = base + thread;
        const uint value = (index < params.blockCount) ? blockTotals[index] : 0;

        temp[thread] = value;
        GroupMemoryBarrierWithGroupSync();

        for(uint offset = 1; offset < THREADS; offset <<= 1){
            const uint addend = (thread >= offset) ? temp[thread - offset] : 0;
            GroupMemoryBarrierWithGroupSync();
            temp[thread] += addend;
            GroupMemoryBarrierWithGroupSync();
        }

        const uint inclusive = temp[thread];
        if(index < params.blockCount){
            blockTotals[index] = carry + inclusive - value;
        }
        GroupMemoryBarrierWithGroupSync();

        if(thread == THREADS - 1){
            carry += inclusive;
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
