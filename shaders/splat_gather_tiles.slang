// Binning, prolaz 3: pločice preslozene onim redom koji je dao sort po dubini.
//
// Prvi sort je premjestao parove po dubini i pritom nosio samo svoj indeks. Da bi drugi sort
// mogao sortirati po pločici, kljucevi pločica moraju stajati u tom NOVOM poretku - inace bi
// drugi sort sortirao stare kljuceve uz nove vrijednosti.
struct Params{
    uint pairCount;
    uint padding0;
    uint padding1;
    uint padding2;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> tileKeys;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> sortValues;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> gathered;

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint index = threadId.x;
    if(index >= params.pairCount){
        return;
    }
    gathered[index] = tileKeys[sortValues[index]];
}
