struct ExposureParams{
    float key;
    float padding0;
    float padding1;
    float padding2;
}

[[vk::push_constant]] ConstantBuffer<ExposureParams> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> histogram;
[[vk::binding(1,0)]] RWStructuredBuffer<float> result;

groupshared uint weighted[256];
groupshared uint counted[256];

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    uint bin = threadId.x;
    uint count = histogram[bin];

    weighted[bin] = count * bin;
    counted[bin] = count;

    GroupMemoryBarrierWithGroupSync();

    for(uint stride = 128; stride > 0; stride >>= 1){
        if(bin < stride){
            weighted[bin] += weighted[bin + stride];
            counted[bin] += counted[bin + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if(bin == 0){
        float total = max(float(counted[0]), 1.0);
        float average = float(weighted[0]) / total / 255.0;
        result[0] = average;
        result[1] = params.key / max(average, 1e-4);
    }
}
