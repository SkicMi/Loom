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

    //Koliko je piksela daleko susjed iz kojeg se racuna nagib.
    //
    //Jedan je tocno za dubinu koju je nacrtao rasterizator - ona je glatka do piksela. Karta
    //iz modela nije: ona je procjena, i na skali jednog piksela je sum. Nagib racunat iz
    //susjednih piksela taj sum pojacava, pa normale poskakuju i ploha koja je glatka ispadne
    //zrnata - na licu se to vidi kao da je ispikselirano.
    //
    //Siri razmak to smiruje, i gubi tocno onoliko sitnog detalja koliko ga u procjeni ionako
    //nije bilo
    int radius;

    //Koliki nagib model izmislja na plohi koja je RAVNA, bezdimenzionalno (dz po dx).
    //
    //Mjeri se, ne pogada: nad ravnim zidom sintetskog portreta Depth Anything V2 small daje
    //RMS 0.20 i 95. postotak 0.30, dok je istina 0.0000 tocno. Sirenje stencila protiv toga
    //ne pomaze nista (greska normale 10.46 stupnjeva na radiusu 3, 10.41 na radiusu 7) jer
    //sum nije bijeli po pikselu nego niskofrekventna struktura - prosjecivanje je ne dira.
    //
    //Zato se ne mazi nego se oduzima ENERGIJA: sqrt(max(m^2 - noise^2, 0)). Nagib velicine
    //samog suma padne na nulu, a pravi nagib od 1.0 se sa 0.30 smanji na 0.954 - izmisljeni
    //reljef nestane, a oblik ostane.
    //
    //Koliko oduzeti kaze SUBJEKT, ne zid: zid se s vecim brojem samo sve vise izravnava, ali
    //greska na subjektu ima minimum i tamo se vidi gdje je pretjerano. Mjereno protiv pravih
    //normala, radius 3:
    //    noise  0.00  0.20  0.30  0.35  0.50  0.70  1.00
    //    zid   10.46  3.30  0.68  0.29  0.06  0.01  0.00
    //    lice  10.51  9.62  9.26  9.28  9.84 11.69 16.13
    //Minimum je na 0.30, a to je bas 95. postotak izmisljenog nagiba - pa je pravilo
    //"oduzmi 95. postotak onoga sto model izmislja na ravnom", ne RMS
    float noiseSlope;

    //Gornja granica nagiba, mekano preko tanh. Na silueti razlika dubine preko dva piksela
    //odleti u desetke, pa normala legne skoro vodoravno i rub zasvijetli. Nula iskljucuje
    float slopeLimit;
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

    int step = max(push.radius, 1);

    bool haveX, haveY;
    float3 alongX = tangentAlong(center.xyz, fetch(pixel + int2(-step,0)), fetch(pixel + int2(step,0)), haveX);
    float3 alongY = tangentAlong(center.xyz, fetch(pixel + int2(0,-step)), fetch(pixel + int2(0,step)), haveY);

    if(!haveX || !haveY){
        normalMap[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    //Redoslijed je alongY pa alongX, a ne obrnuto: piksel raste udesno i PREMA DOLJE, dok
    //view prostor ima Y prema gore. Ovim redom ploha okrenuta prema kameri dobije (0,0,1)
    float3 normal = cross(alongY, alongX);

    float size = sqrt(dot(normal, normal));
    if(size < 1e-12){
        //Dvije tangente su iste crte - kroz njih prolazi beskonacno ploha, pa nijedna nije ta
        normalMap[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    normal = normal / size;

    //Normala i nagib su ista stvar zapisana dvaput: N = normalize(float3(slope, 1)). Zato se
    //ovdje ide u nagib, dira mu se samo DULJINA, i vraca natrag - smjer se ne pomice ni za
    //koliko, a rastav je tocan pa je bez diranja identitet.
    //
    //Ploha gotovo okrenuta na bok (mali z) se preskace: tamo nagib tezi u beskonacno i
    //rastav vise nije upotrebljiv
    if((push.noiseSlope > 0.0 || push.slopeLimit > 0.0) && normal.z > 1e-3){
        float2 slope = normal.xy / normal.z;
        float magnitude = sqrt(dot(slope, slope));

        if(magnitude > 1e-9){
            float shrunk = sqrt(max(magnitude * magnitude - push.noiseSlope * push.noiseSlope, 0.0));
            if(push.slopeLimit > 0.0) shrunk = push.slopeLimit * tanh(shrunk / push.slopeLimit);

            normal = normalize(float3(slope * (shrunk / magnitude), 1.0));
        }
    }

    normalMap[id.xy] = float4(normal, 1.0);
}
