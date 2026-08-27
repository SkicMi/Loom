//Svjetlo nad G-bufferom: ploha dolazi iz slike, a ne iz mesha.
//
//Fragment ne zna ni za jedan trokut - zna gdje je tocka i kamo gleda, a to je sve sto
//osvjetljenje ikad treba. Racuna ISTIM kodom kao i crtez preko mesha (shadeSurface). Da su
//to dvije funkcije koje "rade isto", tvrdnja da relight daje istu sliku ne bi se dala
//dokazati - dala bi se samo izjaviti.
//
//Ovdje se crta samo nase svjetlo. Za ubacivanje u postojecu snimku ide relight_composite.

#include "include/Relight.slang"

[[vk::binding(2,1)]] ConstantBuffer<RelightData> relight;

//Jedini vertex stage cijelog relighta: trokut preko ekrana je isti trokut, pa ga
//relight_composite koristi iz ovog filea. Stoji ovdje a ne u includeu jer CMake stageove
//trazi u tekstu samog shadera
[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID){
    VSOutput output;
    output.texCoord = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    output.position = float4(output.texCoord * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    Surface surface = surfaceAt(input.texCoord, relight.inverseView);

    //Gdje nema plohe, nema ni osvjetljenja. Ne crta se crno nego se ne crta nista - pozadina
    //ostaje ono sto je vec bilo
    if(!surface.exists){
        discard;
    }

    float3 lit = shadeSurface(relight.baseColor.rgb, surface.worldPos, surface.N, surface.V,
                              relight.shininess, relight.specularStrength);

    return float4(lit, 1.0);
}
