// PBR: metalnost i hrapavost, isti model kao glTF i UsdPreviewSurface.
//
// Cook-Torrance s GGX raspodjelom, Smithovom geometrijom (Schlick-GGX, visina) i Schlickovim
// Fresnelom; difuzno Lambert, umanjeno za ono sto je odbijeno i za metalnost - metal nema
// difuznog dijela, njegova boja JE boja odsjaja (F0). Isti izrazi kao u glTF referentnom
// prikazivacu, pa model izgleda kao u Blenderu ili Sketchfabu.
//
// MAPE (set 1, redom): osnovna boja (sRGB), metalnost-hrapavost (G, B), normal (tangentni,
// +Y gore), occlusion (R), emisija (sRGB). Mapa koje nema je 1x1 neutralna - bijela ili ravna
// normala - pa shader ne grana po tome ima li mape.
//
// TANGENTE IZ DERIVACIJA. Vrh u Loomu nema tangentu, a glTF je cesto ni ne nosi. Okvir se sastavi
// po pikselu iz promjene polozaja i UV-a (Schuler, "Normal Mapping Without Precomputed Tangents"):
// isti rezultat kao MikkTSpace na glatkim mrezama, s tim da je na rubovima UV otoka malo grublji.
//
// OKOLINA JE ANALITICKA: nebo odozgo, tlo odozdo, glatki prijelaz - za difuzno i (za hrapavo
// zamuceno) za odsjaj, uz Karisovu aproksimaciju integrala BRDF-a. To nije slika okoline (IBL s
// predfiltriranom mapom), ali metal bez okoline je crn, a ovako ima sto odbijati.
//
// Izlaz je vec tonski mapiran (ACES, Narkowicz) i linearan; sRGB kodira meta/swapchain
#include "include/Lighting.slang"

struct PbrData{
    float4 baseColor;
    float4 emissive;            //rgb = boja * jacina
    float4 skyColor;            //a = izlozenost
    float4 groundColor;
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint alphaMode;             //0 neprozirno, 1 maska, 2 prozirno
    float padding0;
    float padding1;
}

struct Push{
    float4x4 model;
    float4x4 normalMatrix;
}

[[vk::push_constant]] ConstantBuffer<Push> push;

[[vk::binding(0,1)]] Sampler2D baseColorMap;
[[vk::binding(1,1)]] Sampler2D metallicRoughnessMap;
[[vk::binding(2,1)]] Sampler2D normalMap;
[[vk::binding(3,1)]] Sampler2D occlusionMap;
[[vk::binding(4,1)]] Sampler2D emissiveMap;
[[vk::binding(5,1)]] ConstantBuffer<PbrData> material;

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
    float4 world = mul(push.model, float4(input.position, 1.0));
    output.position = mul(frame.projection, mul(frame.view, world));
    output.color = input.color;
    output.texCoord = input.texCoord;
    output.worldPos = world.xyz;
    output.normal = mul(push.normalMatrix, float4(input.normal, 0.0)).xyz;
    return output;
}

static const float PI = 3.14159265;

float distributionGGX(float NdotH, float alpha){
    float a2 = alpha * alpha;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float smithGGX(float NdotV, float NdotL, float roughness){
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float3 fresnelSchlick(float cosTheta, float3 F0){
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

//Karis, "Physically Based Shading on Mobile": integral BRDF-a po okolini, bez tablice
float3 environmentBRDF(float3 F0, float roughness, float NdotV){
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return F0 * ab.x + ab.y;
}

//Nebo i tlo: glatki prijelaz oko horizonta, siri kad je povrsina hrapava
float3 environment(float3 direction, float roughness){
    float t = smoothstep(-0.2 - roughness * 0.6, 0.2 + roughness * 0.6, direction.y);
    return lerp(material.groundColor.rgb, material.skyColor.rgb, t);
}

float3 aces(float3 x){
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 perturbedNormal(float3 N, float3 worldPos, float2 uv, float3 tangentNormal){
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = rsqrt(max(dot(T, T), dot(B, B)));
    if(!(invmax < 1e30)) return N;              //UV se ne mijenja: nema okvira
    //glTF normal mapa je +Y gore u UV-u koji raste prema dolje u slici: B ide suprotno od v
    float3x3 TBN = float3x3(T * invmax, -B * invmax, N);
    return normalize(mul(tangentNormal, TBN));
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target{
    float4 base = baseColorMap.Sample(input.texCoord) * material.baseColor * float4(input.color, 1.0);
    if(material.alphaMode == 1 && base.a < material.alphaCutoff) discard;

    float3 N = normalize(input.normal);
    if(!frontFacing) N = -N;                    //dvostrana ploha: poledjina gleda prema kameri
    float3 tangentNormal = normalMap.Sample(input.texCoord).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;
    N = perturbedNormal(N, input.worldPos, input.texCoord, normalize(tangentNormal));

    float4 mr = metallicRoughnessMap.Sample(input.texCoord);
    float metallic = saturate(material.metallic * mr.b);
    float roughness = clamp(material.roughness * mr.g, 0.03, 1.0);
    float alpha = roughness * roughness;
    float occlusion = lerp(1.0, occlusionMap.Sample(input.texCoord).r, material.occlusionStrength);

    float3 V = normalize(frame.cameraPosition.xyz - input.worldPos);
    float NdotV = max(dot(N, V), 1e-4);
    float3 F0 = lerp(float3(0.04), base.rgb, metallic);
    float3 diffuseColor = base.rgb * (1.0 - metallic);

    float3 radiance = float3(0.0);
    for(uint i = 0; i < frame.lightCount; ++i){
        GpuLight light = lights[i];
        float3 L;
        float attenuation = 1.0;
        if(light.positionOrDirection.w < 0.5){
            L = normalize(-light.positionOrDirection.xyz);
        }else{
            float3 toLight = light.positionOrDirection.xyz - input.worldPos;
            float d = length(toLight);
            L = toLight / max(d, 1e-5);
            float range = max(light.params.x, 1e-4);
            float falloff = saturate(1.0 - pow(d / range, 4.0));
            attenuation = falloff * falloff / max(d * d, 1e-4);
        }
        float NdotL = dot(N, L);
        if(NdotL <= 0.0) continue;
        float3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);
        float3 F = fresnelSchlick(VdotH, F0);
        float3 specular = distributionGGX(NdotH, alpha) * smithGGX(NdotV, NdotL, roughness) * F / max(4.0 * NdotV * NdotL, 1e-4);
        float3 diffuse = (1.0 - F) * diffuseColor / PI;
        radiance += (diffuse + specular) * light.color.rgb * NdotL * attenuation;
    }

    //Okolina: difuzno iz smjera normale, odsjaj iz zrcaljenog smjera, oboje zasjenjeno occlusionom
    float3 R = reflect(-V, N);
    float3 ambientDiffuse = environment(N, 1.0) * diffuseColor;
    float3 ambientSpecular = environment(R, roughness) * environmentBRDF(F0, roughness, NdotV);
    radiance += (ambientDiffuse + ambientSpecular) * occlusion;

    radiance += emissiveMap.Sample(input.texCoord).rgb * material.emissive.rgb;
    float3 mapped = aces(radiance * material.skyColor.a);

    //Prozirno se slaze premultiplicirano (BlendMode::Premultiplied), ostalo je neprozirno
    float outAlpha = material.alphaMode == 2 ? base.a : 1.0;
    return float4(mapped * outAlpha, outAlpha);
}
