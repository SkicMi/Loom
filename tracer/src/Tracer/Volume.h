#pragma once
//=============================================================================================
// VOLUMENI - jednolika magla u orijentiranim kutijama (Scene::volumes).
//
// Medij je siv po gubitku (sigma_t = density, isti za sve boje), a boja je u albedu rasprsenja:
// tako je slobodni put jedan broj, uzorkuje se tocno (bez vecinskog uzorkovanja) i tezina
// dogadjaja je samo albedo. Vise kutija se moze preklapati: duz zrake je gustoca po dijelovima
// konstantna (zbroj kutija koje pokrivaju odsjecak), pa se put trazi odsjecak po odsjecak.
//
// Faza je Henyey-Greenstein. cosTheta je kosinus kuta izmedju smjera zrake koja dolazi (od
// kamere) i novog smjera: za zraku iz kamere smjera d i svjetlo u smjeru wi to je dot(d, wi) -
// g > 0 znaci da se svjetlo iza tocke (gledano od kamere) vidi jace: sjaj oko sunca.
//
// Isti racun je u shaders/tracer.slang (volumeSpans, opticalDepth, sampleVolume).
//=============================================================================================
#include "Tracer/Scene.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

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

//Kutije koje zraka presijece (najvise MaxVolumeSpans)
inline uint32_t volumeSpans(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                            const glm::vec3& o, const glm::vec3& d, float tMax, VolumeSpan* out){
    uint32_t n = 0;
    for(uint32_t i = 0; i < volumes.size() && n < MaxVolumeSpans; ++i){
        if(!(volumes[i].density > 0.0f)) continue;
        float t0, t1;
        if(boxInterval(inverses[i], o, d, tMax, t0, t1)) out[n++] = {t0, t1, i};
    }
    return n;
}

//Opticka debljina do tMax: prolaz kroz maglu je exp(-ovo)
inline float opticalDepth(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                          const glm::vec3& o, const glm::vec3& d, float tMax){
    VolumeSpan spans[MaxVolumeSpans];
    const uint32_t n = volumeSpans(volumes, inverses, o, d, tMax, spans);
    float tau = 0.0f;
    for(uint32_t i = 0; i < n; ++i) tau += volumes[spans[i].index].density * (spans[i].t1 - spans[i].t0);
    return tau;
}

//Slobodni put: false kad zraka prode do tMax bez dogadjaja. Inace t i kutija koja rasprsuje
//(izabrana razmjerno gustoci medju onima koje tu tocku pokrivaju). u.x put, u.y izbor kutije
inline bool sampleVolume(const std::vector<Volume>& volumes, const std::vector<glm::mat4>& inverses,
                         const glm::vec3& o, const glm::vec3& d, float tMax, glm::vec2 u, float& t, uint32_t& which){
    VolumeSpan spans[MaxVolumeSpans];
    const uint32_t n = volumeSpans(volumes, inverses, o, d, tMax, spans);
    if(n == 0) return false;
    float edges[2 * MaxVolumeSpans];
    for(uint32_t i = 0; i < n; ++i){ edges[2 * i] = spans[i].t0; edges[2 * i + 1] = spans[i].t1; }
    std::sort(edges, edges + 2 * n);
    float tau = -std::log(std::max(1e-12f, 1.0f - u.x));
    for(uint32_t e = 0; e + 1 < 2 * n; ++e){
        const float a = edges[e], b = edges[e + 1];
        if(!(b > a)) continue;
        const float middle = 0.5f * (a + b);
        float sigma = 0.0f;
        for(uint32_t i = 0; i < n; ++i) if(spans[i].t0 <= middle && middle <= spans[i].t1) sigma += volumes[spans[i].index].density;
        if(sigma <= 0.0f) continue;
        if(tau <= sigma * (b - a)){
            t = a + tau / sigma;
            float pick = u.y * sigma;
            which = spans[0].index;
            for(uint32_t i = 0; i < n; ++i){
                if(!(spans[i].t0 <= middle && middle <= spans[i].t1)) continue;
                which = spans[i].index;
                pick -= volumes[spans[i].index].density;
                if(pick < 0.0f) break;
            }
            return true;
        }
        tau -= sigma * (b - a);
    }
    return false;
}

inline float phaseHG(float cosTheta, float g){
    const float denominator = 1.0f + g * g - 2.0f * g * cosTheta;
    return (1.0f - g * g) / (4.0f * glm::pi<float>() * denominator * std::sqrt(std::max(denominator, 1e-12f)));
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

}
