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
    float4 shadowSlope;  //x = rast odmaka, y = slobodno, z = rast debljine, w = prednji pojas
    float4 occlusion;    //x = jacina, y = blizi prsten (px), z = dalji prsten (px), w = mjerilo (m)
    float4 occluder;     //x = samo maska zaklanja, y = polumjer glacanja zaklona (px),
                         //z = koliko zraka, w = polumjer svjetla (m)
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
//Srednja od pet vrijednosti, po poznatom identitetu:
//    median5 = median3( max(min(a,b), min(c,d)), min(max(a,b), max(c,d)), e )
//
//Medijan a ne prosjek: prosjek preko siluete pomijesa covjeka i zid dva metra iza njega pa
//izmisli kosinu koje nema, dok medijan uzme onu stranu koje ima vise
float median3(float x, float y, float z){
    return max(min(x, y), min(max(x, y), z));
}

float median5(float a, float b, float c, float d, float e){
    return median3(max(min(a, b), min(c, d)), min(max(a, b), max(c, d)), e);
}

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

    //Jedan trag prema jednoj tocki svjetla. Vraca 0 ili 1 - namjerno tvrdo, jer je tvrda
    //odluka jedina koju se da usporediti s analitickom geometrijom do zadnjeg piksela
    float march(float3 toLight, float reach, float lightDepth, float dither){
        int steps = int(data.shadow.x);
        float stride = reach / float(steps);
        float growth = data.shadowSlope.x;

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

            float2 uv = at / data.imageSize.xy;

            //TKO SMIJE ZAKLANJATI. Bez maske zaklon je sve sto se vidi, pa i zid sam sebi
            if(data.occluder.x > 0.5 && occluderMask.SampleLevel(uv, 0.0).r < 0.5) continue;

            float4 stored = positionMap.SampleLevel(uv, 0.0);
            if(stored.w < 0.5) continue;   //nebo: nema plohe koja bi zaklanjala

            //GEOMETRIJA KOJA BACA SJENU NIJE ISTA KOJA SE SJENCA: tragu je nabor na majici
            //samo mrlja, pa smije citati medijan pet uzoraka umjesto same tocke
            float storedDistance = -stored.z;

            if(data.occluder.y > 0.0){
                float radius = data.occluder.y;
                float2 stepX = float2(radius, 0.0) / data.imageSize.xy;
                float2 stepY = float2(0.0, radius) / data.imageSize.xy;

                float4 left = positionMap.SampleLevel(uv - stepX, 0.0);
                float4 right = positionMap.SampleLevel(uv + stepX, 0.0);
                float4 up = positionMap.SampleLevel(uv - stepY, 0.0);
                float4 down = positionMap.SampleLevel(uv + stepY, 0.0);

                float far = 1e6;
                storedDistance = median5(storedDistance,
                    left.w  > 0.5 ? -left.z  : far,
                    right.w > 0.5 ? -right.z : far,
                    up.w    > 0.5 ? -up.z    : far,
                    down.w  > 0.5 ? -down.z  : far);
            }

            //Koliko je zapisana ploha BLIZE kameri od tocke do koje smo dohodali. Predznak je
            //ovdje jedina stvar koja odlucuje: obrnut, sve je u sjeni
            float inFront = (-sample.z) - storedDistance;

            //Odmak raste s putem, jer lazni zaklon raste s njim
            float bias = data.shadow.w + t * growth;

            //Sto se dalje odhodalo, to se manje zna o tome sto je iza uzorkovane plohe
            float thickness = data.shadow.z * (1.0 + (t / reach) * data.shadowSlope.z);

            //Ploha blize kameri od svjetla ne moze zaklanjati - svjetlo je ispred nje
            bool ahead = true;
            if(data.shadowSlope.w > 0.0 && lightDepth > 0.0){
                ahead = (storedDistance - lightDepth) > 0.5 * data.shadowSlope.w;
            }

            if(ahead && inFront > bias && inFront < thickness) return 0.0;
        }

        return 1.0;
    }

    float visibility(GpuLight light, float3 worldPos){
        int steps = int(data.shadow.x);
        if(steps <= 0) return 1.0;

        //Deterministicko razbijanje pruga: bez pomaka bi se svi pikseli uzorkovali na istim
        //udaljenostima i rub sjene bi bio stepenica. Hash po pikselu, ne slucajan broj - ista
        //slika dva puta mora dati istu sjenu
        float dither = frac(sin(dot(pixel, float2(12.9898, 78.233))) * 43758.5453);

        float lightDepth = (light.positionOrDirection.w == 0.0) ? -1.0
            : -mul(data.view, float4(light.positionOrDirection.xyz, 1.0)).z;

        //SVJETLO KAO PLOHA, NE KAO TOCKA.
        //
        //Tocka daje samo tvrdu sjenu; polusjena postoji zato sto se dio izvora vidi a dio ne.
        //Zato se hoda vise puta, svaki put prema DRUGOJ tocki na disku polumjera lightRadius,
        //i broji koliko ih je stiglo. Jezgra ostaje potpuno tamna jer nijedna zraka ne prode,
        //a rub dobiva sirinu koja raste s velicinom svjetla - dakle iz fizike, ne iz koraka.
        //
        //Ovo si smijemo priustiti jer slika smije trajati: realtime bi platio N tragova po
        //pikselu, nama je to jedina cijena
        int rays = max(int(data.occluder.z), 1);
        float lightRadius = data.occluder.w;

        float3 toLight;
        float reach = data.shadow.y;
        float distance = 0.0;

        if(light.positionOrDirection.w == 0.0){
            //Usmjereno svjetlo: smjer je isti svugdje, pa se hoda zadanu duljinu
            toLight = normalize(-mul(data.view, float4(light.positionOrDirection.xyz, 0.0)).xyz);
        }
        else{
            float3 lightView = mul(data.view, float4(light.positionOrDirection.xyz, 1.0)).xyz;
            float3 away = lightView - point;
            distance = length(away);
            if(distance < 1e-4) return 1.0;

            toLight = away / distance;

            //Dalje od svjetla nema sto zaklanjati - hodati preko njega znaci naci zaklon
            //koji je iza izvora
            reach = min(reach, distance);
        }

        if(rays <= 1 || lightRadius <= 0.0){
            return march(toLight, reach, lightDepth, dither);
        }

        //Dvije osi okomite na smjer prema svjetlu, da se disk moze razapeti
        float3 helper = abs(toLight.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        float3 axisU = normalize(cross(helper, toLight));
        float3 axisV = cross(toLight, axisU);

        float reached = 0.0;

        for(int ray = 0; ray < rays; ++ray){
            //Vogelov spiralni raspored: tocke po disku ravnomjerno, bez slucajnih brojeva.
            //Pomak po pikselu i dalje razbija pruge, a ista slika dva puta daje istu sjenu
            float index = float(ray) + 0.5;
            float radius = lightRadius * sqrt(index / float(rays));
            float angle = index * 2.39996323 + dither * 6.28318531;

            float3 offset = axisU * (radius * cos(angle)) + axisV * (radius * sin(angle));

            //Svaka zraka krece s DRUGOG mjesta unutar koraka. Bez toga svih N tragova
            //uzorkuje na istim udaljenostima, pa im se greska kvantizacije poklapa i zbroj
            //je jednako zrnat kao jedan trag - disk se rasprsi, a korak ne

            //Zraka prema pomaknutoj tocki na izvoru. Za usmjereno svjetlo je pomak kutni, pa
            //se polumjer cita kao tangens - izvor je beskonacno dalek i nema ga se gdje pomaci
            float3 rayDirection = (light.positionOrDirection.w == 0.0)
                ? normalize(toLight + offset)
                : normalize(toLight * distance + offset);

            reached += march(rayDirection, reach, lightDepth,
                             frac(dither + index / float(rays)));
        }

        return reached / float(rays);
    }
}

[[vk::binding(0,1)]] Sampler2D positionMap;
[[vk::binding(1,1)]] Sampler2D normalMap;

//Tko smije zaklanjati. Bijelo znaci "svatko", pa je slika koja se ne postavi bijeli piksel i
//sve ostaje kako je bilo
[[vk::binding(2,1)]] Sampler2D occluderMask;

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

//AMBIJENTNA OKLUZIJA IZ SAME KARTE DUBINE.
//
//Ono sto ubaceno svjetlo cini SJEDECIM: bez nje predmet lebdi nad pozadinom, jer nista ne
//potamni tamo gdje se dodiruju. Ne treba joj svjetlo - racuna se iz geometrije.
//
//Racuna se ovdje, u fragment shaderu, a ne u zasebnu teksturu. TypeGPU je preracunava jer
//mora stati u osam milisekundi; nama slika smije trajati, pa 32 dodatna uzorka po pikselu
//stedi cijelu jednu sliku, njen binding i njeno odrzavanje.
//
//Dva prstena: blizi hvata dodir, dalji sjenu udubine. Susjed se broji utoliko koliko je
//BLIZE (cleared) i utoliko koliko je jos uvijek u dodiru (contact) - drugo bez prvog bi
//svaku daleku pozadinu proglasilo zaklonom
float ambientOcclusion(float2 texCoord, float4 settings, float2 imageSize){
    if(settings.x <= 0.0) return 0.0;

    float4 centre = positionMap.SampleLevel(texCoord, 0.0);
    if(centre.w < 0.5) return 0.0;

    float centreDistance = -centre.z;
    float scale = max(settings.w, 1e-4);

    //Prag ispod kojeg je razlika sum, i domet preko kojeg susjed vise nije u dodiru. Oboje
    //vezano uz mjerilo, pa se namjesta jedan broj a ne tri
    float floorDistance = 0.1 * scale;
    float range = 4.0 * scale;

    float total = 0.0;
    float count = 0.0;

    for(int ring = 0; ring < 2; ++ring){
        float radius = (ring == 0) ? settings.y : settings.z;

        for(int step = 0; step < 8; ++step){
            float angle = float(step) * 0.7853981634;   //osmina kruga
            float2 offset = float2(cos(angle), sin(angle)) * radius;

            float4 neighbour = positionMap.SampleLevel(texCoord + offset / imageSize, 0.0);
            if(neighbour.w < 0.5) continue;

            //Pozitivno znaci da je susjed BLIZE kameri, dakle da nesto strsi prema nama
            float difference = centreDistance - (-neighbour.z);

            float contact = 1.0 - saturate(abs(difference) / range);
            float cleared = max(difference - floorDistance, 0.0);

            total += saturate(cleared / scale) * contact;
            count += 1.0;
        }
    }

    if(count < 0.5) return 0.0;
    return saturate(settings.x * total / count);
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
