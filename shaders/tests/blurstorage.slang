struct BlurParams{
    uint2 size;
}

[[vk::push_constant]] ConstantBuffer<BlurParams> params;
[[vk::image_format("rgba8")]] [[vk::binding(0,0)]] RWTexture2D<float4> source;
[[vk::image_format("rgba8")]] [[vk::binding(1,0)]] RWTexture2D<float4> destination;

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    int2 pixel = int2(threadId.xy);

    if(pixel.x >= int(params.size.x) || pixel.y >= int(params.size.y)){
        return;
    }

    int2 last = int2(params.size) - int2(1,1);
    float4 sum = float4(0.0);

    for(int dy = -1; dy <= 1; ++dy){
        for(int dx = -1; dx <= 1; ++dx){
            int2 tap = clamp(pixel + int2(dx,dy), int2(0,0), last);
            sum += source[uint2(tap)];
        }
    }

    destination[uint2(pixel)] = sum / 9.0;
}
