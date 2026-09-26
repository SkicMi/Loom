#include "Tracer/Temporal.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Tracer{

namespace{
float luminance(const glm::vec4& c){ return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
//Catmull-Rom tezine za udio t (4 uzorka, -1..2)
glm::vec4 catmullRom(float t){
    const float t2 = t * t, t3 = t2 * t;
    return glm::vec4(-0.5f * t3 + t2 - 0.5f * t, 1.5f * t3 - 2.5f * t2 + 1.0f, -1.5f * t3 + 2.0f * t2 + 0.5f * t, 0.5f * t3 - 0.5f * t2);
}
}

TemporalStats stabilize(Frame& frame, const Camera& camera, TemporalHistory& history, float strength){
    TemporalStats stats;
    const uint32_t w = frame.width, h = frame.height;
    const size_t n = frame.pixelCount();
    const bool usable = history.valid && strength > 0.0f && history.frame.width == w && history.frame.height == h &&
                        frame.depth.size() == n && frame.normal.size() == n * 3 && frame.albedo.size() == n * 3 &&
                        history.frame.depth.size() == n && history.frame.normal.size() == n * 3 && history.frame.albedo.size() == n * 3;
    if(usable){
        const Frame& old = history.frame;
        const glm::mat4 worldToCamera = glm::inverse(camera.cameraToWorld);
        const glm::mat4 worldToOld = glm::inverse(history.camera.cameraToWorld);
        std::vector<float> out(frame.cg);
        for(uint32_t y = 0; y < h; ++y) for(uint32_t x = 0; x < w; ++x){
            const size_t i = size_t(y) * w + x;
            const float depth = frame.depth[i];
            if(!(depth > 0.0f) || depth >= NoDepth * 0.5f) continue;
            //Tocka svijeta iz dubine: zraka kroz srediste piksela (sredina lece), do dubine duz -Z
            glm::vec3 origin, direction;
            camera.ray(glm::vec2(float(x) + 0.5f, float(y) + 0.5f), glm::vec2(0.5f), origin, direction);
            const float along = -glm::dot(glm::vec3(worldToCamera * glm::vec4(direction, 0.0f)), glm::vec3(0.0f, 0.0f, 1.0f));
            if(along <= 1e-6f) continue;
            const glm::vec3 world = origin + direction * (depth / along);
            const glm::vec3 inOld = glm::vec3(worldToOld * glm::vec4(world, 1.0f));
            if(inOld.z >= 0.0f){ ++stats.rejected; continue; }
            const glm::vec2 at = history.camera.pixelOf(inOld) - glm::vec2(0.5f);
            const float expected = -inOld.z;
            const glm::vec3 normal(frame.normal[i * 3], frame.normal[i * 3 + 1], frame.normal[i * 3 + 2]);
            //Cetiri bilinearna susjeda: jesu li na istoj plohi (dubina i normala)
            const int x0 = int(std::floor(at.x)), y0 = int(std::floor(at.y));
            const float fx = at.x - float(x0), fy = at.y - float(y0);
            //Osvjetljenje (boja / albedo) i pokrivenost: tekstura je uvijek iz ovog kadra, ostra
            auto texel = [&](int qx, int qy){
                qx = std::clamp(qx, 0, int(w) - 1); qy = std::clamp(qy, 0, int(h) - 1);
                const size_t q = size_t(qy) * w + size_t(qx);
                const glm::vec3 modulation = glm::max(glm::vec3(old.albedo[q * 3], old.albedo[q * 3 + 1], old.albedo[q * 3 + 2]), glm::vec3(0.02f));
                return glm::vec4(glm::vec3(old.cg[q * 4], old.cg[q * 4 + 1], old.cg[q * 4 + 2]) / modulation, old.cg[q * 4 + 3]);
            };
            glm::vec4 sum(0.0f), low(1e30f), high(-1e30f);
            float weight = 0.0f;
            bool allSame = true;
            for(int k = 0; k < 4; ++k){
                const int qx = x0 + (k & 1), qy = y0 + (k >> 1);
                bool same = qx >= 0 && qy >= 0 && qx < int(w) && qy < int(h);
                if(same){
                    const size_t q = size_t(qy) * w + size_t(qx);
                    const glm::vec3 oldNormal(old.normal[q * 3], old.normal[q * 3 + 1], old.normal[q * 3 + 2]);
                    same = std::abs(old.depth[q] - expected) <= 0.02f * expected && glm::dot(normal, oldNormal) >= 0.9f;
                }
                if(!same){ allSame = false; continue; }
                const glm::vec4 c = texel(qx, qy);
                const float wk = ((k & 1) ? fx : 1.0f - fx) * ((k >> 1) ? fy : 1.0f - fy);
                sum += wk * c;
                weight += wk;
                low = glm::min(low, c);
                high = glm::max(high, c);
            }
            if(weight < 0.5f){ ++stats.rejected; continue; }
            glm::vec4 past = sum / weight;
            if(allSame){
                //Sve na istoj plohi: Catmull-Rom (ostro - bilinearno bi se kroz kadrove zamutilo),
                //stegnut na raspon cetiri susjeda da nema prstenova
                const glm::vec4 wx = catmullRom(fx), wy = catmullRom(fy);
                glm::vec4 sharp(0.0f);
                for(int j = 0; j < 4; ++j) for(int k = 0; k < 4; ++k) sharp += wx[k] * wy[j] * texel(x0 - 1 + k, y0 - 1 + j);
                past = glm::clamp(sharp, low, high);
            }
            const glm::vec3 modulation = glm::max(glm::vec3(frame.albedo[i * 3], frame.albedo[i * 3 + 1], frame.albedo[i * 3 + 2]), glm::vec3(0.02f));
            const glm::vec4 now(glm::vec3(frame.cg[i * 4], frame.cg[i * 4 + 1], frame.cg[i * 4 + 2]) / modulation, frame.cg[i * 4 + 3]);
            //Promjena svjetla (sjena koja putuje, lampa): razlika izvan suma piksela. Ostatak suma
            //poslije filtra je nekoliko puta manji od suma sirove procjene (Frame::variance)
            const float sigma = (i < frame.variance.size() ? std::sqrt(std::max(0.0f, frame.variance[i])) : 0.0f) /
                                std::max(0.02f, luminance(glm::vec4(modulation, 0.0f)));
            const float tolerance = 0.5f * sigma + 0.02f * luminance(now) + 1e-4f;
            const float difference = std::abs(luminance(past) - luminance(now));
            const float trust = std::clamp(2.0f - difference / tolerance, 0.0f, 1.0f);
            if(!(trust > 0.0f)){ ++stats.rejected; continue; }
            const glm::vec4 mixed = now + (past - now) * (strength * trust);
            for(int k = 0; k < 3; ++k) out[i * 4 + size_t(k)] = mixed[k] * modulation[k];
            out[i * 4 + 3] = mixed.w;
            ++stats.reused;
        }
        frame.cg.swap(out);
    }
    history.frame.width = w;
    history.frame.height = h;
    history.frame.cg = frame.cg;
    history.frame.depth = frame.depth;
    history.frame.normal = frame.normal;
    history.frame.albedo = frame.albedo;
    history.camera = camera;
    history.valid = true;
    return stats;
}

}
