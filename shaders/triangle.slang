
struct FrameData{
    float4x4 view;
    float4x4 projection;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
}

struct Push{
    float4x4 model;
    float4x4 normalMatrix;
}

[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0,0)]] ConstantBuffer<FrameData> frame;

struct VSInput{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 texCoord;
    [[vk::location(3)]] float3 normal;
}

struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input){
    VSOutput output;
    float4 world = mul(push.model, float4(input.position,1.0));
    output.position = mul(frame.projection, mul(frame.view,world));
    output.color = input.color;
    output.worldPos = world.xyz;
    output.normal = mul(push.normalMatrix, float4(input.normal,0.0)).xyz;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    float3 N = normalize(input.normal);
    float3 L = normalize(-frame.lightDirection.xyz);
    float3 V = normalize(frame.cameraPosition.xyz - input.worldPos);
    float3 H = normalize(L + V);

    float diffuse = max(dot(N,L), 0.0);
    float specular = pow(max(dot(N,H), 0.0), 32.0);

    float3 base = input.color;
    float3 lit = base * (frame.ambientColor.rgb + frame.lightColor.rgb * diffuse)
               + frame.lightColor.rgb * specular;
    return float4(lit, 1.0);
}
