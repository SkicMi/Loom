#include "WeaverProceduraTextures.h"
#include "WeaverProcedura.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Engine::WeaverProcedura{
namespace{

constexpr float tau = 6.28318530718f;

uint32_t hash(int x, int y, uint32_t seed){
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
    return h;
}
float random01(int x, int y, uint32_t seed){ return float(hash(x, y, seed) >> 8) * (1.0f / 16777216.0f); }
int wrap(int value, int period){ return ((value % period) + period) % period; }
float fract(float value){ return value - std::floor(value); }
float smooth(float t){ return t * t * (3.0f - 2.0f * t); }
float smoothstep(float a, float b, float x){ return smooth(std::clamp((x - a) / (b - a), 0.0f, 1.0f)); }

// Value noise that repeats every periodU x periodV cells over the unit square.
float noise(float u, float v, int periodU, int periodV, uint32_t seed){
    const float x = u * float(periodU), y = v * float(periodV);
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const float fx = smooth(x - float(ix)), fy = smooth(y - float(iy));
    auto at = [&](int dx, int dy){ return random01(wrap(ix + dx, periodU), wrap(iy + dy, periodV), seed); };
    return glm::mix(glm::mix(at(0, 0), at(1, 0), fx), glm::mix(at(0, 1), at(1, 1), fx), fy);
}

// Fractal sum, 0..1, still periodic: each octave doubles an integer period.
float fbm(float u, float v, int period, int octaves, uint32_t seed, int stretchV = 1){
    float sum = 0.0f, amplitude = 1.0f, total = 0.0f;
    for(int o = 0; o < octaves; ++o){
        sum += amplitude * noise(u, v, period << o, (period << o) * stretchV, seed + uint32_t(o) * 101u);
        total += amplitude;
        amplitude *= 0.5f;
    }
    return sum / total;
}

struct Cell{ float f1 = 1e9f, f2 = 1e9f; uint32_t id = 0; };

// Periodic Voronoi over n x n jittered points; distances in unit-square coordinates.
Cell voronoi(float u, float v, int n, uint32_t seed){
    Cell cell;
    const int cx = int(std::floor(u * float(n))), cy = int(std::floor(v * float(n)));
    for(int dy = -1; dy <= 1; ++dy)
        for(int dx = -1; dx <= 1; ++dx){
            const int gx = cx + dx, gy = cy + dy;
            const int wx = wrap(gx, n), wy = wrap(gy, n);
            const glm::vec2 point{(float(gx) + 0.15f + 0.7f * random01(wx, wy, seed)) / float(n),
                                  (float(gy) + 0.15f + 0.7f * random01(wx, wy, seed + 7u)) / float(n)};
            const float d = glm::length(point - glm::vec2(u, v));
            if(d < cell.f1){ cell.f2 = cell.f1; cell.f1 = d; cell.id = hash(wx, wy, seed); }
            else if(d < cell.f2) cell.f2 = d;
        }
    return cell;
}

glm::vec3 tint(glm::vec3 colour, float amount){ return glm::clamp(colour * amount, glm::vec3(0.0f), glm::vec3(1.0f)); }

struct Texel{
    glm::vec3 albedo{0.5f};      // sRGB 0..1
    float roughness = 0.8f, metallic = 0.0f;
    float height = 0.0f;         // meters, relative
};

// Distance to the nearest edge of a grid cell, in meters, for a cell of (width, height) meters.
float edgeDistance(float fx, float fy, float width, float height){
    return std::min(std::min(fx, 1.0f - fx) * width, std::min(fy, 1.0f - fy) * height);
}

using Shader = std::function<Texel(float u, float v)>;

Shader shaderFor(const std::string& name){
    if(name == "plaster") return [](float u, float v){
        Texel t;
        t.albedo = tint({0.82f, 0.79f, 0.72f}, 0.93f + 0.1f * fbm(u, v, 6, 5, 11));
        t.height = 0.0015f * fbm(u, v, 32, 4, 12);
        t.roughness = 0.88f;
        return t;
    };
    if(name == "concrete") return [](float u, float v){
        Texel t;
        const float pores = noise(u, v, 160, 160, 23);
        t.albedo = tint({0.60f, 0.60f, 0.58f}, 0.82f + 0.24f * fbm(u, v, 3, 6, 21));
        t.height = 0.001f * fbm(u, v, 24, 3, 22);
        if(pores > 0.86f){ t.albedo *= 0.62f; t.height -= 0.0015f; }
        t.roughness = 0.9f;
        return t;
    };
    if(name == "brick") return [](float u, float v){
        // Running bond: 14 courses and 4 bricks per meter, 12 mm mortar.
        constexpr int courses = 14, bricks = 4;
        const float y = v * courses;
        const int row = int(std::floor(y));
        const float x = u * bricks + (row % 2 ? 0.5f : 0.0f);
        const int col = wrap(int(std::floor(x)), bricks);
        const float edge = edgeDistance(fract(x), fract(y), 1.0f / bricks, 1.0f / courses);
        Texel t;
        const float grain = fbm(u, v, 64, 3, 31);
        if(edge < 0.006f){
            t.albedo = tint({0.70f, 0.68f, 0.64f}, 0.85f + 0.2f * grain);
            t.height = 0.0f;
            t.roughness = 0.95f;
        }else{
            const float shade = 0.82f + 0.3f * random01(col, wrap(row, courses), 32);
            glm::vec3 base{0.55f, 0.24f, 0.17f};
            if(random01(col, wrap(row, courses), 33) < 0.12f) base = {0.40f, 0.18f, 0.13f};
            t.albedo = tint(base, shade * (0.9f + 0.2f * grain));
            t.height = 0.004f + 0.006f * smoothstep(0.006f, 0.011f, edge) + 0.0015f * grain;
            t.roughness = 0.85f;
        }
        return t;
    };
    if(name == "stone") return [](float u, float v){
        const Cell cell = voronoi(u, v, 5, 41);
        const float edge = cell.f2 - cell.f1;
        Texel t;
        const float grain = fbm(u, v, 12, 5, 42);
        if(edge < 0.018f){
            t.albedo = tint({0.62f, 0.60f, 0.56f}, 0.8f + 0.2f * grain);
            t.height = 0.0f;
            t.roughness = 0.95f;
        }else{
            static const glm::vec3 palette[] = {{0.52f, 0.50f, 0.46f}, {0.60f, 0.56f, 0.48f}, {0.45f, 0.44f, 0.42f}, {0.56f, 0.52f, 0.47f}};
            t.albedo = tint(palette[cell.id % 4], 0.8f + 0.35f * grain);
            t.height = 0.006f + 0.009f * smoothstep(0.018f, 0.07f, edge) + 0.004f * grain;
            t.roughness = 0.9f;
        }
        return t;
    };
    auto woodGrain = [](float u, float v, int rings, uint32_t seed){
        const float warp = fbm(u, v, 2, 4, seed, 4);
        return 0.5f + 0.5f * std::sin(tau * (v * float(rings) + 6.0f * warp));
    };
    if(name == "wood_planks") return [woodGrain](float u, float v){
        // 7 boards per meter across V, one butt joint per board per meter along U.
        constexpr int boards = 7;
        const float y = v * boards;
        const int row = wrap(int(std::floor(y)), boards);
        const float joint = random01(row, 0, 51);
        const float along = fract(u - joint);
        const float edge = std::min(std::min(fract(y), 1.0f - fract(y)) / boards, std::min(along, 1.0f - along));
        Texel t;
        const float grain = woodGrain(u, v, 90, 52);
        const glm::vec3 base = tint({0.55f, 0.38f, 0.22f}, 0.85f + 0.3f * random01(row, 1, 53));
        t.albedo = tint(glm::mix(base * 0.78f, base, grain), 0.95f + 0.1f * fbm(u, v, 32, 2, 54));
        t.height = 0.0006f * grain;
        t.roughness = 0.62f + 0.1f * grain;
        if(edge < 0.0016f){ t.albedo *= 0.35f; t.height = -0.002f; t.roughness = 0.9f; }
        return t;
    };
    if(name == "wood_beam") return [woodGrain](float u, float v){
        Texel t;
        const float grain = woodGrain(u, v, 60, 61);
        t.albedo = glm::mix(glm::vec3(0.30f, 0.19f, 0.10f), glm::vec3(0.42f, 0.28f, 0.15f), grain) * (0.9f + 0.2f * fbm(u, v, 8, 3, 62));
        t.height = 0.001f * grain + 0.001f * fbm(u, v, 16, 3, 63);
        t.roughness = 0.7f;
        return t;
    };
    if(name == "roof_tiles") return [](float u, float v){
        // Rows of 25 cm down the slope (V grows down the slope), 20 cm tiles, every other row
        // shifted half a tile. Each tile rises toward its lower edge, which overlaps the next row.
        constexpr int rows = 4, tiles = 5;
        const float y = v * rows;
        const int row = int(std::floor(y));
        const float x = u * tiles + (row % 2 ? 0.5f : 0.0f);
        const int col = wrap(int(std::floor(x)), tiles);
        const float fx = fract(x), fy = fract(y);
        Texel t;
        const float shade = 0.85f + 0.25f * random01(col, wrap(row, rows), 71);
        t.albedo = tint({0.62f, 0.25f, 0.16f}, shade * (0.9f + 0.15f * fbm(u, v, 16, 3, 72)) * (0.8f + 0.2f * fy));
        t.height = 0.012f * fy + 0.006f * std::sin(3.14159f * fx);
        t.roughness = 0.8f;
        if(std::min(fx, 1.0f - fx) / tiles < 0.003f){ t.albedo *= 0.4f; t.height = 0.0f; }
        return t;
    };
    if(name == "roof_metal") return [](float u, float v){
        // Standing seams every 50 cm, running down the slope along V.
        const float seam = std::min(fract(u * 2.0f), 1.0f - fract(u * 2.0f)) * 0.5f;
        Texel t;
        t.albedo = tint({0.36f, 0.40f, 0.43f}, 0.95f + 0.08f * fbm(u, v, 4, 3, 81));
        t.height = 0.02f * std::exp(-(seam / 0.01f) * (seam / 0.01f)) + 0.0008f * fbm(u, v, 3, 2, 82);
        t.metallic = 0.7f;
        t.roughness = 0.42f;
        return t;
    };
    if(name == "glass") return [](float u, float v){
        Texel t;
        t.albedo = {0.55f, 0.72f, 0.80f};
        t.height = 0.00005f * fbm(u, v, 4, 2, 91);
        t.roughness = 0.05f;
        return t;
    };
    if(name == "metal" || name == "steel_chain") return [chain = name == "steel_chain"](float u, float v){
        // Brushed along U: noise stretched 32 times along V.
        const float brushed = fbm(u, v, 2, 4, 101, 32);
        Texel t;
        t.albedo = tint(chain ? glm::vec3(0.48f, 0.49f, 0.51f) : glm::vec3(0.62f, 0.63f, 0.65f), 0.92f + 0.12f * brushed);
        t.height = 0.0002f * brushed;
        t.metallic = 1.0f;
        t.roughness = (chain ? 0.42f : 0.28f) + 0.12f * brushed;
        return t;
    };
    if(name == "asphalt") return [](float u, float v){
        const float stones = noise(u, v, 256, 256, 111);
        Texel t;
        t.albedo = tint({0.16f, 0.16f, 0.17f}, 0.85f + 0.3f * fbm(u, v, 4, 4, 112));
        if(stones > 0.78f) t.albedo += glm::vec3(0.12f * (stones - 0.78f) / 0.22f);
        t.height = 0.003f * stones;
        t.roughness = 0.95f;
        return t;
    };
    if(name == "paving") return [](float u, float v){
        constexpr int slabs = 2;
        const int col = wrap(int(std::floor(u * slabs)), slabs), row = wrap(int(std::floor(v * slabs)), slabs);
        const float edge = edgeDistance(fract(u * slabs), fract(v * slabs), 1.0f / slabs, 1.0f / slabs);
        Texel t;
        t.albedo = tint({0.66f, 0.63f, 0.57f}, (0.88f + 0.2f * random01(col, row, 121)) * (0.9f + 0.15f * fbm(u, v, 8, 4, 122)));
        t.height = 0.006f * smoothstep(0.004f, 0.01f, edge) + 0.001f * fbm(u, v, 32, 3, 123);
        t.roughness = 0.85f;
        if(edge < 0.004f){ t.albedo *= 0.5f; t.roughness = 0.95f; }
        return t;
    };
    if(name == "rope_fiber") return [](float u, float v){
        // Twisted strands: stripes along the diagonal, 12 per repeat, with fibres along them.
        const float strand = 0.5f + 0.5f * std::sin(tau * (u + v) * 12.0f);
        const float fibres = noise(u + v, u - v, 64, 64, 131);
        Texel t;
        t.albedo = tint({0.66f, 0.55f, 0.36f}, 0.75f + 0.25f * strand + 0.1f * fibres);
        t.height = 0.004f * std::sqrt(strand) + 0.0005f * fibres;
        t.roughness = 0.9f;
        return t;
    };
    if(name == "ground_dirt") return [](float u, float v){
        const Cell pebble = voronoi(u, v, 24, 141);
        const float soil = fbm(u, v, 3, 6, 142);
        Texel t;
        t.albedo = glm::mix(glm::vec3(0.30f, 0.22f, 0.15f), glm::vec3(0.45f, 0.35f, 0.24f), soil);
        t.height = 0.006f * soil;
        if(pebble.f1 < 0.012f){ t.albedo = tint({0.55f, 0.52f, 0.48f}, 0.8f + 0.3f * random01(int(pebble.id % 97u), 0, 143)); t.height += 0.004f; }
        t.roughness = 1.0f;
        return t;
    };
    if(name == "grass") return [](float u, float v){
        const float blades = fbm(u, v, 48, 3, 151, 1) * 0.5f + noise(u, v, 256, 32, 152) * 0.5f;
        const float patches = fbm(u, v, 3, 4, 153);
        Texel t;
        t.albedo = glm::mix(glm::vec3(0.16f, 0.30f, 0.10f), glm::vec3(0.40f, 0.52f, 0.22f), blades * 0.7f + patches * 0.3f);
        t.height = 0.006f * blades;
        t.roughness = 0.95f;
        return t;
    };
    auto weave = [](float u, float v, int threads, float& over){
        const float x = u * float(threads), y = v * float(threads);
        const bool warpOnTop = (int(std::floor(x)) + int(std::floor(y))) % 2 == 0;
        const float profile = warpOnTop ? std::sin(3.14159f * fract(x)) : std::sin(3.14159f * fract(y));
        over = warpOnTop ? 1.0f : 0.0f;
        return profile;
    };
    if(name == "fabric" || name == "linen") return [weave, linen = name == "linen"](float u, float v){
        float over = 0.0f;
        const float profile = weave(u, v, linen ? 160 : 120, over);
        const float slubs = fbm(u, v, 16, 3, linen ? 161u : 162u, 4);
        Texel t;
        const glm::vec3 base = linen ? glm::vec3(0.74f, 0.71f, 0.64f) : glm::vec3(0.34f, 0.45f, 0.60f);
        t.albedo = tint(base, 0.8f + 0.2f * profile + 0.08f * over + 0.1f * slubs);
        t.height = 0.0006f * profile;
        t.roughness = 0.93f;
        return t;
    };
    if(name == "ceramic") return [](float u, float v){
        constexpr int tiles = 5;
        const float edge = edgeDistance(fract(u * tiles), fract(v * tiles), 1.0f / tiles, 1.0f / tiles);
        Texel t;
        t.albedo = tint({0.93f, 0.93f, 0.91f}, 0.97f + 0.04f * fbm(u, v, 5, 3, 171));
        t.height = 0.002f * smoothstep(0.0015f, 0.004f, edge);
        t.roughness = 0.12f;
        if(edge < 0.0015f){ t.albedo = {0.62f, 0.61f, 0.58f}; t.roughness = 0.9f; }
        return t;
    };
    if(name == "lacquer") return [](float u, float v){
        Texel t;
        t.albedo = tint({0.90f, 0.90f, 0.88f}, 0.98f + 0.03f * fbm(u, v, 8, 2, 181));
        t.height = 0.0003f * fbm(u, v, 64, 3, 182);
        t.roughness = 0.25f;
        return t;
    };
    if(name == "leather") return [](float u, float v){
        const Cell cell = voronoi(u, v, 40, 191);
        const float crease = smoothstep(0.0f, 0.006f, cell.f2 - cell.f1);
        Texel t;
        t.albedo = tint({0.40f, 0.24f, 0.14f}, (0.75f + 0.25f * crease) * (0.9f + 0.2f * fbm(u, v, 4, 3, 192)));
        t.height = 0.0008f * crease;
        t.roughness = 0.55f + 0.1f * (1.0f - crease);
        return t;
    };
    return {};
}

uint8_t toByte(float value){ return uint8_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); }

}

bool makeMaterialTextures(const std::string& material, uint32_t size, MaterialTextures& output, std::string& error){
    if(materialId(material) == 0){ error = "unknown material: " + material; return false; }
    if(size < 16 || size > 2048 || (size & (size - 1)) != 0){ error = "texture size must be a power of two from 16 to 2048"; return false; }
    const Shader shader = shaderFor(material);
    if(!shader){ error = "no procedural texture for " + material; return false; }
    MaterialTextures result;
    result.size = size;
    result.worldSize = 1.0f;
    const std::size_t count = std::size_t(size) * size;
    std::vector<float> height(count);
    result.color.resize(count * 4);
    result.metallicRoughness.resize(count * 4);
    result.normal.resize(count * 4);
    for(uint32_t y = 0; y < size; ++y)
        for(uint32_t x = 0; x < size; ++x){
            const Texel t = shader((float(x) + 0.5f) / float(size), (float(y) + 0.5f) / float(size));
            const std::size_t i = std::size_t(y) * size + x;
            height[i] = t.height;
            result.color[i * 4 + 0] = toByte(t.albedo.r);
            result.color[i * 4 + 1] = toByte(t.albedo.g);
            result.color[i * 4 + 2] = toByte(t.albedo.b);
            result.color[i * 4 + 3] = 255;
            result.metallicRoughness[i * 4 + 0] = 255;
            result.metallicRoughness[i * 4 + 1] = toByte(t.roughness);
            result.metallicRoughness[i * 4 + 2] = toByte(t.metallic);
            result.metallicRoughness[i * 4 + 3] = 255;
        }
    // Normals from the height field (meters) with wrap-around differences; one texel is
    // worldSize / size meters. +X toward +U, +Y toward -V (toward the top of the image).
    const float texel = result.worldSize / float(size);
    for(uint32_t y = 0; y < size; ++y)
        for(uint32_t x = 0; x < size; ++x){
            auto h = [&](int dx, int dy){ return height[std::size_t(wrap(int(y) + dy, int(size))) * size + std::size_t(wrap(int(x) + dx, int(size)))]; };
            const float du = (h(1, 0) - h(-1, 0)) / (2.0f * texel);
            const float dv = (h(0, 1) - h(0, -1)) / (2.0f * texel);
            const glm::vec3 n = glm::normalize(glm::vec3(-du, dv, 1.0f));
            const std::size_t i = std::size_t(y) * size + x;
            result.normal[i * 4 + 0] = toByte(n.x * 0.5f + 0.5f);
            result.normal[i * 4 + 1] = toByte(n.y * 0.5f + 0.5f);
            result.normal[i * 4 + 2] = toByte(n.z * 0.5f + 0.5f);
            result.normal[i * 4 + 3] = 255;
        }
    output = std::move(result);
    error.clear();
    return true;
}

}
