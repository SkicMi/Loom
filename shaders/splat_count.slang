// Binning, prolaz 1: koliko pločica zahvaca svaki splat - REDOM PO DUBINI.
//
// Rezultat ide u polje koje prefiksni zbroj pretvori u pomake. Isti okvir racuna i prolaz
// sirenja, iz istog includea - vidi SplatTiles.slang zasto to nije prepisano dvaput.
//
// Mjesto k u polju nije splat k nego k-ti splat po dubini (order[k]). Zato pomaci, a s njima i
// parovi, nastaju vec poredani sprijeda natrag - vidi splat_depth_keys.slang.
#include "include/SplatTiles.slang"

struct Params{
    uint2 grid;
    uint tileSize;
    uint splatCount;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<float4> splats;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> counts;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> order;   //indeksi splatova, poredani po dubini

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint index = threadId.x;
    if(index >= params.splatCount){
        return;
    }

    const SplatData splat = readSplat(splats, order[index]);
    const float radius = splat.conicOpacityDepth.w;

    //Polumjer nula znaci splat koji se ne crta: prepare() ga je odbacio, ili je iza kamere
    if(radius <= 0.0){
        counts[index] = 0;
        return;
    }

    //Okvir je kvadrat oko kruga; broje se samo pločice u kojima splat stvarno moze nesto nacrtati
    const int4 bounds = tileBounds(splat.centerConic.xy, radius, params.grid, params.tileSize);
    uint touched = 0;
    for(int ty = bounds.y; ty < bounds.w; ++ty){
        for(int tx = bounds.x; tx < bounds.z; ++tx){
            if(splatTouchesTile(splat, tx, ty, params.tileSize)){
                ++touched;
            }
        }
    }
    counts[index] = touched;
}
