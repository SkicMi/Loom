//Puni sliku stope iz dubine koju je depth prepass upravo napisao.
//
//Jedan thread po tekselu stope, a teksel pokriva blok piksela (16x16 na Turingu). Iz tog
//bloka se uzima NAJBLIZA dubina, ne prosjecna: ako u bloku ima ista blizu, blok ostaje
//ostar. Prosjek bi zamutio rub gdje se blizi objekt dodiruje s dalekom pozadinom, a to je
//tocno mjesto gdje se pogruboljenje najvise vidi.

#include "include/Depth.slang"

struct Push{
    uint2 rateExtent;      //koliko teksela slika stope ima
    uint2 texelSize;       //koliko piksela pokriva jedan teksel
    float2 depthSize;      //velicina dubinske slike u pikselima
    float nearPlane;
    float farPlane;
    float quarterDistance;    //dalje od ovoga: 2x2
    float sixteenthDistance;  //dalje od ovoga: 4x4
}

[[vk::push_constant]] ConstantBuffer<Push> push;

[[vk::binding(0,0)]] Sampler2D depthMap;
[[vk::image_format("r8ui")]]
[[vk::binding(1,0)]] RWTexture2D<uint> rateMap;

//Formula je u include/Depth.slang, jer je treba i odprojekcija. Ovdje ostaje samo ime koje
//zna za nas push - pozivi ispod se nisu promijenili, pa se ni slika ne smije promijeniti
float viewDistance(float ndcZ){
    return linearizeDepth(ndcZ, push.nearPlane, push.farPlane);
}

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    if(id.x >= push.rateExtent.x || id.y >= push.rateExtent.y) return;

    float nearest = push.farPlane;

    //Cetiri po cetiri uzorka po tekselu. Citati svih 256 piksela bloka bilo bi tocnije i
    //nitko ne bi vidio razliku - stopa ima tri vrijednosti, ne tristo
    for(uint sampleY = 0; sampleY < 4; ++sampleY){
        for(uint sampleX = 0; sampleX < 4; ++sampleX){
            float2 inside = (float2(sampleX, sampleY) + 0.5) / 4.0;
            float2 pixel = (float2(id.xy) + inside) * float2(push.texelSize);
            float2 uv = pixel / push.depthSize;

            float depth = depthMap.SampleLevel(uv, 0.0).r;
            nearest = min(nearest, viewDistance(depth));
        }
    }

    //Vulkanovo pakiranje: log2 sirine u gornja dva bita, log2 visine u donja
    uint rate = 0;                                          //1x1
    if(nearest > push.sixteenthDistance) rate = 10;         //4x4
    else if(nearest > push.quarterDistance) rate = 5;       //2x2

    rateMap[id.xy] = rate;
}
