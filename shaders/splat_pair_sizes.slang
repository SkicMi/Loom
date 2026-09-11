// Broj parova i velicine dispatcha, izracunati na kartici.
//
// Broj parova zna se tek kad PrefixSum zbroji pločice po splatu - a to se dogadja NA KARTICI, jer
// je tamo i priprema. Sve iza toga (dva sorta, skupljanje, rasponi) treba taj broj za velicinu
// dispatcha. Da ga procesor procita natrag, kadar bi stao na pola i cekao; umjesto toga ovaj
// shader upise velicine u buffer, a vkCmdDispatchIndirect ih cita izravno odande.
//
// BROJ SE OGRANICAVA NA maxPairs, i ovo je jedino mjesto gdje se to radi. Svi bufferi parova su
// toliki: sirenje (splat_expand) ne pise iza te granice, pa ni sortovi ne smiju gledati dalje.
// Neograniceni broj ostaje zapisan, da procesor poslije kadra moze reci da scena ne stane.
//
// RASPORED JE UGOVOR S TRI STRANE - SplatRenderer.h (slotovi), RadixSort::sortIndirect (broj,
// blokovi, trojka) i splat_gather_tiles/splat_ranges (broj u [0]):
//
//   [0] broj parova, ograniceni     [1] radix blokovi     [2..4] radix dispatch: blokovi, 1, 1
//   [5..7] dispatch po grupama: grupe, 1, 1
//   [8] broj parova koji je scena trazila, neograniceni

struct Params{
    uint maxPairs;
    uint groupSize;        //dretvi po grupi u skupljanju i rasponima
    uint radixBlockSize;   //RadixSort::elementsPerBlock - salje ga C++, da ne nastane treci primjerak
    uint padding0;
}

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0,0)]] RWStructuredBuffer<uint> total;   //PrefixSum::getTotal
[[vk::binding(1,0)]] RWStructuredBuffer<uint> sizes;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain(){
    const uint requested = total[0];
    const uint pairs = min(requested, params.maxPairs);

    const uint blocks = (pairs + params.radixBlockSize - 1) / params.radixBlockSize;
    const uint groups = (pairs + params.groupSize - 1) / params.groupSize;

    sizes[0] = pairs;
    sizes[1] = blocks;
    sizes[2] = blocks;
    sizes[3] = 1;
    sizes[4] = 1;
    sizes[5] = groups;
    sizes[6] = 1;
    sizes[7] = 1;
    sizes[8] = requested;
}
