#include "Tracer/Denoise.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <thread>
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

    //Vodici se procitaju jednom u gusta polja: u petlji od 25 susjeda po pikselu i pet prolaza
    //citanje iz Framea (tri floata na preskok) i pow/exp po susjedu bili su vise od pola vremena
    std::vector<glm::vec3> normals(n), albedos(n);
    std::vector<float> coverage(n), lum(n);
    std::vector<uint8_t> empty(n);
    for(size_t i = 0; i < n; ++i){
        normals[i] = normalAt(i);
        albedos[i] = albedoAt(i);
        coverage[i] = frame.cg[i * 4 + 3];
        empty[i] = glm::dot(normals[i], normals[i]) == 0.0f;
    }
    const std::vector<float>& depth = frame.depth;

    //Retci na sve jezgre: svaki piksel pise samo svoje mjesto, pa nema dijeljenja
    auto parallelRows = [&](const auto& body){
        const unsigned threads = std::max(1u, std::min(std::thread::hardware_concurrency(), unsigned(h)));
        std::atomic<int> next{0};
        auto work = [&]{ for(int y = next++; y < h; y = next++) body(y); };
        std::vector<std::thread> pool;
        for(unsigned t = 1; t < threads; ++t) pool.emplace_back(work);
        work();
        for(std::thread& t : pool) t.join();
    };

    auto pass = [&](int step, std::vector<glm::vec3>& source, std::vector<glm::vec3>& target,
                    const std::vector<float>* vIn, std::vector<float>* vOut, bool luminanceStop){
        if(luminanceStop) for(size_t i = 0; i < n; ++i) lum[i] = luminance(source[i]);
        parallelRows([&](int y){
            for(int x = 0; x < w; ++x){
                const size_t p = size_t(y) * size_t(w) + size_t(x);
                if(luminanceStop && coverage[p] <= 0.0f){ target[p] = source[p]; if(vOut) (*vOut)[p] = vIn ? (*vIn)[p] : 0.0f; continue; }
                const glm::vec3 np = normals[p], ap = albedos[p];
                const float zp = depth[p], lp = luminanceStop ? lum[p] : 0.0f, cp = coverage[p];
                const float inverseSigmaL = luminanceStop && vIn ? 1.0f / (4.0f * std::sqrt(std::max(0.0f, (*vIn)[p])) + 1e-4f) : 0.0f;
                glm::vec3 sum(0.0f);
                float weightSum = 0.0f, varianceSum = 0.0f;
                for(int dy = -2; dy <= 2; ++dy){
                    const int qy = y + dy * step;
                    if(qy < 0 || qy >= h) continue;
                    for(int dx = -2; dx <= 2; ++dx){
                        const int qx = x + dx * step;
                        if(qx < 0 || qx >= w) continue;
                        const size_t q = size_t(qy) * size_t(w) + size_t(qx);
                        float weight = Kernel[std::abs(dx)] * Kernel[std::abs(dy)];
                        //Ista ploha: normala (^64 kao sest kvadriranja), dubina, pokrivenost, albedo
                        if(!(empty[p] && empty[q])){
                            float nn = std::max(0.0f, glm::dot(np, normals[q]));
                            nn *= nn; nn *= nn; nn *= nn; nn *= nn; nn *= nn; nn *= nn;
                            weight *= nn;
                        }
                        weight *= std::max(0.0f, 1.0f - 4.0f * std::abs(cp - coverage[q]));
                        if(weight <= 0.0f) continue;
                        const float zq = depth[q];
                        const float scale = std::max(1e-6f, 0.02f * float(step) * std::min(zp, zq));
                        //Tri eksponencijalna faktora kao jedan exp zbroja
                        float exponent = std::abs(zp - zq) / scale + glm::length(ap - albedos[q]) * 8.0f;
                        if(luminanceStop) exponent += std::abs(lp - lum[q]) * inverseSigmaL;
                        weight *= std::exp(-exponent);
                        if(weight <= 0.0f) continue;
                        sum += source[q] * weight;
                        weightSum += weight;
                        if(vIn) varianceSum += weight * weight * (*vIn)[q];
                    }
                }
                target[p] = weightSum > 0.0f ? sum / weightSum : source[p];
                if(vOut) (*vOut)[p] = weightSum > 0.0f ? varianceSum / (weightSum * weightSum) : (vIn ? (*vIn)[p] : 0.0f);
            }
        });
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
