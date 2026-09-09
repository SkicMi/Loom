// Test specijalizacijskih konstanti: shader koji ne zna svoju velicinu dok se pipeline ne stvori.
//
// GROUP je i velicina radne grupe i broj koji shader zapisuje. Zato se iz rezultata vidi dvoje
// odjednom: da je konstanta stigla do koda, i da je stigla do same radne grupe - a to su dvije
// razlicite stvari, jer prvo bi radilo i s push constantom a drugo ne bi.
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
    //Koliko je dretvi stvarno pokrenuto: svaka broji jednu. Ovo je jedini nacin da se izmjeri
    //velicina radne grupe iznutra, jer je ona za shader konstanta a ne podatak
    InterlockedAdd(results[params.slot * 2 + 0], 1u);

    //I sto konstanta kaze da jest. Jedna dretva je dovoljna
    if(threadId.x == 0){
        results[params.slot * 2 + 1] = GROUP;
    }
}
