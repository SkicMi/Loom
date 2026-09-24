// Splat scene to the screen, with one color fix that appears nowhere else.
//
// GAUSSIAN SPLATTING IS TRAINED ON sRGB IMAGES. What comes out of it as "color" are values in
// sRGB space, not linear light - training chose them to match photograph pixels. Loom's
// swapchain is eB8G8R8A8Srgb and itself encodes linear to sRGB on write, so those values would
// be encoded a SECOND TIME: the picture comes out washed out, as if someone lifted the
// brightness.
//
// So here we run the encoding backwards - sRGB back to linear - so what the swapchain writes
// is exactly what training saw.
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

//Official sRGB curve, not pow(2.2): they differ in the dark tones, and shadows are exactly
//what was washed out in this scene
float3 srgbToLinear(float3 color){
    return select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    float4 color = source.Sample(input.texCoord);
    return float4(srgbToLinear(saturate(color.rgb)) * material.baseColor.rgb, color.a);
}
