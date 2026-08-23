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

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    float4 color = source.Sample(input.texCoord);
    return float4(color.rgb * material.baseColor.rgb, color.a);
}
