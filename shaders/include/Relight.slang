//Ono sto oba relight prolaza dijele: sto je ploha i kako se do nje dolazi.
//
//Razlikuju se samo u tome sto rade s rezultatom - jedan crta svoje svjetlo, drugi ga dodaje
//na snimku koja je vec tu.

#include "Lighting.slang"

struct RelightData{
    //Tocke i normale su u view prostoru, jer je dubina iz koje su nastale bila. Svjetla su u
    //svjetskom. Ovo je most izmedu to dvoje, i jedini razlog zasto relight treba kameru
    float4x4 inverseView;

    float4 baseColor;
    float shininess;
    float specularStrength;
    float padding0;
    float padding1;
}

[[vk::binding(0,1)]] Sampler2D positionMap;
[[vk::binding(1,1)]] Sampler2D normalMap;

struct VSOutput{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
}

//Ploha pod ovim pikselom, prebacena u svjetski prostor. exists je false gdje plohe nema
struct Surface{
    float3 worldPos;
    float3 N;
    float3 V;
    bool exists;
}

Surface surfaceAt(float2 texCoord, float4x4 inverseView){
    Surface out;

    float4 point = positionMap.SampleLevel(texCoord, 0.0);
    float4 normal = normalMap.SampleLevel(texCoord, 0.0);

    out.exists = point.w > 0.5 && normal.w > 0.5;
    if(!out.exists){
        out.worldPos = float3(0,0,0);
        out.N = float3(0,0,1);
        out.V = float3(0,0,1);
        return out;
    }

    out.worldPos = mul(inverseView, float4(point.xyz, 1.0)).xyz;

    //Normala je smjer, ne mjesto: w = 0, pa je translacija ne dira
    out.N = normalize(mul(inverseView, float4(normal.xyz, 0.0)).xyz);
    out.V = normalize(frame.cameraPosition.xyz - out.worldPos);
    return out;
}
