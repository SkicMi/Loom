// Radix sort, prolaz 3 od 3: svaki element na svoje mjesto.
//
// Blok ponovno prebroji svoje znamenke - isto kao u prolazu 1, do zadnjeg bita - pa te brojeve
// pretvori u pomake PO DRETVAMA. Odrediste elementa je onda zbroj tri broja:
//
//   pomak bloka       gdje pocinje ta znamenka iz ovog bloka, iz prolaza 2
//   pomak dretve      koliko istih znamenaka imaju dretve prije ove, unutar bloka
//   koliko ih je ta   koliko ih je ova dretva vec zapisala
//   dretva vec pisala
//
// Ta tri zbroja su cijeli razlog zasto je sort STABILAN: unutar znamenke poredak je po bloku,
// pa po dretvi, pa po redu unutar dretve - a to je tocno izvorni poredak, jer dretva drzi
// susjedni komad. Stabilnost nije ukras: dubina i pločica se sortiraju odvojeno, i drugi sort
// smije prerasporediti prvi samo ako sam nista ne mijesa.
struct Params{
    uint count;
    uint shift;
    uint blockCount;
    uint countBase;   //DIRECT: broj je u ovoj konstanti; inace indeks u countSource
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> keysIn;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> valuesIn;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> keysOut;
[[vk::binding(3,0)]] RWStructuredBuffer<uint> valuesOut;
[[vk::binding(4,0)]] RWStructuredBuffer<uint> blockOffsets;
[[vk::binding(5,0)]] RWStructuredBuffer<uint> countSource;   //vidi RadixSort::sortIndirect

static const uint DIRECT = 0xFFFFFFFF;

//Broj elemenata i blokova: iz push konstante, ili iz buffera kad ga zna samo kartica. If a ne
//ternarni operator - shader smije izracunati obje grane, a DIRECT kao indeks cita izvan polja
uint elementCount(){
    if(params.countBase != DIRECT) return countSource[params.countBase];
    return params.count;
}
uint blockTotal(){
    if(params.countBase != DIRECT) return countSource[params.countBase + 1];
    return params.blockCount;
}

static const uint THREADS = 128;
static const uint PER_THREAD = 8;
static const uint DIGITS = 16;

groupshared uint counts[DIGITS * THREADS];

[shader("compute")]
[numthreads(128,1,1)]
void computeMain(uint3 threadId : SV_GroupThreadID, uint3 groupId : SV_GroupID){
    const uint thread = threadId.x;
    const uint count = elementCount();
    const uint blockCount = blockTotal();
    const uint block = groupId.x;

    for(uint d = 0; d < DIGITS; ++d){
        counts[d * THREADS + thread] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint blockStart = block * THREADS * PER_THREAD;

    for(uint e = 0; e < PER_THREAD; ++e){
        const uint index = blockStart + thread * PER_THREAD + e;
        if(index < count){
            const uint digit = (keysIn[index] >> params.shift) & (DIGITS - 1);
            counts[digit * THREADS + thread] += 1;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    //Broj pojava postaje pomak: za svaku znamenku, ekskluzivni zbroj preko dretvi. Na mjesto
    //istog polja, jer stari broj vise nikome ne treba
    if(thread < DIGITS){
        uint running = 0;
        for(uint t = 0; t < THREADS; ++t){
            const uint howMany = counts[thread * THREADS + t];
            counts[thread * THREADS + t] = running;
            running += howMany;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    //I sad redom kroz svoje elemente. Redom je bitno: to je ono sto cuva izvorni poredak
    uint written[DIGITS];
    for(uint d = 0; d < DIGITS; ++d){
        written[d] = 0;
    }

    for(uint e = 0; e < PER_THREAD; ++e){
        const uint index = blockStart + thread * PER_THREAD + e;
        if(index < count){
            const uint key = keysIn[index];
            const uint digit = (key >> params.shift) & (DIGITS - 1);

            const uint destination = blockOffsets[digit * blockCount + block]
                                   + counts[digit * THREADS + thread]
                                   + written[digit];

            keysOut[destination] = key;
            valuesOut[destination] = valuesIn[index];
            written[digit] += 1;
        }
    }
}
