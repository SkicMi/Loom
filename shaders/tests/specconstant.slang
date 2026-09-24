// Test of specialization constants: a shader that does not know its own size until the pipeline
// is created.
//
// GROUP is both the workgroup size and the value the shader writes out. The result proves two
// things at once: that the constant reached the code, and that it reached the actual workgroup -
// two different things, because the first would also work with a push constant and the second
// would not.
[[vk::constant_id(0)]] const uint GROUP = 8;

struct Params{
    uint slot;
    uint padding0;
    uint padding1;
    uint padding2;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> results;

[shader("compute")]
[numthreads(GROUP, 1, 1)]
void computeMain(uint3 threadId : SV_GroupThreadID){
    //How many threads were actually launched: each counts one. This is the only way to measure
    //the workgroup size from inside, because to the shader it is a constant, not data
    InterlockedAdd(results[params.slot * 2 + 0], 1u);

    //And what the constant claims it is. One thread is enough
    if(threadId.x == 0){
        results[params.slot * 2 + 1] = GROUP;
    }
}
