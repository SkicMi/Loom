
struct FrameData{
    float4x4 view;
    float4x4 projection;
    float4 cameraPosition;
    float4 ambientColor;
    uint lightCount;
    uint padding0;
    uint padding1;
    uint padding2;
}

struct GpuLight{
    float4 positionOrDirection;
    float4 color;
    float4 params;
}

struct Push{
    float4x4 model;
    float4x4 normalMatrix;
}

[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0,0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1,0)]] StructuredBuffer<GpuLight> lights;

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
    float3 V = normalize(frame.cameraPosition.xyz - input.worldPos);

    float3 base = input.color;
    float3 lit = base * frame.ambientColor.rgb;

    for(uint i = 0; i < frame.lightCount; ++i){
        GpuLight light = lights[i];

        float3 L;
        float attenuation = 1.0;

        if(light.positionOrDirection.w == 0.0){
            L = normalize(-light.positionOrDirection.xyz);
        }
        else{
            float3 toLight = light.positionOrDirection.xyz - input.worldPos;
            float dist = max(length(toLight), 1e-4);
            L = toLight / dist;

            float range = max(light.params.x, 1e-4);
            float window = saturate(1.0 - pow(dist / range, 4.0));
            attenuation = window * window / (dist * dist);
        }

        float3 H = normalize(L + V);
        float diffuse = max(dot(N,L), 0.0);
        float specular = pow(max(dot(N,H),0.0), 32.0);

        lit += base * light.color.rgb * diffuse * attenuation;
        lit += light.color.rgb * specular * attenuation;
    }

    return float4(lit,1.0);
}
