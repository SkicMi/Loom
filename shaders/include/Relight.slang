//Ono sto oba relight prolaza dijele: sto je ploha i kako se do nje dolazi.
//
//Razlikuju se samo u tome sto rade s rezultatom - jedan crta svoje svjetlo, drugi ga dodaje
//na snimku koja je vec tu.

#include "Lighting.slang"

struct RelightData{
    //Tocke i normale su u view prostoru, jer je dubina iz koje su nastale bila. Svjetla su u
    //svjetskom. Ovo je most izmedu to dvoje, i jedini razlog zasto relight treba kameru
    float4x4 inverseView;

    //I natrag: trag sjene ide kroz sliku pozicija, koja je u view prostoru, pa se svjetlo
    //mora dovesti k njoj umjesto da se svaka tocka vodi u svjetski prostor
    float4x4 view;

    float4 baseColor;
    float4 surface;      //x = shininess, y = specularStrength
    float4 intrinsics;   //fx, fy, cx, cy - kojima se tocka vraca u piksel
    float4 shadow;       //x = koraka (0 = bez sjene), y = duljina, z = debljina, w = odmak
    float4 imageSize;    //xy
}

//Sjena trazena kroz samu sliku.
//
//Karte sjena ovdje nema i ne moze je biti: dubina daje LJUSKU, ne geometriju - postoji samo
//prednja ploha onoga sto se vidi. Zato se zaklon trazi hodanjem: od tocke prema svjetlu, u
//koracima, i na svakom se koraku pogleda sto u slici stoji na tom mjestu. Ako je ono blize
//kameri od nase tocke, nesto je izmedu.
//
//Granica je posteno reci: sjenu baca samo ono sto je U KADRU. Predmet iza kamere ili izvan
//ruba ne postoji ni u jednoj slici, pa ni ne moze zakloniti. To je granica postupka, ne
//njegova greska - i to je razlog zasto se ovo zove kontaktna sjena a ne sjena.
struct ScreenTrace : IOcclusion{
    float3 point;        //tocka koja se sjenca, u view prostoru
    float2 pixel;        //gdje je ona na ekranu - za razbijanje pruga
    RelightData data;

    //Vraca piksel u kojem bi se view tocka vidjela. Isto sto i odprojekcija, samo unatrag
    float2 project(float3 view){
        float depth = -view.z;
        return float2(data.intrinsics.x * view.x / depth + data.intrinsics.z,
                      data.intrinsics.y * view.y / depth + data.intrinsics.w);
    }

    float visibility(GpuLight light, float3 worldPos){
        int steps = int(data.shadow.x);
        if(steps <= 0) return 1.0;

        float3 toLight;
        float reach = data.shadow.y;

        if(light.positionOrDirection.w == 0.0){
            //Usmjereno svjetlo: smjer je isti svugdje, pa se hoda zadanu duljinu
            toLight = normalize(-mul(data.view, float4(light.positionOrDirection.xyz, 0.0)).xyz);
        }
        else{
            float3 lightView = mul(data.view, float4(light.positionOrDirection.xyz, 1.0)).xyz;
            float3 away = lightView - point;
            float distance = length(away);
            if(distance < 1e-4) return 1.0;

            toLight = away / distance;

            //Dalje od svjetla nema sto zaklanjati - hodati preko njega znaci naci zaklon
            //koji je iza izvora
            reach = min(reach, distance);
        }

        float stride = reach / float(steps);

        //Deterministicko razbijanje pruga: bez pomaka bi se svi pikseli uzorkovali na istim
        //udaljenostima i rub sjene bi bio stepenica. Hash po pikselu, ne slucajan broj - ista
        //slika dva puta mora dati istu sjenu
        float dither = frac(sin(dot(pixel, float2(12.9898, 78.233))) * 43758.5453);

        for(int i = 0; i < steps; ++i){
            float t = (float(i) + dither) * stride + data.shadow.w;
            float3 sample = point + toLight * t;

            //Iza kamere se ne da projicirati
            if(sample.z > -1e-3) break;

            float2 at = project(sample);
            if(at.x < 0.0 || at.y < 0.0 || at.x >= data.imageSize.x || at.y >= data.imageSize.y){
                //Izaslo iz kadra: ono sto je vani ne postoji ni u jednoj slici
                break;
            }

            float4 stored = positionMap.SampleLevel(at / data.imageSize.xy, 0.0);
            if(stored.w < 0.5) continue;   //nebo: nema plohe koja bi zaklanjala

            //Koliko je zapisana ploha BLIZE kameri od tocke do koje smo dohodali. Pozitivno
            //znaci da je nesto izmedu nas i svjetla.
            //
            //Predznak je ovdje jedina stvar koja odlucuje: obrnut, sve je u sjeni - i sto je
            //gore, provjera "unutar sjene je tamno" tada prolazi iz krivog razloga
            float inFront = (-sample.z) - (-stored.z);

            //Ploha je pred nama, ali ne predaleko. Bez gornje granice bi svaka bila
            //beskonacno duboka i zaklanjala sve iza sebe do kraja scene - a mi vidimo samo
            //njeno lice i o njenoj debljini ne znamo nista
            if(inFront > data.shadow.w && inFront < data.shadow.z) return 0.0;
        }

        return 1.0;
    }
}

[[vk::binding(0,1)]] Sampler2D positionMap;
[[vk::binding(1,1)]] Sampler2D normalMap;

struct VSOutput{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
}

//Ploha pod ovim pikselom, prebacena u svjetski prostor. exists je false gdje plohe nema
struct Surface{
    float3 worldPos;
    float3 viewPos;      //ista tocka prije pretvorbe - trag sjene hoda u ovom prostoru
    float3 N;
    float3 V;
    bool exists;
}

Surface surfaceAt(float2 texCoord, float4x4 inverseView){
    Surface out;

    float4 point = positionMap.SampleLevel(texCoord, 0.0);
    float4 normal = normalMap.SampleLevel(texCoord, 0.0);

    out.exists = point.w > 0.5 && normal.w > 0.5;
    out.viewPos = point.xyz;
    if(!out.exists){
        out.worldPos = float3(0,0,0);
        out.N = float3(0,0,1);
        out.V = float3(0,0,1);
        return out;
    }

    out.worldPos = mul(inverseView, float4(point.xyz, 1.0)).xyz;

    //Normala je smjer, ne mjesto: w = 0, pa je translacija ne dira
    out.N = normalize(mul(inverseView, float4(normal.xyz, 0.0)).xyz);
    out.V = normalize(frame.cameraPosition.xyz - out.worldPos);
    return out;
}
