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

//Svi mediji na zraci: kutije (odsjeci) i magle po visini
struct MediaOnRay{
    VolumeSpan spans[MaxVolumeSpans];
    uint32_t spanCount = 0;
    HeightRay heights[MaxVolumeSpans];
    uint32_t heightIndex[MaxVolumeSpans];
    uint32_t heightCount = 0;
    float tau(const std::vector<Volume>& volumes, float t) const{
        float sum = 0.0f;
        for(uint32_t i = 0; i < spanCount; ++i)
            sum += volumes[spans[i].index].density * std::max(0.0f, std::min(t, spans[i].t1) - spans[i].t0);
        for(uint32_t i = 0; i < heightCount; ++i) sum += heightTau(heights[i], t);
        return sum;
    }
    float sigma(const std::vector<Volume>& volumes, float t) const{
        float sum = 0.0f;
        for(uint32_t i = 0; i < spanCount; ++i) if(spans[i].t0 <= t && t <= spans[i].t1) sum += volumes[spans[i].index].density;
        for(uint32_t i = 0; i < heightCount; ++i) sum += heightSigma(heights[i], t);
        return sum;
    }
};

inline MediaOnRay mediaOnRay(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                             const glm::vec3& o, const glm::vec3& d, float tMax){
    MediaOnRay m;
    m.spanCount = volumeSpans(volumes, inverses, o, d, tMax, m.spans);
    for(uint32_t i = 0; i < volumes.size() && m.heightCount < MaxVolumeSpans; ++i){
        if(volumes[i].shape != Volume::Shape::Height || !(volumes[i].density > 0.0f)) continue;
        m.heights[m.heightCount] = heightRay(volumes[i], o, d);
        m.heightIndex[m.heightCount++] = i;
    }
    return m;
}

//Opticka debljina do tMax: prolaz kroz maglu je exp(-ovo)
inline float opticalDepth(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                          const glm::vec3& o, const glm::vec3& d, float tMax){
    return mediaOnRay(volumes, inverses, o, d, tMax).tau(volumes, tMax);
}

//Slobodni put: false kad zraka prode do tMax bez dogadjaja. Inace t i medij koji rasprsuje
//(razmjerno gustoci u tocki). u.x put, u.y izbor medija
inline bool sampleVolume(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                         const glm::vec3& o, const glm::vec3& d, float tMax, glm::vec2 u, float& t, uint32_t& which){
    const MediaOnRay m = mediaOnRay(volumes, inverses, o, d, tMax);
    if(m.spanCount == 0 && m.heightCount == 0) return false;
    const float target = -std::log(std::max(1e-12f, 1.0f - u.x));
    if(m.heightCount == 0){
        //Samo kutije: po dijelovima konstantno, tocno
        float edges[2 * MaxVolumeSpans];
        for(uint32_t i = 0; i < m.spanCount; ++i){ edges[2 * i] = m.spans[i].t0; edges[2 * i + 1] = m.spans[i].t1; }
        std::sort(edges, edges + 2 * m.spanCount);
        float tau = target;
        bool found = false;
        for(uint32_t e = 0; e + 1 < 2 * m.spanCount && !found; ++e){
            const float a = edges[e], b = edges[e + 1];
            if(!(b > a)) continue;
            const float sigma = m.sigma(volumes, 0.5f * (a + b));
            if(sigma <= 0.0f) continue;
            if(tau <= sigma * (b - a)){ t = a + tau / sigma; found = true; }
            else tau -= sigma * (b - a);
        }
        if(!found) return false;
    }else{
        //Newton sa zastitom bisekcijom na tau(t) = cilj
        if(m.tau(volumes, tMax) < target) return false;
        float lo = 0.0f, hi = tMax;
        if(std::isinf(hi)){
            hi = 1.0f;
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
    }
    //Medij razmjerno gustoci u t
    const float total = m.sigma(volumes, t);
    float pick = u.y * total;
    which = m.spanCount > 0 ? m.spans[0].index : m.heightIndex[0];
    for(uint32_t i = 0; i < m.spanCount; ++i){
        if(!(m.spans[i].t0 <= t && t <= m.spans[i].t1)) continue;
        which = m.spans[i].index;
        pick -= volumes[m.spans[i].index].density;
        if(pick < 0.0f) return true;
    }
    for(uint32_t i = 0; i < m.heightCount; ++i){
        which = m.heightIndex[i];
        pick -= heightSigma(m.heights[i], t);
        if(pick < 0.0f) return true;
    }
    return true;
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
