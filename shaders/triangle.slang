#include "include/Lighting.slang"

struct Push{
    float4x4 model;
    float4x4 normalMatrix;
}

[[vk::push_constant]] ConstantBuffer<Push> push;

struct VSInput{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 texCoord;
    [[vk::location(3)]] float3 normal;
}

struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD0;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input){
    VSOutput output;
    float4 world = mul(push.model, float4(input.position,1.0));
    output.position = mul(frame.projection, mul(frame.view,world));
    output.color = input.color;
    output.texCoord = input.texCoord;
    output.worldPos = world.xyz;
    output.normal = mul(push.normalMatrix, float4(input.normal,0.0)).xyz;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    float3 N = normalize(input.normal);
    float3 V = normalize(frame.cameraPosition.xyz - input.worldPos);

    //Bez materijala: boja je vrhova, a sjaj su brojevi koje je ovaj shader oduvijek imao
    float3 lit = shadeSurface(input.color, input.worldPos, N, V, 32.0, 1.0);
    return float4(lit,1.0);
}
