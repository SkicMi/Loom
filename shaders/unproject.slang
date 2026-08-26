//Dubina natrag u tocke: svaki piksel postaje tocka u prostoru pred kamerom.
//
//Ovo je prvi korak ubacivanja svjetla u snimku. Depth buffer zna koliko je sto daleko, ali
//ne i GDJE je - a svjetlo koje pada po inverznom kvadratu treba tocku, ne udaljenost. Piksel
//i njegova dubina, kroz intrinsike kamere, daju tu tocku jednoznacno.
//
//Namjerno ne zna odakle je dubina dosla. Je li je napisao Loomov depth pass ili model za
//procjenu dubine nad tudom snimkom, racun je isti - i zato ce isti shader posluziti obome.

#include "include/Depth.slang"

struct Push{
    uint2 size;          //velicina slike, u pikselima; dubina i pozicije su iste velicine
    float fx;            //zariste u pikselima
    float fy;            //negativan kad projekcija okrece Y, i tada se okret sam ponisti
    float cx;            //glavna tocka
    float cy;
    float nearPlane;
    float farPlane;
}

[[vk::push_constant]] ConstantBuffer<Push> push;

[[vk::binding(0,0)]] Sampler2D depthMap;

[[vk::image_format("rgba32f")]]
[[vk::binding(1,0)]] RWTexture2D<float4> positionMap;

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    if(id.x >= push.size.x || id.y >= push.size.y) return;

    //Sredina piksela, ne njegov kut. Pola piksela promasaja na rubu kadra je vise nego sto
    //cijela ova racunica smije pogrijesiti
    float2 pixel = float2(id.xy) + 0.5;
    float2 uv = pixel / float2(push.size);

    float ndcZ = depthMap.SampleLevel(uv, 0.0).r;

    //Pozadina: nista nije nacrtano, dubina je ostala na vrijednosti brisanja. Tu nema plohe,
    //pa nema ni tocke - i svjetlo je ne smije obasjati. w = 0 znaci "ovdje nicega nema"
    if(ndcZ >= 1.0){
        positionMap[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float viewDepth = linearizeDepth(ndcZ, push.nearPlane, push.farPlane);

    //px = fx * x / dubina + cx, rijeseno po x. Isto za y. z je udaljenost, s predznakom
    //jer kamera gleda niz -Z
    float3 position;
    position.x = (pixel.x - push.cx) * viewDepth / push.fx;
    position.y = (pixel.y - push.cy) * viewDepth / push.fy;
    position.z = -viewDepth;

    positionMap[id.xy] = float4(position, 1.0);
}
