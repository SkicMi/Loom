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
    uint4 extra;            // seed, conditionalOffset, bitovi (1 staklene sjene, 2 bez mipmapa), najmanje uzoraka prije zaustavljanja
    float4 distortion;      // fx, fy, cx, cy objektiva (fx 0: ravna leca)
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
    uint sampleIndex;
    uint rowStart;
    uint rowCount;
    uint padding;
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
    if((info.w & 2u) != 0) return texelsFloat[at];
    const uint p = texels[at];
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
    const float3 v0 = triangles[slot * 3].xyz;
    const float3 e1 = triangles[slot * 3 + 1].xyz;
    const float3 e2 = triangles[slot * 3 + 2].xyz;
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
}

// Propusnost zrake sjene (Tracer::Renderer shadowTransmittance): 0 kad je zaklonjena, inace
// umnozak propusnosti stakala na putu (samo uz staklene sjene). crossed: prosla kroz staklo
float3 shadowTransmittance(float3 origin, float3 direction, float tMax, uint mode, bool glass,
                           uint pixelSeed, uint sampleIndex, uint salt, out bool crossed){
    crossed = false;
    float3 through = float3(1.0);
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
};

uint pickLight(uint count, float choice){
    uint low = 0, high = count;
    while(low < high){
        const uint middle = (low + high) / 2;
        if(lights[middle].pick.y > choice) high = middle; else low = middle + 1;
    }
    return min(low, count - 1);
}

bool sampleLight(Params P, float3 p, float choice, float2 u, out LightSample ls){
    ls.wi = float3(0.0, 1.0, 0.0); ls.distance = INF; ls.value = float3(0.0); ls.pdf = 0.0; ls.delta = false;
    const uint count = P.counts.x;
    if(count == 0) return false;
    const Light light = lights[pickLight(count, choice)];
    const float pick = light.pick.x;
    if(pick <= 0.0) return false;
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
    const float3 a = triangles[slot * 3].xyz, e1 = triangles[slot * 3 + 1].xyz, e2 = triangles[slot * 3 + 2].xyz;
    const float su = sqrt(u.x);
    const float b1 = 1.0 - su, b2 = u.y * su;
    const float3 q = a + e1 * b1 + e2 * b2;
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
    bool object, catcher, miss;
    float3 lit, shadowed;
    float3 albedo, normal;
};

PathResult trace(Params P, float2 pixel, uint sampleIndex, uint pixelSeed){
    PathResult result;
    result.radiance = float3(0.0); result.background = float3(0.0);
    result.object = false; result.catcher = false; result.miss = false;
    result.lit = float3(0.0); result.shadowed = float3(0.0);
    result.albedo = float3(0.0); result.normal = float3(0.0);

    Sampler sampler;
    sampler.seed = pixelSeed; sampler.index = sampleIndex; sampler.dimension = 0;
    const float sceneRadius = P.lens2.w;
    const float epsilonScale = max(1e-6 * sceneRadius, 1e-7);
    const float clampValue = P.values.x;
    const uint maxBounces = P.counts.w;

    const float2 filterSample = sampler.next2D();
    const float2 lensSample = sampler.next2D();
    float3 origin, direction;
    cameraRay(P, pixel + float2(tent(filterSample.x), tent(filterSample.y)), lensSample, origin, direction);
    float coneWidth = 0.0, coneSpread = 1.0 / max(1.0, P.lens.x);

    float3 beta = float3(1.0);
    float3 radiance = float3(0.0);
    float previousPdf = 0.0;
    bool mirrorChain = true;
    float3 previousPoint = origin;
    const bool glass = (P.extra.z & 1u) != 0;
    bool sawRough = false, caustic = false;

    for(uint depth = 0;; ++depth){
        Hit hit;
        hit.t = INF; hit.slot = NONE; hit.u = 0.0; hit.v = 0.0;
        const uint salt = depth * 7919u + 13u;
        traverse(origin, direction, INF, depth == 0 ? 0u : 1u, false, pixelSeed, sampleIndex, salt, hit);
        const bool valid = hit.slot != NONE;

        // kugla svjetla ispred plohe
        float sphereT = valid ? hit.t : INF;
        int sphere = -1;
        for(uint i = 0; i < P.counts.z; ++i){
            const uint index = lightLists[P.counts.y + i];
            const Light l = lights[index];
            float t;
            if(hitSphere(origin, direction, l.position.xyz, l.position.w, sphereT, t)){ sphereT = t; sphere = int(index); }
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
                const float lightPdf = l.pick.x / (2.0 * PI * oneMinusCosFromSin2(sin2));
                le *= powerHeuristic(previousPdf, lightPdf);
            }
            if(!caustic) radiance += clampContribution(beta * le, depth > 1, clampValue);
            if(depth == 0) result.object = true;
            break;
        }

        if(!valid){
            if(depth == 0){
                result.miss = true;
                result.background = escaped(P, direction, 0.0, true, false, false);
            }else if(!caustic){
                radiance += clampContribution(beta * escaped(P, direction, previousPdf, false, mirrorChain, true), depth > 1, clampValue);
            }
            break;
        }

        //-- ploha --------------------------------------------------------------------------------
        const uint original = asuint(triangles[hit.slot * 3].w);
        const uint flags = asuint(triangles[hit.slot * 3 + 1].w);
        const uint emitter = asuint(triangles[hit.slot * 3 + 2].w);
        const uint4 info = triangleInfo[original];
        const Material material = materials[info.w];
        const float b0 = 1.0 - hit.u - hit.v;
        const float3 e1 = triangles[hit.slot * 3 + 1].xyz, e2 = triangles[hit.slot * 3 + 2].xyz;
        const float3 p = triangles[hit.slot * 3].xyz + e1 * hit.u + e2 * hit.v;
        float3 ng = normalize(cross(e1, e2));
        const float4 va = vertices[info.x * 3], vb = vertices[info.y * 3], vc = vertices[info.z * 3];
        float3 ns = va.xyz * b0 + vb.xyz * hit.u + vc.xyz * hit.v;
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
                if(!sampleLight(P, p, choice.x, sampler.next2D(), ls)) continue;
                const float cosine = dot(ng, ls.wi);
                if(cosine <= 0.0) continue;
                const float3 c = ls.value * (cosine / PI) / ls.pdf;
                result.lit += c;
                bool crossed;
                result.shadowed += c * shadowTransmittance(from, ls.wi, ls.distance * (1.0 - 1e-4), 3u, glass,
                                                           pixelSeed, sampleIndex, salt + 101u, crossed);
            }
            break;
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
                        break;
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
                const float lightPdf = cosLight > 0.0 ? l.pick.x * dist2 / (cosLight * l.pick.w) : 0.0;
                w = powerHeuristic(previousPdf, lightPdf);
            }
            radiance += clampContribution(beta * emitted * w, depth > 1, clampValue);
        }
        if(depth == 0) result.object = true;
        if(depth >= maxBounces) break;

        float3 tangentX, tangentY;
        basis(ns, tangentX, tangentY);
        const Bsdf bsdf = makeBsdf(baseColor, metallic, roughness, ior, material.layers.x, transmission, clearcoat,
                                   material.layers.w, float3(dot(wo, tangentX), dot(wo, tangentY), dot(wo, ns)), eta);

        //-- izravno svjetlo ---------------------------------------------------------------------------
        {
            LightSample ls;
            const float2 choice = sampler.next2D();
            const float2 u = sampler.next2D();
            if(sampleLight(P, p, choice.x, u, ls)){
                const float3 wiLocal = float3(dot(ls.wi, tangentX), dot(ls.wi, tangentY), dot(ls.wi, ns));
                const bool geometricSide = dot(ls.wi, ng) > 0.0;
                if(geometricSide == (wiLocal.z > 0.0)){
                    float bsdfPdf;
                    const float3 f = evalBsdf(bsdf, wiLocal, bsdfPdf);
                    if(luminance(f) > 0.0){
                        const float limit = ls.distance >= INF ? INF : ls.distance * (1.0 - 1e-4);
                        bool crossed;
                        const float3 through = shadowTransmittance(offsetPoint(p, ng, geometricSide, epsilonScale), ls.wi, limit, 2u,
                                                                   glass, pixelSeed, sampleIndex, salt + 211u, crossed);
                        if(luminance(through) > 0.0){
                            const float w = ls.delta || crossed ? 1.0 : powerHeuristic(ls.pdf, bsdfPdf);
                            radiance += clampContribution(beta * f * ls.value * through * (w / ls.pdf), depth > 0, clampValue);
                        }
                    }
                }
            }
        }

        //-- odbijanje ---------------------------------------------------------------------------------
        const float2 u = sampler.next2D();
        const float2 choice = sampler.next2D();
        float3 wiLocal, weight;
        float pdf;
        bool glossy;
        if(!sampleBsdf(bsdf, u, choice.x, wiLocal, weight, pdf, glossy)) break;
        const float3 wi = tangentX * wiLocal.x + tangentY * wiLocal.y + ns * wiLocal.z;
        const bool geometricSide = dot(wi, ng) > 0.0;
        if(geometricSide != (wiLocal.z > 0.0)) break;
        beta *= weight;
        const float betaLum = luminance(beta);
        if(!(betaLum > 0.0) || isinf(betaLum) || isnan(betaLum)) break;
        mirrorChain = mirrorChain && glossy;
        if(!glossy) coneSpread += 0.2;
        if(glass){
            if(!glossy){ sawRough = true; caustic = false; }
            else if(!geometricSide && sawRough && (flags & TRANSMISSIVE) != 0) caustic = true;
        }
        previousPdf = pdf;
        previousPoint = p;
        if(depth >= 3){
            const float keep = clamp(max(max(beta.r, beta.g), beta.b), 0.05, 0.95);
            if(choice.y >= keep) break;
            beta /= keep;
        }
        origin = offsetPoint(p, ng, geometricSide, epsilonScale);
        direction = wi;
    }
    result.radiance = radiance;
    return result;
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
bool adaptiveConverged(float luminanceSum, float luminance2Sum, float samples, float threshold, uint minSamples){
    if(threshold <= 0.0 || samples < float(max(8u, minSamples)) || fmod(samples, 8.0) != 0.0) return false;
    const float mean = luminanceSum / samples;
    const float var = max(0.0, luminance2Sum / samples - mean * mean);
    return sqrt(var / samples) < threshold * sqrt(mean + 1e-4);
}

//=============================================================================================
// JEDAN UZORAK PO PIKSELU, POJAS REDAKA
//=============================================================================================
[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID){
    const Params P = paramsBuffer[0];
    const uint width = uint(P.lens2.y), height = uint(P.lens2.z);
    const uint x = id.x, y = push.rowStart + id.y;
    if(x >= width || id.y >= push.rowCount || y >= height) return;
    const uint pixel = y * width + x;
    const uint pixelSeed = hash1(hash1(x * 0x9E3779B1u ^ y) ^ (y * 0x85EBCA77u)) ^ hash1(P.extra.x);
    if(adaptiveConverged(accAlbedo[pixel].w, accNormal[pixel].w, accShadowed[pixel].w, P.values.w, P.extra.w)) return;

    const PathResult r = trace(P, float2(float(x) + 0.5, float(y) + 0.5), push.sampleIndex, pixelSeed);
    float4 cg = accCg[pixel];
    if(r.object){ cg.rgb += r.radiance; cg.w += 1.0; }
    accCg[pixel] = cg;
    float4 background = accBackground[pixel];
    if(r.miss){ background.rgb += r.background; background.w += 1.0; }
    if(r.catcher){
        background.rgb += r.background;
        float4 lit = accLit[pixel];
        lit.rgb += r.lit;
        lit.w += 1.0;
        accLit[pixel] = lit;
    }
    accBackground[pixel] = background;
    float4 shadowed = accShadowed[pixel];
    shadowed.rgb += r.shadowed;
    shadowed.w += 1.0;
    accShadowed[pixel] = shadowed;
    const float lum = luminance(r.radiance + r.background);
    accAlbedo[pixel] = accAlbedo[pixel] + float4(r.albedo, lum);
    accNormal[pixel] = accNormal[pixel] + float4(r.normal, lum * lum);

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
        if(traverseDepth(o, d, hit)) depth = -toCamera(P, o + d * hit.t).z;
        accDepth[pixel] = depth;
    }
}

