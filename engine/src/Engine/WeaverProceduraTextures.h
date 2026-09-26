#pragma once

// Procedural PBR textures for the Procedura material library (materialLibrary()): colour,
// metallic-roughness and normal maps made from tileable noise, cells and bond patterns, so every
// material has a real surface without image files. One repeat covers worldSize meters; UV
// projections in meters (projectUVs) therefore show bricks, planks and tiles at their real size.
//
// Pure CPU and deterministic: the same material, size and textureGeneratorVersion give the same
// bytes, so a cache can key files by those three.

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::WeaverProcedura{

constexpr uint32_t textureGeneratorVersion = 1;

struct MaterialTextures{
    uint32_t size = 0;                          // square, tiles seamlessly in both directions
    std::vector<uint8_t> color;                 // RGBA8, sRGB
    std::vector<uint8_t> metallicRoughness;     // RGBA8, linear; G roughness, B metallic (glTF)
    std::vector<uint8_t> normal;                // RGBA8, tangent space, +Y toward -V image rows (glTF / OpenGL)
    float worldSize = 1.0f;                     // meters one repeat covers
};

// false for a name outside materialLibrary() or a size that is not a power of two in 16..2048.
bool makeMaterialTextures(const std::string& material, uint32_t size, MaterialTextures& output, std::string& error);

}
