
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
    //x = range, y = 1 when a shadow map is bound for this light, z = shadow depth bias
    float4 params;
    float4x4 lightViewProjection;
}

struct Push{
    float4x4 model;
    float4x4 normalMatrix;
}

[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0,0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1,0)]] StructuredBuffer<GpuLight> lights;

//The shadow map, as a comparison sampler on set 0. SampleCmpLevelZero does not return a
//depth - it returns how much of the 2x2 neighbourhood passed "is my reference nearer than
//what is stored here", filtered by the same hardware that does bilinear. That free 2x2 is
//why a shadow edge is not a staircase
[[vk::binding(2,0)]] Sampler2DShadow shadowMap;

//1.0 fully lit, 0.0 fully shadowed. A light without a map bound (params.y == 0) returns 1.0
//without ever touching the sampler
float shadowVisibility(GpuLight light, float3 worldPos){
    if(light.params.y < 0.5) return 1.0;

    float4 clip = mul(light.lightViewProjection, float4(worldPos, 1.0));
    float3 proj = clip.xyz / clip.w;

    //Behind the light's far plane there is no map to consult, and the border is opaque white
    //so anything outside the box sideways already compares as lit
    if(proj.z > 1.0) return 1.0;

    float2 uv = proj.xy * 0.5 + 0.5;

    //The bias is subtracted from the reference, not added to the stored depth: a fragment
    //has to be nearer than the map by this much before it counts as lit
    return shadowMap.SampleCmpLevelZero(uv, proj.z - light.params.z);
}

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

        //Shadow kills the whole contribution of this light, diffuse and specular alike.
        //Ambient is untouched, which is why a shadowed surface is exactly its ambient term
        //and not black
        float visibility = shadowVisibility(light, input.worldPos);

        lit += base * light.color.rgb * diffuse * attenuation * visibility;
        lit += light.color.rgb * specular * attenuation * visibility;
    }

    return float4(lit,1.0);
}
