// Radix sort, prolaz 1 od 3: koliko kojih znamenaka ima u kojem bloku.
//
// Blok je 1024 elementa: 128 dretvi po 8 komada, i svaka dretva drzi SUSJEDNI komad. To nije
// stvar ukusa - poredak unutar bloka mora ostati izvorni da bi sort bio stabilan, a stabilan
// mora biti jer se dubina i pločica sortiraju u dva odvojena prolaza (vidi RadixSort.h).
//
// Izlaz je [znamenka][blok], tim redom: sljedeci prolaz to zbraja u pomake, a pomak za
// znamenku d mora obuhvatiti SVE blokove manjih znamenaka prije nego dodje do svog.
struct Params{
    uint count;       //koliko elemenata stvarno ima
    uint shift;       //koja cetvorka bitova se gleda: 0, 4, 8, ... 28
    uint blockCount;  //koliko blokova, jer izlaz je [znamenka][blok]
    uint padding0;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> keys;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> blockHistogram;

static const uint THREADS = 128;
static const uint PER_THREAD = 8;
static const uint DIGITS = 16;

groupshared uint counts[DIGITS * THREADS];

//ODBACENA IDEJA, i neka stoji zapisana da se ne pokusa drugi put: kljucevi su se prepisivali u
//dijeljenu memoriju "susjednim" citanjem (dretva t uzima t, t+128, t+256...) da bi susjedne
//dretve citale susjedne adrese. Izmjereno na 741883 elementa: 26.01 ms prije, 34.12 ms poslije.
//Premisa je bila kriva - dretva koja cita osam susjednih uinta cita tocno jednu cache liniju i
//iskoristi je cijelu, a grupa i dalje pokriva susjedan komad memorije. Nije se imalo sto dobiti,
//a dijeljena memorija i barijera se placaju.

[shader("compute")]
[numthreads(128,1,1)]
void computeMain(uint3 threadId : SV_GroupThreadID, uint3 groupId : SV_GroupID){
    const uint thread = threadId.x;
    const uint block = groupId.x;

    for(uint d = 0; d < DIGITS; ++d){
        counts[d * THREADS + thread] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint blockStart = block * THREADS * PER_THREAD;
    for(uint e = 0; e < PER_THREAD; ++e){
        const uint index = blockStart + thread * PER_THREAD + e;
        if(index < params.count){
            const uint digit = (keys[index] >> params.shift) & (DIGITS - 1);
            counts[digit * THREADS + thread] += 1;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    //Jedna dretva po znamenki zbroji svoj red. Ovo je 16 dretvi koje rade dok 112 ceka, i to
    //je u redu: sve iznad ovoga je vec procitalo cijeli blok iz memorije
    if(thread < DIGITS){
        uint total = 0;
        for(uint t = 0; t < THREADS; ++t){
            total += counts[thread * THREADS + t];
        }
        blockHistogram[thread * params.blockCount + block] = total;
    }
}
