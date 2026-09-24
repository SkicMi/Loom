// SIFT descriptors on the GPU (Loom SiftDescriber), pass 1 of 4: horizontal Gaussian blur of the
// 8-bit frame into floats. Same kernel (computed on the CPU), same order of terms and no fused
// multiply-add (`precise`), so every sum is bit-identical to Engine's blurred().
//
// One thread per pixel.
struct Params{
    uint width;
    uint height;
    int reach;
    uint kernelOffset;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] StructuredBuffer<uint> image;      // 4 pixels per uint, row after row
[[vk::binding(1,0)]] StructuredBuffer<float> kernels;
[[vk::binding(2,0)]] RWStructuredBuffer<float> across;

float pixel(int x, int y){
    x = clamp(x, 0, int(params.width) - 1);
    y = clamp(y, 0, int(params.height) - 1);
    const uint i = uint(y) * params.width + uint(x);
    return float((image[i >> 2] >> ((i & 3u) * 8u)) & 0xFFu);
}

[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    if(id.x >= params.width || id.y >= params.height) return;
    precise float sum = 0.0f;
    for(int d = -params.reach; d <= params.reach; ++d){
        precise float term = kernels[params.kernelOffset + uint(d + params.reach)] * pixel(int(id.x) + d, int(id.y));
        sum = sum + term;
    }
    across[id.y * params.width + id.x] = sum;
}
