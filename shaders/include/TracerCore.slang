#ifndef LOOM_RAY_QUERY
#define LOOM_RAY_QUERY 0
#endif
// LOOMTRACER NA KARTICI: isti path tracer kao tracer/src/Tracer/Renderer.cpp, redak po redak.
//
// ZASTO PORT, A NE NOVI RENDERER. Procesorska verzija je izmjerena protiv analitickih odgovora
// (bijela pec, sunce na Lambertu, tockasto svjetlo, faktor oblika, projekcija - test_tracer).
// Kartica mora dati ISTO ocekivanje, i to se provjerava protiv tih istih brojeva i protiv same
// procesorske slike (test_tracer_gpu). Svaka razlika u algoritmu bila bi razlika koju nijedan
// test ne bi znao objasniti, pa se ovdje ne izmislja nista: isti BVH (gradi ga procesor, ovdje se
// samo obilazi), ista tezina svjetala, iste tablice energije GGX-a, isti Sobol s Owenom.
//
// JEDNA NIT = JEDAN PIKSEL, JEDAN UZORAK. Dispatch pokrije pojas redaka (rowStart, rowCount), pa
// editor moze render rasporediti preko kadrova i ostati odziv - kartica nikad ne dobije posao od
// nekoliko sekundi (Windows ga tada ubije kao zaglavljen). Zbrojevi po pikselu su u storage
// bufferima; svaki piksel pise samo svoja mjesta, pa nema atomika.
//
// Raspored buffera i Params mora se slagati s TracerGpu/GpuTracer.cpp - tamo su static_asserti.

static const float PI = 3.14159265358979;
static const float INF = 1e30;
static const uint NONE = 0xFFFFFFFFu;

// zastavice trokuta (Tracer::TriangleFlag)
static const uint ALPHA_TESTED = 1;
static const uint CATCHER = 2;
static const uint NO_CAMERA = 4;
static const uint NO_SHADOW = 8;
static const uint TRANSMISSIVE = 16;

// vrste svjetla (Tracer::LightRecord::Kind)
static const uint LIGHT_SUN = 0;
static const uint LIGHT_SPHERE = 1;
static const uint LIGHT_TRIANGLE = 2;
static const uint LIGHT_SKY = 3;

struct Params{
    float4 cameraX, cameraY, cameraZ, cameraPos;    // kamera -> svijet, stupci
    float4 inverseX, inverseY, inverseZ, inversePos; // svijet -> kamera, stupci
    float4 lens;            // focal, cx, cy, apertureRadius
    float4 lens2;           // focusDistance, width, height, sceneRadius
    uint4 counts;           // lights, suns, spheres, maxBounces
    uint4 env;              // textured, envTexture, envWidth, envHeight
    float4 envColor;        // boja (bez mape), intenzitet
    float4 toMap0, toMap1, toMap2;       // svijet -> mapa, stupci
    float4 fromMap0, fromMap1, fromMap2; // mapa -> svijet, stupci
    uint4 flags;            // skyActive, skyVisibleToCamera, backplateTexture (NONE), skyLightIndex (NONE)
    float4 values;          // indirectClamp, k1, k2, prag prilagodljivog uzorkovanja (0 iskljuceno)
    uint4 extra;            // seed, conditionalOffset, bitovi (1 staklene sjene, 2 bez mipmapa, 4 bez ekviangularnog), najmanje uzoraka prije zaustavljanja
    float4 distortion;      // fx, fy, cx, cy objektiva (fx 0: ravna leca)
    float4 holdout;         // tekstura dubine stvarne scene (bitovi, NONE), bias, broj kutija magle (bitovi), neprozirnih trokuta u BLAS-u (bitovi, ray query)
    uint4 motion;           // kljucevi geometrije, kljucevi kamere, slotova, vrhova (Scene::motion)
    float4 lightTree;       // udio lokalnih svjetala (stablo), kandidata za RIS (bitovi), beskonacnih svjetala (bitovi), cvorova (bitovi)
};

struct Material{
    float4 baseColor;       // rgb, opacity
    float4 surface;         // metallic, roughness, normalScale, ior
    float4 layers;          // specular, transmission, clearcoat, clearcoatRoughness
    float4 emission;        // rgb * strength, alphaCutoff
    int4 textures;          // baseColor, metallicRoughness, normal, emission
    uint4 mode;             // alphaMode (0 opaque, 1 mask, 2 blend), emisija s obje strane (1)
};

struct Light{
    float4 radiance;        // rgb, kind (bitovi)
    float4 position;        // xyz, radius
    float4 axis;            // xyz, cosMax
    float4 cone;            // oneMinusCos, cosOuter, cosInner, flags (bitovi: 1 delta, 2 spot)
    float4 pick;            // vjerojatnost, zbroj do ukljucivo, trokut (bitovi), povrsina
};

struct Push{
    uint sampleIndex;       // prvi uzorak ovog dispatcha
    uint rowStart;
    uint rowCount;
    uint flags;             // 1: brojati koherenciju (GpuTracer::setProfiling)
    uint sampleCount;       // uzoraka po pikselu u ovom dispatchu (regeneracija putanja)
};

[[vk::push_constant]] ConstantBuffer<Push> push;
[[vk::binding(0,0)]] StructuredBuffer<Params> paramsBuffer;
[[vk::binding(1,0)]] StructuredBuffer<float4> nodes;        // 2 po cvoru: (min, leftOrFirst), (max, count)
[[vk::binding(2,0)]] StructuredBuffer<float4> triangles;    // 3 po trokutu, redom BVH-a: (v0, izvorni), (e1, zastavice), (e2, svjetlo)
[[vk::binding(3,0)]] StructuredBuffer<uint4> triangleInfo;  // po izvornom trokutu: vrhovi, materijal
[[vk::binding(4,0)]] StructuredBuffer<float4> vertices;     // 3 po vrhu: (n, u), (v, t.xyz), (t.w, 0, 0, 0)
[[vk::binding(5,0)]] StructuredBuffer<Material> materials;
[[vk::binding(6,0)]] StructuredBuffer<uint4> textureInfo;   // pocetak, sirina, visina, zastavice (1 sRGB, 2 float, 4 ponavljanje)
[[vk::binding(7,0)]] StructuredBuffer<uint> texels;         // RGBA8
[[vk::binding(8,0)]] StructuredBuffer<float4> texelsFloat;
[[vk::binding(9,0)]] StructuredBuffer<Light> lights;
[[vk::binding(10,0)]] StructuredBuffer<uint> lightLists;    // sunca, pa kugle
[[vk::binding(11,0)]] StructuredBuffer<float> envTables;    // marginal (visina + 1), pa conditional
[[vk::binding(12,0)]] StructuredBuffer<float> energy;       // E, A, B - po 32 x 32
[[vk::binding(13,0)]] RWStructuredBuffer<float4> accCg;       // rgb, pokrivenost
[[vk::binding(14,0)]] RWStructuredBuffer<float4> accBackground; // rgb, promaseni uzorci
[[vk::binding(15,0)]] RWStructuredBuffer<float4> accLit;      // rgb, catcher uzorci
[[vk::binding(16,0)]] RWStructuredBuffer<float4> accShadowed; // rgb, svi uzorci
[[vk::binding(17,0)]] RWStructuredBuffer<float4> accAlbedo;   // rgb, zbroj luminancije
[[vk::binding(18,0)]] RWStructuredBuffer<float4> accNormal;   // xyz, zbroj kvadrata luminancije
[[vk::binding(19,0)]] RWStructuredBuffer<float> accDepth;
[[vk::binding(20,0)]] StructuredBuffer<float4> volumes;
[[vk::binding(21,0)]] RWStructuredBuffer<uint> adaptive;     // stanje prilagodljivog uzorkovanja po pikselu
// MOTION BLUR (Scene::motion): kamera (4 stupca po kljucu), pa trokuti po kljucu redom BVH-a
// (v0, e1, e2); normale vrhova po kljucu. Prazno (jedan element) bez pomaka
[[vk::binding(22,0)]] StructuredBuffer<float4> motionData;
[[vk::binding(23,0)]] StructuredBuffer<float4> motionNormals;
// Teksture kadra (snimka, holdout): mijenjaju se svaki kadar, teksture scene ne (UploadCache)
[[vk::binding(24,0)]] StructuredBuffer<uint> frameTexels;
[[vk::binding(25,0)]] StructuredBuffer<float4> frameTexelsFloat;
// STABLO SVJETALA (Tracer::CompiledScene): cvor = (min, snaga), (max, thetaO), (os, thetaE),
// (lijevo, desno, list, roditelj - bitovi); po svjetlu njegov list (NONE: beskonacno); beskonacna
// svjetla (indeks - bitovi, kumulativ)
[[vk::binding(26,0)]] StructuredBuffer<float4> lightTreeNodes;
[[vk::binding(27,0)]] StructuredBuffer<uint> lightTreeLeaf;
[[vk::binding(28,0)]] StructuredBuffer<float4> infiniteLights;
// KOHERENCIJA (profiliranje): po dubini putanje 0..15 zbroj aktivnih traka vala, sirine vala,
// razlicitih materijala u valu i broj valova. Mjeri divergenciju koju bi wavefront (razvrstavanje
// putanja po materijalu) uklonio - na bilo kojoj kartici, bez alata proizvodjaca
[[vk::binding(29,0)]] RWStructuredBuffer<uint> coherence;
// Razina je KORAK petlje odbijanja u dispatchu (profileStep), ne dubina putanje: s regeneracijom
// su u istom koraku trake na raznim dubinama. Za jedan uzorak po dispatchu je korak = dubina.
// Koraci od 15 dalje idu u zadnju razinu
static uint profileStep = 0;
void countCoherence(uint shadeId){
    if((push.flags & 1u) == 0) return;
    const uint depth = min(profileStep, 15u);
    const uint active = WaveActiveCountBits(true);
    // razliciti materijali: najmanji jos neprebrojani id (jednoliko za val), pa ponovi
    uint distinct = 0;
    bool counted = false;
    for(uint guard = 0; guard < 64; ++guard){
        const uint picked = WaveActiveMin(counted ? 0xFFFFFFFFu : shadeId);
        if(shadeId == picked) counted = true;
        distinct += 1;
        if(WaveActiveAllTrue(counted)) break;
    }
    if(WaveIsFirstLane()){
        InterlockedAdd(coherence[depth * 4], active);
        InterlockedAdd(coherence[depth * 4 + 1], WaveGetLaneCount());
        InterlockedAdd(coherence[depth * 4 + 2], distinct);
        InterlockedAdd(coherence[depth * 4 + 3], 1u);
    }
}
// trenutak zrake u otvoru zatvaraca - postavi ga trace() za cijelu putanju
static float rayTime = 0.5;

void motionSegment(uint count, float time, out uint key, out float fraction){
    const float s = clamp(time, 0.0, 1.0) * float(count - 1);
    key = min(uint(s), count - 2);
    fraction = s - float(key);
}
// Trokut slota u trenutku rayTime (Bvh::traverse s kljucevima)
void triangleAt(uint slot, out float3 v0, out float3 e1, out float3 e2){
    const uint4 m = paramsBuffer[0].motion;
    if(m.x < 2){
        v0 = triangles[slot * 3].xyz; e1 = triangles[slot * 3 + 1].xyz; e2 = triangles[slot * 3 + 2].xyz;
        return;
    }
    uint k; float f;
    motionSegment(m.x, rayTime, k, f);
    const uint a = m.y * 4 + (k * m.z + slot) * 3, b = a + m.z * 3;
    v0 = lerp(motionData[a].xyz, motionData[b].xyz, f);
    e1 = lerp(motionData[a + 1].xyz, motionData[b + 1].xyz, f);
    e2 = lerp(motionData[a + 2].xyz, motionData[b + 2].xyz, f);
}
float3 normalAt(uint vertex, float3 fallback){
    const uint4 m = paramsBuffer[0].motion;
    if(m.x < 2) return fallback;
    uint k; float f;
    motionSegment(m.x, rayTime, k, f);
    return lerp(motionNormals[k * m.w + vertex].xyz, motionNormals[(k + 1) * m.w + vertex].xyz, f);
}
#if LOOM_RAY_QUERY
// Hardverske zrake: jedan BLAS s dvije geometrije - 0 obicni neprozirni trokuti, 1 trokuti sa
// zastavicama (alfa, catcher, kamera, sjena, staklo) koje filtrira ista accept() kao BVH put.
// slotOf: (geometrija, primitiv) -> slot BVH-a, pa se sve ostalo cita kao i prije
[[vk::binding(30,0)]] RaytracingAccelerationStructure sceneAccel;
[[vk::binding(31,0)]] StructuredBuffer<uint> slotOf;
// Hardverski test presjeka nije nas: s ishodistem tik uz plohu (pomak ~1e-6 scene) zna vratiti
// samu plohu na t ~ 0. Najmanji t je zato malo veci od pomaka ishodista
float rayQueryMinT(){ return 1e-5 * paramsBuffer[0].lens2.w; }
uint slotFor(uint geometry, uint primitive){ return slotOf[(geometry == 0 ? 0u : asuint(paramsBuffer[0].holdout.w)) + primitive]; }
#endif     // 5 po kutiji: 3 retka svijet -> kutija, (albedo, gustoca), (g, 0, 0, 0)

//=============================================================================================
// POMOCNO
//=============================================================================================
float luminance(float3 c){ return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

float3 transformPoint(float4 x, float4 y, float4 z, float4 w, float3 p){ return x.xyz * p.x + y.xyz * p.y + z.xyz * p.z + w.xyz; }
float3 transformVector(float4 x, float4 y, float4 z, float3 v){ return x.xyz * v.x + y.xyz * v.y + z.xyz * v.z; }

float3 toCamera(Params P, float3 p){ return transformPoint(P.inverseX, P.inverseY, P.inverseZ, P.inversePos, p); }

void basis(float3 n, out float3 t, out float3 b){
    const float s = n.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (s + n.z);
    const float c = n.x * n.y * a;
    t = float3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = float3(c, s + n.y * n.y * a, -n.y);
}

float powerHeuristic3(float mine, float a, float b){
    const float top = max(mine, max(a, b));
    if(!(top > 0.0) || top >= INF) return top >= INF && mine >= top ? 1.0 : 0.0;
    mine /= top; a /= top; b /= top;
    return mine * mine / (mine * mine + a * a + b * b);
}
float powerHeuristic(float a, float b){
    const float a2 = a * a, b2 = b * b;
    return a2 + b2 > 0.0 ? a2 / (a2 + b2) : 0.0;
}

float oneMinusCosFromSin2(float sin2){
    const float cosMax = sqrt(max(0.0, 1.0 - sin2));
    return sin2 / (1.0 + cosMax);
}

float3 sampleCone(float3 axis, float cosMax, float2 u){
    const float cosTheta = 1.0 - u.x * (1.0 - cosMax);
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 2.0 * PI * u.y;
    float3 t, b;
    basis(axis, t, b);
    return normalize(t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + axis * cosTheta);
}

bool hitSphere(float3 origin, float3 direction, float3 centre, float radius, float tMax, out float t){
    const float3 oc = origin - centre;
    const float b = dot(oc, direction);
    const float c = dot(oc, oc) - radius * radius;
    const float disc = b * b - c;
    t = 0.0;
    if(disc < 0.0) return false;
    const float s = sqrt(disc);
    t = -b - s;
    if(t <= 1e-7) t = -b + s;
    return t > 1e-7 && t < tMax;
}

float spotFalloff(float cosAngle, float cosOuter, float cosInner){
    if(cosAngle <= cosOuter) return 0.0;
    if(cosAngle >= cosInner) return 1.0;
    const float t = (cosAngle - cosOuter) / max(1e-6, cosInner - cosOuter);
    return t * t * (3.0 - 2.0 * t);
}

//=============================================================================================
// UZORCI - Tracer/Sampler.h
//=============================================================================================
uint laineKarras(uint x, uint seed){
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}
uint owen(uint x, uint seed){ return reversebits(laineKarras(reversebits(x), seed)); }
uint hash1(uint x){
    x = x * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (x >> 22u) ^ x;
}
uint hash2(uint a, uint b){ return hash1(a ^ hash1(b + 0x9e3779b9u)); }
// Smjerni brojevi druge Sobolove dimenzije (v_0 = 1 << 31, v_k = v_{k-1} ^ (v_{k-1} >> 1)); petlja
// ide samo po postavljenim bitovima indeksa (firstbitlow), ne po svih 32
static const uint SOBOL_DIRECTIONS[32] = { 0x80000000u, 0xc0000000u, 0xa0000000u, 0xf0000000u, 0x88000000u, 0xcc000000u, 0xaa000000u, 0xff000000u, 0x80800000u, 0xc0c00000u, 0xa0a00000u, 0xf0f00000u, 0x88880000u, 0xcccc0000u, 0xaaaa0000u, 0xffff0000u, 0x80008000u, 0xc000c000u, 0xa000a000u, 0xf000f000u, 0x88008800u, 0xcc00cc00u, 0xaa00aa00u, 0xff00ff00u, 0x80808080u, 0xc0c0c0c0u, 0xa0a0a0a0u, 0xf0f0f0f0u, 0x88888888u, 0xccccccccu, 0xaaaaaaaau, 0xffffffffu };
uint sobol1(uint index){
    uint result = 0;
    while(index != 0){
        result ^= SOBOL_DIRECTIONS[firstbitlow(index)];
        index &= index - 1u;
    }
    return result;
}
float toUnit(uint x){ return float(x >> 8) * (1.0 / 16777216.0); }

struct Sampler{
    uint seed;
    uint index;
    uint dimension;
    [mutating] float2 next2D(){
        const uint pairSeed = hash2(seed, dimension);
        dimension += 1;
        const uint shuffled = owen(index, pairSeed);
        const uint x = owen(reversebits(shuffled), hash2(pairSeed, 0x51ed27u));
        const uint y = owen(sobol1(shuffled), hash2(pairSeed, 0xa3c59ac3u));
        return float2(toUnit(x), toUnit(y));
    }
};

//=============================================================================================
// TEKSTURE - Tracer::Texture::sample
//=============================================================================================
float srgbToLinear(float v){ return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }

float4 fetch(uint4 info, int x, int y){
    const int w = int(info.y), h = int(info.z);
    if((info.w & 4u) != 0){
        x = x % w; if(x < 0) x += w;
        y = y % h; if(y < 0) y += h;
    }else{
        x = clamp(x, 0, w - 1);
        y = clamp(y, 0, h - 1);
    }
    const uint at = info.x + uint(y) * info.y + uint(x);
    const bool frame = (info.w & 65536u) != 0;         // snimka / holdout: spremnik kadra
    if((info.w & 2u) != 0) return frame ? frameTexelsFloat[at] : texelsFloat[at];
    const uint p = frame ? frameTexels[at] : texels[at];
    float4 c = float4(float(p & 255u), float((p >> 8) & 255u), float((p >> 16) & 255u), float(p >> 24)) / 255.0;
    if((info.w & 1u) != 0) c.rgb = float3(srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b));
    return c;
}

float4 sampleLevel(uint4 info, float2 uv){
    const float x = uv.x * float(info.y) - 0.5;
    const float y = uv.y * float(info.z) - 0.5;
    const float fx = floor(x), fy = floor(y);
    const float tx = x - fx, ty = y - fy;
    const int ix = int(fx), iy = int(fy);
    const float4 a = fetch(info, ix, iy), b = fetch(info, ix + 1, iy);
    const float4 c = fetch(info, ix, iy + 1), d = fetch(info, ix + 1, iy + 1);
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
}

float4 sampleTexture(uint index, float2 uv){
    const uint4 info = textureInfo[index];
    if(info.y == 0 || info.z == 0) return float4(1.0);
    return sampleLevel(info, uv);
}

// Mipmape (Tracer::Texture::sample(uv, lod)): razine leze jedna za drugom iza osnovne, broj
// razina (s osnovnom) je u bitovima 8..15 zastavica
uint4 levelInfo(uint4 info, uint level){
    uint start = info.x, w = info.y, h = info.z;
    for(uint i = 0; i < level; ++i){ start += w * h; w = max(1u, w / 2u); h = max(1u, h / 2u); }
    return uint4(start, w, h, info.w);
}
float lodFor(uint index, float lodBase){
    const uint4 info = textureInfo[index];
    return lodBase + 0.5 * log2(float(info.y) * float(info.z));
}
float4 sampleTextureLod(uint index, float2 uv, float lod){
    const uint4 info = textureInfo[index];
    if(info.y == 0 || info.z == 0) return float4(1.0);
    const uint levels = max(1u, (info.w >> 8) & 255u);
    if(!(lod > 0.0) || levels == 1u) return sampleLevel(info, uv);
    const float l = min(lod, float(levels - 1u));
    const uint l0 = uint(floor(l));
    const float f = l - float(l0);
    const float4 c0 = sampleLevel(levelInfo(info, l0), uv);
    if(f <= 0.0 || l0 + 1u >= levels) return c0;
    return lerp(c0, sampleLevel(levelInfo(info, l0 + 1u), uv), f);
}

//=============================================================================================
// NEBO - Tracer/Environment.cpp
//=============================================================================================
float2 directionToLatLong(float3 d){
    float u = atan2(d.x, -d.z) / (2.0 * PI);
    if(u < 0.0) u += 1.0;
    const float v = acos(clamp(d.y, -1.0, 1.0)) / PI;
    return float2(min(u, 0.99999994), min(v, 0.99999994));
}
float3 latLongToDirection(float2 uv){
    const float theta = uv.y * PI, phi = uv.x * 2.0 * PI;
    return float3(sin(theta) * sin(phi), cos(theta), -sin(theta) * cos(phi));
}

float3 envTexel(Params P, uint x, uint y){
    return fetch(textureInfo[P.env.y], int(x), int(y)).rgb;
}

float3 skyRadiance(Params P, float3 d){
    if(P.env.x == 0) return P.envColor.rgb * P.envColor.w;
    const float3 local = transformVector(P.toMap0, P.toMap1, P.toMap2, d);
    const float2 uv = directionToLatLong(local);
    const uint w = P.env.z, h = P.env.w;
    return envTexel(P, min(uint(uv.x * float(w)), w - 1), min(uint(uv.y * float(h)), h - 1)) * P.envColor.w;
}

// Prvi indeks i u [0, count) za koji cdf[i + 1] > u
uint findInterval(uint first, uint count, float u){
    uint low = 0, high = count;          // trazi se prvi j u [1, count] s cdf[j] > u
    while(low < high){
        const uint middle = (low + high) / 2;
        if(envTables[first + middle + 1] > u) high = middle; else low = middle + 1;
    }
    return min(low, count - 1);
}

float3 skySample(Params P, float2 u, out float3 direction, out float pdf){
    pdf = 0.0;
    direction = float3(0.0, 1.0, 0.0);
    if(P.env.x == 0){
        const float z = 1.0 - 2.0 * u.x, r = sqrt(max(0.0, 1.0 - z * z));
        const float phi = 2.0 * PI * u.y;
        direction = float3(r * cos(phi), z, r * sin(phi));
        pdf = 1.0 / (4.0 * PI);
        return P.envColor.rgb * P.envColor.w;
    }
    const uint w = P.env.z, h = P.env.w;
    const uint conditionalStart = P.extra.y;
    const uint y = findInterval(0, h, u.y);
    const float m0 = envTables[y], m1 = envTables[y + 1];
    const float dy = (u.y - m0) / max(1e-12, m1 - m0);
    const uint row = conditionalStart + y * (w + 1);
    const uint x = findInterval(row, w, u.x);
    const float c0 = envTables[row + x], c1 = envTables[row + x + 1];
    const float dx = (u.x - c0) / max(1e-12, c1 - c0);
    const float2 uv = float2((float(x) + clamp(dx, 0.0, 0.9999)) / float(w), (float(y) + clamp(dy, 0.0, 0.9999)) / float(h));
    const float3 local = latLongToDirection(uv);
    direction = transformVector(P.fromMap0, P.fromMap1, P.fromMap2, local);
    const float sinTheta = sqrt(max(0.0, 1.0 - local.y * local.y));
    if(sinTheta <= 1e-6) return float3(0.0);
    pdf = (m1 - m0) * (c1 - c0) * float(w) * float(h) / (2.0 * PI * PI * sinTheta);
    return envTexel(P, x, y) * P.envColor.w;
}

float skyDensity(Params P, float3 d){
    if(P.env.x == 0) return 1.0 / (4.0 * PI);
    const float3 local = transformVector(P.toMap0, P.toMap1, P.toMap2, d);
    const float2 uv = directionToLatLong(local);
    const uint w = P.env.z, h = P.env.w;
    const uint x = min(uint(uv.x * float(w)), w - 1);
    const uint y = min(uint(uv.y * float(h)), h - 1);
    const float sinTheta = sqrt(max(0.0, 1.0 - local.y * local.y));
    if(sinTheta <= 1e-6) return 0.0;
    const uint row = P.extra.y + y * (w + 1);
    const float probability = (envTables[y + 1] - envTables[y]) * (envTables[row + x + 1] - envTables[row + x]);
    return probability * float(w) * float(h) / (2.0 * PI * PI * sinTheta);
}

//=============================================================================================
// BVH - Tracer/Bvh.h
//=============================================================================================
struct Hit{
    float t;
    uint slot;          // mjesto u polju trokuta (redom BVH-a)
    float u, v;
};

bool triangleTest(uint slot, float3 origin, float3 direction, float tMax, out float t, out float u, out float v){
    float3 v0, e1, e2;
    triangleAt(slot, v0, e1, e2);
    t = 0.0; u = 0.0; v = 0.0;
    const float3 pv = cross(direction, e2);
    const float det = dot(e1, pv);
    if(abs(det) < 1e-12) return false;
    const float inv = 1.0 / det;
    const float3 tv = origin - v0;
    u = dot(tv, pv) * inv;
    if(u < 0.0 || u > 1.0) return false;
    const float3 qv = cross(tv, e1);
    v = dot(direction, qv) * inv;
    if(v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, qv) * inv;
    return t > 0.0 && t < tMax;
}

// originScaled = origin * inverse: po osi jedno mnozenje-oduzimanje (FMA)
bool slab(uint node, float3 originScaled, float3 inverse, float tMax, out float entry){
    const float3 lo = nodes[node * 2].xyz, hi = nodes[node * 2 + 1].xyz;
    const float3 t0 = lo * inverse - originScaled, t1 = hi * inverse - originScaled;
    const float3 near = min(t0, t1), far = max(t0, t1);
    entry = max(max(near.x, near.y), max(near.z, 0.0));
    const float exit = min(min(far.x, far.y), min(far.z, tMax));
    return entry <= exit;
}

float2 uvOf(uint original, float u, float v){
    const uint4 info = triangleInfo[original];
    const float2 a = float2(vertices[info.x * 3].w, vertices[info.x * 3 + 1].x);
    const float2 b = float2(vertices[info.y * 3].w, vertices[info.y * 3 + 1].x);
    const float2 c = float2(vertices[info.z * 3].w, vertices[info.z * 3 + 1].x);
    return a * (1.0 - u - v) + b * u + c * v;
}

// Alfa: maska po pragu, prozirnost stohasticki (isti hash kao procesor)
bool alphaPasses(uint original, float u, float v, uint pixelSeed, uint sampleIndex, uint salt){
    const Material m = materials[triangleInfo[original].w];
    float alpha = m.baseColor.w;
    if(m.textures.x >= 0) alpha *= sampleTexture(uint(m.textures.x), uvOf(original, u, v)).a;
    if(m.mode.x == 1) return alpha >= m.emission.w;
    return toUnit(hash1(pixelSeed ^ hash2(original, salt + sampleIndex * 977u))) < alpha;
}

// mode: 0 kamera, 1 odbijanje, 2 sjena, 3 sjena catchera (catcheri ne zaklanjaju)
bool accept(uint slot, float u, float v, uint mode, uint pixelSeed, uint sampleIndex, uint salt){
    const uint flags = asuint(triangles[slot * 3 + 1].w);
    if(mode == 0 && (flags & NO_CAMERA) != 0) return false;
    if(mode >= 2 && (flags & NO_SHADOW) != 0) return false;
    if(mode == 3 && (flags & CATCHER) != 0) return false;
    if((flags & ALPHA_TESTED) != 0 && !alphaPasses(asuint(triangles[slot * 3].w), u, v, pixelSeed, sampleIndex, salt)) return false;
    return true;
}

// Najblizi (anyHit = false) ili bilo koji (anyHit = true) prihvaceni pogodak
bool traverse(float3 origin, float3 direction, float tMax, uint mode, bool anyHit,
              uint pixelSeed, uint sampleIndex, uint salt, inout Hit hit){
#if LOOM_RAY_QUERY
    RayDesc ray;
    ray.Origin = origin; ray.Direction = direction; ray.TMin = rayQueryMinT(); ray.TMax = tMax;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(sceneAccel, anyHit ? RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH : RAY_FLAG_NONE, 0xFF, ray);
    while(query.Proceed()){
        if(query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
        const uint slot = slotFor(query.CandidateGeometryIndex(), query.CandidatePrimitiveIndex());
        const float2 b = query.CandidateTriangleBarycentrics();
        if(accept(slot, b.x, b.y, mode, pixelSeed, sampleIndex, salt)) query.CommitNonOpaqueTriangleHit();
    }
    if(query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
    const float2 b = query.CommittedTriangleBarycentrics();
    hit.slot = slotFor(query.CommittedGeometryIndex(), query.CommittedPrimitiveIndex());
    hit.t = query.CommittedRayT(); hit.u = b.x; hit.v = b.y;
    return true;
#else
    float3 d = direction;
    d.x = abs(d.x) > 1e-20 ? d.x : (d.x < 0.0 ? -1e-20 : 1e-20);
    d.y = abs(d.y) > 1e-20 ? d.y : (d.y < 0.0 ? -1e-20 : 1e-20);
    d.z = abs(d.z) > 1e-20 ? d.z : (d.z < 0.0 ? -1e-20 : 1e-20);
    const float3 inverse = 1.0 / d;
    const float3 originScaled = origin * inverse;
    uint stack[64];
    int top = 0;
    float entry;
    if(!slab(0, originScaled, inverse, tMax, entry)) return false;
    stack[top++] = 0;
    bool found = false;
    while(top > 0){
        const uint node = stack[--top];
        const uint leftOrFirst = asuint(nodes[node * 2].w);
        const uint count = asuint(nodes[node * 2 + 1].w);
        if(count > 0){
            for(uint i = 0; i < count; ++i){
                const uint slot = leftOrFirst + i;
                float t, u, v;
                if(!triangleTest(slot, origin, direction, tMax, t, u, v)) continue;
                if(!accept(slot, u, v, mode, pixelSeed, sampleIndex, salt)) continue;
                if(anyHit) return true;
                tMax = t;
                hit.t = t; hit.slot = slot; hit.u = u; hit.v = v;
                found = true;
            }
            continue;
        }
        const uint left = leftOrFirst, right = left + 1;
        float entryLeft, entryRight;
        const bool hitLeft = slab(left, originScaled, inverse, tMax, entryLeft);
        const bool hitRight = slab(right, originScaled, inverse, tMax, entryRight);
        if(hitLeft && hitRight){
            if(entryLeft <= entryRight){ stack[top++] = right; stack[top++] = left; }
            else{ stack[top++] = left; stack[top++] = right; }
        }else if(hitLeft) stack[top++] = left;
        else if(hitRight) stack[top++] = right;
    }
    return found;
#endif
}

// Propusnost zrake sjene (Tracer::Renderer shadowTransmittance): 0 kad je zaklonjena, inace
// umnozak propusnosti stakala na putu (samo uz staklene sjene). crossed: prosla kroz staklo
float3 surfaceTransmittance(float3 origin, float3 direction, float tMax, uint mode, bool glass,
                            uint pixelSeed, uint sampleIndex, uint salt, out bool crossed){
    crossed = false;
    float3 through = float3(1.0);
#if LOOM_RAY_QUERY
    RayDesc ray;
    ray.Origin = origin; ray.Direction = direction; ray.TMin = rayQueryMinT(); ray.TMax = tMax;
    RayQuery<RAY_FLAG_NONE> query;
    // prvi zaklon zavrsi potragu; staklo se ne potvrdi, samo oslabi (svaki trokut jednom: geometrija bez duplikata)
    query.TraceRayInline(sceneAccel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, ray);
    while(query.Proceed()){
        if(query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
        const uint slot = slotFor(query.CandidateGeometryIndex(), query.CandidatePrimitiveIndex());
        const float2 b = query.CandidateTriangleBarycentrics();
        if(!accept(slot, b.x, b.y, mode, pixelSeed, sampleIndex, salt)) continue;
        const uint flags = asuint(triangles[slot * 3 + 1].w);
        if(!glass || (flags & TRANSMISSIVE) == 0){ query.CommitNonOpaqueTriangleHit(); continue; }
        const Material m = materials[triangleInfo[asuint(triangles[slot * 3].w)].w];
        const float3 n = normalize(cross(triangles[slot * 3 + 1].xyz, triangles[slot * 3 + 2].xyz));
        const float F = fresnelDielectric(abs(dot(n, direction)), max(1.0001, m.surface.w));
        through *= m.baseColor.rgb * (m.layers.y * (1.0 - m.surface.x) * (1.0 - F));
        crossed = true;
    }
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? float3(0.0) : through;
#else
    float3 d = direction;
    d.x = abs(d.x) > 1e-20 ? d.x : (d.x < 0.0 ? -1e-20 : 1e-20);
    d.y = abs(d.y) > 1e-20 ? d.y : (d.y < 0.0 ? -1e-20 : 1e-20);
    d.z = abs(d.z) > 1e-20 ? d.z : (d.z < 0.0 ? -1e-20 : 1e-20);
    const float3 inverse = 1.0 / d;
    const float3 originScaled = origin * inverse;
    uint stack[64];
    int top = 0;
    float entry;
    if(!slab(0, originScaled, inverse, tMax, entry)) return through;
    stack[top++] = 0;
    while(top > 0){
        const uint node = stack[--top];
        const uint leftOrFirst = asuint(nodes[node * 2].w);
        const uint count = asuint(nodes[node * 2 + 1].w);
        if(count > 0){
            for(uint i = 0; i < count; ++i){
                const uint slot = leftOrFirst + i;
                float t, u, v;
                if(!triangleTest(slot, origin, direction, tMax, t, u, v)) continue;
                if(!accept(slot, u, v, mode, pixelSeed, sampleIndex, salt)) continue;
                const uint flags = asuint(triangles[slot * 3 + 1].w);
                if(!glass || (flags & TRANSMISSIVE) == 0) return float3(0.0);
                const Material m = materials[triangleInfo[asuint(triangles[slot * 3].w)].w];
                const float3 n = normalize(cross(triangles[slot * 3 + 1].xyz, triangles[slot * 3 + 2].xyz));
                const float F = fresnelDielectric(abs(dot(n, direction)), max(1.0001, m.surface.w));
                through *= m.baseColor.rgb * (m.layers.y * (1.0 - m.surface.x) * (1.0 - F));
                crossed = true;
            }
            continue;
        }
        const uint left = leftOrFirst, right = left + 1;
        float entryLeft, entryRight;
        const bool hitLeft = slab(left, originScaled, inverse, tMax, entryLeft);
        const bool hitRight = slab(right, originScaled, inverse, tMax, entryRight);
        if(hitLeft) stack[top++] = left;
        if(hitRight) stack[top++] = right;
    }
    return through;
#endif
}

//=============================================================================================
// MAGLA - Tracer/Volume.h
//=============================================================================================
static const uint MAX_VOLUME_SPANS = 8;
// Po volumenu 8 float4: 3 retka svijet -> kutija, (albedo, gustoca), (g, g2, udio drugog, vrsta),
// (ishodiste, k = 1/visina), (gore, rub), (sum, mjerilo suma, 0, 0). Vrsta 0 kutija, 1 visina
static const uint VOLUME_STRIDE = 8;
bool isHeightFog(uint v){ return asuint(volumes[v * VOLUME_STRIDE + 4].w) == 1u; }
float volumeDensity(uint v){ return volumes[v * VOLUME_STRIDE + 3].w; }

bool boxInterval(uint v, float3 o, float3 d, float tMax, out float t0, out float t1){
    const float4 r0 = volumes[v * VOLUME_STRIDE], r1 = volumes[v * VOLUME_STRIDE + 1], r2 = volumes[v * VOLUME_STRIDE + 2];
    const float3 lo = float3(dot(r0.xyz, o) + r0.w, dot(r1.xyz, o) + r1.w, dot(r2.xyz, o) + r2.w);
    const float3 ld = float3(dot(r0.xyz, d), dot(r1.xyz, d), dot(r2.xyz, d));
    t0 = 0.0;
    t1 = tMax;
    for(int k = 0; k < 3; ++k){
        if(abs(ld[k]) < 1e-20){
            if(lo[k] < -0.5 || lo[k] > 0.5) return false;
            continue;
        }
        const float a = (-0.5 - lo[k]) / ld[k], b = (0.5 - lo[k]) / ld[k];
        t0 = max(t0, min(a, b));
        t1 = min(t1, max(a, b));
    }
    return t1 > t0;
}

// magla po visini duz zrake: sigma(t) = d0 exp(-k (h0 + dh t))
struct HeightRay{ float d0; float k; float h0; float dh; };
HeightRay heightRay(uint v, float3 o, float3 d){
    HeightRay h;
    const float4 a = volumes[v * VOLUME_STRIDE + 5], b = volumes[v * VOLUME_STRIDE + 6];
    h.d0 = volumeDensity(v); h.k = a.w; h.h0 = dot(o - a.xyz, b.xyz); h.dh = dot(d, b.xyz);
    return h;
}
float heightSigma(HeightRay h, float t){ return h.d0 * exp(min(80.0, -h.k * (h.h0 + h.dh * t))); }
float heightTau(HeightRay h, float t){
    if(!(t > 0.0)) return 0.0;
    const float base = h.d0 * exp(min(80.0, -h.k * h.h0));
    const float x = h.k * h.dh;
    if(t >= INF) return x > 1e-12 ? base / x : INF;
    if(abs(x) * t < 1e-4) return base * t * (1.0 - 0.5 * x * t);
    return base * (1.0 - exp(min(80.0, -x * t))) / x;
}

// nehomogena gustoca kutije (Tracer::densityFactor): meki rub i sum, isti hash kao na procesoru
uint latticeHash(int x, int y, int z){
    uint h = uint(x) * 0x8da6b343u ^ uint(y) * 0xd8163841u ^ uint(z) * 0xcb1ab31fu;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
float latticeValue(int x, int y, int z){ return float(latticeHash(x, y, z) >> 8) * (1.0 / 16777216.0); }
float valueNoise(float3 p){
    const float3 f = floor(p);
    const float3 x = p - f;
    const float3 w = x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
    const int ix = int(f.x), iy = int(f.y), iz = int(f.z);
    const float a000 = latticeValue(ix, iy, iz), a100 = latticeValue(ix + 1, iy, iz);
    const float a010 = latticeValue(ix, iy + 1, iz), a110 = latticeValue(ix + 1, iy + 1, iz);
    const float a001 = latticeValue(ix, iy, iz + 1), a101 = latticeValue(ix + 1, iy, iz + 1);
    const float a011 = latticeValue(ix, iy + 1, iz + 1), a111 = latticeValue(ix + 1, iy + 1, iz + 1);
    const float x00 = a000 + (a100 - a000) * w.x, x10 = a010 + (a110 - a010) * w.x;
    const float x01 = a001 + (a101 - a001) * w.x, x11 = a011 + (a111 - a011) * w.x;
    const float y0 = x00 + (x10 - x00) * w.y, y1 = x01 + (x11 - x01) * w.y;
    return y0 + (y1 - y0) * w.z;
}
float cloudNoise(float3 p){
    float sum = 0.0, amplitude = 1.0;
    for(int octave = 0; octave < 4; ++octave){
        sum += amplitude * valueNoise(p + float3(float(octave) * 17.31));
        amplitude *= 0.5;
        p *= 2.03;
    }
    const float n = clamp((sum / 1.875 - 0.25) * 2.0, 0.0, 1.0);
    return n * n * (3.0 - 2.0 * n);
}
float volumeEdge(uint v){ return volumes[v * VOLUME_STRIDE + 6].w; }
float volumeNoise(uint v){ return volumes[v * VOLUME_STRIDE + 7].x; }
bool volumeVaries(uint v){ return !isHeightFog(v) && (volumeEdge(v) > 0.0 || volumeNoise(v) > 0.0); }
float majorantOf(uint v){ return volumeDensity(v) * (isHeightFog(v) ? 1.0 : 1.0 + max(0.0, volumeNoise(v))); }
float densityFactor(uint v, float3 p){
    const float4 r0 = volumes[v * VOLUME_STRIDE], r1 = volumes[v * VOLUME_STRIDE + 1], r2 = volumes[v * VOLUME_STRIDE + 2];
    const float3 l = float3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w, dot(r2.xyz, p) + r2.w);
    float f = 1.0;
    const float edge = volumeEdge(v);
    if(edge > 0.0){
        for(int k = 0; k < 3; ++k){
            const float x = clamp((0.5 - abs(l[k])) / edge, 0.0, 1.0);
            f *= x * x * (3.0 - 2.0 * x);
        }
    }
    const float4 noise = volumes[v * VOLUME_STRIDE + 7];
    if(noise.x > 0.0 && f > 0.0){
        const float3 size = float3(1.0 / max(1e-20, length(r0.xyz)), 1.0 / max(1e-20, length(r1.xyz)), 1.0 / max(1e-20, length(r2.xyz)));
        const float n = cloudNoise(l * size / max(noise.y, 1e-4));
        f *= (1.0 - noise.x) + 2.0 * noise.x * n;
    }
    return f;
}

// generator za delta i ratio tracking (Tracer::VolumeRng)
struct VolumeRng{
    uint state;
    [mutating] float next(){
        state = state * 747796405u + 2891336453u;
        uint w = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        w = (w >> 22u) ^ w;
        return float(w >> 8) * (1.0 / 16777216.0);
    }
};

struct MediaOnRay{
    float3 o;
    float3 d;
    float s0[MAX_VOLUME_SPANS];
    float s1[MAX_VOLUME_SPANS];
    float spanSigma[MAX_VOLUME_SPANS];      // majoranta
    uint spanIndex[MAX_VOLUME_SPANS];
    uint spanCount;
    HeightRay heights[MAX_VOLUME_SPANS];
    uint heightIndex[MAX_VOLUME_SPANS];
    uint heightCount;
    bool varies;
    float tau(float t){
        float sum = 0.0;
        for(uint i = 0; i < spanCount; ++i) sum += spanSigma[i] * max(0.0, min(t, s1[i]) - s0[i]);
        for(uint i = 0; i < heightCount; ++i) sum += heightTau(heights[i], t);
        return sum;
    }
    float sigma(float t){
        float sum = 0.0;
        for(uint i = 0; i < spanCount; ++i) if(s0[i] <= t && t <= s1[i]) sum += spanSigma[i];
        for(uint i = 0; i < heightCount; ++i) sum += heightSigma(heights[i], t);
        return sum;
    }
    bool empty(){ return spanCount == 0 && heightCount == 0; }
    // dio zrake s medijem (Tracer::MediaOnRay::extent)
    bool extent(float tMax, out float a, out float b){
        a = heightCount > 0 ? 0.0 : INF;
        b = heightCount > 0 ? tMax : 0.0;
        for(uint i = 0; i < spanCount; ++i){ a = min(a, s0[i]); b = max(b, s1[i]); }
        return !empty() && b > a;
    }
    float eventPdf(float t){ return sigma(t) * exp(-tau(t)); }
    uint pick(float t, float u){
        float left = u * sigma(t);
        uint which = spanCount > 0 ? spanIndex[0] : heightIndex[0];
        for(uint i = 0; i < spanCount; ++i){
            if(!(s0[i] <= t && t <= s1[i])) continue;
            which = spanIndex[i];
            left -= spanSigma[i];
            if(left < 0.0) return which;
        }
        for(uint i = 0; i < heightCount; ++i){
            which = heightIndex[i];
            left -= heightSigma(heights[i], t);
            if(left < 0.0) return which;
        }
        return which;
    }
    float realSigma(uint index, float t){
        if(isHeightFog(index)){
            for(uint i = 0; i < heightCount; ++i) if(heightIndex[i] == index) return heightSigma(heights[i], t);
            return 0.0;
        }
        return volumeVaries(index) ? volumeDensity(index) * densityFactor(index, o + d * t) : volumeDensity(index);
    }
    // ukupna stvarna gustoca u t i medij razmjerno njoj (Tracer::MediaOnRay::realTotal)
    float realTotal(float t, float u, out uint which){
        float values[2 * MAX_VOLUME_SPANS];
        uint owners[2 * MAX_VOLUME_SPANS];
        uint n = 0;
        float total = 0.0;
        for(uint i = 0; i < spanCount; ++i){
            if(!(s0[i] <= t && t <= s1[i])) continue;
            const float s = volumeVaries(spanIndex[i]) ? realSigma(spanIndex[i], t) : spanSigma[i];
            values[n] = s; owners[n] = spanIndex[i]; n++; total += s;
        }
        for(uint i = 0; i < heightCount; ++i){
            const float s = heightSigma(heights[i], t);
            values[n] = s; owners[n] = heightIndex[i]; n++; total += s;
        }
        which = n > 0 ? owners[0] : 0u;
        float left = u * total;
        for(uint i = 0; i < n; ++i){
            which = owners[i];
            left -= values[i];
            if(left < 0.0) break;
        }
        return total;
    }
    // propusnost do t: homogeno analiticki, nehomogene kutije ratio trackingom
    float transmittance(float t, inout VolumeRng rng){
        float smooth = 0.0;
        for(uint i = 0; i < spanCount; ++i)
            if(!volumeVaries(spanIndex[i])) smooth += spanSigma[i] * max(0.0, min(t, s1[i]) - s0[i]);
        for(uint i = 0; i < heightCount; ++i) smooth += heightTau(heights[i], t);
        float T = exp(-smooth);
        for(uint i = 0; i < spanCount; ++i){
            if(!(T > 0.0)) break;
            const uint v = spanIndex[i];
            if(!volumeVaries(v) || !(spanSigma[i] > 0.0)) continue;
            const float end = min(t, s1[i]);
            const float density = volumeDensity(v);
            float x = s0[i];
            for(int step = 0; step < 1024; ++step){
                x -= log(max(1e-12, 1.0 - rng.next())) / spanSigma[i];
                if(x >= end) break;
                T *= 1.0 - density * densityFactor(v, o + d * x) / spanSigma[i];
                if(T < 0.05){
                    if(rng.next() >= 0.5){ T = 0.0; break; }
                    T *= 2.0;
                }
            }
        }
        return T;
    }
};

MediaOnRay mediaOnRay(uint count, float3 o, float3 d, float tMax){
    MediaOnRay m;
    m.o = o; m.d = d;
    m.spanCount = 0; m.heightCount = 0; m.varies = false;
    for(uint v = 0; v < count; ++v){
        if(!(volumeDensity(v) > 0.0)) continue;
        if(isHeightFog(v)){
            if(m.heightCount < MAX_VOLUME_SPANS){ m.heights[m.heightCount] = heightRay(v, o, d); m.heightIndex[m.heightCount] = v; m.heightCount++; }
            continue;
        }
        if(m.spanCount >= MAX_VOLUME_SPANS) continue;
        float t0, t1;
        if(boxInterval(v, o, d, tMax, t0, t1)){
            m.s0[m.spanCount] = t0; m.s1[m.spanCount] = t1; m.spanIndex[m.spanCount] = v;
            m.spanSigma[m.spanCount] = majorantOf(v);
            m.varies = m.varies || volumeVaries(v);
            m.spanCount++;
        }
    }
    return m;
}

float volumeTransmittance(uint count, float3 o, float3 d, float tMax, inout VolumeRng rng){
    MediaOnRay m = mediaOnRay(count, o, d, tMax);
    return m.transmittance(tMax, rng);
}

// t s tau(t) = target po majoranti (Tracer::solveTau)
bool solveTau(MediaOnRay m, float tMax, float from, float target, out float t){
    t = from;
    if(m.heightCount == 0){
        uint n = m.spanCount;
        float edges[2 * MAX_VOLUME_SPANS];
        for(uint i = 0; i < n; ++i){ edges[2 * i] = m.s0[i]; edges[2 * i + 1] = m.s1[i]; }
        for(uint i = 1; i < 2 * n; ++i){
            const float key = edges[i];
            int j = int(i) - 1;
            while(j >= 0 && edges[j] > key){ edges[j + 1] = edges[j]; --j; }
            edges[j + 1] = key;
        }
        float tau = target;
        for(uint e = 0; e + 1 < 2 * n; ++e){
            const float a = edges[e], b = edges[e + 1];
            if(!(b > a)) continue;
            const float sigma = m.sigma(0.5 * (a + b));
            if(sigma <= 0.0) continue;
            if(tau <= sigma * (b - a)){ t = max(from, a + tau / sigma); return true; }
            tau -= sigma * (b - a);
        }
        return false;
    }
    if(m.tau(tMax) < target) return false;
    float lo = from, hi = tMax;
    if(hi >= INF){
        hi = max(1.0, 2.0 * from);
        for(int k = 0; k < 80 && m.tau(hi) < target; ++k) hi *= 2.0;
    }
    float x = 0.5 * (lo + hi);
    for(int iteration = 0; iteration < 48; ++iteration){
        const float f = m.tau(x) - target;
        if(abs(f) <= 1e-6 * max(1.0, target)) break;
        if(f > 0.0) hi = x; else lo = x;
        const float slope = m.sigma(x);
        float next = slope > 0.0 ? x - f / slope : 0.5 * (lo + hi);
        if(!(next > lo && next < hi)) next = 0.5 * (lo + hi);
        if(next == x) break;
        x = next;
    }
    t = x;
    return true;
}

// slobodni put s delta trackingom kroz nehomogene kutije (Tracer::sampleVolume)
bool sampleVolume(MediaOnRay m, float tMax, float2 u, uint seed, out float t, out uint which){
    t = 0.0; which = 0;
    if(m.empty()) return false;
    float target = -log(max(1e-12, 1.0 - u.x));
    float from = 0.0;
    VolumeRng rng;
    rng.state = seed;
    for(int step = 0; step < 256; ++step){
        if(!solveTau(m, tMax, from, target, t)) return false;
        which = m.pick(t, u.y);
        if(!m.varies || !volumeVaries(which)) return true;
        const float real = m.realSigma(which, t);
        if(rng.next() * majorantOf(which) < real) return true;
        from = t;
        target -= log(max(1e-12, 1.0 - rng.next()));
        u.y = rng.next();
    }
    return false;
}

// ekviangularno (Kulla & Fajardo 2012) - Tracer/Volume.h
struct Equiangular{ float delta; float D; float thetaA; float thetaB; };
Equiangular equiangular(float3 o, float3 d, float3 c, float a, float b){
    Equiangular e;
    e.delta = dot(c - o, d);
    e.D = max(length(c - (o + d * e.delta)), 1e-5 * (1.0 + abs(e.delta)));
    e.thetaA = atan((a - e.delta) / e.D);
    e.thetaB = b >= INF ? 0.5 * PI : atan((b - e.delta) / e.D);
    return e;
}
float equiangularSample(Equiangular e, float u){ return e.delta + e.D * tan(e.thetaA + (e.thetaB - e.thetaA) * u); }
float equiangularPdf(Equiangular e, float t){
    const float span = e.thetaB - e.thetaA;
    if(!(span > 0.0)) return 0.0;
    const float x = t - e.delta;
    return e.D / (span * (e.D * e.D + x * x));
}

float phaseHG(float cosTheta, float g){
    const float denominator = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) / (4.0 * PI * denominator * sqrt(max(denominator, 1e-12)));
}
// mjesavina dvaju rezanja (Tracer::phaseOf)
float phaseOf(uint v, float cosTheta){
    const float4 p = volumes[v * VOLUME_STRIDE + 4];
    return p.z > 0.0 ? (1.0 - p.z) * phaseHG(cosTheta, p.x) + p.z * phaseHG(cosTheta, p.y) : phaseHG(cosTheta, p.x);
}

float3 samplePhaseHG(float3 d, float g, float2 u, out float pdf){
    float cosTheta;
    if(abs(g) < 1e-3) cosTheta = 1.0 - 2.0 * u.x;
    else{
        const float s = (1.0 - g * g) / (1.0 - g + 2.0 * g * u.x);
        cosTheta = clamp((1.0 + g * g - s * s) / (2.0 * g), -1.0, 1.0);
    }
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 2.0 * PI * u.y;
    const float3 a = normalize(abs(d.x) > 0.9 ? cross(d, float3(0, 1, 0)) : cross(d, float3(1, 0, 0)));
    const float3 b = cross(d, a);
    pdf = phaseHG(cosTheta, g);
    return normalize(d * cosTheta + (a * cos(phi) + b * sin(phi)) * sinTheta);
}
float3 samplePhase(uint v, float3 d, float2 u, out float pdf){
    const float4 p = volumes[v * VOLUME_STRIDE + 4];
    float g = p.x;
    if(p.z > 0.0){
        if(u.x < p.z){ g = p.y; u.x = min(u.x / p.z, 0.99999994); }
        else u.x = min((u.x - p.z) / (1.0 - p.z), 0.99999994);
    }
    float single;
    const float3 next = samplePhaseHG(d, g, u, single);
    pdf = phaseOf(v, dot(d, next));
    return next;
}

// Zraka sjene: stakla na putu (surfaceTransmittance) puta prolaz kroz maglu
float3 shadowTransmittance(float3 origin, float3 direction, float tMax, uint mode, bool glass,
                           uint pixelSeed, uint sampleIndex, uint salt, uint volumeCount, out bool crossed){
    float3 through = surfaceTransmittance(origin, direction, tMax, mode, glass, pixelSeed, sampleIndex, salt, crossed);
    if(volumeCount > 0 && luminance(through) > 0.0){
        VolumeRng rng;
        rng.state = hash2(pixelSeed ^ hash2(salt, sampleIndex * 977u + 0x5bd1e995u), 0x2545f491u);
        through *= volumeTransmittance(volumeCount, origin, direction, tMax, rng);
    }
    return through;
}

//=============================================================================================
// BSDF - Tracer/Bsdf.cpp
//=============================================================================================
float alphaFrom(float roughness){ return max(roughness * roughness, 2e-4); }

float ggxD(float3 h, float a){
    if(h.z <= 0.0) return 0.0;
    const float a2 = a * a;
    const float s = (h.x * h.x + h.y * h.y) / a2 + h.z * h.z;
    return 1.0 / (PI * a2 * s * s);
}
float ggxLambda(float3 w, float a){
    const float z2 = w.z * w.z;
    if(z2 <= 0.0) return 1e30;
    const float t2 = (w.x * w.x + w.y * w.y) / z2;
    return 0.5 * (-1.0 + sqrt(1.0 + a * a * t2));
}
float ggxG1(float3 w, float a){ return 1.0 / (1.0 + ggxLambda(w, a)); }
float ggxG2(float3 wo, float3 wi, float a){ return 1.0 / (1.0 + ggxLambda(wo, a) + ggxLambda(wi, a)); }

float3 sampleVisibleNormal(float3 wo, float a, float2 u){
    const float3 stretched = normalize(float3(wo.x * a, wo.y * a, wo.z));
    const float phi = 2.0 * PI * u.x;
    const float z = (1.0 - u.y) * (1.0 + stretched.z) - stretched.z;
    const float sinTheta = sqrt(clamp(1.0 - z * z, 0.0, 1.0));
    const float3 cap = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);
    const float3 h = cap + stretched;
    return normalize(float3(h.x * a, h.y * a, max(h.z, 1e-9)));
}
float visibleNormalPdf(float3 wo, float3 h, float a){
    if(wo.z <= 0.0) return 0.0;
    return ggxG1(wo, a) * max(0.0, dot(wo, h)) * ggxD(h, a) / wo.z;
}
float schlick(float f0, float c){
    const float m = clamp(1.0 - c, 0.0, 1.0);
    const float m2 = m * m;
    return f0 + (1.0 - f0) * m2 * m2 * m;
}
float3 schlick3(float3 f0, float c){
    const float m = clamp(1.0 - c, 0.0, 1.0);
    const float m2 = m * m;
    return f0 + (float3(1.0) - f0) * (m2 * m2 * m);
}
float energyLookup(uint table, float mu, float roughness){
    const float x = clamp(mu, 0.0, 1.0) * 31.0;
    const float y = clamp(roughness, 0.0, 1.0) * 31.0;
    const int x0 = min(int(x), 30), y0 = min(int(y), 30);
    const float tx = x - float(x0), ty = y - float(y0);
    const uint base = table * 1024u;
    const float a = energy[base + uint(y0) * 32u + uint(x0)], b = energy[base + uint(y0) * 32u + uint(x0 + 1)];
    const float c = energy[base + uint(y0 + 1) * 32u + uint(x0)], d = energy[base + uint(y0 + 1) * 32u + uint(x0 + 1)];
    return (a * (1.0 - tx) + b * tx) * (1.0 - ty) + (c * (1.0 - tx) + d * tx) * ty;
}
float fresnelDielectric(float cosI, float eta){
    cosI = clamp(cosI, -1.0, 1.0);
    if(cosI < 0.0){ eta = 1.0 / eta; cosI = -cosI; }
    const float sin2T = (1.0 - cosI * cosI) / (eta * eta);
    if(sin2T >= 1.0) return 1.0;
    const float cosT = sqrt(max(0.0, 1.0 - sin2T));
    const float parallel = (eta * cosI - cosT) / (eta * cosI + cosT);
    const float perpendicular = (cosI - eta * cosT) / (cosI + eta * cosT);
    return 0.5 * (parallel * parallel + perpendicular * perpendicular);
}

static const int COAT = 0;
static const int METAL = 1;
static const int SPECULAR = 2;
static const int DIFFUSE = 3;
static const int GLASS = 4;

struct Bsdf{
    float3 baseColor;
    float metallic, roughness, ior, specular, transmission, clearcoat, clearcoatRoughness;
    float3 wo;
    float eta;
    float alpha, coatAlpha;
    float dielectricF0, specularScale;
    float3 metalF0;
    float weights[5];
    float coatAttenuation, diffuseScale, specularMs;
    float3 metalMs;
};

Bsdf makeBsdf(float3 baseColor, float metallic, float roughness, float ior, float specular, float transmission,
              float clearcoat, float clearcoatRoughness, float3 wo, float eta){
    Bsdf b;
    b.metallic = clamp(metallic, 0.0, 1.0);
    b.roughness = clamp(roughness, 0.0, 1.0);
    b.transmission = clamp(transmission, 0.0, 1.0);
    b.clearcoat = clamp(clearcoat, 0.0, 1.0);
    b.clearcoatRoughness = clamp(clearcoatRoughness, 0.0, 1.0);
    b.baseColor = clamp(baseColor, float3(0.0), float3(1.0));
    b.ior = ior;
    b.specular = specular;
    wo.z = max(wo.z, 1e-5);
    b.wo = normalize(wo);
    b.eta = eta;
    b.alpha = alphaFrom(b.roughness);
    b.coatAlpha = alphaFrom(b.clearcoatRoughness);
    const float f = (ior - 1.0) / (ior + 1.0);
    b.dielectricF0 = clamp(f * f, 0.0, 1.0);
    b.specularScale = clamp(specular, 0.0, 1.0);
    b.metalF0 = b.baseColor;

    const float mu = b.wo.z;
    const float E = max(1e-3, energyLookup(0, mu, b.roughness));
    const float A = energyLookup(1, mu, b.roughness), B = energyLookup(2, mu, b.roughness);
    b.specularMs = 1.0 + b.dielectricF0 * (1.0 - E) / E;
    b.metalMs = float3(1.0) + b.metalF0 * ((1.0 - E) / E);
    const float specularAlbedo = min(1.0, b.specularScale * (b.dielectricF0 * A + B) * b.specularMs);
    b.diffuseScale = 1.0 - specularAlbedo;
    const float coatAlbedo = b.clearcoat > 0.0
        ? 0.04 * energyLookup(1, mu, b.clearcoatRoughness) + energyLookup(2, mu, b.clearcoatRoughness) : 0.0;
    b.coatAttenuation = 1.0 - b.clearcoat * coatAlbedo;

    const float metal = b.metallic, dielectric = 1.0 - metal;
    const float glass = dielectric * b.transmission, opaque = dielectric * (1.0 - b.transmission);
    b.weights[COAT] = b.clearcoat * coatAlbedo;
    b.weights[METAL] = b.coatAttenuation * metal * max(0.05, luminance((b.metalF0 * A + float3(B)) * b.metalMs));
    b.weights[SPECULAR] = b.coatAttenuation * opaque * specularAlbedo;
    b.weights[DIFFUSE] = b.coatAttenuation * opaque * b.diffuseScale * luminance(b.baseColor);
    b.weights[GLASS] = b.coatAttenuation * glass;
    float sum = 0.0;
    for(int k = 0; k < 5; ++k) sum += b.weights[k];
    if(sum <= 0.0){ b.weights[DIFFUSE] = 1.0; sum = 1.0; }
    for(int k = 0; k < 5; ++k) b.weights[k] /= sum;
    return b;
}

float3 evalLobe(Bsdf b, int lobe, float3 wi, out float pdf){
    pdf = 0.0;
    const float metal = b.metallic, dielectric = 1.0 - metal;
    const float glass = dielectric * b.transmission, opaque = dielectric * (1.0 - b.transmission);
    const float3 wo = b.wo;
    if(lobe == COAT || lobe == METAL || lobe == SPECULAR){
        if(wi.z <= 0.0) return float3(0.0);
        const float a = lobe == COAT ? b.coatAlpha : b.alpha;
        const float3 h = normalize(wo + wi);
        const float oh = dot(wo, h);
        if(oh <= 0.0) return float3(0.0);
        const float D = ggxD(h, a);
        pdf = ggxG1(wo, a) * D / (4.0 * wo.z);
        const float common = D * ggxG2(wo, wi, a) / (4.0 * wo.z);
        if(lobe == COAT) return float3(b.clearcoat * schlick(0.04, oh) * common);
        if(lobe == METAL) return b.coatAttenuation * metal * schlick3(b.metalF0, oh) * b.metalMs * common;
        return float3(b.coatAttenuation * opaque * b.specularScale * schlick(b.dielectricF0, oh) * b.specularMs * common);
    }
    if(lobe == DIFFUSE){
        if(wi.z <= 0.0) return float3(0.0);
        pdf = wi.z / PI;
        return b.coatAttenuation * opaque * b.diffuseScale * b.baseColor * (wi.z / PI);
    }
    // GLASS
    const float cosO = wo.z, cosI = wi.z;
    if(cosI == 0.0) return float3(0.0);
    const bool reflect = cosI > 0.0;
    const float etap = reflect ? 1.0 : b.eta;
    float3 wm = wi * etap + wo;
    if(dot(wm, wm) <= 0.0) return float3(0.0);
    wm = normalize(wm);
    if(wm.z < 0.0) wm = -wm;
    if(dot(wm, wi) * cosI < 0.0 || dot(wm, wo) * cosO < 0.0) return float3(0.0);
    const float F = fresnelDielectric(dot(wo, wm), b.eta);
    const float D = ggxD(wm, b.alpha), G = ggxG2(wo, wi, b.alpha);
    const float scale = b.coatAttenuation * glass;
    if(reflect){
        pdf = visibleNormalPdf(wo, wm, b.alpha) / (4.0 * abs(dot(wo, wm))) * F;
        return float3(scale * D * G * F / (4.0 * cosO));
    }
    const float denom = dot(wi, wm) + dot(wo, wm) / etap;
    const float denom2 = denom * denom;
    if(denom2 <= 0.0) return float3(0.0);
    pdf = visibleNormalPdf(wo, wm, b.alpha) * abs(dot(wi, wm)) / denom2 * (1.0 - F);
    const float ft = (1.0 - F) * D * G * abs(dot(wi, wm) * dot(wo, wm) / (cosO * denom2)) / (etap * etap);
    return scale * b.baseColor * ft;
}

float3 evalBsdf(Bsdf b, float3 wi, out float pdf){
    float3 f = float3(0.0);
    pdf = 0.0;
    for(int lobe = 0; lobe < 5; ++lobe){
        if(b.weights[lobe] <= 0.0) continue;
        float lobePdf;
        f += evalLobe(b, lobe, wi, lobePdf);
        pdf += b.weights[lobe] * lobePdf;
    }
    return f;
}

bool sampleBsdf(Bsdf b, float2 u, float choice, out float3 wiOut, out float3 weight, out float pdfOut, out bool glossy){
    wiOut = float3(0.0, 0.0, 1.0); weight = float3(0.0); pdfOut = 0.0; glossy = false;
    int lobe = 4;
    float start = 0.0;
    for(int k = 0; k < 5; ++k){
        if(b.weights[k] <= 0.0) continue;
        if(choice < start + b.weights[k] || k == 4){ lobe = k; break; }
        start += b.weights[k];
    }
    while(lobe > 0 && b.weights[lobe] <= 0.0) --lobe;
    const float rest = clamp((choice - start) / max(b.weights[lobe], 1e-12), 0.0, 0.99999994);
    float3 wi;
    if(lobe == DIFFUSE){
        const float r = sqrt(u.x), phi = 2.0 * PI * u.y;
        wi = float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u.x)));
    }else if(lobe == GLASS){
        const float3 h = sampleVisibleNormal(b.wo, b.alpha, u);
        const float oh = dot(b.wo, h);
        const float F = fresnelDielectric(oh, b.eta);
        if(rest < F){
            wi = 2.0 * oh * h - b.wo;
            if(wi.z <= 0.0) return false;
        }else{
            const float sin2T = (1.0 - oh * oh) / (b.eta * b.eta);
            if(sin2T >= 1.0) return false;
            const float cosT = sqrt(1.0 - sin2T);
            wi = -b.wo / b.eta + (oh / b.eta - cosT) * h;
            if(wi.z >= 0.0) return false;
            wi = normalize(wi);
        }
    }else{
        const float3 h = sampleVisibleNormal(b.wo, lobe == COAT ? b.coatAlpha : b.alpha, u);
        wi = 2.0 * dot(b.wo, h) * h - b.wo;
        if(wi.z <= 0.0) return false;
    }
    float pdf;
    const float3 f = evalBsdf(b, wi, pdf);
    if(!(pdf > 0.0) || isinf(pdf) || isnan(pdf)) return false;
    wiOut = wi;
    pdfOut = pdf;
    weight = f / pdf;
    glossy = lobe != DIFFUSE && (lobe == COAT ? b.coatAlpha : b.alpha) < 0.09;
    return true;
}

//=============================================================================================
// SVJETLA - Renderer.cpp, sampleLight
//=============================================================================================
struct LightSample{
    float3 wi;
    float distance;
    float3 value;
    float pdf;
    bool delta;
    uint light;         // izabrano svjetlo i vjerojatnost izbora (pdf / pick = gustoca svjetla)
    float pick;
};

uint pickLight(uint count, float choice){
    uint low = 0, high = count;
    while(low < high){
        const uint middle = (low + high) / 2;
        if(lights[middle].pick.y > choice) high = middle; else low = middle + 1;
    }
    return min(low, count - 1);
}

// Stablo svjetala - CompiledScene::lightTreeImportance / chooseLight / choiceProbability
float lightTreeImportance(uint node, float3 p, float3 n){
    const float4 a = lightTreeNodes[node * 4], b = lightTreeNodes[node * 4 + 1], c = lightTreeNodes[node * 4 + 2];
    const float3 centre = 0.5 * (a.xyz + b.xyz);
    const float r = 0.5 * length(b.xyz - a.xyz);
    const float3 v = centre - p;
    const float d2 = dot(v, v);
    const float dist = sqrt(d2);
    const float thetaU = dist > r ? asin(min(1.0, r / dist)) : PI;
    const float3 toP = dist > 0.0 ? -v / dist : float3(0.0, 0.0, 1.0);
    const float thetaI = acos(clamp(dot(c.xyz, toP), -1.0, 1.0));
    const float thetaP = max(0.0, thetaI - b.w - thetaU);
    if(thetaP >= c.w) return 0.0;
    float importance = a.w * cos(thetaP) / max(d2, max(r * r, 1e-8));
    if(dot(n, n) > 0.0){
        const float thetaN = acos(clamp(dot(n, -toP), -1.0, 1.0));
        const float thetaNP = max(0.0, thetaN - thetaU);
        if(thetaNP >= 0.5 * PI) return 0.0;
        importance *= cos(thetaNP);
    }
    return max(importance, 0.0);
}
// upit stablu: tocka (p, n) ili odsjecak zrake o + t d, t u [a, b] (CompiledScene::chooseLightOnSegment)
struct LightQuery{ float3 p; float3 n; float3 d; float a; float b; bool segment; };
LightQuery pointQuery(float3 p, float3 n){
    LightQuery q; q.p = p; q.n = n; q.d = float3(0.0); q.a = 0.0; q.b = 0.0; q.segment = false;
    return q;
}
LightQuery segmentQuery(float3 o, float3 d, float a, float b){
    LightQuery q; q.p = o; q.n = float3(0.0); q.d = d; q.a = a; q.b = b; q.segment = true;
    return q;
}
float nodeImportance(uint node, LightQuery q){
    if(!q.segment) return lightTreeImportance(node, q.p, q.n);
    const float4 lo = lightTreeNodes[node * 4], hi = lightTreeNodes[node * 4 + 1];
    const float3 centre = 0.5 * (lo.xyz + hi.xyz);
    const float r = 0.5 * length(hi.xyz - lo.xyz);
    const float3 p = q.p + q.d * clamp(dot(centre - q.p, q.d), q.a, q.b);
    return lightTreeImportance(node, p, float3(0.0)) * max(length(centre - p), max(r, 1e-4));
}
bool chooseLight(Params P, float choice, LightQuery q, out uint light, out float probability){
    light = 0; probability = 0.0;
    const float localPick = P.lightTree.x;
    if(choice < localPick && asuint(P.lightTree.w) > 0){
        float u = min(choice / localPick, 0.99999994);
        float prob = localPick;
        uint node = 0;
        for(uint guard = 0; guard < 64; ++guard){
            const uint4 info = asuint(lightTreeNodes[node * 4 + 3]);
            if(info.z != 0){ light = info.x; probability = prob; return prob > 0.0; }
            const float a = nodeImportance(info.x, q), b = nodeImportance(info.y, q);
            const float total = a + b;
            if(!(total > 0.0)) return false;
            const float pl = a / total;
            if(u < pl){ u = min(u / pl, 0.99999994); prob *= pl; node = info.x; }
            else{ u = min((u - pl) / (1.0 - pl), 0.99999994); prob *= 1.0 - pl; node = info.y; }
        }
        return false;
    }
    const uint count = asuint(P.lightTree.z);
    if(count == 0) return false;
    const float u = localPick < 1.0 ? (choice - localPick) / (1.0 - localPick) : 0.0;
    uint low = 0, high = count;
    while(low < high){
        const uint middle = (low + high) / 2;
        if(infiniteLights[middle].y <= u) low = middle + 1; else high = middle;
    }
    light = asuint(infiniteLights[min(low, count - 1)].x);
    probability = lights[light].pick.x;
    return probability > 0.0;
}
bool chooseLight(Params P, float choice, float3 p, float3 n, out uint light, out float probability){
    return chooseLight(P, choice, pointQuery(p, n), light, probability);
}
float choiceProbability(Params P, uint light, LightQuery q){
    const uint leaf = lightTreeLeaf[light];
    if(leaf == NONE) return lights[light].pick.x;
    float prob = P.lightTree.x;
    uint node = leaf;
    for(uint guard = 0; guard < 64; ++guard){
        const uint parent = asuint(lightTreeNodes[node * 4 + 3].w);
        if(parent == NONE) break;
        const uint4 info = asuint(lightTreeNodes[parent * 4 + 3]);
        const uint sibling = info.x == node ? info.y : info.x;
        const float mine = nodeImportance(node, q), other = nodeImportance(sibling, q);
        const float total = mine + other;
        if(!(total > 0.0)) return 0.0;
        prob *= mine / total;
        node = parent;
    }
    return prob;
}
float choiceProbability(Params P, uint light, float3 p, float3 n){ return choiceProbability(P, light, pointQuery(p, n)); }

// tocka na trokutu-svjetlu (jednoliko po povrsini)
float3 trianglePoint(Light light, float2 u, out float b1, out float b2){
    const uint slot = asuint(light.pick.z);
    const float su = sqrt(u.x);
    b1 = 1.0 - su; b2 = u.y * su;
    return triangles[slot * 3].xyz + triangles[slot * 3 + 1].xyz * b1 + triangles[slot * 3 + 2].xyz * b2;
}

bool sampleLightIndex(Params P, uint index, float pick, float3 p, float2 u, out LightSample ls){
    ls.wi = float3(0.0, 1.0, 0.0); ls.distance = INF; ls.value = float3(0.0); ls.pdf = 0.0; ls.delta = false;
    ls.light = index; ls.pick = pick;
    const Light light = lights[index];
    const uint kind = asuint(light.radiance.w);
    const uint lightFlags = asuint(light.cone.w);
    const bool delta = (lightFlags & 1u) != 0, spot = (lightFlags & 2u) != 0;
    if(kind == LIGHT_SKY){
        float pdf;
        ls.value = skySample(P, u, ls.wi, pdf);
        if(!(pdf > 0.0)) return false;
        ls.pdf = pdf * pick;
        return true;
    }
    if(kind == LIGHT_SUN){
        if(delta){
            ls.wi = light.axis.xyz; ls.value = light.radiance.rgb; ls.pdf = pick; ls.delta = true;
            return true;
        }
        ls.wi = sampleCone(light.axis.xyz, light.axis.w, u);
        ls.value = light.radiance.rgb;
        ls.pdf = pick / (2.0 * PI * light.cone.x);
        return true;
    }
    if(kind == LIGHT_SPHERE){
        const float3 toCentre = light.position.xyz - p;
        const float d2 = dot(toCentre, toCentre);
        if(d2 <= 0.0) return false;
        const float d = sqrt(d2);
        const float3 axis = toCentre / d;
        const float falloff = spot ? spotFalloff(dot(-axis, light.axis.xyz), light.cone.y, light.cone.z) : 1.0;
        if(falloff <= 0.0) return false;
        if(delta){
            ls.wi = axis; ls.distance = d; ls.value = light.radiance.rgb * falloff / d2; ls.pdf = pick; ls.delta = true;
            return true;
        }
        const float radius = light.position.w;
        if(d <= radius * 1.0001) return false;
        const float sin2 = radius * radius / d2;
        const float omc = oneMinusCosFromSin2(sin2);
        ls.wi = sampleCone(axis, 1.0 - omc, u);
        float t;
        ls.distance = hitSphere(p, ls.wi, light.position.xyz, radius, INF, t) ? t : d - radius;
        ls.value = light.radiance.rgb * falloff;
        ls.pdf = pick / (2.0 * PI * omc);
        return true;
    }
    // trokut: vrhovi iz polja trokuta (redom BVH-a) - svjetlo pamti svoje mjesto
    const uint slot = asuint(light.pick.z);
    const float3 e1 = triangles[slot * 3 + 1].xyz, e2 = triangles[slot * 3 + 2].xyz;
    float b1, b2;
    const float3 q = trianglePoint(light, u, b1, b2);
    const float3 toLight = q - p;
    const float dist2 = dot(toLight, toLight);
    if(dist2 <= 0.0) return false;
    const float dist = sqrt(dist2);
    ls.wi = toLight / dist;
    const float3 nl = normalize(cross(e1, e2));
    const uint original = asuint(triangles[slot * 3].w);
    const Material m = materials[triangleInfo[original].w];
    const float cosLight = m.mode.y != 0 ? abs(dot(nl, ls.wi)) : -dot(nl, ls.wi);
    if(cosLight < 1e-6) return false;
    float3 emitted = m.emission.rgb;
    if(m.textures.w >= 0) emitted *= sampleTexture(uint(m.textures.w), uvOf(original, b1, b2)).rgb;
    ls.value = emitted;
    ls.distance = dist;
    ls.pdf = pick * dist2 / (cosLight * light.pick.w);
    return true;
}

bool sampleLight(Params P, float3 p, float3 n, float choice, float2 u, out LightSample ls){
    ls.wi = float3(0.0, 1.0, 0.0); ls.distance = INF; ls.value = float3(0.0); ls.pdf = 0.0; ls.delta = false;
    ls.light = 0; ls.pick = 1.0;
    if(P.counts.x == 0) return false;
    uint index;
    float pick;
    if(!chooseLight(P, choice, p, n, index, pick)) return false;
    return sampleLightIndex(P, index, pick, p, u, ls);
}

// odsjecak zrake kroz medij (Renderer.cpp MediumSegment) i gustoca ekviangularne strategije
struct MediumSegment{ float3 o; float3 d; float a; float b; float far; };
float equiangularDensity(Params P, MediumSegment segment, uint index, float3 centre, float t){
    const uint kind = asuint(lights[index].radiance.w);
    if(kind != LIGHT_SPHERE && kind != LIGHT_TRIANGLE) return 0.0;
    return equiangularPdf(equiangular(segment.o, segment.d, centre, segment.a, segment.b), t) *
           choiceProbability(P, index, segmentQuery(segment.o, segment.d, segment.a, segment.far));
}

float skyLightDensity(Params P, float3 d){
    if(P.flags.x == 0 || P.flags.w == NONE) return 0.0;
    return skyDensity(P, d) * lights[P.flags.w].pick.x;
}

float3 clampContribution(float3 c, bool indirect, float clampValue){
    if(!indirect || clampValue <= 0.0) return c;
    const float l = luminance(c);
    return l > clampValue ? c * (clampValue / l) : c;
}

// Distorzija lece, u pikselima slike - Tracer::Camera::distortPixel / undistortPixel
bool distortedLens(Params P){ return P.distortion.x > 0.0 && (P.values.y != 0.0 || P.values.z != 0.0); }
float2 distortPixel(Params P, float2 p){
    if(!distortedLens(P)) return p;
    const float2 n = (p - P.distortion.zw) / P.distortion.xy;
    const float r2 = dot(n, n);
    return P.distortion.zw + P.distortion.xy * n * (1.0 + r2 * (P.values.y + r2 * P.values.z));
}
float2 undistortPixel(Params P, float2 p){
    if(!distortedLens(P)) return p;
    const float2 m = (p - P.distortion.zw) / P.distortion.xy;
    float2 n = m;
    for(int step = 0; step < 8; ++step){
        const float r2 = dot(n, n);
        const float factor = 1.0 + r2 * (P.values.y + r2 * P.values.z);
        if(factor <= 0.0) break;
        n = m / factor;
    }
    return P.distortion.zw + P.distortion.xy * n;
}

float3 backplateAt(Params P, float3 local, bool clampToFrame, out bool inside){
    const float2 px = distortPixel(P, float2(P.lens.y + P.lens.x * local.x / -local.z, P.lens.z - P.lens.x * local.y / -local.z));
    float2 uv = px / float2(P.lens2.y, P.lens2.z);
    inside = uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
    if(clampToFrame) uv = clamp(uv, float2(0.0), float2(1.0));
    return sampleTexture(P.flags.z, uv).rgb;
}

// Sto zraka koja je pobjegla vidi: nebo, diskovi sunaca i (za kameru kroz staklo) snimka
float3 escaped(Params P, float3 d, float bsdfPdf, bool fromCamera, bool mirrorChain, bool useMis){
    float3 total = float3(0.0);
    if(!fromCamera && mirrorChain && P.flags.z != NONE){
        const float3 local = transformVector(P.inverseX, P.inverseY, P.inverseZ, d);
        if(local.z < 0.0){
            bool inside;
            return backplateAt(P, local, true, inside);
        }
    }
    if(fromCamera && P.flags.y == 0) return total;
    if(P.flags.x != 0){
        const float w = useMis ? powerHeuristic(bsdfPdf, skyLightDensity(P, d)) : 1.0;
        total += skyRadiance(P, d) * w;
    }
    for(uint i = 0; i < P.counts.y; ++i){
        const Light light = lights[lightLists[i]];
        if(dot(d, light.axis.xyz) < light.axis.w) continue;
        const float lightPdf = light.pick.x / (2.0 * PI * light.cone.x);
        total += light.radiance.rgb * (useMis ? powerHeuristic(bsdfPdf, lightPdf) : 1.0);
    }
    return total;
}

float3 offsetPoint(float3 p, float3 n, bool outward, float epsilonScale){
    const float m = max(max(abs(p.x), abs(p.y)), abs(p.z));
    const float eps = max(epsilonScale, 4e-6 * m);
    return p + n * (outward ? eps : -eps);
}

float tent(float u){
    const float r = 0.75;
    return u < 0.5 ? -r + r * sqrt(2.0 * u) : r - r * sqrt(2.0 - 2.0 * u);
}

void cameraRay(Params P, float2 pixelIn, float2 lens, out float3 origin, out float3 direction){
    const float2 pixel = undistortPixel(P, pixelIn);
    float3 d = float3((pixel.x - P.lens.y) / P.lens.x, -(pixel.y - P.lens.z) / P.lens.x, -1.0);
    float3 o = float3(0.0);
    if(P.lens.w > 0.0){
        const float2 s = lens * 2.0 - 1.0;
        float2 disk = float2(0.0);
        if(s.x != 0.0 || s.y != 0.0){
            const float pi4 = PI * 0.25;
            if(abs(s.x) > abs(s.y)) disk = s.x * float2(cos(pi4 * s.y / s.x), sin(pi4 * s.y / s.x));
            else disk = s.y * float2(cos(2.0 * pi4 - pi4 * s.x / s.y), sin(2.0 * pi4 - pi4 * s.x / s.y));
        }
        o = float3(disk * P.lens.w, 0.0);
        d = d * P.lens2.x - o;
    }
    origin = transformPoint(P.cameraX, P.cameraY, P.cameraZ, P.cameraPos, o);
    direction = normalize(transformVector(P.cameraX, P.cameraY, P.cameraZ, d));
}

//=============================================================================================
// PUTANJA - Renderer::trace
//=============================================================================================
struct PathResult{
    float3 radiance;
    float3 background;
    float3 inscatter;       // magla s kamerine zrake, u cg bez pokrivenosti (Renderer.cpp)
    bool object, catcher, miss;
    float3 lit, shadowed;
    float3 albedo, normal;
};

// STANJE PUTANJE: sve sto tijelo petlje odbijanja nosi iz koraka u korak. trace() je startPath +
// stepPath do kraja; tracePixel vrti vise uzoraka u jednoj petlji (regeneracija putanja)
struct PathState{
    PathResult result;
    Sampler sampler;
    float3 origin, direction;
    float realDepth, coneWidth, coneSpread;
    float3 beta, radiance;
    float previousPdf;
    float3 previousNormal, previousPoint;
    bool mirrorChain, sawRough, caustic, previousMedium;
    MediumSegment previousSegment;
    float previousT, previousEvent;
    uint depth, sampleIndex;
};

PathState startPath(Params P, float2 pixel, uint sampleIndex, uint pixelSeed){
    PathState st;
    st.sampleIndex = sampleIndex;
    st.depth = 0;
    PathResult result;
    result.radiance = float3(0.0); result.background = float3(0.0); result.inscatter = float3(0.0);
    result.object = false; result.catcher = false; result.miss = false;
    result.lit = float3(0.0); result.shadowed = float3(0.0);
    result.albedo = float3(0.0); result.normal = float3(0.0);

    Sampler sampler;
    sampler.seed = pixelSeed; sampler.index = sampleIndex; sampler.dimension = 0;

    const float2 filterSample = sampler.next2D();
    const float2 lensSample = sampler.next2D();
    float3 origin, direction;
    const float2 samplePixel = pixel + float2(tent(filterSample.x), tent(filterSample.y));
    rayTime = 0.5;
    if(P.motion.x >= 2 || P.motion.y >= 2) rayTime = sampler.next2D().x;
    if(P.motion.y >= 2){
        // kamera u trenutku uzorka: pomak linearno, osi linearno pa Gram-Schmidt (Tracer::cameraAt)
        uint k; float f;
        motionSegment(P.motion.y, rayTime, k, f);
        const uint a = k * 4, b = a + 4;
        const float3 x = normalize(lerp(motionData[a].xyz, motionData[b].xyz, f));
        float3 y = lerp(motionData[a + 1].xyz, motionData[b + 1].xyz, f);
        y = normalize(y - x * dot(x, y));
        Params moving = P;
        moving.cameraX = float4(x, 0.0);
        moving.cameraY = float4(y, 0.0);
        moving.cameraZ = float4(cross(x, y), 0.0);
        moving.cameraPos = float4(lerp(motionData[a + 3].xyz, motionData[b + 3].xyz, f), 1.0);
        cameraRay(moving, samplePixel, lensSample, origin, direction);
    }else cameraRay(P, samplePixel, lensSample, origin, direction);
    // stvarna ploha ispred uzorka (Scene::holdout), 0 = nema je
    float realDepth = 0.0;
    const uint holdoutTexture = asuint(P.holdout.x);
    if(holdoutTexture != NONE){
        const uint4 info = textureInfo[holdoutTexture];
        const float2 at = samplePixel * float2(float(info.y) / P.lens2.y, float(info.z) / P.lens2.z);
        realDepth = fetch(info, int(floor(at.x)), int(floor(at.y))).r;
        if(!(realDepth > 0.0) || realDepth > 5e9) realDepth = 0.0;
    }
    float coneWidth = 0.0, coneSpread = 1.0 / max(1.0, P.lens.x);

    float3 beta = float3(1.0);
    float3 radiance = float3(0.0);
    float previousPdf = 0.0;
    float3 previousNormal = float3(0.0);
    bool mirrorChain = true;
    float3 previousPoint = origin;
    bool sawRough = false, caustic = false;
    bool previousMedium = false;
    MediumSegment previousSegment;
    previousSegment.o = float3(0.0); previousSegment.d = float3(0.0);
    previousSegment.a = 0.0; previousSegment.b = 0.0; previousSegment.far = 0.0;
    float previousT = 0.0, previousEvent = 0.0;

    st.result = result;
    st.sampler = sampler;
    st.origin = origin;
    st.direction = direction;
    st.realDepth = realDepth;
    st.coneWidth = coneWidth;
    st.coneSpread = coneSpread;
    st.beta = beta;
    st.radiance = radiance;
    st.previousPdf = previousPdf;
    st.previousNormal = previousNormal;
    st.previousPoint = previousPoint;
    st.mirrorChain = mirrorChain;
    st.sawRough = sawRough;
    st.caustic = caustic;
    st.previousMedium = previousMedium;
    st.previousSegment = previousSegment;
    st.previousT = previousT;
    st.previousEvent = previousEvent;
    return st;
}

bool stepBody(Params P, uint pixelSeed, uint sampleIndex, uint depth, inout PathResult result, inout Sampler sampler,
              inout float3 origin, inout float3 direction, float realDepth, inout float coneWidth, inout float coneSpread,
              inout float3 beta, inout float3 radiance, inout float previousPdf, inout float3 previousNormal,
              inout float3 previousPoint, inout bool mirrorChain, inout bool sawRough, inout bool caustic,
              inout bool previousMedium, inout MediumSegment previousSegment, inout float previousT, inout float previousEvent){
    const float sceneRadius = P.lens2.w;
    const float epsilonScale = max(1e-6 * sceneRadius, 1e-7);
    const float clampValue = P.values.x;
    const uint maxBounces = P.counts.w;
    const uint candidates = max(1u, asuint(P.lightTree.y));
    const bool glass = (P.extra.z & 1u) != 0;
    const uint volumeCount = asuint(P.holdout.z);
    const bool equiangularOn = (P.extra.z & 4u) == 0 && P.lightTree.x > 0.0 && P.counts.x > 0;
    Hit hit;
    hit.t = INF; hit.slot = NONE; hit.u = 0.0; hit.v = 0.0;
    const uint salt = depth * 7919u + 13u;
    traverse(origin, direction, INF, depth == 0 ? 0u : 1u, false, pixelSeed, sampleIndex, salt, hit);
    const bool valid = hit.slot != NONE;
    countCoherence(valid ? triangleInfo[asuint(triangles[hit.slot * 3].w)].w : 0xFFFFFFFEu);

    // kugla svjetla ispred plohe
    float sphereT = valid ? hit.t : INF;
    int sphere = -1;
    for(uint i = 0; i < P.counts.z; ++i){
        const uint index = lightLists[P.counts.y + i];
        const Light l = lights[index];
        float t;
        if(hitSphere(origin, direction, l.position.xyz, l.position.w, sphereT, t)){ sphereT = t; sphere = int(index); }
    }
    // magla: dogadjaj rasprsenja prije plohe ili svjetla (Renderer.cpp)
    if(volumeCount > 0){
        const float2 free = sampler.next2D();
        float t;
        uint which;
        float end = sphere >= 0 ? sphereT : (valid ? hit.t : INF);
        // holdout: iza stvarne plohe iz splata kamera ne vidi maglu (Renderer.cpp)
        if(depth == 0 && realDepth > 0.0){
            const float forward = -dot(direction, P.cameraZ.xyz);
            if(forward > 1e-6) end = min(end, realDepth * (1.0 + P.holdout.y) / forward);
        }
        MediaOnRay media = mediaOnRay(volumeCount, origin, direction, end);
        MediumSegment segment;
        segment.o = origin; segment.d = direction;
        const bool inMedium = media.extent(end, segment.a, segment.b);
        segment.far = segment.b >= INF ? segment.a + 2.0 * sceneRadius : segment.b;
        if(inMedium && equiangularOn && depth < maxBounces){
            const float2 choice = sampler.next2D(), u = sampler.next2D(), v = sampler.next2D();
            uint index;
            float pick;
            if(chooseLight(P, choice.x, segmentQuery(origin, direction, segment.a, segment.far), index, pick)){
                const Light light = lights[index];
                const uint kind = asuint(light.radiance.w);
                if(kind == LIGHT_SPHERE || kind == LIGHT_TRIANGLE){
                    float b1, b2;
                    const float3 centre = kind == LIGHT_TRIANGLE ? trianglePoint(light, u, b1, b2) : light.position.xyz;
                    const Equiangular e = equiangular(origin, direction, centre, segment.a, segment.b);
                    const float te = equiangularSample(e, v.x);
                    const float pe = equiangularPdf(e, te);
                    const float event = (te >= segment.a && te <= segment.b) ? media.eventPdf(te) : 0.0;
                    const float3 p = origin + direction * te;
                    // stvarno sigma(t) T(t) (Renderer.cpp)
                    float real = event;
                    uint scatterer = 0;
                    if(event > 0.0){
                        if(media.varies){
                            VolumeRng rng;
                            rng.state = hash2(pixelSeed ^ hash2(salt + 523u, sampleIndex * 977u), 0x68e31da4u);
                            real = media.realTotal(te, v.y, scatterer);
                            if(real > 0.0) real *= media.transmittance(te, rng);
                        }else scatterer = media.pick(te, v.y);
                    }
                    LightSample ls;
                    if(pe > 0.0 && real > 0.0 && sampleLightIndex(P, index, pick, p, u, ls)){
                        const float3 albedo = volumes[scatterer * VOLUME_STRIDE + 3].rgb;
                        const float phase = phaseOf(scatterer, dot(direction, ls.wi));
                        if(phase > 0.0 && luminance(albedo * ls.value) > 0.0){
                            const float limit = ls.distance >= INF ? INF : ls.distance * (1.0 - 1e-4);
                            bool crossed;
                            const float3 through = shadowTransmittance(p, ls.wi, limit, 2u, glass, pixelSeed, sampleIndex, salt + 419u,
                                                                       volumeCount, crossed);
                            if(luminance(through) > 0.0){
                                const float own = ls.pdf / pick;
                                const float w = powerHeuristic3(pe * ls.pdf, event * choiceProbability(P, index, p, float3(0.0)) * own,
                                                                ls.delta || crossed ? 0.0 : event * phase);
                                const float3 c = clampContribution(beta * albedo * ls.value * through * (phase * real * w / (pe * ls.pdf)),
                                                                   depth > 0, clampValue);
                                if(depth == 0) result.inscatter += c; else radiance += c;
                            }
                        }
                    }
                }
            }
        }
        if(inMedium && sampleVolume(media, end, free, hash2(pixelSeed ^ hash2(salt + 211u, sampleIndex * 977u), 0x1b873593u), t, which)){
            const float4 medium = volumes[which * VOLUME_STRIDE + 3];
            const float event = media.eventPdf(t);
            const float3 p = origin + direction * t;
            if(depth == 0) result.object = true;
            beta *= medium.rgb;
            if(depth >= maxBounces || !(luminance(beta) > 0.0)) return false;
            {
                // RIS kao na plohi (Renderer.cpp)
                LightSample chosen;
                chosen.wi = float3(0.0, 1.0, 0.0); chosen.distance = INF; chosen.value = float3(0.0); chosen.pdf = 1.0; chosen.delta = false;
                chosen.light = 0; chosen.pick = 1.0;
                float chosenPhase = 0.0, chosenTarget = 0.0, weightSum = 0.0;
                for(uint k = 0; k < candidates; ++k){
                    LightSample ls;
                    const float2 choice = sampler.next2D();
                    const float2 u = sampler.next2D();
                    if(!sampleLight(P, p, float3(0.0), choice.x, u, ls)) continue;
                    const float phase = phaseOf(which, dot(direction, ls.wi));
                    const float target = luminance(ls.value) * phase * (ls.delta ? 1.0 : powerHeuristic(ls.pdf, phase));
                    if(!(target > 0.0)) continue;
                    weightSum += target / ls.pdf;
                    if(choice.y * weightSum < target / ls.pdf){ chosen = ls; chosenPhase = phase; chosenTarget = target; }
                }
                if(chosenTarget > 0.0){
                    const float limit = chosen.distance >= INF ? INF : chosen.distance * (1.0 - 1e-4);
                    bool crossed;
                    const float3 through = shadowTransmittance(p, chosen.wi, limit, 2u, glass, pixelSeed, sampleIndex, salt + 307u,
                                                               volumeCount, crossed);
                    if(luminance(through) > 0.0){
                        float equi = 0.0;
                        if(equiangularOn){
                            const Light light = lights[chosen.light];
                            const float3 centre = asuint(light.radiance.w) == LIGHT_TRIANGLE ? p + chosen.wi * chosen.distance : light.position.xyz;
                            equi = equiangularDensity(P, segment, chosen.light, centre, t) * (chosen.pdf / chosen.pick);
                        }
                        const float w = powerHeuristic3(event * chosen.pdf, chosen.delta || crossed ? 0.0 : event * chosenPhase, equi);
                        radiance += clampContribution(beta * chosen.value * through * (chosenPhase * w * weightSum / (float(candidates) * chosenTarget)),
                                                      depth > 0, clampValue);
                    }
                }
            }
            float pdf;
            const float3 next = samplePhase(which, direction, sampler.next2D(), pdf);
            const float2 roulette = sampler.next2D();
            mirrorChain = false;
            previousPdf = pdf;
            previousPoint = p;
            previousNormal = float3(0.0);
            previousMedium = true;
            previousSegment = segment;
            previousT = t;
            previousEvent = event;
            coneSpread += 0.2;
            if(glass){ sawRough = true; caustic = false; }
            if(depth >= 3){
                const float keep = clamp(max(max(beta.r, beta.g), beta.b), 0.05, 0.95);
                if(roulette.x >= keep) return false;
                beta /= keep;
            }
            origin = p;
            direction = next;
            return true;
        }
    }

    if(sphere >= 0){
        const Light l = lights[uint(sphere)];
        const float3 p = origin + direction * sphereT;
        float falloff = 1.0;
        if((asuint(l.cone.w) & 2u) != 0) falloff = spotFalloff(dot(normalize(p - l.position.xyz), l.axis.xyz), l.cone.y, l.cone.z);
        float3 le = l.radiance.rgb * falloff;
        if(depth > 0){
            const float3 toCentre = l.position.xyz - previousPoint;
            const float d2 = dot(toCentre, toCentre);
            const float sin2 = min(1.0, l.position.w * l.position.w / max(d2, 1e-20));
            const float lightPdf = choiceProbability(P, uint(sphere), previousPoint, previousNormal) / (2.0 * PI * oneMinusCosFromSin2(sin2));
            if(previousMedium && equiangularOn){
                const float equi = equiangularDensity(P, previousSegment, uint(sphere), l.position.xyz, previousT) /
                                   (2.0 * PI * oneMinusCosFromSin2(sin2));
                le *= powerHeuristic3(previousEvent * previousPdf, previousEvent * lightPdf, equi);
            }else le *= powerHeuristic(previousPdf, lightPdf);
        }
        if(!caustic) radiance += clampContribution(beta * le, depth > 1, clampValue);
        if(depth == 0) result.object = true;
        return false;
    }

    if(!valid){
        if(depth == 0){
            result.miss = true;
            result.background = escaped(P, direction, 0.0, true, false, false);
        }else if(!caustic){
            radiance += clampContribution(beta * escaped(P, direction, previousPdf, false, mirrorChain, true), depth > 1, clampValue);
        }
        return false;
    }

    //-- ploha --------------------------------------------------------------------------------
    const uint original = asuint(triangles[hit.slot * 3].w);
    const uint flags = asuint(triangles[hit.slot * 3 + 1].w);
    const uint emitter = asuint(triangles[hit.slot * 3 + 2].w);
    const uint4 info = triangleInfo[original];
    const Material material = materials[info.w];
    const float b0 = 1.0 - hit.u - hit.v;
    float3 v0, e1, e2;
    triangleAt(hit.slot, v0, e1, e2);
    const float3 p = v0 + e1 * hit.u + e2 * hit.v;
    if(depth == 0 && realDepth > 0.0 && -toCamera(P, p).z > realDepth * (1.0 + P.holdout.y)){
        result.miss = true;
        result.background = escaped(P, direction, 0.0, true, false, false);
        return false;
    }
    float3 ng = normalize(cross(e1, e2));
    const float4 va = vertices[info.x * 3], vb = vertices[info.y * 3], vc = vertices[info.z * 3];
    float3 ns = normalAt(info.x, va.xyz) * b0 + normalAt(info.y, vb.xyz) * hit.u + normalAt(info.z, vc.xyz) * hit.v;
    ns = dot(ns, ns) > 1e-20 ? normalize(ns) : ng;
    if(dot(ng, ns) < 0.0) ng = -ng;
    const float2 uv = float2(va.w, vertices[info.x * 3 + 1].x) * b0 + float2(vb.w, vertices[info.y * 3 + 1].x) * hit.u +
                      float2(vc.w, vertices[info.z * 3 + 1].x) * hit.v;

    // razina mipmape iz stosca zrake (Renderer.cpp)
    coneWidth += coneSpread * hit.t;
    float lodBase = -INF;
    {
        const float2 uv0 = float2(va.w, vertices[info.x * 3 + 1].x);
        const float2 du = float2(vb.w, vertices[info.y * 3 + 1].x) - uv0, dv = float2(vc.w, vertices[info.z * 3 + 1].x) - uv0;
        const float uvArea = abs(du.x * dv.y - du.y * dv.x);
        const float worldArea = length(cross(e1, e2));
        const float cosHit = max(0.05, abs(dot(ng, direction)));
        if((P.extra.z & 2u) == 0 && uvArea > 0.0 && worldArea > 0.0 && coneWidth > 0.0)
            lodBase = log2(coneWidth / cosHit) + 0.5 * log2(uvArea / worldArea);
    }

    float3 baseColor = material.baseColor.rgb;
    float metallic = material.surface.x, roughness = material.surface.y;
    float transmission = material.layers.y, clearcoat = material.layers.z;
    const float ior = max(1.0001, material.surface.w);
    if(material.textures.x >= 0) baseColor *= sampleTextureLod(uint(material.textures.x), uv, lodFor(uint(material.textures.x), lodBase)).rgb;
    if(material.textures.y >= 0){
        const float4 mr = sampleTextureLod(uint(material.textures.y), uv, lodFor(uint(material.textures.y), lodBase));
        roughness *= mr.g;
        metallic *= mr.b;
    }
    if(material.textures.z >= 0){
        const float4 tangent = float4(vertices[info.x * 3 + 1].yzw, vertices[info.x * 3 + 2].x) * b0 +
                               float4(vertices[info.y * 3 + 1].yzw, vertices[info.y * 3 + 2].x) * hit.u +
                               float4(vertices[info.z * 3 + 1].yzw, vertices[info.z * 3 + 2].x) * hit.v;
        float3 t = tangent.xyz - ns * dot(ns, tangent.xyz);
        if(dot(t, t) > 1e-20){
            t = normalize(t);
            const float3 bt = cross(ns, t) * (tangent.w < 0.0 ? -1.0 : 1.0);
            float3 m = sampleTextureLod(uint(material.textures.z), uv, lodFor(uint(material.textures.z), lodBase)).rgb * 2.0 - 1.0;
            m.x *= material.surface.z;
            m.y *= material.surface.z;
            const float3 bent = t * m.x + bt * m.y + ns * m.z;
            if(dot(bent, bent) > 1e-20) ns = normalize(bent);
        }
    }
    float3 emitted = material.emission.rgb;
    if(material.textures.w >= 0) emitted *= sampleTextureLod(uint(material.textures.w), uv, lodFor(uint(material.textures.w), lodBase)).rgb;

    const float3 wo = -direction;
    const bool outside = dot(ng, wo) > 0.0;
    if(!outside){ ng = -ng; ns = -ns; }
    if(dot(ns, wo) < 1e-4) ns = ng;
    const float eta = outside ? ior : 1.0 / ior;

    if(depth == 0){
        result.normal = outside ? ns : -ns;
        result.albedo = baseColor;
    }

    //-- shadow catcher ---------------------------------------------------------------------------
    if(depth == 0 && (flags & CATCHER) != 0){
        result.catcher = true;
        result.background = escaped(P, direction, 0.0, true, false, false);
        const float3 from = offsetPoint(p, ng, true, epsilonScale);
        for(int k = 0; k < 2; ++k){
            LightSample ls;
            const float2 choice = sampler.next2D();
            if(!sampleLight(P, p, ng, choice.x, sampler.next2D(), ls)) continue;
            const float cosine = dot(ng, ls.wi);
            if(cosine <= 0.0) continue;
            const float3 c = ls.value * (cosine / PI) / ls.pdf;
            result.lit += c;
            bool crossed;
            result.shadowed += c * shadowTransmittance(from, ls.wi, ls.distance * (1.0 - 1e-4), 3u, glass,
                                                       pixelSeed, sampleIndex, salt + 101u, volumeCount, crossed);
        }
        return false;
    }

    //-- stvarna scena u odrazu i lomu -------------------------------------------------------------
    if(depth > 0 && (flags & CATCHER) != 0 && P.flags.z != NONE){
        const float3 local = toCamera(P, p);
        if(local.z < 0.0){
            bool inside;
            const float3 plateColour = backplateAt(P, local, false, inside);
            if(inside){
                if(mirrorChain){
                    radiance += clampContribution(beta * plateColour, depth > 1, clampValue);
                    return false;
                }
                baseColor = min(plateColour, float3(0.9));
                metallic = 0.0;
                transmission = 0.0;
                clearcoat = 0.0;
                roughness = max(roughness, 0.6);
            }
        }
    }

    if(luminance(emitted) > 0.0 && (material.mode.y != 0 || outside) && !caustic){
        float w = 1.0;
        if(depth > 0 && emitter != NONE){
            const Light l = lights[emitter];
            const float dist2 = hit.t * hit.t;
            const float cosLight = abs(dot(ng, direction));
            const float lightPdf = cosLight > 0.0 ? choiceProbability(P, emitter, previousPoint, previousNormal) * dist2 / (cosLight * l.pick.w) : 0.0;
            if(previousMedium && equiangularOn){
                const float equi = cosLight > 0.0 ? equiangularDensity(P, previousSegment, emitter, origin + direction * hit.t, previousT) *
                                                    dist2 / (cosLight * l.pick.w) : 0.0;
                w = powerHeuristic3(previousEvent * previousPdf, previousEvent * lightPdf, equi);
            }else w = powerHeuristic(previousPdf, lightPdf);
        }
        radiance += clampContribution(beta * emitted * w, depth > 1, clampValue);
    }
    if(depth == 0) result.object = true;
    if(depth >= maxBounces) return false;

    float3 tangentX, tangentY;
    basis(ns, tangentX, tangentY);
    const Bsdf bsdf = makeBsdf(baseColor, metallic, roughness, ior, material.layers.x, transmission, clearcoat,
                               material.layers.w, float3(dot(wo, tangentX), dot(wo, tangentY), dot(wo, ns)), eta);

    //-- izravno svjetlo (RIS, Renderer.cpp) ---------------------------------------------------------
    const float3 receiver = transmission > 0.0 ? float3(0.0) : ng;
    {
        LightSample chosen;
        chosen.wi = float3(0.0, 1.0, 0.0); chosen.distance = INF; chosen.value = float3(0.0); chosen.pdf = 1.0; chosen.delta = false;
        float3 chosenF = float3(0.0);
        float chosenBsdfPdf = 0.0, chosenTarget = 0.0, weightSum = 0.0;
        bool chosenSide = false;
        for(uint k = 0; k < candidates; ++k){
            LightSample ls;
            const float2 choice = sampler.next2D();
            const float2 u = sampler.next2D();
            if(!sampleLight(P, p, receiver, choice.x, u, ls)) continue;
            const float3 wiLocal = float3(dot(ls.wi, tangentX), dot(ls.wi, tangentY), dot(ls.wi, ns));
            const bool geometricSide = dot(ls.wi, ng) > 0.0;
            if(geometricSide != (wiLocal.z > 0.0)) continue;
            float bsdfPdf;
            const float3 f = evalBsdf(bsdf, wiLocal, bsdfPdf);
            const float target = luminance(f * ls.value) * (ls.delta ? 1.0 : powerHeuristic(ls.pdf, bsdfPdf));
            if(!(target > 0.0)) continue;
            weightSum += target / ls.pdf;
            if(choice.y * weightSum < target / ls.pdf){
                chosen = ls; chosenF = f; chosenBsdfPdf = bsdfPdf; chosenTarget = target; chosenSide = geometricSide;
            }
        }
        if(chosenTarget > 0.0){
            const float limit = chosen.distance >= INF ? INF : chosen.distance * (1.0 - 1e-4);
            bool crossed;
            const float3 through = shadowTransmittance(offsetPoint(p, ng, chosenSide, epsilonScale), chosen.wi, limit, 2u,
                                                       glass, pixelSeed, sampleIndex, salt + 211u, volumeCount, crossed);
            if(luminance(through) > 0.0){
                const float w = chosen.delta || crossed ? 1.0 : powerHeuristic(chosen.pdf, chosenBsdfPdf);
                radiance += clampContribution(beta * chosenF * chosen.value * through * (w * weightSum / (float(candidates) * chosenTarget)),
                                              depth > 0, clampValue);
            }
        }
    }

    //-- odbijanje ---------------------------------------------------------------------------------
    const float2 u = sampler.next2D();
    const float2 choice = sampler.next2D();
    float3 wiLocal, weight;
    float pdf;
    bool glossy;
    if(!sampleBsdf(bsdf, u, choice.x, wiLocal, weight, pdf, glossy)) return false;
    const float3 wi = tangentX * wiLocal.x + tangentY * wiLocal.y + ns * wiLocal.z;
    const bool geometricSide = dot(wi, ng) > 0.0;
    if(geometricSide != (wiLocal.z > 0.0)) return false;
    beta *= weight;
    const float betaLum = luminance(beta);
    if(!(betaLum > 0.0) || isinf(betaLum) || isnan(betaLum)) return false;
    mirrorChain = mirrorChain && glossy;
    if(!glossy) coneSpread += 0.2;
    if(glass){
        if(!glossy){ sawRough = true; caustic = false; }
        else if(!geometricSide && sawRough && (flags & TRANSMISSIVE) != 0) caustic = true;
    }
    previousPdf = pdf;
    previousMedium = false;
    previousPoint = p;
    previousNormal = receiver;
    if(depth >= 3){
        const float keep = clamp(max(max(beta.r, beta.g), beta.b), 0.05, 0.95);
        if(choice.y >= keep) return false;
        beta /= keep;
    }
    origin = offsetPoint(p, ng, geometricSide, epsilonScale);
    direction = wi;
    return true;
}

// Jedno odbijanje; false kad je putanja gotova (rezultat u s.result)
bool stepPath(Params P, uint pixelSeed, inout PathState s){
    const bool more = stepBody(P, pixelSeed, s.sampleIndex, s.depth, s.result, s.sampler, s.origin, s.direction, s.realDepth,
                               s.coneWidth, s.coneSpread, s.beta, s.radiance, s.previousPdf, s.previousNormal, s.previousPoint,
                               s.mirrorChain, s.sawRough, s.caustic, s.previousMedium, s.previousSegment, s.previousT, s.previousEvent);
    if(!more){ s.result.radiance = s.radiance; return false; }
    s.depth += 1;
    return true;
}

PathResult trace(Params P, float2 pixel, uint sampleIndex, uint pixelSeed){
    PathState s = startPath(P, pixel, sampleIndex, pixelSeed);
    profileStep = 0;
    while(stepPath(P, pixelSeed, s)) profileStep += 1;
    return s.result;
}

// Najblizi trokut koji kamera vidi, bez alfa testa (dubina)
bool traverseDepth(float3 origin, float3 direction, inout Hit hit){
    float3 d = direction;
    d.x = abs(d.x) > 1e-20 ? d.x : (d.x < 0.0 ? -1e-20 : 1e-20);
    d.y = abs(d.y) > 1e-20 ? d.y : (d.y < 0.0 ? -1e-20 : 1e-20);
    d.z = abs(d.z) > 1e-20 ? d.z : (d.z < 0.0 ? -1e-20 : 1e-20);
    const float3 inverse = 1.0 / d;
    const float3 originScaled = origin * inverse;
    uint stack[64];
    int top = 0;
    float entry;
    float tMax = INF;
    if(!slab(0, originScaled, inverse, tMax, entry)) return false;
    stack[top++] = 0;
    bool found = false;
    while(top > 0){
        const uint node = stack[--top];
        const uint leftOrFirst = asuint(nodes[node * 2].w);
        const uint count = asuint(nodes[node * 2 + 1].w);
        if(count > 0){
            for(uint i = 0; i < count; ++i){
                const uint slot = leftOrFirst + i;
                float t, u, v;
                if(!triangleTest(slot, origin, direction, tMax, t, u, v)) continue;
                if((asuint(triangles[slot * 3 + 1].w) & NO_CAMERA) != 0) continue;
                tMax = t;
                hit.t = t; hit.slot = slot; hit.u = u; hit.v = v;
                found = true;
            }
            continue;
        }
        const uint left = leftOrFirst, right = left + 1;
        float entryLeft, entryRight;
        const bool hitLeft = slab(left, originScaled, inverse, tMax, entryLeft);
        const bool hitRight = slab(right, originScaled, inverse, tMax, entryRight);
        if(hitLeft && hitRight){
            if(entryLeft <= entryRight){ stack[top++] = right; stack[top++] = left; }
            else{ stack[top++] = left; stack[top++] = right; }
        }else if(hitLeft) stack[top++] = left;
        else if(hitRight) stack[top++] = right;
    }
    return found;
}

// Isto pravilo kao Tracer::adaptiveConverged
bool adaptiveConverged(float luminanceSum, float luminance2Sum, float samples, float threshold){
    if(threshold <= 0.0 || samples <= 0.0) return false;
    const float mean = luminanceSum / samples;
    const float var = max(0.0, luminance2Sum / samples - mean * mean);
    return sqrt(var / samples) < threshold * sqrt(mean + 1e-4);
}

//=============================================================================================
// UZORCI PO PIKSELU, POJAS REDAKA
//=============================================================================================
// Jedan uzorak jednog piksela; entry point je u tracer.slang (BVH) i tracer_rq.slang (ray query)
void tracePixel(uint3 id){
    const Params P = paramsBuffer[0];
    const uint width = uint(P.lens2.y), height = uint(P.lens2.z);
    const uint x = id.x, y = push.rowStart + id.y;
    if(x >= width || id.y >= push.rowCount || y >= height) return;
    const uint pixel = y * width + x;
    const uint pixelSeed = hash1(hash1(x * 0x9E3779B1u ^ y) ^ (y * 0x85EBCA77u)) ^ hash1(P.extra.x);
    // prilagodljivo (Renderer::adaptiveCheckpoint): bitovi 1/2 gotov na parnoj/neparnoj provjeri, 4 stao.
    // Susjedi se citaju s PROSLE provjere - taj bit u ovom dispatchu nitko ne pise
    uint state = adaptive[pixel];
    if((state & 4u) != 0) return;
    const uint s = push.sampleIndex;
    bool stop = false;
    if(P.values.w > 0.0 && s % 8u == 0u && s >= max(8u, P.extra.w)){
        const uint now = 1u << ((s / 8u) & 1u), before = now ^ 3u;
        const bool own = adaptiveConverged(accAlbedo[pixel].w, accNormal[pixel].w, accShadowed[pixel].w, P.values.w);
        state = own ? (state | now) : (state & ~now);
        stop = own && (state & before) != 0;
        for(uint k = 0; k < 9u; ++k){
            const int nx = int(x) + int(k % 3u) - 1, ny = int(y) + int(k / 3u) - 1;
            const bool inside = k != 4u && nx >= 0 && ny >= 0 && nx < int(width) && ny < int(height);
            if(inside && stop) stop = (adaptive[uint(ny) * width + uint(nx)] & before) != 0;
        }
        if(stop) state |= 4u;
    }
    // upis uvijek, izvan grane: lavapipe (LLVM 20) rusi uvjetni upis u ovaj buffer
    adaptive[pixel] = state;
    if(stop) return;

    // REGENERACIJA PUTANJA: sampleCount uzoraka u JEDNOJ petlji po odbijanjima. Traka kojoj putanja
    // zavrsi odmah pocne sljedeci uzorak istog piksela, pa val ne ceka najdulju putanju za svaki
    // uzorak posebno (od dubine 2 bilo je samo 35 % aktivnih traka). Uzorci su isti kao trace()
    // za svaki indeks; zbroji se skupe lokalno i upisu jednom
    const uint count = max(1u, push.sampleCount);
    const float2 centre = float2(float(x) + 0.5, float(y) + 0.5);
    float4 cg = float4(0.0), background = float4(0.0), lit = float4(0.0), shadowed = float4(0.0);
    float4 albedoSum = float4(0.0), normalSum = float4(0.0);
    PathState path = startPath(P, centre, push.sampleIndex, pixelSeed);
    uint done = 0;
    for(uint guard = 0; guard < 1048576u; ++guard){
        profileStep = guard;
        if(stepPath(P, pixelSeed, path)) continue;
        const PathResult r = path.result;
        if(r.object){ cg.rgb += r.radiance; cg.w += 1.0; }
        cg.rgb += r.inscatter;
        if(r.miss){ background.rgb += r.background; background.w += 1.0; }
        if(r.catcher){
            background.rgb += r.background;
            lit.rgb += r.lit;
            lit.w += 1.0;
        }
        shadowed.rgb += r.shadowed;
        shadowed.w += 1.0;
        const float lum = luminance(r.radiance + r.background + r.inscatter);
        albedoSum += float4(r.albedo, lum);
        normalSum += float4(r.normal, lum * lum);
        done += 1;
        if(done >= count) break;
        path = startPath(P, centre, push.sampleIndex + done, pixelSeed);
    }
    accCg[pixel] = accCg[pixel] + cg;
    accBackground[pixel] = accBackground[pixel] + background;
    if(lit.w > 0.0) accLit[pixel] = accLit[pixel] + lit;
    accShadowed[pixel] = accShadowed[pixel] + shadowed;
    accAlbedo[pixel] = accAlbedo[pixel] + albedoSum;
    accNormal[pixel] = accNormal[pixel] + normalSum;

    // dubina iz sredista piksela, jednom
    if(push.sampleIndex == 0){
        float3 o, d;
        Params centre = P;
        centre.lens.w = 0.0;
        cameraRay(centre, float2(float(x) + 0.5, float(y) + 0.5), float2(0.5), o, d);
        Hit hit;
        hit.t = INF; hit.slot = NONE; hit.u = 0.0; hit.v = 0.0;
        float depth = 1e10;
        // bez alfe, kao procesor: samo zastavica kamere
        rayTime = 0.5;
        if(traverseDepth(o, d, hit)) depth = -toCamera(P, o + d * hit.t).z;
        accDepth[pixel] = depth;
    }
}

