// Binning, prolaz 4: gdje u sortiranom polju pocinje i zavrsava koja pločica.
//
// Nakon drugog sorta parovi su poredani po pločici, pa unutar pločice po dubini. Rub se
// prepoznaje usporedbom sa susjedom: gdje se kljuc promijeni, jedna pločica zavrsava a druga
// pocinje. Prvi i zadnji element su rubovi po definiciji.
//
// Pločice koje nijedan splat ne dira ostaju na nuli, pa im je raspon prazan - zato se polje
// mora ocistiti prije ovoga.
[[vk::binding(0,0)]] RWStructuredBuffer<uint> sortedTiles;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> ranges;   //dva uinta po pločici: pocetak, kraj
[[vk::binding(2,0)]] RWStructuredBuffer<uint> pairSizes;   //[0] broj parova, upisala ga je kartica - vidi splat_pair_sizes.slang

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint index = threadId.x;
    if(index >= pairSizes[0]){
        return;
    }

    const uint tile = sortedTiles[index];

    if(index == 0 || sortedTiles[index - 1] != tile){
        ranges[tile * 2 + 0] = index;
    }
    if(index + 1 == pairSizes[0] || sortedTiles[index + 1] != tile){
        ranges[tile * 2 + 1] = index + 1;
    }
}
