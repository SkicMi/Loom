// G3a: kompozicija splatova grubom silom - svaki piksel gleda svaki splat.
//
// Ovo NIJE rasterizator kojim ce se scena crtati. Ovo je referenca: cijena je broj piksela puta
// broj splatova, sto za pravu scenu nije upotrebljivo ni priblizno. Ali je ocito tocna, jer u
// njoj nema nijedne pločice, nijednog raspona i nijednog pomaka koji bi mogao biti krivo
// izracunat - samo petlja po redu kojim su splatovi dani.
//
// G3b pise pravi rasterizator s pločicama, a njegov test je da daje ISTU sliku kao ovaj.
//
// ALFA KOMPOZICIJA IDE SPRIJEDA NATRAG, i zato je poredak sve. Svaki splat uzima svoj dio od
// onoga sto je od svjetla ostalo, a ostatak (transmittance) prosljedjuje dalje:
//
//   C += T * alpha * boja
//   T *= (1 - alpha)
//
// Dvije granice koje nisu matematika nego mjera opreza, obje iz referentne implementacije:
// alfa se odsijeca na 0.99 da T nikad ne padne na tocnu nulu (u fazi T bi tamo nestao gradijent),
// i doprinos ispod 1/255 se preskace jer se ionako ne vidi u osam bita.
struct Params{
    uint2 size;
    uint count;
    uint padding0;
}

[[vk::push_constant]] ConstantBuffer<Params> params;

//Tri float4 po splatu: sredina i conic, pa conic.z s neprozirnoscu i dubinom, pa boja.
//Citaju se kao float4 jer je tako raspored u memoriji isti na obje strane bez pravila napamet
[[vk::binding(0,0)]] RWStructuredBuffer<float4> splats;
[[vk::image_format("rgba32f")]] [[vk::binding(1,0)]] RWTexture2D<float4> destination;

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint2 pixel = threadId.xy;
    if(pixel.x >= params.size.x || pixel.y >= params.size.y){
        return;
    }

    //Sredina piksela, ne njegov ugao. Pola piksela pomaka je pola piksela pomaka cijele slike
    const float2 point = float2(pixel) + float2(0.5, 0.5);

    float3 color = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;

    for(uint i = 0; i < params.count; ++i){
        const float4 centerConic = splats[i * 3 + 0];
        const float4 conicOpacityDepth = splats[i * 3 + 1];
        const float4 splatColor = splats[i * 3 + 2];

        const float2 offset = centerConic.xy - point;

        //Eksponent gaussiana: -0.5 * d' * conic * d, gdje je conic inverz kovarijance
        const float power = -0.5 * (centerConic.z * offset.x * offset.x
                                  + conicOpacityDepth.x * offset.y * offset.y)
                          - centerConic.w * offset.x * offset.y;

        //Pozitivan eksponent znaci da conic nije pozitivno definitan - takav splat nije elipsa
        if(power > 0.0){
            continue;
        }

        const float alpha = min(0.99, conicOpacityDepth.y * exp(power));
        if(alpha < 1.0 / 255.0){
            continue;
        }

        color += transmittance * alpha * splatColor.rgb;
        transmittance *= (1.0 - alpha);

        //Iza ovoga se vise nista ne vidi, pa se ostatak reda ne mora ni pogledati
        if(transmittance < 1e-4){
            break;
        }
    }

    //Alfa slike je koliko je svjetla zaustavljeno. Pozadina se time da umetnuti poslije, a i
    //trening je treba kao zasebnu velicinu
    destination[pixel] = float4(color, 1.0 - transmittance);
}
