//Normale iz slike tocaka.
//
//Ploha koja prolazi kroz tri susjedne tocke ima normalu okomitu na obje njene tangente, pa
//je racun sam po sebi jedno vektorsko mnozenje. Sav posao je u tome KOJEG susjeda uzeti.
//
//Naivna razlika uvijek gleda naprijed, i na silueti - gdje se blizi objekt dodiruje s
//dalekom pozadinom - uzme susjeda koji je na DRUGOJ plohi. Tangenta tada odleti u daljinu i
//normala ispadne nagnuta za desetke stupnjeva. To se u relightu vidi kao svijetla nit oko
//svakog ruba, i nijedno filtriranje je poslije ne mice.
//
//Zato se za svaku os gleda na obje strane i uzima onaj susjed koji je po dubini BLIZI
//sredistu - taj je gotovo sigurno na istoj plohi. Cetiri uzorka umjesto dva, bez ijednog
//praga koji bi se morao namjestati.

struct Push{
    uint2 size;
}

[[vk::push_constant]] ConstantBuffer<Push> push;

[[vk::binding(0,0)]] Sampler2D positionMap;

[[vk::image_format("rgba32f")]]
[[vk::binding(1,0)]] RWTexture2D<float4> normalMap;

float4 fetch(int2 pixel){
    //Izvan slike NEMA susjeda, i to se mora reci naglas. Stezanje na rub vratilo bi samo
    //srediste natrag: razlika prema njemu je nula, nula pobjeduje izbor "blizi po dubini",
    //i tangenta ispadne nulti vektor - pa cijeli rub slike ostane bez normale
    if(pixel.x < 0 || pixel.y < 0 ||
       pixel.x >= int(push.size.x) || pixel.y >= int(push.size.y)){
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float2 uv = (float2(pixel) + 0.5) / float2(push.size);
    return positionMap.SampleLevel(uv, 0.0);
}

//Tangenta u smjeru rastuceg piksela, uzeta s one strane koja je na istoj plohi.
//valid izlazi false samo ako nijedna strana nije ploha
float3 tangentAlong(float3 center, float4 back, float4 forward, out bool valid){
    bool hasBack = back.w > 0.5;
    bool hasForward = forward.w > 0.5;

    valid = hasBack || hasForward;
    if(!valid) return float3(0,0,0);

    if(hasBack && hasForward){
        //Manja razlika u dubini znaci istu plohu. Na ravnoj plohi su obje jednake i izbor
        //ne mijenja nista - razlika se vidi tocno tamo gdje je i vazna
        float backStep = abs(back.z - center.z);
        float forwardStep = abs(forward.z - center.z);
        return backStep < forwardStep ? (center - back.xyz) : (forward.xyz - center);
    }

    return hasForward ? (forward.xyz - center) : (center - back.xyz);
}

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    if(id.x >= push.size.x || id.y >= push.size.y) return;

    int2 pixel = int2(id.xy);
    float4 center = fetch(pixel);

    //Nema plohe - nema ni normale. Pozadina se ne osjencava
    if(center.w < 0.5){
        normalMap[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    bool haveX, haveY;
    float3 alongX = tangentAlong(center.xyz, fetch(pixel + int2(-1,0)), fetch(pixel + int2(1,0)), haveX);
    float3 alongY = tangentAlong(center.xyz, fetch(pixel + int2(0,-1)), fetch(pixel + int2(0,1)), haveY);

    if(!haveX || !haveY){
        normalMap[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    //Redoslijed je alongY pa alongX, a ne obrnuto: piksel raste udesno i PREMA DOLJE, dok
    //view prostor ima Y prema gore. Ovim redom ploha okrenuta prema kameri dobije (0,0,1)
    float3 normal = cross(alongY, alongX);

    float length = sqrt(dot(normal, normal));
    if(length < 1e-12){
        //Dvije tangente su iste crte - kroz njih prolazi beskonacno ploha, pa nijedna nije ta
        normalMap[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    normalMap[id.xy] = float4(normal / length, 1.0);
}
