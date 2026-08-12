
struct Push{
    float4x4 mvp;
}

[[vk::push_constant]] ConstantBuffer<Push> push;

struct VSInput{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
}

struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input){
    VSOutput output;
    output.position = mul(push.mvp, float4(input.position,1.0));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    return float4(input.color,1.0);
}