// SIFT descriptors on the GPU, pass 3 of 4: gradient magnitude and direction of the blurred frame
// (or of the frame itself when the band needs no blur), central differences with the edge repeated.
//
// One thread per pixel.
struct Params{
    uint width;
    uint height;
    uint packedSource;      // 1: read the frame itself (4 pixels per uint), 0: read the blur
    uint padding0;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] StructuredBuffer<uint> image;
[[vk::binding(1,0)]] StructuredBuffer<uint> soft;
[[vk::binding(2,0)]] RWStructuredBuffer<float> magnitude;
[[vk::binding(3,0)]] RWStructuredBuffer<float> direction;

float pixel(int x, int y){
    x = clamp(x, 0, int(params.width) - 1);
    y = clamp(y, 0, int(params.height) - 1);
    const uint i = uint(y) * params.width + uint(x);
    if(params.packedSource != 0) return float((image[i >> 2] >> ((i & 3u) * 8u)) & 0xFFu);
    return float(soft[i]);
}

// The gradients come from 8-bit pixels, so they very often lie exactly on an axis or a diagonal -
// and those directions fall exactly on a boundary between histogram bins (-pi/2 is bin 27 of 36).
// The GPU's atan2 lands a bit to either side, the peak moves one bin and the whole descriptor turns.
// So those directions are the float the CPU's atan2 returns, not an approximation of it
float angleOf(float y, float x){
    const float HALF_PI = 1.57079637f, PI = 3.14159274f, QUARTER_PI = 0.785398185f, THREE_QUARTER_PI = 2.35619450f;
    if(x == 0.0f){
        if(y > 0.0f) return HALF_PI;
        if(y < 0.0f) return -HALF_PI;
        return 0.0f;
    }
    if(y == 0.0f) return x > 0.0f ? 0.0f : PI;
    if(abs(x) == abs(y)){
        if(x > 0.0f) return y > 0.0f ? QUARTER_PI : -QUARTER_PI;
        return y > 0.0f ? THREE_QUARTER_PI : -THREE_QUARTER_PI;
    }
    return atan2(y, x);
}

[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    if(id.x >= params.width || id.y >= params.height) return;
    const int x = int(id.x), y = int(id.y);
    const float gx = 0.5f * (pixel(x + 1, y) - pixel(x - 1, y));
    const float gy = 0.5f * (pixel(x, y + 1) - pixel(x, y - 1));
    const uint index = id.y * params.width + id.x;
    magnitude[index] = sqrt(gx * gx + gy * gy);
    direction[index] = angleOf(gy, gx);
}
