struct BlurParams{
    uint2 size;
}

[[vk::push_constant]] ConstantBuffer<BlurParams> params;
[[vk::binding(0,0)]] Sampler2D source;
[[vk::image_format("r32f")]] [[vk::binding(1,0)]] RWTexture2D<float> destination;

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    int2 pixel = int2(threadId.xy);

    if(pixel.x >= int(params.size.x) || pixel.y >= int(params.size.y)){
        return;
    }

    int2 last = int2(params.size) - int2(1,1);
    float sum = 0.0;

    for(int dy = -1; dy <= 1; ++dy){
        for(int dx = -1; dx <= 1; ++dx){
            int2 tap = clamp(pixel + int2(dx,dy), int2(0,0), last);
            sum += source.Load(int3(tap, 0)).b;
        }
    }

    destination[uint2(pixel)] = sum * 255.0 / 9.0;
}
