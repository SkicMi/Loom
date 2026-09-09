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
    uint padding0;
    uint padding1;
    uint padding2;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<float4> splats;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> ranges;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> sortValues;
[[vk::binding(3,0)]] RWStructuredBuffer<uint> splatIndices;
[[vk::image_format("rgba32f")]] [[vk::binding(4,0)]] RWTexture2D<float4> destination;

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

    float3 color = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;

    for(uint j = first; j < last; ++j){
        const uint pair = sortValues[j];
        const SplatData splat = readSplat(splats, splatIndices[pair]);

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

    if(inside){
        destination[pixel] = float4(color, 1.0 - transmittance);
    }
}
