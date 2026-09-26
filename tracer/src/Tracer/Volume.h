#pragma once
//=============================================================================================
// VOLUMENI - magla u orijentiranim kutijama i eksponencijalna magla po visini (Scene::volumes).
//
// Medij je siv po gubitku (sigma_t = gustoca, ista za sve boje), a boja je u albedu rasprsenja:
// slobodni put je jedan broj, uzorkuje se tocno i tezina dogadjaja je samo albedo.
//
//   KUTIJA   jednolika gustoca u kutiji -0.5..0.5 lokalno; vise kutija se smije preklapati - duz
//            zrake je gustoca po dijelovima konstantna, opticka debljina linearna po dijelovima
//   VISINA   sigma(p) = d0 * exp(-k * h), h = visina iznad ishodista duz lokalne +Y, k = 1/height.
//            Duz zrake h = h0 + t*dh, pa je opticka debljina analiticka:
//            tau(a,b) = d0 e^(-k h0) (e^(-k dh a) - e^(-k dh b)) / (k dh)
//
// SLOBODNI PUT: tau(t) svih medija zajedno raste monotono; trazi se t s tau(t) = -ln(1-u).
// Samo kutije: po dijelovima, tocno. S maglom po visini: Newton sa zastitom bisekcijom na
// zbroju analitickih funkcija (tocno do float preciznosti). Medij koji rasprsuje se bira
// razmjerno svojoj gustoci u toj tocki. Isti racun je u shaders/include/TracerCore.slang.
//
// NEHOMOGENA KUTIJA (meki rub, sum): gustoca d * rub(p) * sum(p) <= d (1 + sum) = majoranta.
// Slobodni put ide po majoranti (za ostale medije tocno kao prije) - DELTA TRACKING: u
// nehomogenoj kutiji dogadjaj je stvaran s vjerojatnoscu sigma(p) / majoranta, inace se
// nastavlja dalje. Propusnost zrake sjene: analiticki za homogene medije, RATIO TRACKING kroz
// nehomogene (umnozak 1 - sigma/majoranta po probnim tockama) - nepristrano, bez koraka.
//
// FAZA: mjesavina dvaju Henyey-Greenstein rezanja (g, g2, udio lobeMix). cosTheta je kosinus
// izmedju smjera zrake (od kamere) i novog smjera: g > 0 = sjaj oko sunca.
//=============================================================================================
#include "Tracer/Scene.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace Tracer{

constexpr uint32_t MaxVolumeSpans = 8;         //kutija na jednoj zraci (vise se preskoci)

struct VolumeSpan{ float t0, t1; uint32_t index; };

//Odsjecak zrake (o + t d, t u [0, tMax]) unutar kutije -0.5..0.5 lokalno. inverse: svijet -> kutija
inline bool boxInterval(const glm::mat4& inverse, const glm::vec3& o, const glm::vec3& d, float tMax, float& t0, float& t1){
    const glm::vec3 lo = glm::vec3(inverse * glm::vec4(o, 1.0f));
    const glm::vec3 ld = glm::mat3(inverse) * d;
    t0 = 0.0f;
    t1 = tMax;
    for(int k = 0; k < 3; ++k){
        if(std::abs(ld[k]) < 1e-20f){
            if(lo[k] < -0.5f || lo[k] > 0.5f) return false;
            continue;
        }
        const float a = (-0.5f - lo[k]) / ld[k], b = (0.5f - lo[k]) / ld[k];
        t0 = std::max(t0, std::min(a, b));
        t1 = std::min(t1, std::max(a, b));
    }
    return t1 > t0;
}

//Kutije koje zraka presijece (najvise MaxVolumeSpans); magla po visini nije kutija
inline uint32_t volumeSpans(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                            const glm::vec3& o, const glm::vec3& d, float tMax, VolumeSpan* out){
    uint32_t n = 0;
    for(uint32_t i = 0; i < volumes.size() && n < MaxVolumeSpans; ++i){
        if(volumes[i].shape != Volume::Shape::Box || !(volumes[i].density > 0.0f)) continue;
        float t0, t1;
        if(boxInterval(inverses[i], o, d, tMax, t0, t1)) out[n++] = {t0, t1, i};
    }
    return n;
}

//-- nehomogena gustoca kutije ------------------------------------------------------------------
//Hash cjelobrojne resetke i vrijednosni sum (isti bitovi na kartici)
inline uint32_t latticeHash(int x, int y, int z){
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
inline float valueNoise(const glm::vec3& p){
    const glm::vec3 f = glm::floor(p);
    const glm::vec3 x = p - f;
    const glm::vec3 w = x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
    const int ix = int(f.x), iy = int(f.y), iz = int(f.z);
    auto at = [&](int dx, int dy, int dz){ return float(latticeHash(ix + dx, iy + dy, iz + dz) >> 8) * (1.0f / 16777216.0f); };
    const float x00 = at(0, 0, 0) + (at(1, 0, 0) - at(0, 0, 0)) * w.x, x10 = at(0, 1, 0) + (at(1, 1, 0) - at(0, 1, 0)) * w.x;
    const float x01 = at(0, 0, 1) + (at(1, 0, 1) - at(0, 0, 1)) * w.x, x11 = at(0, 1, 1) + (at(1, 1, 1) - at(0, 1, 1)) * w.x;
    const float y0 = x00 + (x10 - x00) * w.y, y1 = x01 + (x11 - x01) * w.y;
    return y0 + (y1 - y0) * w.z;
}
//4 oktave, 0..1, pa kontrast (smoothstep 0.25..0.75): pramenovi i praznine, srednja ~0.5
inline float cloudNoise(glm::vec3 p){
    float sum = 0.0f, amplitude = 1.0f;
    for(int octave = 0; octave < 4; ++octave){
        sum += amplitude * valueNoise(p + glm::vec3(float(octave) * 17.31f));
        amplitude *= 0.5f;
        p *= 2.03f;
    }
    const float n = std::clamp((sum / 1.875f - 0.25f) * 2.0f, 0.0f, 1.0f);
    return n * n * (3.0f - 2.0f * n);
}
//Udio nazivne gustoce u tocki p (0..1+noise). inverse: svijet -> kutija; redak k inverza ima
//duljinu 1/velicina_k, pa je sum u svjetskim mjerilima kutije i giba se s njom
inline float densityFactor(const Volume& v, const glm::mat4& inverse, const glm::vec3& p){
    const glm::vec3 l = glm::vec3(inverse * glm::vec4(p, 1.0f));
    float f = 1.0f;
    if(v.edge > 0.0f){
        for(int k = 0; k < 3; ++k){
            const float x = std::clamp((0.5f - std::abs(l[k])) / v.edge, 0.0f, 1.0f);
            f *= x * x * (3.0f - 2.0f * x);
        }
    }
    if(v.noise > 0.0f && f > 0.0f){
        glm::vec3 size;
        for(int k = 0; k < 3; ++k) size[k] = 1.0f / std::max(1e-20f, glm::length(glm::vec3(inverse[0][k], inverse[1][k], inverse[2][k])));
        const float n = cloudNoise(l * size / std::max(v.noiseScale, 1e-4f));
        f *= (1.0f - v.noise) + 2.0f * v.noise * n;
    }
    return f;
}
inline float majorantOf(const Volume& v){ return v.density * (v.shape == Volume::Shape::Box ? 1.0f + std::max(0.0f, v.noise) : 1.0f); }

//Mali generator za delta i ratio tracking (broj koraka nije unaprijed poznat)
struct VolumeRng{
    uint32_t state;
    float next(){
        state = state * 747796405u + 2891336453u;
        uint32_t w = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        w = (w >> 22u) ^ w;
        return float(w >> 8) * (1.0f / 16777216.0f);
    }
};

//-- magla po visini -----------------------------------------------------------------------------
struct HeightRay{ float d0, k, h0, dh; };       //sigma(t) = d0 * exp(-k * (h0 + dh t))

inline HeightRay heightRay(const Volume& v, const glm::vec3& o, const glm::vec3& d){
    const glm::vec3 up = glm::normalize(glm::vec3(v.toWorld[1]));
    const glm::vec3 origin(v.toWorld[3]);
    return {v.density, 1.0f / std::max(v.height, 1e-6f), glm::dot(o - origin, up), glm::dot(d, up)};
}
inline float heightSigma(const HeightRay& h, float t){
    return h.d0 * std::exp(std::min(80.0f, -h.k * (h.h0 + h.dh * t)));
}
//tau od 0 do t (t smije biti beskonacan)
inline float heightTau(const HeightRay& h, float t){
    if(!(t > 0.0f)) return 0.0f;
    const float base = h.d0 * std::exp(std::min(80.0f, -h.k * h.h0));
    const float x = h.k * h.dh;
    if(std::isinf(t)) return x > 1e-12f ? base / x : std::numeric_limits<float>::infinity();
    if(std::abs(x) * t < 1e-4f) return base * t * (1.0f - 0.5f * x * t);
    return base * (1.0f - std::exp(std::min(80.0f, -x * t))) / x;
}

//Svi mediji na zraci: kutije (odsjeci, s majorantom) i magle po visini. tau i sigma su po
//MAJORANTI (za homogene medije to je stvarna gustoca)
struct MediaOnRay{
    glm::vec3 o{0.0f}, d{0.0f};
    VolumeSpan spans[MaxVolumeSpans];
    float spanSigma[MaxVolumeSpans];            //majoranta
    bool spanVaries[MaxVolumeSpans];            //nehomogena: stvarna gustoca po tocki
    uint32_t spanCount = 0;
    HeightRay heights[MaxVolumeSpans];
    uint32_t heightIndex[MaxVolumeSpans];
    uint32_t heightCount = 0;
    bool varies = false;

    float tau(const std::vector<Volume>&, float t) const{
        float sum = 0.0f;
        for(uint32_t i = 0; i < spanCount; ++i) sum += spanSigma[i] * std::max(0.0f, std::min(t, spans[i].t1) - spans[i].t0);
        for(uint32_t i = 0; i < heightCount; ++i) sum += heightTau(heights[i], t);
        return sum;
    }
    float sigma(const std::vector<Volume>&, float t) const{
        float sum = 0.0f;
        for(uint32_t i = 0; i < spanCount; ++i) if(spans[i].t0 <= t && t <= spans[i].t1) sum += spanSigma[i];
        for(uint32_t i = 0; i < heightCount; ++i) sum += heightSigma(heights[i], t);
        return sum;
    }
    bool empty() const{ return spanCount == 0 && heightCount == 0; }
    //Dio zrake [a, b] u kojem ima medija (odsjeci su vec rezani na tMax; magla po visini je svuda)
    bool extent(float tMax, float& a, float& b) const{
        if(empty()) return false;
        a = heightCount > 0 ? 0.0f : std::numeric_limits<float>::infinity();
        b = heightCount > 0 ? tMax : 0.0f;
        for(uint32_t i = 0; i < spanCount; ++i){ a = std::min(a, spans[i].t0); b = std::max(b, spans[i].t1); }
        return b > a;
    }
    //Gustoca dogadjaja po majoranti sigma(t) exp(-tau(t)) - samo za MIS tezine (ista funkcija u
    //svim strategijama, pa smije biti priblizna)
    float eventPdf(const std::vector<Volume>& volumes, float t) const{
        return sigma(volumes, t) * std::exp(-tau(volumes, t));
    }
    //Medij razmjerno majoranti u t (probni dogadjaj delta trackinga)
    uint32_t pick(const std::vector<Volume>& volumes, float t, float u) const{
        float left = u * sigma(volumes, t);
        uint32_t which = spanCount > 0 ? spans[0].index : heightIndex[0];
        for(uint32_t i = 0; i < spanCount; ++i){
            if(!(spans[i].t0 <= t && t <= spans[i].t1)) continue;
            which = spans[i].index;
            left -= spanSigma[i];
            if(left < 0.0f) return which;
        }
        for(uint32_t i = 0; i < heightCount; ++i){
            which = heightIndex[i];
            left -= heightSigma(heights[i], t);
            if(left < 0.0f) return which;
        }
        return which;
    }
    //Stvarna gustoca medija `index` u t
    float realSigma(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses, uint32_t index, float t) const{
        const Volume& v = volumes[index];
        if(v.shape == Volume::Shape::Height){
            for(uint32_t i = 0; i < heightCount; ++i) if(heightIndex[i] == index) return heightSigma(heights[i], t);
            return 0.0f;
        }
        return v.heterogeneous() ? v.density * densityFactor(v, inverses[index], o + d * t) : v.density;
    }
    //Ukupna stvarna gustoca u t i medij razmjerno njoj (u); total 0: nista
    float realTotal(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses, float t, float u, uint32_t& which) const{
        float values[2 * MaxVolumeSpans];
        uint32_t owners[2 * MaxVolumeSpans];
        uint32_t n = 0;
        float total = 0.0f;
        for(uint32_t i = 0; i < spanCount; ++i){
            if(!(spans[i].t0 <= t && t <= spans[i].t1)) continue;
            const float s = spanVaries[i] ? realSigma(volumes, inverses, spans[i].index, t) : spanSigma[i];
            values[n] = s; owners[n++] = spans[i].index; total += s;
        }
        for(uint32_t i = 0; i < heightCount; ++i){
            const float s = heightSigma(heights[i], t);
            values[n] = s; owners[n++] = heightIndex[i]; total += s;
        }
        which = n > 0 ? owners[0] : 0u;
        float left = u * total;
        for(uint32_t i = 0; i < n; ++i){
            which = owners[i];
            left -= values[i];
            if(left < 0.0f) break;
        }
        return total;
    }
    //Propusnost do t: homogeni dijelovi analiticki, nehomogene kutije ratio trackingom
    float transmittance(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses, float t, VolumeRng& rng) const{
        float smooth = 0.0f;
        for(uint32_t i = 0; i < spanCount; ++i)
            if(!spanVaries[i]) smooth += spanSigma[i] * std::max(0.0f, std::min(t, spans[i].t1) - spans[i].t0);
        for(uint32_t i = 0; i < heightCount; ++i) smooth += heightTau(heights[i], t);
        float T = std::exp(-smooth);
        for(uint32_t i = 0; i < spanCount && T > 0.0f; ++i){
            if(!spanVaries[i] || !(spanSigma[i] > 0.0f)) continue;
            const float end = std::min(t, spans[i].t1);
            float x = spans[i].t0;
            const Volume& v = volumes[spans[i].index];
            for(int step = 0; step < 1024; ++step){
                x -= std::log(std::max(1e-12f, 1.0f - rng.next())) / spanSigma[i];
                if(x >= end) break;
                T *= 1.0f - v.density * densityFactor(v, inverses[spans[i].index], o + d * x) / spanSigma[i];
                //Ruski rulet kad propusnost padne: nepristrano, bez dugih repova kroz gustu maglu
                if(T < 0.05f){
                    if(rng.next() >= 0.5f){ T = 0.0f; break; }
                    T *= 2.0f;
                }
            }
        }
        return T;
    }
};

inline MediaOnRay mediaOnRay(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                             const glm::vec3& o, const glm::vec3& d, float tMax){
    MediaOnRay m;
    m.o = o; m.d = d;
    m.spanCount = volumeSpans(volumes, inverses, o, d, tMax, m.spans);
    for(uint32_t i = 0; i < m.spanCount; ++i){
        const Volume& v = volumes[m.spans[i].index];
        m.spanSigma[i] = majorantOf(v);
        m.spanVaries[i] = v.heterogeneous();
        m.varies = m.varies || m.spanVaries[i];
    }
    for(uint32_t i = 0; i < volumes.size() && m.heightCount < MaxVolumeSpans; ++i){
        if(volumes[i].shape != Volume::Shape::Height || !(volumes[i].density > 0.0f)) continue;
        m.heights[m.heightCount] = heightRay(volumes[i], o, d);
        m.heightIndex[m.heightCount++] = i;
    }
    return m;
}

//Propusnost do tMax (zraka sjene)
inline float volumeTransmittance(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                                 const glm::vec3& o, const glm::vec3& d, float tMax, VolumeRng& rng){
    return mediaOnRay(volumes, inverses, o, d, tMax).transmittance(volumes, inverses, tMax, rng);
}
//Opticka debljina homogenih medija (nehomogeni po majoranti) - samo za prikaz u sucelju
inline float opticalDepth(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                          const glm::vec3& o, const glm::vec3& d, float tMax){
    return mediaOnRay(volumes, inverses, o, d, tMax).tau(volumes, tMax);
}

//t s tau(t) = target po majoranti; false kad je tau(tMax) manji
inline bool solveTau(const std::vector<Volume>& volumes, const MediaOnRay& m, float tMax, float from, float target, float& t){
    if(m.heightCount == 0){
        //Samo kutije: po dijelovima konstantno, tocno
        float edges[2 * MaxVolumeSpans];
        for(uint32_t i = 0; i < m.spanCount; ++i){ edges[2 * i] = m.spans[i].t0; edges[2 * i + 1] = m.spans[i].t1; }
        std::sort(edges, edges + 2 * m.spanCount);
        float tau = target;
        for(uint32_t e = 0; e + 1 < 2 * m.spanCount; ++e){
            const float a = edges[e], b = edges[e + 1];
            if(!(b > a)) continue;
            const float sigma = m.sigma(volumes, 0.5f * (a + b));
            if(sigma <= 0.0f) continue;
            if(tau <= sigma * (b - a)){ t = std::max(from, a + tau / sigma); return true; }
            tau -= sigma * (b - a);
        }
        return false;
    }
    //Newton sa zastitom bisekcijom na tau(t) = cilj
    if(m.tau(volumes, tMax) < target) return false;
    float lo = from, hi = tMax;
    if(std::isinf(hi)){
        hi = std::max(1.0f, 2.0f * from);
        for(int k = 0; k < 80 && m.tau(volumes, hi) < target; ++k) hi *= 2.0f;
    }
    float x = 0.5f * (lo + hi);
    for(int iteration = 0; iteration < 48; ++iteration){
        const float f = m.tau(volumes, x) - target;
        if(std::abs(f) <= 1e-6f * std::max(1.0f, target)) break;
        if(f > 0.0f) hi = x; else lo = x;
        const float slope = m.sigma(volumes, x);
        float next = slope > 0.0f ? x - f / slope : 0.5f * (lo + hi);
        if(!(next > lo && next < hi)) next = 0.5f * (lo + hi);
        if(next == x) break;
        x = next;
    }
    t = x;
    return true;
}

//Slobodni put: false kad zraka prode do tMax bez dogadjaja. Inace t i medij koji rasprsuje.
//u.x put, u.y izbor medija (prvi probni dogadjaj); dalje (nehomogeno) generator sa sjemenom seed
inline bool sampleVolume(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses, const MediaOnRay& m,
                         float tMax, glm::vec2 u, uint32_t seed, float& t, uint32_t& which){
    if(m.empty()) return false;
    float target = -std::log(std::max(1e-12f, 1.0f - u.x));
    float from = 0.0f;
    VolumeRng rng{seed};
    for(int step = 0; step < 256; ++step){
        if(!solveTau(volumes, m, tMax, from, target, t)) return false;
        which = m.pick(volumes, t, u.y);
        if(!m.varies || !volumes[which].heterogeneous()) return true;
        //Delta tracking: stvaran s vjerojatnoscu sigma / majoranta
        const float real = m.realSigma(volumes, inverses, which, t);
        if(rng.next() * majorantOf(volumes[which]) < real) return true;
        from = t;
        target -= std::log(std::max(1e-12f, 1.0f - rng.next()));
        u.y = rng.next();
    }
    return false;
}
inline bool sampleVolume(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                         const glm::vec3& o, const glm::vec3& d, float tMax, glm::vec2 u, uint32_t seed, float& t, uint32_t& which){
    return sampleVolume(volumes, inverses, mediaOnRay(volumes, inverses, o, d, tMax), tMax, u, seed, t, which);
}

//-- ekviangularno uzorkovanje (Kulla & Fajardo 2012) -------------------------------------------
//Udaljenost na zraci o + t d, t u [a, b], s gustocom razmjernom 1/r^2 prema tocki c: kut prema c
//je jednolik. Kod tockastih i malih svjetala u magli slobodni put vecinu uzoraka baci daleko od
//svjetla - ovo ih stavi tamo gdje je jednostruko rasprsenje jako. D ima donju granicu (zraka kroz
//samo svjetlo); ista je u uzorku i gustoci pa je gustoca tocna.
struct Equiangular{ float delta = 0.0f, D = 1.0f, thetaA = 0.0f, thetaB = 0.0f; };

inline Equiangular equiangular(const glm::vec3& o, const glm::vec3& d, const glm::vec3& c, float a, float b){
    Equiangular e;
    e.delta = glm::dot(c - o, d);
    e.D = std::max(glm::length(c - (o + d * e.delta)), 1e-5f * (1.0f + std::abs(e.delta)));
    e.thetaA = std::atan((a - e.delta) / e.D);
    e.thetaB = std::isinf(b) ? 0.5f * glm::pi<float>() : std::atan((b - e.delta) / e.D);
    return e;
}
inline float equiangularSample(const Equiangular& e, float u){
    return e.delta + e.D * std::tan(e.thetaA + (e.thetaB - e.thetaA) * u);
}
inline float equiangularPdf(const Equiangular& e, float t){
    const float span = e.thetaB - e.thetaA;
    if(!(span > 0.0f)) return 0.0f;
    const float x = t - e.delta;
    return e.D / (span * (e.D * e.D + x * x));
}

//-- faza ----------------------------------------------------------------------------------------
inline float phaseHG(float cosTheta, float g){
    const float denominator = 1.0f + g * g - 2.0f * g * cosTheta;
    return (1.0f - g * g) / (4.0f * glm::pi<float>() * denominator * std::sqrt(std::max(denominator, 1e-12f)));
}
inline float phaseOf(const Volume& v, float cosTheta){
    return v.lobeMix > 0.0f ? (1.0f - v.lobeMix) * phaseHG(cosTheta, v.anisotropy) + v.lobeMix * phaseHG(cosTheta, v.anisotropy2)
                            : phaseHG(cosTheta, v.anisotropy);
}

//Novi smjer oko d po HG; pdf = phaseHG(dot(d, novi), g)
inline glm::vec3 samplePhaseHG(const glm::vec3& d, float g, glm::vec2 u, float& pdf){
    float cosTheta;
    if(std::abs(g) < 1e-3f) cosTheta = 1.0f - 2.0f * u.x;
    else{
        const float s = (1.0f - g * g) / (1.0f - g + 2.0f * g * u.x);
        cosTheta = std::clamp((1.0f + g * g - s * s) / (2.0f * g), -1.0f, 1.0f);
    }
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * glm::pi<float>() * u.y;
    const glm::vec3 a = glm::normalize(std::abs(d.x) > 0.9f ? glm::cross(d, glm::vec3(0, 1, 0)) : glm::cross(d, glm::vec3(1, 0, 0)));
    const glm::vec3 b = glm::cross(d, a);
    pdf = phaseHG(cosTheta, g);
    return glm::normalize(d * cosTheta + (a * std::cos(phi) + b * std::sin(phi)) * sinTheta);
}
//Mjesavina rezanja: rezanj po udjelu, pdf je pdf mjesavine
inline glm::vec3 samplePhase(const Volume& v, const glm::vec3& d, glm::vec2 u, float& pdf){
    float g = v.anisotropy;
    if(v.lobeMix > 0.0f){
        if(u.x < v.lobeMix){ g = v.anisotropy2; u.x = std::min(u.x / v.lobeMix, 0.99999994f); }
        else u.x = std::min((u.x - v.lobeMix) / (1.0f - v.lobeMix), 0.99999994f);
    }
    float single;
    const glm::vec3 next = samplePhaseHG(d, g, u, single);
    pdf = phaseOf(v, glm::dot(d, next));
    return next;
}

}
