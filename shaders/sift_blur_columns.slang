// SIFT descriptors on the GPU, pass 2 of 4: vertical Gaussian blur back to 8 bits, clamped and
// truncated exactly like Engine's blurred(). Rows past the edge repeat the edge row.
//
// One thread per pixel.
struct Params{
    uint width;
    uint height;
    int reach;
    uint kernelOffset;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] StructuredBuffer<float> kernels;
[[vk::binding(1,0)]] StructuredBuffer<float> across;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> soft;     // one pixel per uint

[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    if(id.x >= params.width || id.y >= params.height) return;
    precise float sum = 0.0f;
    for(int d = -params.reach; d <= params.reach; ++d){
        const int row = clamp(int(id.y) + d, 0, int(params.height) - 1);
        precise float term = kernels[params.kernelOffset + uint(d + params.reach)] * across[uint(row) * params.width + id.x];
        sum = sum + term;
    }
    soft[id.y * params.width + id.x] = uint(max(0.0f, min(255.0f, sum)));
}
