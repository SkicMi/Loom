#include "include/Lighting.slang"

struct MaterialData{
    float4 baseColor;
    float shininess;
    float specularStrength;
    float padding0;
    float padding1;
}

struct Push{
    float4x4 model;
    float4x4 normalMatrix;
}

[[vk::push_constant]] ConstantBuffer<Push> push;

[[vk::binding(0,1)]] Sampler2D albedo;
[[vk::binding(1,1)]] ConstantBuffer<MaterialData> material;

struct VSInput{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 texCoord;
    [[vk::location(3)]] float3 normal;

}

struct VSOutput{
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
    output.position = mul(frame.projection, mul(frame.view, world));
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

    float3 base = albedo.Sample(input.texCoord).rgb * material.baseColor.rgb;
    float3 lit = shadeSurface(base, input.worldPos, N, V, material.shininess, material.specularStrength);
    return float4(lit,1.0);
}
