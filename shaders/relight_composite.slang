//Svjetlo ubaceno U SNIMKU.
//
//Ovo je ono zbog cega cijela grana postoji. Snimka je vec osvijetljena necim, mi joj dodajemo
//jos jedno svjetlo, i rezultat mora izgledati kao da je to svjetlo bilo tamo kad se snimalo.
//
//Snimka je pritom DVIJE stvari odjednom: pocetna vrijednost (ono sto se vec vidi) i albedo
//(boja kojom ploha odbija novo svjetlo). To drugo je aproksimacija, i treba je reci naglas:
//piksel snimke nije albedo nego albedo pomnozen starim osvjetljenjem. Dok albedo ne dolazi
//iz svoje slike, novo svjetlo je zato mjestimicno pretamno tamo gdje je staro bilo tamno.
//
//Ono sto ta aproksimacija dobiva, i zbog cega je vrijedna: crno ostaje crno. Ploha koja u
//snimci ne odbija nista ne moze zasvijetliti ni pod kakvim novim svjetlom, a to je greska
//koja se u naivnom zbrajanju vidi prva.
//
//Vertex stage dolazi iz relight.slang - trokut preko ekrana je isti trokut.

#include "include/Relight.slang"

[[vk::binding(2,1)]] Sampler2D plate;
[[vk::binding(3,1)]] ConstantBuffer<RelightData> relight;

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    float4 existing = plate.SampleLevel(input.texCoord, 0.0);
    Surface surface = surfaceAt(input.texCoord, relight.inverseView);

    //Nema plohe - nema sto osvijetliti. Snimka prolazi nedirnuta, pa je ovaj prolaz cijela
    //slika i ne trazi da je itko prije njega ispunio
    if(!surface.exists){
        return existing;
    }

    //baseColor mnozi snimku, pa se albedo moze prigusiti bez diranja same snimke
    float3 base = existing.rgb * relight.baseColor.rgb;

    //Zaklon se trazi kroz samu sliku - jedina sjena koju snimka moze dati
    ScreenTrace trace;
    trace.point = surface.viewPos;
    trace.pixel = input.texCoord * relight.imageSize.xy;
    trace.data = relight;

    //Krece se OD snimke, ne zbraja na kraju: isti redoslijed zbrajanja kao da je scena
    //crtana odjednom, pa se rezultat da usporediti do zadnjeg bita
    float3 lit = accumulateLight(existing.rgb, base, surface.worldPos, surface.N, surface.V,
                                 relight.surface.x, relight.surface.y, relight.surface.z, trace);

    return float4(lit, existing.a);
}
