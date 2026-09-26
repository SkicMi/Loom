#include "Tracer/Environment.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Tracer{

namespace{
float luminance(const glm::vec3& c){ return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

glm::mat3 rotationY(float angle){
    const float c = std::cos(angle), s = std::sin(angle);
    //Stupci: slike osi X, Y, Z
    return glm::mat3(glm::vec3(c, 0.0f, -s), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(s, 0.0f, c));
}

//Prvi indeks i za koji cdf[i + 1] > u, u rasponu [0, count)
uint32_t findInterval(const float* cdf, uint32_t count, float u){
    const float* end = cdf + count + 1;
    const float* at = std::upper_bound(cdf, end, u);
    const long index = long(at - cdf) - 1;
    return uint32_t(std::clamp(index, 0L, long(count) - 1));
}
}

glm::vec2 directionToLatLong(const glm::vec3& d){
    float u = std::atan2(d.x, -d.z) / (2.0f * glm::pi<float>());
    if(u < 0.0f) u += 1.0f;
    const float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / glm::pi<float>();
    return {std::min(u, 0.99999994f), std::min(v, 0.99999994f)};
}

glm::vec3 latLongToDirection(const glm::vec2& uv){
    const float theta = uv.y * glm::pi<float>(), phi = uv.x * 2.0f * glm::pi<float>();
    return {std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi)};
}

void EnvironmentSampler::build(const Environment& environment){
    env = &environment;
    toMap = rotationY(-environment.rotation);
    fromMap = rotationY(environment.rotation);
    textured = environment.map.valid();
    marginal.clear(); conditional.clear(); rowWeight.clear();
    if(!textured){
        average = luminance(environment.color) * environment.intensity;
        nonBlack = average > 0.0f;
        return;
    }
    width = environment.map.width;
    height = environment.map.height;
    conditional.assign(size_t(height) * (width + 1), 0.0f);
    rowWeight.assign(height, 0.0f);
    marginal.assign(height + 1, 0.0f);
    double luminanceSum = 0.0, solidSum = 0.0;
    for(uint32_t y = 0; y < height; ++y){
        const float sinTheta = std::sin(glm::pi<float>() * (float(y) + 0.5f) / float(height));
        float* cdf = conditional.data() + size_t(y) * (width + 1);
        double sum = 0.0;
        for(uint32_t x = 0; x < width; ++x){
            const float lum = std::max(0.0f, luminance(texel(x, y)));
            luminanceSum += double(lum) * sinTheta;
            solidSum += sinTheta;
            sum += double(lum) * sinTheta;
            cdf[x + 1] = float(sum);
        }
        rowWeight[y] = float(sum);
        for(uint32_t x = 1; x <= width; ++x) cdf[x] = sum > 0.0 ? float(double(cdf[x]) / sum) : float(x) / float(width);
        cdf[width] = 1.0f;
        marginal[y + 1] = marginal[y] + float(sum);
    }
    const float total = marginal[height];
    nonBlack = total > 0.0f && environment.intensity > 0.0f;
    for(uint32_t y = 1; y <= height; ++y) marginal[y] = total > 0.0f ? marginal[y] / total : float(y) / float(height);
    marginal[height] = 1.0f;
    average = solidSum > 0.0 ? float(luminanceSum / solidSum) * environment.intensity : 0.0f;
}

glm::vec3 EnvironmentSampler::texel(uint32_t x, uint32_t y) const{
    return glm::vec3(env->map.fetch(int(x), int(y)));
}

glm::vec3 EnvironmentSampler::radiance(const glm::vec3& d) const{
    if(!env) return glm::vec3(0.0f);
    if(!textured) return env->color * env->intensity;
    const glm::vec2 uv = directionToLatLong(toMap * d);
    return texel(std::min(uint32_t(uv.x * float(width)), width - 1), std::min(uint32_t(uv.y * float(height)), height - 1)) * env->intensity;
}

glm::vec3 EnvironmentSampler::sample(const glm::vec2& u, glm::vec3& direction, float& pdfOut) const{
    pdfOut = 0.0f;
    if(!nonBlack) return glm::vec3(0.0f);
    if(!textured){
        const float z = 1.0f - 2.0f * u.x, r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float phi = 2.0f * glm::pi<float>() * u.y;
        direction = glm::vec3(r * std::cos(phi), z, r * std::sin(phi));
        pdfOut = 1.0f / (4.0f * glm::pi<float>());
        return env->color * env->intensity;
    }
    const uint32_t y = findInterval(marginal.data(), height, u.y);
    const float dy = (u.y - marginal[y]) / std::max(1e-12f, marginal[y + 1] - marginal[y]);
    const float* cdf = conditional.data() + size_t(y) * (width + 1);
    const uint32_t x = findInterval(cdf, width, u.x);
    const float dx = (u.x - cdf[x]) / std::max(1e-12f, cdf[x + 1] - cdf[x]);
    const glm::vec2 uv((float(x) + std::clamp(dx, 0.0f, 0.9999f)) / float(width),
                       (float(y) + std::clamp(dy, 0.0f, 0.9999f)) / float(height));
    const glm::vec3 local = latLongToDirection(uv);
    direction = fromMap * local;
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - local.y * local.y));
    if(sinTheta <= 1e-6f) return glm::vec3(0.0f);
    const float total = 1.0f;       //marginal je normiran
    const float probability = (marginal[y + 1] - marginal[y]) * (cdf[x + 1] - cdf[x]) / total;
    pdfOut = probability * float(width) * float(height) / (2.0f * glm::pi<float>() * glm::pi<float>() * sinTheta);
    return texel(x, y) * env->intensity;
}

float EnvironmentSampler::pdf(const glm::vec3& d) const{
    if(!nonBlack) return 0.0f;
    if(!textured) return 1.0f / (4.0f * glm::pi<float>());
    const glm::vec3 local = toMap * d;
    const glm::vec2 uv = directionToLatLong(local);
    const uint32_t x = std::min(uint32_t(uv.x * float(width)), width - 1);
    const uint32_t y = std::min(uint32_t(uv.y * float(height)), height - 1);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - local.y * local.y));
    if(sinTheta <= 1e-6f) return 0.0f;
    const float* cdf = conditional.data() + size_t(y) * (width + 1);
    const float probability = (marginal[y + 1] - marginal[y]) * (cdf[x + 1] - cdf[x]);
    return probability * float(width) * float(height) / (2.0f * glm::pi<float>() * glm::pi<float>() * sinTheta);
}

}
