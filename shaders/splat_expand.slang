// Binning, prolaz 2: jedan par (pločica, splat) po svakoj pločici koju splat zahvaca.
//
// Ovdje se jedan splat pretvara u onoliko zapisa koliko pločica dira. Pomak je vec izracunat -
// prefiksni zbroj je rekao gdje ciji dio pocinje - pa svaka dretva pise samo u svoj komad i
// nijedna ne treba atomik.
//
// Kljucevi su dva odvojena polja, a ne jedan 64-bitni: sort je stabilan, pa se prvo sortira po
// dubini a onda po pločici, i drugi sort ne pokvari prvi. Vidi RadixSort.h.
#include "include/SplatTiles.slang"

struct Params{
    uint2 grid;
    uint tileSize;
    uint splatCount;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<float4> splats;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> offsets;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> tileKeys;
[[vk::binding(3,0)]] RWStructuredBuffer<uint> depthKeys;
[[vk::binding(4,0)]] RWStructuredBuffer<uint> splatIndices;
[[vk::binding(5,0)]] RWStructuredBuffer<uint> sortValues;

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

    const SplatData splat = readSplat(splats, index);
    const float radius = splat.conicOpacityDepth.w;
    if(radius <= 0.0){
        return;
    }

    const int4 bounds = tileBounds(splat.centerConic.xy, radius, params.grid, params.tileSize);
    const uint depthKey = keyFromFloat(splat.conicOpacityDepth.z);

    uint write = offsets[index];

    for(int ty = bounds.y; ty < bounds.w; ++ty){
        for(int tx = bounds.x; tx < bounds.z; ++tx){
            tileKeys[write] = uint(ty) * params.grid.x + uint(tx);
            depthKeys[write] = depthKey;
            splatIndices[write] = index;
            sortValues[write] = write;   //sort premjesta ovo, pa se preko njega nadje par
            ++write;
        }
    }
}
