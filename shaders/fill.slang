struct FillParams{
    uint count;
}

[[vk::push_constant]] ConstantBuffer<FillParams> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(64,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    uint index = threadId.x;
    if(index >= params.count){
        return;
    }
    output[index] = index * 2654435761u;
}
