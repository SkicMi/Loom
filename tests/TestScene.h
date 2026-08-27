#pragma once
#include "Core/CameraIntrinsics.h"
#include "Core/LoomInitializer.h"
#include "Vulkan/Vertex.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

//Koliko kuta smije otici na sam ZAPIS dubine, prije nego se racunu ista prigovori.
//
//Zapisana dubina je float32: na vrijednosti d njen korak je d * 2^-23. Kroz obrat
//linearizacije taj korak u pravim jedinicama iznosi (far-near) * D^2 / (near*far) puta
//toliko, a normala ga vidi kao nagib te visine nad sirinom jednog piksela. Sve je poznato,
//pa se prag racuna umjesto da se bira okom.
//
//Vrijedi za ravne plohe okrenute prema kameri; ploha pod ostrim kutom ima veci korak dubine
//po pikselu, pa je ovo donja granica i mjereni broj se uvijek i ispisuje
inline float depthPrecisionAngle(float viewDepth, const CameraIntrinsics& intrinsics){
    const float n = intrinsics.nearPlane;
    const float f = intrinsics.farPlane;

    const float storedDepth = (f / (f - n)) * (1.0f - n / viewDepth);
    const float depthStep = storedDepth * 1.1920929e-7f;                //float32 eps
    const float unitsPerStep = (f - n) * viewDepth * viewDepth / (n * f) * depthStep;

    const float pixelSize = viewDepth / std::abs(intrinsics.fx);
    return glm::degrees(std::atan(unitsPerStep / pixelSize));
}

//Najveci nagib zrcalnog clana pow(cos, shininess) po kutu. Postize se na cos^2 = (s-1)/s
inline float specularSlope(float shininess){
    if(shininess <= 1.0f) return 1.0f;
    const float cosine = std::sqrt((shininess - 1.0f) / shininess);
    return shininess * std::pow(cosine, shininess - 1.0f) * std::sqrt(1.0f - cosine * cosine);
}

//Koliko se izracunato svjetlo smije razlikovati samo zato sto normala dolazi iz zapisane
//dubine umjesto iz trokuta. Difuzni clan se s kutom mijenja najvise 1:1, zrcalni najvise
//specularSlope puta - i oboje je pomnozeno bojom svjetla, koja nije veca od 1
inline float lightingTolerance(float viewDepth, const CameraIntrinsics& intrinsics, float shininess){
    const float radians = glm::radians(depthPrecisionAngle(viewDepth, intrinsics));
    return radians * (1.0f + specularSlope(shininess));
}

//Shared material for the tests: one cube, one checkerboard, one known pattern, and the
//sRGB conversions the GPU performs, so a test can predict what the GPU should produce

inline std::vector<Vertex> cubeVertices(){
    return {
        {{-0.5f,-0.5f, 0.5f},{1,0,0},{0,1},{ 0, 0, 1}}, {{ 0.5f,-0.5f, 0.5f},{1,0,0},{1,1},{ 0, 0, 1}},
        {{ 0.5f, 0.5f, 0.5f},{1,0,0},{1,0},{ 0, 0, 1}}, {{-0.5f, 0.5f, 0.5f},{1,0,0},{0,0},{ 0, 0, 1}},
        {{ 0.5f,-0.5f,-0.5f},{0,1,0},{0,1},{ 0, 0,-1}}, {{-0.5f,-0.5f,-0.5f},{0,1,0},{1,1},{ 0, 0,-1}},
        {{-0.5f, 0.5f,-0.5f},{0,1,0},{1,0},{ 0, 0,-1}}, {{ 0.5f, 0.5f,-0.5f},{0,1,0},{0,0},{ 0, 0,-1}},
        {{ 0.5f,-0.5f, 0.5f},{0,0,1},{0,1},{ 1, 0, 0}}, {{ 0.5f,-0.5f,-0.5f},{0,0,1},{1,1},{ 1, 0, 0}},
        {{ 0.5f, 0.5f,-0.5f},{0,0,1},{1,0},{ 1, 0, 0}}, {{ 0.5f, 0.5f, 0.5f},{0,0,1},{0,0},{ 1, 0, 0}},
        {{-0.5f,-0.5f,-0.5f},{1,1,0},{0,1},{-1, 0, 0}}, {{-0.5f,-0.5f, 0.5f},{1,1,0},{1,1},{-1, 0, 0}},
        {{-0.5f, 0.5f, 0.5f},{1,1,0},{1,0},{-1, 0, 0}}, {{-0.5f, 0.5f,-0.5f},{1,1,0},{0,0},{-1, 0, 0}},
        {{-0.5f, 0.5f, 0.5f},{1,0,1},{0,1},{ 0, 1, 0}}, {{ 0.5f, 0.5f, 0.5f},{1,0,1},{1,1},{ 0, 1, 0}},
        {{ 0.5f, 0.5f,-0.5f},{1,0,1},{1,0},{ 0, 1, 0}}, {{-0.5f, 0.5f,-0.5f},{1,0,1},{0,0},{ 0, 1, 0}},
        {{-0.5f,-0.5f,-0.5f},{0,1,1},{0,1},{ 0,-1, 0}}, {{ 0.5f,-0.5f,-0.5f},{0,1,1},{1,1},{ 0,-1, 0}},
        {{ 0.5f,-0.5f, 0.5f},{0,1,1},{1,0},{ 0,-1, 0}}, {{-0.5f,-0.5f, 0.5f},{0,1,1},{0,0},{ 0,-1, 0}}
    };
}

inline std::vector<uint16_t> cubeIndices(){
    return {0,1,2, 2,3,0,  4,5,6, 6,7,4,  8,9,10, 10,11,8,
            12,13,14, 14,15,12,  16,17,18, 18,19,16,  20,21,22, 22,23,20};
}

inline std::vector<uint8_t> makeCheckerboard(uint32_t size, uint32_t cell){
    std::vector<uint8_t> px(size_t(size) * size * 4);
    for(uint32_t y = 0; y < size; ++y){
        for(uint32_t x = 0; x < size; ++x){
            uint8_t v = (((x / cell) + (y / cell)) % 2) == 0 ? 255 : 0;
            size_t i = (size_t(y) * size + x) * 4;
            px[i+0] = v; px[i+1] = v; px[i+2] = v; px[i+3] = 255;
        }
    }
    return px;
}

//What imagewrite.comp.spv produces: a pattern inside the region, magenta outside
inline std::vector<uint8_t> patternPixels(uint32_t width, uint32_t height, uint32_t regionW, uint32_t regionH){
    std::vector<uint8_t> px(size_t(width) * height * 4);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            size_t i = (size_t(y) * width + x) * 4;
            bool inRegion = (x < regionW && y < regionH);
            px[i+0] = inRegion ? uint8_t(x % 256) : 255;
            px[i+1] = inRegion ? uint8_t(y % 256) : 0;
            px[i+2] = inRegion ? uint8_t((x ^ y) % 256) : 255;
            px[i+3] = 255;
        }
    }
    return px;
}

//A 3x3 box blur with clamped edges, the way blur.comp.spv does it
inline std::vector<uint8_t> blurBytes(const std::vector<uint8_t>& src, uint32_t width, uint32_t height){
    std::vector<uint8_t> out(src.size());
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            for(uint32_t c = 0; c < 4; ++c){
                uint32_t sum = 0;
                for(int dy = -1; dy <= 1; ++dy){
                    for(int dx = -1; dx <= 1; ++dx){
                        int tx = int(x) + dx, ty = int(y) + dy;
                        tx = tx < 0 ? 0 : (tx > int(width)-1 ? int(width)-1 : tx);
                        ty = ty < 0 ? 0 : (ty > int(height)-1 ? int(height)-1 : ty);
                        sum += src[(size_t(ty) * width + tx) * 4 + c];
                    }
                }
                out[(size_t(y) * width + x) * 4 + c] = uint8_t(sum / 9.0 + 0.5);
            }
        }
    }
    return out;
}

inline double srgbToLinear(double c){
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

inline double linearToSrgb(double c){
    if(c <= 0.0) return 0.0;
    if(c >= 1.0) return 1.0;
    return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0/2.4) - 0.055;
}

inline uint8_t encodeByte(double linear){
    return uint8_t(linearToSrgb(linear) * 255.0 + 0.5);
}

struct ByteDiff{
    size_t different = 0;
    size_t maxDelta = 0;
    size_t overOne = 0;
};

inline ByteDiff diffBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b){
    ByteDiff diff;
    const size_t count = a.size() < b.size() ? a.size() : b.size();
    for(size_t i = 0; i < count; ++i){
        size_t d = size_t(a[i] > b[i] ? a[i] - b[i] : b[i] - a[i]);
        if(d){
            ++diff.different;
            if(d > 1) ++diff.overOne;
            if(d > diff.maxDelta) diff.maxDelta = d;
        }
    }
    return diff;
}

//A VulkanImage is not a RenderTarget, so it is read back by hand
inline std::vector<uint8_t> readImagePixels(const LoomInitializer& loom, const VulkanImage& image, vk::Extent2D extent){
    const vk::DeviceSize bytes = vk::DeviceSize(extent.width) * extent.height * 4;
    VulkanBuffer staging(loom.device, bytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
    loom.command.copyImageToBuffer(image.getImage(), staging.getBuffer(), extent);
    std::vector<uint8_t> out(static_cast<size_t>(bytes), 0);
    staging.download(out.data(), bytes);
    return out;
}

inline size_t countNonBlack(const std::vector<uint8_t>& pixels){
    size_t count = 0;
    for(size_t i = 0; i + 3 < pixels.size(); i += 4){
        if(pixels[i] || pixels[i+1] || pixels[i+2]) ++count;
    }
    return count;
}
