// Binning, prolaz 2: jedan par (pločica, splat) po svakoj pločici koju splat zahvaca.
//
// Ovdje se jedan splat pretvara u onoliko zapisa koliko pločica dira. Pomak je vec izracunat -
// prefiksni zbroj je rekao gdje ciji dio pocinje - pa svaka dretva pise samo u svoj komad i
// nijedna ne treba atomik.
//
// SPLATOVI SE OBILAZE PO DUBINI (order), pa parovi nastaju vec poredani sprijeda natrag. Sort po
// pločici je stabilan i taj poredak ne dira - zato sort po dubini ide nad splatovima, a ne nad
// sedam puta brojnijim parovima.
//
// Vrijednost uz kljuc pločice je SAM INDEKS SPLATA. Rasterizator ga cita izravno, bez skoka kroz
// jos jedno polje - a taj skok placa svaki splat svakog piksela.
#include "include/SplatTiles.slang"

struct Params{
    uint2 grid;
    uint tileSize;
    uint splatCount;
    uint maxPairs;    //iza ove granice nema mjesta - vidi splat_pair_sizes.slang
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<float4> splats;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> offsets;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> tileKeys;
[[vk::binding(3,0)]] RWStructuredBuffer<uint> pairSplats;
[[vk::binding(4,0)]] RWStructuredBuffer<uint> order;   //indeksi splatova, poredani po dubini

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint index = threadId.x;
    if(index >= params.splatCount){
        return;
    }

    const uint splatIndex = order[index];
    const SplatData splat = readSplat(splats, splatIndex);
    const float radius = splat.conicOpacityDepth.w;
    if(radius <= 0.0){
        return;
    }

    const int4 bounds = tileBounds(splat.centerConic.xy, radius, params.grid, params.tileSize);

    uint write = offsets[index];

    for(int ty = bounds.y; ty < bounds.w; ++ty){
        for(int tx = bounds.x; tx < bounds.z; ++tx){
            //Iza granice nema mjesta. Broj parova se ogranici na kartici, a slika kojoj dio
            //parova nedostaje manja je steta od pisanja po tudjoj memoriji
            if(write >= params.maxPairs){
                return;
            }
            tileKeys[write] = uint(ty) * params.grid.x + uint(tx);
            pairSplats[write] = splatIndex;
            ++write;
        }
    }
}
