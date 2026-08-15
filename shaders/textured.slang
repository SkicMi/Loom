struct Push{
    float4x4 mvp;
}

[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0)]] Sampler2D albedo;

struct VSInput{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 texCoord;

}

struct VSOutput{
    float4 position : SV_Position;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD0;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input){
    VSOutput output;
    output.position = mul(push.mvp, float4(input.position,1.0));
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    return albedo.Sample(input.texCoord);
}