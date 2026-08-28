//Osvjetljenje: sve sto je zajednicko svakome tko sjenca plohu.
//
//Stajalo je dvaput, doslovno prepisano, u triangle.slang i textured.slang - devedeset redaka
//koji se moraju slagati u zadnjem bitu da bi dvije slike iste scene bile ista slika. Sada
//stoji jednom, i relight nad G-bufferom racuna istim kodom kao i crtez preko mesha.
//
//To nije urednost nego tvrdnja koja se inace ne bi dala dokazati: "isto svjetlo" znaci ISTI
//KOD, a ne dva koda koja rade isto.


struct FrameData{
    float4x4 view;
    float4x4 projection;
    float4 cameraPosition;
    float4 ambientColor;
    uint lightCount;
    uint padding0;
    uint padding1;
    uint padding2;
}

struct GpuLight{
    float4 positionOrDirection;
    float4 color;
    //x = range, y = kind of shadow (0 none, 1 flat map, 2 cube), z = bias, w = cube near
    float4 params;
    //x = which map of that kind, in the arrays on set 0
    float4 shadow;
    float4x4 lightViewProjection;
}

[[vk::binding(0,0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1,0)]] StructuredBuffer<GpuLight> lights;

//The shadow map, as a comparison sampler on set 0. SampleCmpLevelZero does not return a
//depth - it returns how much of the 2x2 neighbourhood passed "is my reference nearer than
//what is stored here", filtered by the same hardware that does bilinear. That free 2x2 is
//why a shadow edge is not a staircase
//Velicina dolazi iz CMakea kroz -D, isti broj koji vidi i C++. Prije je stajala ovdje kao
//literal uz komentar da se mora poklapati - a da se raziđe, pipeline bi se svejedno preveo
[[vk::binding(2,0)]] Sampler2DShadow shadowMaps[LOOM_MAX_SHADOW_MAPS];

//1.0 fully lit, 0.0 fully shadowed. A light without a map bound (params.y == 0) returns 1.0
//without ever touching the sampler
float shadowVisibility(GpuLight light, float3 worldPos){
    if(light.params.y < 0.5) return 1.0;

    float4 clip = mul(light.lightViewProjection, float4(worldPos, 1.0));
    float3 proj = clip.xyz / clip.w;

    //Behind the light's far plane there is no map to consult, and the border is opaque white
    //so anything outside the box sideways already compares as lit
    if(proj.z > 1.0) return 1.0;

    float2 uv = proj.xy * 0.5 + 0.5;

    //The bias is subtracted from the reference, not added to the stored depth: a fragment
    //has to be nearer than the map by this much before it counts as lit
    //Indeks dolazi iz svjetla, a petlja po svjetlima ide jednako u svakoj invokaciji, pa je
    //dinamicki uniforman - sto je tocno ono sto shaderSampledImageArrayDynamicIndexing trazi
    return shadowMaps[int(light.shadow.x)].SampleCmpLevelZero(uv, proj.z - light.params.z);
}

//A point light's shadow cube. There is no matrix here on purpose: a cube is looked up with
//a direction, and the reference depth is rebuilt from how far along its longest axis the
//fragment is. That is the same number the face wrote, arrived at by arithmetic instead of
//by another matrix multiply
[[vk::binding(3,0)]] SamplerCubeShadow shadowCubes[LOOM_MAX_SHADOW_CUBES];

float cubeVisibility(GpuLight light, float3 worldPos){
    float3 toFragment = worldPos - light.positionOrDirection.xyz;
    float3 magnitude = abs(toFragment);

    //Whichever face this direction lands on, that face's depth axis is the largest component
    float axis = max(magnitude.x, max(magnitude.y, magnitude.z));

    float near = max(light.params.w, 1e-4);
    float far = max(light.params.x, near * 2.0);

    //Closer than the near plane there is no map, and nothing that close is occluded anyway
    if(axis <= near) return 1.0;

    //The same value a 90 degree projection with these planes writes, worked out by hand:
    //ndcZ = far * (axis - near) / ((far - near) * axis)
    float reference = (far * (axis - near)) / ((far - near) * axis);

    return shadowCubes[int(light.shadow.x)].SampleCmpLevelZero(toFragment, reference - light.params.z);
}


//Jedna ploha, sva svjetla. base je albedo, worldPos i N su u svjetskom prostoru, V gleda od
//plohe prema kameri.
//
//Odavde nadalje je svejedno je li ploha dosla iz mesha ili iz slike dubine - sve sto ova
//funkcija zna je gdje je tocka, kamo gleda i koje je boje
//Zaklon koji shadeSurface ne zna sam izracunati.
//
//Sjenu iz karte sjena Loom vec racuna ovdje. Ali kad se svjetlo ubacuje u snimku, karte
//sjena nema - postoji samo ono sto se vidi u kadru, i zaklon se mora TRAZITI kroz tu istu
//dubinu. Taj racun ne pripada osvjetljenju nego prolazu koji ima dubinu pri ruci.
//
//Zato je zaklon parametar. Crtez preko mesha ubaci NoOcclusion, koja se prevede u nista, pa
//taj put ne placa ni jednu instrukciju - a osvjetljenje ostaje jedna funkcija za oba puta
interface IOcclusion{
    float visibility(GpuLight light, float3 worldPos);
}

struct NoOcclusion : IOcclusion{
    float visibility(GpuLight light, float3 worldPos){ return 1.0; }
}

//Doprinos svih svjetala, dodan na ono sto je vec bilo.
//
//start postoji zbog kompozicije: kad se svjetlo ubacuje u snimku, ono sto je vec na slici
//JEST pocetna vrijednost, pa se zbraja istim redom kojim bi se zbrajalo da je scena crtana
//odjednom. Da kompozicija zbraja na kraju, zbroj bi se razlikovao u zadnjem bitu - a onda
//se "isto svjetlo" vise ne bi dalo dokazati, samo izjaviti
float3 accumulateLight<T : IOcclusion>(float3 start, float3 base, float3 worldPos,
                                       float3 N, float3 V,
                                       float shininess, float specularStrength,
                                       T occlusion){
    float3 lit = start;

    for(uint i = 0; i < frame.lightCount; ++i){
        GpuLight light = lights[i];

        float3 L;
        float attenuation = 1.0;

        if(light.positionOrDirection.w == 0.0){
            L = normalize(-light.positionOrDirection.xyz);
        }
        else{
            float3 toLight = light.positionOrDirection.xyz - worldPos;
            float dist = max(length(toLight), 1e-4);
            L = toLight / dist;

            float range = max(light.params.x, 1e-4);
            float window = saturate(1.0 - pow(dist / range, 4.0));
            attenuation = window * window / (dist * dist);
        }

        float3 H = normalize(L + V);
        float diffuse = max(dot(N,L), 0.0);
        float specular = pow(max(dot(N,H),0.0), shininess) * specularStrength;

        //Shadow kills the whole contribution of this light, diffuse and specular alike.
        //Ambient is untouched, which is why a shadowed surface is exactly its ambient term
        //and not black
        //params.y says which kind of map this light has, if any: 1 a single image, 2 a cube
        float visibility = 1.0;
        if(light.params.y > 1.5){
            visibility = cubeVisibility(light, worldPos);
        }
        else if(light.params.y > 0.5){
            visibility = shadowVisibility(light, worldPos);
        }

        //Zaklon iz kadra mnozi ono sto je karta sjena vec rekla. Svjetlo koje je zaklonjeno
        //jednim od ta dva puta zaklonjeno je - drugi ga ne moze vratiti
        visibility *= occlusion.visibility(light, worldPos);

        lit += base * light.color.rgb * diffuse * attenuation * visibility;
        lit += light.color.rgb * specular * attenuation * visibility;
    }

    return lit;
}

float3 accumulateLight(float3 start, float3 base, float3 worldPos, float3 N, float3 V,
                       float shininess, float specularStrength){
    return accumulateLight(start, base, worldPos, N, V, shininess, specularStrength,
                           NoOcclusion());
}

//Ploha osvijetljena od nule: ambijent pa svjetla. Ono sto crtez preko mesha oduvijek radi
float3 shadeSurface(float3 base, float3 worldPos, float3 N, float3 V,
                    float shininess, float specularStrength){
    return accumulateLight(base * frame.ambientColor.rgb, base, worldPos, N, V,
                           shininess, specularStrength, NoOcclusion());
}
