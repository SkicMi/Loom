//Dubina natrag u tocke: svaki piksel postaje tocka u prostoru pred kamerom.
//
//Ovo je prvi korak ubacivanja svjetla u snimku. Depth buffer zna koliko je sto daleko, ali
//ne i GDJE je - a svjetlo koje pada po inverznom kvadratu treba tocku, ne udaljenost. Piksel
//i njegova dubina, kroz intrinsike kamere, daju tu tocku jednoznacno.
//
//Namjerno ne zna odakle je dubina dosla. Je li je napisao Loomov depth pass ili model za
//procjenu dubine nad tudom snimkom, racun je isti - i zato ce isti shader posluziti obome.

#include "include/Depth.slang"

//Odakle je dubina i sto njen broj znaci. Cjevovod je isti u oba slucaja - i jedno i drugo
//je jedan kanal iz semplirljive slike - pa se razlikuje samo racun koji broj pretvori u
//udaljenost
static const uint DEPTH_FROM_BUFFER = 0;   //Loomov depth buffer: nelinearan, kroz near/far
static const uint DEPTH_DISPARITY   = 1;   //model za relativnu dubinu: 1 blizu, 0 daleko
static const uint DEPTH_METRIC      = 2;   //metri izravno

struct Push{
    uint2 size;          //velicina slike, u pikselima; dubina i pozicije su iste velicine
    float fx;            //zariste u pikselima
    float fy;            //negativan kad projekcija okrece Y, i tada se okret sam ponisti
    float cx;            //glavna tocka
    float cy;
    float nearPlane;
    float farPlane;

    uint encoding;
    float nearDistance;  //DEPTH_DISPARITY: sto znaci vrijednost 1
    float farDistance;   //DEPTH_DISPARITY: sto znaci vrijednost 0
    float metricScale;   //DEPTH_METRIC: metri = vrijednost * ovo
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

    float stored = depthMap.SampleLevel(uv, 0.0).r;

    float viewDepth;

    if(push.encoding == DEPTH_FROM_BUFFER){
        //Pozadina: nista nije nacrtano, dubina je ostala na vrijednosti brisanja. Tu nema
        //plohe, pa nema ni tocke - i svjetlo je ne smije obasjati
        if(stored >= 1.0){
            positionMap[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
            return;
        }
        viewDepth = linearizeDepth(stored, push.nearPlane, push.farPlane);
    }
    else if(push.encoding == DEPTH_DISPARITY){
        //Model za relativnu dubinu ne daje udaljenost nego nesto proporcionalno njenoj
        //RECIPROCNOJ vrijednosti - blize je vece. Zato se ne interpolira udaljenost izmedu
        //blizu i daleko, nego njena reciprocna vrijednost; inace scena ispadne rastegnuta
        //u daljini a zbijena blizu.
        //
        //    1/Z = (1/near - 1/far) * d + 1/far
        float nearD = max(push.nearDistance, 1e-4);
        float farD = max(push.farDistance, nearD * 1.0001);

        float reciprocal = (1.0 / nearD - 1.0 / farD) * saturate(stored) + 1.0 / farD;
        viewDepth = 1.0 / max(reciprocal, 1e-6);
    }
    else{
        viewDepth = stored * push.metricScale;

        //Nula znaci "model ovdje nije imao sto reci" - najcesce nebo. Tocka na udaljenosti
        //nula je tocka u oku kamere, a svjetlo bi je obasjalo beskonacno jako
        if(viewDepth <= 0.0){
            positionMap[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
            return;
        }
    }

    //px = fx * x / dubina + cx, rijeseno po x. Isto za y. z je udaljenost, s predznakom
    //jer kamera gleda niz -Z
    float3 position;
    position.x = (pixel.x - push.cx) * viewDepth / push.fx;
    position.y = (pixel.y - push.cy) * viewDepth / push.fy;
    position.z = -viewDepth;

    positionMap[id.xy] = float4(position, 1.0);
}
