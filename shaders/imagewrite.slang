struct ImageParams{
    uint2 size;
    uint mode;
    uint padding0;
}

[[vk::push_constant]] ConstantBuffer<ImageParams> params;
[[vk::image_format("rgba8")]] [[vk::binding(0,0)]] RWTexture2D<float4> target;

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    uint2 pixel = threadId.xy;

    if(pixel.x >= params.size.x || pixel.y >= params.size.y){
        return;
    }

    float4 color;
    if(params.mode == 0){
        color = float4(1.0, 0.0, 1.0, 1.0);
    }
    else{
        color = float4(float(pixel.x % 256) / 255.0,
                       float(pixel.y % 256) / 255.0,
                       float((pixel.x ^ pixel.y) % 256) / 255.0,
                       1.0);
    }

    target[pixel] = color;
}
