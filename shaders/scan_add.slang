// Prefiksni zbroj, prolaz 3 od 3: svakom bloku se doda sto ide prije njega.
//
// Prolaz 1 je unutar bloka vec dao ispravan zbroj, prolaz 2 je rekao gdje blok pocinje.
// Ovdje se to samo zbroji - i zato je ovo jedini prolaz bez ijedne barijere.
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

    //Ukupan zbroj: pomak zadnjeg bloka plus ono sto zadnji blok nosi. Prolaz 1 je taj zbroj
    //vec spremio u total[0] prije nego ga je prolaz 2 prepisao pomakom
    if(groupId.x == params.blockCount - 1 && threadId.x == 0){
        total[0] += offset;
    }
}
