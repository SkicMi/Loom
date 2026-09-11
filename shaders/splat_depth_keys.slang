// Kljuc dubine po SPLATU, i poredak koji ce ga pratiti kroz sort.
//
// Sortira se 742 tisuce splatova umjesto pet milijuna parova. Brojanje i sirenje onda idu tim
// poretkom, pa parovi nastaju vec poredani sprijeda natrag - i ostaje samo sort po pločici, koji
// je stabilan i taj poredak ne dira. Slika je ista kao kad su se sortirali parovi: jednake
// dubine i dalje ostaju poredane po indeksu splata, jer je i ovaj sort stabilan.
//
// Splat koji se ne crta (polumjer nula) dobiva kljuc kao i svaki drugi. Gdje zavrsi svejedno je:
// brojanje mu da nula pločica, i nijedan par ne nastane.

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

//Isti pretvarac koji radi RadixSort::keyFromFloat na procesoru: pozitivnima se okrene bit
//predznaka, negativnima svi. Dubina je pozitivna, ali funkcija koja radi za pola ulaza je mina
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
