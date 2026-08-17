
struct FrameData{
    float4x4 view;
    float4x4 projection;
}

struct Push{
    float4x4 model;
}

[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0,0)]] ConstantBuffer<FrameData> frame;

struct VSInput{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 texCoord;
}

struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input){
    VSOutput output;
    float4 world = mul(push.model, float4(input.position,1.0));
    output.position = mul(frame.projection, mul(frame.view,world));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    return float4(input.color,1.0);
}