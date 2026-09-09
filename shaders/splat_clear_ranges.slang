// Rasponi se ciste svaki kadar. Pločica koju nijedan splat ne dira mora ostati prazna, a ne
// zadrzati ono sto je u njoj pisalo prosli put - kamera se pomakla, scena nije.
struct Params{
    uint tileCount;
    uint padding0;
    uint padding1;
    uint padding2;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> ranges;

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint index = threadId.x;
    if(index >= params.tileCount){
        return;
    }
    ranges[index * 2 + 0] = 0;
    ranges[index * 2 + 1] = 0;
}
