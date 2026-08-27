//Svjetlo nad G-bufferom: ploha dolazi iz slike, a ne iz mesha.
//
//Ovo je mjesto na kojem se ubacivanje svjetla u snimku konacno dogada. Fragment ne zna ni za
//jedan trokut - zna gdje je tocka (iz slike pozicija) i kamo gleda (iz slike normala), a to
//je sve sto osvjetljenje ikad i treba.
//
//Racuna ISTIM kodom kao i crtez preko mesha: shadeSurface iz include/Lighting.slang. Da su
//to dvije funkcije koje "rade isto", tvrdnja da relight daje istu sliku ne bi se dala
//dokazati - dala bi se samo izjaviti.

#include "include/Lighting.slang"

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
[[vk::binding(2,1)]] ConstantBuffer<RelightData> relight;

struct VSOutput{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
}

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID){
    VSOutput output;
    output.texCoord = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    output.position = float4(output.texCoord * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    float4 point = positionMap.SampleLevel(input.texCoord, 0.0);
    float4 surface = normalMap.SampleLevel(input.texCoord, 0.0);

    //Gdje nema plohe, nema ni osvjetljenja. Ne crta se crno nego se ne crta nista - pozadina
    //ostaje ono sto je vec bilo, a to je tocno ono sto kompozicija poslije treba
    if(point.w < 0.5 || surface.w < 0.5){
        discard;
    }

    float3 worldPos = mul(relight.inverseView, float4(point.xyz, 1.0)).xyz;

    //Normala je smjer, ne mjesto: w = 0, pa je translacija ne dira
    float3 N = normalize(mul(relight.inverseView, float4(surface.xyz, 0.0)).xyz);
    float3 V = normalize(frame.cameraPosition.xyz - worldPos);

    float3 lit = shadeSurface(relight.baseColor.rgb, worldPos, N, V,
                              relight.shininess, relight.specularStrength);

    return float4(lit, 1.0);
}
