#include "Tracer/Film.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tracer{

namespace{

uint8_t toByte(float v){
    return uint8_t(std::clamp(int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)), 0, 255));
}

//AgX (Troy Sobotka), polinomska aproksimacija Benjamina Wrenscha. Ulaz linearni Rec.709, izlaz
//linearni Rec.709 u [0,1] spreman za sRGB krivulju
glm::vec3 agx(glm::vec3 v){
    const glm::mat3 inset(0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
                          0.0784335999999992f, 0.878468636469772f, 0.0784336f,
                          0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);
    const glm::mat3 outset(1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
                           -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f,
                           -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f);
    const float minEv = -12.47393f, maxEv = 4.026069f;
    v = inset * glm::max(v, glm::vec3(1e-10f));
    v = glm::clamp(glm::vec3(std::log2(std::max(v.x, 1e-10f)), std::log2(std::max(v.y, 1e-10f)), std::log2(std::max(v.z, 1e-10f))),
                   glm::vec3(minEv), glm::vec3(maxEv));
    v = (v - minEv) / (maxEv - minEv);
    const glm::vec3 x2 = v * v, x4 = x2 * x2;
    v = 15.5f * x4 * x2 - 40.14f * x4 * v + 31.96f * x4 - 6.868f * x2 * v + 0.4298f * x2 + 0.1191f * v - 0.00232f;
    v = outset * v;
    return glm::pow(glm::max(v, glm::vec3(0.0f)), glm::vec3(2.2f));
}

}

std::vector<float> composite(const Frame& frame, Backdrop backdrop, const Texture* plate){
    const size_t n = frame.pixelCount();
    std::vector<float> out(n * 4, 0.0f);
    const bool havePlate = backdrop == Backdrop::Plate && plate && plate->valid();
    for(size_t i = 0; i < n; ++i){
        const float* c = frame.cg.data() + i * 4;
        const float a = std::clamp(c[3], 0.0f, 1.0f);
        const glm::vec3 cg(c[0], c[1], c[2]);
        const glm::vec3 s(frame.shadow[i * 3], frame.shadow[i * 3 + 1], frame.shadow[i * 3 + 2]);
        glm::vec3 rgb;
        float alpha = 1.0f;
        switch(backdrop){
        case Backdrop::Environment:
            rgb = cg + s * glm::vec3(frame.background[i * 3], frame.background[i * 3 + 1], frame.background[i * 3 + 2]);
            break;
        case Backdrop::Transparent:{
            //Sjena na nicemu: crno, s alfom koliko sjena uzme svjetla (Blenderov shadow catcher)
            const float taken = 1.0f - std::clamp((s.r + s.g + s.b) / 3.0f, 0.0f, 1.0f);
            rgb = cg;
            alpha = a + (1.0f - a) * taken;
            break;
        }
        case Backdrop::Plate:{
            glm::vec3 p(0.0f);
            if(havePlate){
                const uint32_t x = uint32_t(i % frame.width), y = uint32_t(i / frame.width);
                const glm::vec2 uv((float(x) + 0.5f) / float(frame.width), (float(y) + 0.5f) / float(frame.height));
                p = glm::vec3(plate->sample(uv));
            }
            rgb = cg + (1.0f - a) * s * p;
            break;
        }
        }
        out[i * 4] = rgb.r; out[i * 4 + 1] = rgb.g; out[i * 4 + 2] = rgb.b; out[i * 4 + 3] = alpha;
    }
    return out;
}

std::vector<uint8_t> toDisplay(const std::vector<float>& rgba, uint32_t width, uint32_t height,
                               ViewTransform view, float exposureStops){
    const size_t n = size_t(width) * height;
    std::vector<uint8_t> out(n * 4, 0);
    const float gain = std::exp2(exposureStops);
    for(size_t i = 0; i < n && i * 4 + 3 < rgba.size(); ++i){
        const float a = std::clamp(rgba[i * 4 + 3], 0.0f, 1.0f);
        glm::vec3 c = glm::vec3(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]) * gain;
        //Premultiplicirano -> ravno za PNG (koji ne zna za premultiplikaciju), pa krivulja
        if(a > 0.0f && a < 1.0f) c /= a;
        if(view == ViewTransform::AgX) c = agx(c);
        out[i * 4] = toByte(linearToSrgb(c.r));
        out[i * 4 + 1] = toByte(linearToSrgb(c.g));
        out[i * 4 + 2] = toByte(linearToSrgb(c.b));
        out[i * 4 + 3] = toByte(a);
    }
    return out;
}

std::vector<uint8_t> depthToDisplay(const Frame& frame){
    const size_t n = frame.pixelCount();
    float nearest = std::numeric_limits<float>::infinity(), farthest = 0.0f;
    for(float d : frame.depth){
        if(d >= NoDepth * 0.5f || !(d > 0.0f)) continue;
        nearest = std::min(nearest, d);
        farthest = std::max(farthest, d);
    }
    std::vector<uint8_t> out(n * 4, 255);
    for(size_t i = 0; i < n; ++i){
        const float d = frame.depth[i];
        float v = 0.0f;
        if(d < NoDepth * 0.5f && d > 0.0f && farthest > nearest) v = 1.0f - (d - nearest) / (farthest - nearest);
        else if(d < NoDepth * 0.5f && d > 0.0f) v = 1.0f;
        out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = toByte(v);
    }
    return out;
}

std::vector<uint8_t> normalToDisplay(const Frame& frame){
    const size_t n = frame.pixelCount();
    std::vector<uint8_t> out(n * 4, 255);
    for(size_t i = 0; i < n; ++i){
        for(int k = 0; k < 3; ++k) out[i * 4 + size_t(k)] = toByte(0.5f + 0.5f * frame.normal[i * 3 + size_t(k)]);
    }
    return out;
}

std::vector<uint8_t> albedoToDisplay(const Frame& frame){
    const size_t n = frame.pixelCount();
    std::vector<uint8_t> out(n * 4, 255);
    for(size_t i = 0; i < n; ++i){
        for(int k = 0; k < 3; ++k) out[i * 4 + size_t(k)] = toByte(linearToSrgb(frame.albedo[i * 3 + size_t(k)]));
    }
    return out;
}

}
