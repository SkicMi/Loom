// SIFT descriptors on the GPU, pass 4 of 4: for every feature of one scale band, the dominant
// orientation (36-bin histogram, parabola through the peak) and the 4x4x8 histogram with trilinear
// spreading, normalised, clamped and normalised again - Engine's dominantAngle() and
// buildDescriptor() line for line.
//
// One workgroup of 64 threads per feature. Each thread walks its own rows of the patch into its
// own histogram in shared memory, and the 64 are then added in a fixed order - so the result is
// the same on every run, and differs from the CPU only in the order of the sums and in the last
// bits of exp, sin and cos.
struct Params{
    uint width;
    uint height;
    uint firstKey;
    uint keyCount;
    uint orient;
    float clampAt;
    uint padding0;
    uint padding1;
}

struct Key{
    int centreX;
    int centreY;
    uint patch;
    uint slot;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] StructuredBuffer<float> magnitude;
[[vk::binding(1,0)]] StructuredBuffer<float> direction;
[[vk::binding(2,0)]] StructuredBuffer<Key> keys;
[[vk::binding(3,0)]] RWStructuredBuffer<uint> descriptors;   // 32 uints (128 bytes) per slot
[[vk::binding(4,0)]] RWStructuredBuffer<float> angles;
[[vk::binding(5,0)]] RWStructuredBuffer<uint> valid;

static const float TWO_PI = 6.28318530718f;

// Correctly rounded a / b. Vulkan lets OpFDiv be 2.5 ulp off, and the quotients below decide a
// histogram bin: -pi/2 over 2 pi times 36 is exactly 27 on the CPU and 26.99999 with a loose divide,
// and the peak moves one bin. One residual step with a real fused multiply-add rounds it the IEEE way
float divide(float a, float b){
    precise float inverse = 1.0f / b;
    precise float q = a * inverse;
    precise float r = fma(-q, b, a);
    return fma(r, inverse, q);
}

bool normalise(inout float values[128]){
    float sum = 0.0f;
    for(uint i = 0; i < 128; ++i) sum += values[i] * values[i];
    const float length = sqrt(sum);
    if(length < 1e-12f) return false;
    for(uint i = 0; i < 128; ++i) values[i] = divide(values[i], length);
    return true;
}


static const uint THREADS = 64;
groupshared float partial[THREADS * 128];
groupshared float orientation[36];
groupshared float angleShared;

// Rows dy = -reach + thread, -reach + thread + 64, ... of the patch
float dominantAngle(Key key, uint thread){
    for(uint b = 0; b < 36; ++b) partial[thread * 128 + b] = 0.0f;
    const int reach = int(key.patch);
    const float sigma = 0.5f * float(key.patch);
    for(int dy = -reach + int(thread); dy <= reach; dy += int(THREADS)){
        for(int dx = -reach; dx <= reach; ++dx){
            const int x = key.centreX + dx, y = key.centreY + dy;
            if(x < 0 || y < 0 || x >= int(params.width) || y >= int(params.height)) continue;
            const float away = float(dx * dx + dy * dy);
            if(away > float(reach * reach)) continue;
            const uint index = uint(y) * params.width + uint(x);
            const float weight = exp(-away / (2.0f * sigma * sigma)) * magnitude[index];
            float turn = direction[index];
            if(turn < 0.0f) turn += TWO_PI;
            precise float place = divide(turn, TWO_PI) * 36.0f;
            const uint bin = min(35u, uint(place));
            partial[thread * 128 + bin] += weight;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if(thread < 36){
        float sum = 0.0f;
        for(uint t = 0; t < THREADS; ++t) sum += partial[t * 128 + thread];
        orientation[thread] = sum;
    }
    GroupMemoryBarrierWithGroupSync();
    if(thread == 0){
        uint best = 0;
        for(uint b = 1; b < 36; ++b) if(orientation[b] > orientation[best]) best = b;
        const float left = orientation[(best + 35) % 36];
        const float peak = orientation[best];
        const float right = orientation[(best + 1) % 36];
        const float bottom = left - 2.0f * peak + right;
        float offset = 0.0f;
        if(abs(bottom) > 1e-9f) offset = divide(0.5f * (left - right), bottom);
        offset = max(-0.5f, min(0.5f, offset));
        precise float angle = divide(float(best) + 0.5f + offset, 36.0f) * TWO_PI;
        angleShared = angle;
    }
    GroupMemoryBarrierWithGroupSync();
    return angleShared;
}

[shader("compute")]
[numthreads(64,1,1)]
void computeMain(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID){
    const uint thread = local.x;
    if(group.x >= params.keyCount) return;
    const Key key = keys[params.firstKey + group.x];

    const float angle = params.orient != 0 ? dominantAngle(key, thread) : 0.0f;

    for(uint i = 0; i < 128; ++i) partial[thread * 128 + i] = 0.0f;
    const int reach = int(key.patch);
    const float cellWidth = float(key.patch) * 2.0f / 4.0f;
    const float sigma = float(key.patch);
    const float cosine = cos(-angle), sine = sin(-angle);

    for(int dy = -reach + int(thread); dy <= reach; dy += int(THREADS)){
        for(int dx = -reach; dx <= reach; ++dx){
            const int x = key.centreX + dx, y = key.centreY + dy;
            if(x < 1 || y < 1 || x + 1 >= int(params.width) || y + 1 >= int(params.height)) continue;
            const float u = float(dx) * cosine - float(dy) * sine;
            const float v = float(dx) * sine + float(dy) * cosine;
            precise float cellX = divide(u, cellWidth) + 2.0f - 0.5f;
            precise float cellY = divide(v, cellWidth) + 2.0f - 0.5f;
            if(cellX <= -1.0f || cellY <= -1.0f || cellX >= 4.0f || cellY >= 4.0f) continue;
            const uint index = uint(y) * params.width + uint(x);
            const float weight = exp(-(u * u + v * v) / (2.0f * sigma * sigma)) * magnitude[index];
            if(weight <= 0.0f) continue;
            float turn = direction[index] - angle;
            while(turn < 0.0f) turn += TWO_PI;
            while(turn >= TWO_PI) turn -= TWO_PI;
            precise float binPlace = divide(turn, TWO_PI) * 8.0f;
            const int cx0 = int(floor(cellX)), cy0 = int(floor(cellY));
            const int b0 = int(floor(binPlace));
            const float fx = cellX - float(cx0), fy = cellY - float(cy0), fb = binPlace - float(b0);
            for(int ix = 0; ix <= 1; ++ix){
                const int cx = cx0 + ix;
                if(cx < 0 || cx >= 4) continue;
                const float wx = ix != 0 ? fx : 1.0f - fx;
                for(int iy = 0; iy <= 1; ++iy){
                    const int cy = cy0 + iy;
                    if(cy < 0 || cy >= 4) continue;
                    const float wy = iy != 0 ? fy : 1.0f - fy;
                    for(int ib = 0; ib <= 1; ++ib){
                        const int bin = (b0 + ib) % 8;
                        const float wb = ib != 0 ? fb : 1.0f - fb;
                        partial[thread * 128 + uint((cy * 4 + cx) * 8 + bin)] += weight * wx * wy * wb;
                    }
                }
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    //Columns summed in a fixed order: thread t owns values t and t + 64
    for(uint i = thread; i < 128; i += THREADS){
        float sum = 0.0f;
        for(uint t = 0; t < THREADS; ++t) sum += partial[t * 128 + i];
        partial[i] = sum;      // row 0 is only read by this column's owner from here on
    }
    GroupMemoryBarrierWithGroupSync();
    if(thread != 0) return;

    float histogram[128];
    for(uint i = 0; i < 128; ++i) histogram[i] = partial[i];
    const uint slot = key.slot;
    valid[slot] = 0;
    if(!normalise(histogram)) return;
    for(uint i = 0; i < 128; ++i) histogram[i] = min(histogram[i], params.clampAt);
    if(!normalise(histogram)) return;
    for(uint word = 0; word < 32; ++word){
        uint packed = 0;
        for(uint lane = 0; lane < 4; ++lane){
            const uint value = uint(min(255.0f, floor(histogram[word * 4 + lane] * 512.0f + 0.5f)));
            packed |= value << (lane * 8u);
        }
        descriptors[slot * 32 + word] = packed;
    }
    angles[slot] = angle;
    valid[slot] = 1;
}
