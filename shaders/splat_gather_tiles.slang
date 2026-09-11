// Binning, prolaz 3: pločice preslozene onim redom koji je dao sort po dubini.
//
// Prvi sort je premjestao parove po dubini i pritom nosio samo svoj indeks. Da bi drugi sort
// mogao sortirati po pločici, kljucevi pločica moraju stajati u tom NOVOM poretku - inace bi
// drugi sort sortirao stare kljuceve uz nove vrijednosti.
[[vk::binding(0,0)]] RWStructuredBuffer<uint> tileKeys;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> sortValues;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> gathered;
[[vk::binding(3,0)]] RWStructuredBuffer<uint> pairSizes;   //[0] broj parova, upisala ga je kartica - vidi splat_pair_sizes.slang

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint index = threadId.x;
    if(index >= pairSizes[0]){
        return;
    }
    gathered[index] = tileKeys[sortValues[index]];
}
