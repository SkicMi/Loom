// Radix sort, prolaz 2 od 3: od broja pojava do pomaka.
//
// Ekskluzivni prefiksni zbroj preko cijele tablice [znamenka][blok], tim redom. Rezultat na
// mjestu [d][b] je "koliko elemenata ide ispred prvog elementa znamenke d iz bloka b" - a to
// je tocno mjesto na koje ce prolaz 3 pisati.
//
// PRVA VERZIJA JE BILA JEDNA DRETVA, i to je bila kriva procjena, pa neka stoji zapisano:
// tablica ima 16 * blockCount unosa, sto je za scenu od 741883 gaussiana 11600 unosa i 92800
// ovisnih citanja kroz osam prolaza. Racunao sam to kao "16 tisuca zbrajanja, zanemarivo".
// Nije zanemarivo - jedna dretva ceka memoriju na svakom koraku jer sljedeci zbroj ovisi o
// prethodnom. Izmjereno na Intel UHD, cijeli sort od 741883 elementa:
//
//   sa scanom u jednoj dretvi   39.24 ms
//   bez scana (smece, samo za   21.18 ms
//     mjerenje vremena)
//                               -> scan je bio 18 ms, gotovo pola posla
//
// Sad je jedna grupa od 256 dretvi koja ide kroz tablicu u komadima: unutar komada Hillis-
// Steele u dijeljenoj memoriji, a izmedju komada se nosi jedan broj. Citanja su susjedna,
// dubina lanca je log2(256) umjesto duljine tablice.
struct Params{
    uint count;
    uint shift;
    uint blockCount;
    uint countBase;   //DIRECT: broj je u ovoj konstanti; inace indeks u countSource
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> blockHistogram;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> countSource;   //vidi RadixSort::sortIndirect

static const uint DIRECT = 0xFFFFFFFF;

//Broj blokova: iz push konstante, ili iz buffera kad ga zna samo kartica. Vidi radix_histogram
uint blockTotal(){
    if(params.countBase != DIRECT) return countSource[params.countBase + 1];
    return params.blockCount;
}

static const uint THREADS = 256;
static const uint DIGITS = 16;

groupshared uint temp[THREADS];
groupshared uint carry;

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_GroupThreadID){
    const uint thread = threadId.x;
    const uint blockCount = blockTotal();
    const uint total = DIGITS * blockCount;

    if(thread == 0){
        carry = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    //Petlja je jednaka za sve dretve, pa su barijere unutar nje ispravne. Zadnji komad je
    //nepotpun i dopunjava se nulama, koje zbroju ne mijenjaju nista
    for(uint base = 0; base < total; base += THREADS){
        const uint index = base + thread;
        const uint value = (index < total) ? blockHistogram[index] : 0;

        temp[thread] = value;
        GroupMemoryBarrierWithGroupSync();

        for(uint offset = 1; offset < THREADS; offset <<= 1){
            const uint addend = (thread >= offset) ? temp[thread - offset] : 0;
            GroupMemoryBarrierWithGroupSync();
            temp[thread] += addend;
            GroupMemoryBarrierWithGroupSync();
        }

        //Ukljucivi zbroj minus vlastita vrijednost je iskljucivi - jeftinije nego drugi prolaz
        const uint inclusive = temp[thread];
        if(index < total){
            blockHistogram[index] = carry + inclusive - value;
        }
        GroupMemoryBarrierWithGroupSync();

        if(thread == THREADS - 1){
            carry += inclusive;
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
