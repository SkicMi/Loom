struct HistogramParams{
    uint2 size;
    uint mode;
    uint padding0;
}

[[vk::push_constant]] ConstantBuffer<HistogramParams> params;
[[vk::binding(0,0)]] Sampler2D source;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> histogram;

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    uint2 pixel = threadId.xy;

    if(pixel.x >= params.size.x || pixel.y >= params.size.y){
        return;
    }

    float4 color = source.Load(int3(int2(pixel), 0));

    float value = params.mode == 0
                ? color.r
                : dot(color.rgb, float3(0.2126, 0.7152, 0.0722));

    uint bin = uint(clamp(value * 255.0 + 0.5, 0.0, 255.0));

    InterlockedAdd(histogram[bin], 1u);
}
