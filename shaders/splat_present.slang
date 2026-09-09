// Splat scena na ekran, uz jedan ispravak boje koji se ne vidi nigdje drugdje.
//
// GAUSSIAN SPLATTING SE TRENIRA NA sRGB SLIKAMA. Ono sto iz njega ispadne kao "boja" su
// vrijednosti u sRGB prostoru, a ne linearno svjetlo - trening ih je birao tako da se slozu s
// pikselima fotografija. Loomov swapchain je eB8G8R8A8Srgb i sam kodira linearno u sRGB pri
// pisanju, pa bi te vrijednosti bile kodirane DRUGI PUT: slika ispadne isprana, kao da joj je
// netko dignuo svjetlinu.
//
// Zato se ovdje radi obrnuto od kodiranja - sRGB natrag u linearno - da bi ono sto swapchain
// zapise bilo tocno ono sto je trening vidio.
struct MaterialData{
    float4 baseColor;
    float shininess;
    float specularStrength;
    float padding0;
    float padding1;
}

[[vk::binding(0,1)]] Sampler2D source;
[[vk::binding(1,1)]] ConstantBuffer<MaterialData> material;

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

//Sluzbena sRGB krivulja, ne pow(2.2): razlikuju se u tamnim tonovima, a sjene su upravo ono
//sto je u ovoj sceni bilo isprano
float3 srgbToLinear(float3 color){
    return select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    float4 color = source.Sample(input.texCoord);
    return float4(srgbToLinear(saturate(color.rgb)) * material.baseColor.rgb, color.a);
}
