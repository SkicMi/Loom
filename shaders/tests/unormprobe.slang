struct ProbeParams{
    uint2 size;
}

[[vk::push_constant]] ConstantBuffer<ProbeParams> params;
[[vk::image_format("rgba8")]] [[vk::binding(0,0)]] RWTexture2D<float4> destination;

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    uint2 pixel = threadId.xy;

    if(pixel.x >= params.size.x || pixel.y >= params.size.y){
        return;
    }

    float fractions[8] = {0.0, 0.1, 0.4, 0.4999, 0.5, 0.5001, 0.5556, 0.9};
    float value = (float(pixel.x) + fractions[pixel.y % 8]) / 255.0;

    destination[pixel] = float4(value, value, value, 1.0);
}
