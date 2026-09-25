#include "Tracer/Denoise.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Tracer{

namespace{
float luminance(const glm::vec3& c){ return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
const float Kernel[3] = {3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f};
}

void denoiseFrame(Frame& frame){
    const int w = int(frame.width), h = int(frame.height);
    const size_t n = frame.pixelCount();
    if(n == 0) return;

    std::vector<glm::vec3> modulation(n), light(n), next(n);
    std::vector<float> variance(frame.variance), nextVariance(n);
    auto normalAt = [&](size_t i){ return glm::vec3(frame.normal[i * 3], frame.normal[i * 3 + 1], frame.normal[i * 3 + 2]); };
    auto albedoAt = [&](size_t i){ return glm::vec3(frame.albedo[i * 3], frame.albedo[i * 3 + 1], frame.albedo[i * 3 + 2]); };
    for(size_t i = 0; i < n; ++i){
        modulation[i] = glm::max(albedoAt(i), glm::vec3(0.02f));
        light[i] = glm::vec3(frame.cg[i * 4], frame.cg[i * 4 + 1], frame.cg[i * 4 + 2]) / modulation[i];
        //Varijanca je bila za boju; svjetlo je boja / albedo
        const float m = std::max(0.02f, luminance(modulation[i]));
        variance[i] /= m * m;
    }

    auto pass = [&](int step, std::vector<glm::vec3>& source, std::vector<glm::vec3>& target,
                    const std::vector<float>* vIn, std::vector<float>* vOut, bool luminanceStop){
        for(int y = 0; y < h; ++y){
            for(int x = 0; x < w; ++x){
                const size_t p = size_t(y) * size_t(w) + size_t(x);
                const float coverage = frame.cg[p * 4 + 3];
                if(luminanceStop && coverage <= 0.0f){ target[p] = source[p]; if(vOut) (*vOut)[p] = vIn ? (*vIn)[p] : 0.0f; continue; }
                const glm::vec3 np = normalAt(p);
                const glm::vec3 ap = albedoAt(p);
                const float zp = frame.depth[p];
                const float lp = luminance(source[p]);
                const float sigmaL = luminanceStop && vIn ? 4.0f * std::sqrt(std::max(0.0f, (*vIn)[p])) + 1e-4f : 1.0f;
                glm::vec3 sum(0.0f);
                float weightSum = 0.0f, varianceSum = 0.0f;
                for(int dy = -2; dy <= 2; ++dy){
                    const int qy = y + dy * step;
                    if(qy < 0 || qy >= h) continue;
                    for(int dx = -2; dx <= 2; ++dx){
                        const int qx = x + dx * step;
                        if(qx < 0 || qx >= w) continue;
                        const size_t q = size_t(qy) * size_t(w) + size_t(qx);
                        const float k = Kernel[std::abs(dx)] * Kernel[std::abs(dy)];
                        const glm::vec3 nq = normalAt(q);
                        float weight = k;
                        //Ista ploha: normala, dubina, pokrivenost
                        const float nn = glm::dot(np, nq);
                        const bool bothEmpty = glm::dot(np, np) == 0.0f && glm::dot(nq, nq) == 0.0f;
                        weight *= bothEmpty ? 1.0f : std::pow(std::max(0.0f, nn), 64.0f);
                        const float zq = frame.depth[q];
                        const float scale = std::max(1e-6f, 0.02f * float(step) * std::min(zp, zq));
                        weight *= std::exp(-std::abs(zp - zq) / scale);
                        weight *= std::max(0.0f, 1.0f - 4.0f * std::abs(coverage - frame.cg[q * 4 + 3]));
                        weight *= std::exp(-glm::length(ap - albedoAt(q)) * 8.0f);
                        if(luminanceStop) weight *= std::exp(-std::abs(lp - luminance(source[q])) / sigmaL);
                        if(weight <= 0.0f) continue;
                        sum += source[q] * weight;
                        weightSum += weight;
                        if(vIn) varianceSum += weight * weight * (*vIn)[q];
                    }
                }
                target[p] = weightSum > 0.0f ? sum / weightSum : source[p];
                if(vOut) (*vOut)[p] = weightSum > 0.0f ? varianceSum / (weightSum * weightSum) : (vIn ? (*vIn)[p] : 0.0f);
            }
        }
    };

    for(int i = 0; i < 5; ++i){
        pass(1 << i, light, next, &variance, &nextVariance, true);
        light.swap(next);
        variance.swap(nextVariance);
    }
    for(size_t i = 0; i < n; ++i){
        const glm::vec3 c = light[i] * modulation[i];
        frame.cg[i * 4] = c.r; frame.cg[i * 4 + 1] = c.g; frame.cg[i * 4 + 2] = c.b;
    }

    //Sjena na catcheru: bez albeda i bez mjere suma, samo ista ploha. Tri prolaza
    std::vector<glm::vec3> shadow(n), shadowNext(n);
    bool anyShadow = false;
    for(size_t i = 0; i < n; ++i){
        shadow[i] = glm::vec3(frame.shadow[i * 3], frame.shadow[i * 3 + 1], frame.shadow[i * 3 + 2]);
        anyShadow = anyShadow || shadow[i] != glm::vec3(1.0f);
    }
    if(anyShadow){
        for(int i = 0; i < 3; ++i){
            pass(1 << i, shadow, shadowNext, nullptr, nullptr, false);
            shadow.swap(shadowNext);
        }
        for(size_t i = 0; i < n; ++i) for(int k = 0; k < 3; ++k) frame.shadow[i * 3 + size_t(k)] = shadow[i][k];
    }
    frame.denoised = true;
}

}
