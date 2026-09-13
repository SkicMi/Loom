// Rasterizator: jedna radna grupa po pločici, jedna dretva po pikselu.
//
// Ovo je ono zbog cega postoji sve prije: pločica gleda SAMO splatove koji je diraju, i to vec
// poredane sprijeda natrag. Gruba sila iz G3a radi isto, samo sto svaki piksel prolazi kroz
// sve splatove na svijetu - a test ovoga je da daju ISTU sliku.
//
// VELICINA PLOČICE JE SPECIJALIZACIJSKA KONSTANTA, jer je ujedno i velicina radne grupe, a ona
// se u Vulkanu ne da poslati push constantom. Iz istog SPIR-V-a tako ispada 8x8, 16x16 ili
// 32x32, i G5 moze izmjeriti koja je najbrza.
#include "include/SplatTiles.slang"

[[vk::constant_id(0)]] const uint TILE = 16;

struct Params{
    uint2 imageSize;
    uint2 grid;
    uint pairCount;
    uint boxVisible;
    uint padding1;
    uint padding2;
    float4 focalPrincipal;   //fx, fy, cx, cy
    float4 boxCenter;        //u POGLEDU
    float4 boxHalfExtent;
    float4 boxAxisX;
    float4 boxAxisY;
    float4 boxColor;
}

// UBACENA KOCKA. Presjek zrake i kutije, u sustavu pogleda.
//
// Zasto ovdje a ne Loomom: splat se stapa sprijeda natrag i nema jednu dubinu po pikselu, pa bi
// spajanje s tudjim depth bufferom trazilo citanje natrag i pretvorbu jedinica. Ovako dubina
// izlazi u ISTIM jedinicama koje splat vec nosi.
//
// Zraka se namjerno NE normalizira. Smjer ima z = -1, pa je parametar t tocno jednak dubini
// (-viewZ) - istoj velicini koju splat nosi u conicOpacityDepth.z. Normalizirana zraka bi trazila
// jos jedno dijeljenje i jedno mjesto na kojem se konvencija moze razici
struct BoxHit{
    bool hit;
    float depth;
    float3 normal;   //u pogledu
};

BoxHit intersectBox(float3 direction, float4 center, float4 halfExtent, float3 axisX, float3 axisY){
    BoxHit result;
    result.hit = false;
    result.depth = 0.0;
    result.normal = float3(0.0, 0.0, 1.0);

    //Treca os je vektorski produkt prve dvije: kutija je kruta, pa je treca odredjena. Time push
    //constant ostaje unutar 128 bajtova koje Vulkan jamci
    const float3 axisZ = cross(axisX, axisY);

    //Zraka u sustav kutije: ishodiste je kamera, dakle nula
    const float3 toCenter = center.xyz;
    const float3 origin = float3(-dot(toCenter, axisX), -dot(toCenter, axisY), -dot(toCenter, axisZ));
    const float3 ray = float3(dot(direction, axisX), dot(direction, axisY), dot(direction, axisZ));

    float near = -1e30;
    float far = 1e30;
    int nearAxis = 0;
    float nearSign = 1.0;

    for(int axis = 0; axis < 3; ++axis){
        const float d = ray[axis];
        const float o = origin[axis];
        const float h = halfExtent[axis];

        if(abs(d) < 1e-12){
            //Zraka je usporedna s ovim parom ploha: ili je stalno unutra ili nikad
            if(abs(o) > h) return result;
            continue;
        }

        float t0 = (-h - o) / d;
        float t1 = ( h - o) / d;
        float sign = -1.0;
        if(t0 > t1){
            const float swap = t0; t0 = t1; t1 = swap;
            sign = 1.0;
        }

        if(t0 > near){
            near = t0;
            nearAxis = axis;
            nearSign = sign;
        }
        far = min(far, t1);
        if(near > far) return result;
    }

    //Iza kamere se ne crta. Kad je kamera UNUTAR kutije, near je negativan i takoder se odbija -
    //unutrasnjost kocke nije ono sto se ovdje zeli vidjeti
    if(near <= 0.0) return result;

    result.hit = true;
    result.depth = near;
    result.normal = nearSign * (nearAxis == 0 ? axisX : (nearAxis == 1 ? axisY : axisZ));
    return result;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<float4> splats;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> ranges;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> pairSplats;   //koji splat, po parovima - vec poredano
[[vk::image_format("rgba32f")]] [[vk::binding(3,0)]] RWTexture2D<float4> destination;

[shader("compute")]
[numthreads(TILE, TILE, 1)]
void computeMain(uint3 threadId : SV_GroupThreadID, uint3 groupId : SV_GroupID){
    const uint2 pixel = groupId.xy * TILE + threadId.xy;
    const uint tile = groupId.y * params.grid.x + groupId.x;

    //Dretva izvan slike i dalje sudjeluje u grupi, samo nista ne zapise. Rani izlaz bi bio
    //greska kad se jednom uvede zajednicko ucitavanje u dijeljenu memoriju
    const bool inside = pixel.x < params.imageSize.x && pixel.y < params.imageSize.y;

    const float2 point = float2(pixel) + float2(0.5, 0.5);

    const uint first = ranges[tile * 2 + 0];
    const uint last  = ranges[tile * 2 + 1];

    //Kocka se presijeca prije petlje: njezina dubina odlucuje gdje petlja staje
    BoxHit box;
    box.hit = false;
    box.depth = 1e30;
    box.normal = float3(0.0, 0.0, 1.0);

    if(params.boxVisible != 0){
        const float3 direction = float3((point.x - params.focalPrincipal.z) / params.focalPrincipal.x,
                                        (point.y - params.focalPrincipal.w) / params.focalPrincipal.y,
                                        -1.0);
        box = intersectBox(direction, params.boxCenter, params.boxHalfExtent,
                           params.boxAxisX.xyz, params.boxAxisY.xyz);
    }

    float3 color = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;

    for(uint j = first; j < last; ++j){
        //Sort je nosio sam indeks splata - nema skoka kroz polje parova
        const SplatData splat = readSplat(splats, pairSplats[j]);

        //Splat iza kocke se ne vidi. Parovi su vec poredani po dubini, pa se od ovog mjesta dalje
        //ne moze pojaviti nista blize - petlja smije stati, ne samo preskociti
        if(box.hit && splat.conicOpacityDepth.z > box.depth){
            break;
        }

        const float2 offset = splat.centerConic.xy - point;

        //Izvan tri sigme se ne crta. Isto odsijecanje koje je odredilo okvir u pločicama - da
        //nije isto, splat bi zahvacao pločicu u koju nikad nista ne zapise, ili obrnuto
        if(dot(offset, offset) > splat.conicOpacityDepth.w * splat.conicOpacityDepth.w){
            continue;
        }

        const float power = -0.5 * (splat.centerConic.z * offset.x * offset.x
                                  + splat.conicOpacityDepth.x * offset.y * offset.y)
                          - splat.centerConic.w * offset.x * offset.y;

        if(power > 0.0){
            continue;
        }

        const float alpha = min(0.99, splat.conicOpacityDepth.y * exp(power));
        if(alpha < 1.0 / 255.0){
            continue;
        }

        color += transmittance * alpha * splat.color.rgb;
        transmittance *= (1.0 - alpha);

        if(transmittance < 1e-4){
            break;
        }
    }

    //Kocka je iza svih splatova koji su ostali, pa ulazi s preostalom propusnoscu. Sjencanje je
    //namjerno najjednostavnije: ovo je mjerni predmet, a ne dio prizora
    if(box.hit){
        const float lit = 0.35 + 0.65 * max(0.0, dot(normalize(box.normal), normalize(float3(0.4, 0.7, 0.6))));
        color += transmittance * lit * params.boxColor.rgb;
        transmittance = 0.0;
    }

    if(inside){
        destination[pixel] = float4(color, 1.0 - transmittance);
    }
}
