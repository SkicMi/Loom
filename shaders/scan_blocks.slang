// Prefiksni zbroj, prolaz 1 od 3: svaki blok zbroji sam sebe.
//
// Blok je 1024 broja: 256 dretvi po cetiri susjedna. Unutar bloka rezultat je vec ispravan
// ekskluzivni zbroj; sto ide PRIJE tog bloka doda prolaz 3, kad prolaz 2 to izracuna.
//
// Dvorazinski, a ne jedna dretva kroz cijelo polje - to je nauceno na radix scanu, gdje je
// jednodretveni zbroj ispao 18 od 39 ms.
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

groupshared uint partial[THREADS];

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_GroupThreadID, uint3 groupId : SV_GroupID){
    const uint thread = threadId.x;
    const uint blockStart = groupId.x * THREADS * PER_THREAD;

    //Cetiri susjedna broja po dretvi: prvo njihov zbroj, jer se dalje zbraja po dretvama
    uint own[PER_THREAD];
    uint sum = 0;
    for(uint e = 0; e < PER_THREAD; ++e){
        const uint index = blockStart + thread * PER_THREAD + e;
        own[e] = (index < params.count) ? values[index] : 0;
        sum += own[e];
    }

    partial[thread] = sum;
    GroupMemoryBarrierWithGroupSync();

    //Hillis-Steele preko 256 dretvi: dubina lanca je osam koraka, ne 256
    for(uint offset = 1; offset < THREADS; offset <<= 1){
        const uint addend = (thread >= offset) ? partial[thread - offset] : 0;
        GroupMemoryBarrierWithGroupSync();
        partial[thread] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    //Ukljucivi minus vlastiti je iskljucivi: odakle ova dretva krece
    uint running = partial[thread] - sum;

    for(uint e = 0; e < PER_THREAD; ++e){
        const uint index = blockStart + thread * PER_THREAD + e;
        if(index < params.count){
            values[index] = running;
        }
        running += own[e];
    }

    //Koliko cijeli blok nosi, za prolaz 2
    if(thread == THREADS - 1){
        blockTotals[groupId.x] = partial[thread];

        //I zbroj ZADNJEG bloka zasebno, jer ce ga prolaz 2 prepisati pomakom. Ukupan zbroj je
        //pomak zadnjeg bloka plus ovo, i to je jedini nacin da se dozna bez ponovnog citanja
        //polja koje je u meduvremenu postalo nesto drugo
        if(groupId.x == params.blockCount - 1){
            total[0] = partial[thread];
        }
    }
}
