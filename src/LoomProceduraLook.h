#pragma once

// Look of each Procedura library material: a flat colour (.glb export, LoomProceduraGlb.h) and
// the procedural PBR textures (WeaverProceduraTextures.h) the viewport draws, written once as PNG
// files into a cache folder keyed by the texture generator version.

#include <Engine/WeaverProcedura.h>
#include <Engine/WeaverProceduraTextures.h>
#include <Spool/ImageFile.h>
#include <Warp/Stage.h>

#include <glm/glm.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <string>

namespace Loom{

//Boja preview-a (sRGB) za materijal iz Procedura knjiznice (materialLibrary). Dok knjiznica nema
//teksture, materijal se u pogledu razlikuje samo bojom; 0 (bez materijala) ostaje plava preview boja
inline glm::vec3 proceduralMaterialColour(uint16_t material){
    static const std::map<std::string, glm::vec3> looks = {
        {"plaster", {0.82f, 0.79f, 0.72f}}, {"brick", {0.55f, 0.24f, 0.17f}}, {"stone", {0.52f, 0.50f, 0.46f}},
        {"concrete", {0.60f, 0.60f, 0.58f}}, {"wood_planks", {0.55f, 0.38f, 0.22f}}, {"wood_beam", {0.40f, 0.26f, 0.14f}},
        {"roof_tiles", {0.48f, 0.20f, 0.14f}}, {"roof_metal", {0.36f, 0.40f, 0.43f}}, {"glass", {0.55f, 0.72f, 0.80f}},
        {"metal", {0.62f, 0.63f, 0.65f}}, {"steel_chain", {0.48f, 0.49f, 0.51f}}, {"asphalt", {0.16f, 0.16f, 0.17f}},
        {"paving", {0.66f, 0.63f, 0.57f}}, {"rope_fiber", {0.66f, 0.55f, 0.36f}}, {"ground_dirt", {0.38f, 0.29f, 0.20f}},
        {"grass", {0.27f, 0.45f, 0.20f}}, {"fabric", {0.34f, 0.45f, 0.60f}}, {"ceramic", {0.93f, 0.93f, 0.91f}},
        {"lacquer", {0.90f, 0.90f, 0.88f}}, {"leather", {0.40f, 0.24f, 0.14f}}, {"linen", {0.74f, 0.71f, 0.64f}},
    };
    const auto found = looks.find(Engine::WeaverProcedura::materialName(material));
    if(found == looks.end()) return glm::vec3(0.18f, 0.58f, 0.82f);
    return glm::pow(found->second, glm::vec3(2.2f));   //tablica je u sRGB-u, faktor boje materijala je linearan
}

inline std::filesystem::path proceduralTextureDirectory(){
#ifdef LOOM_ROOT_DIR
    const std::filesystem::path root = std::filesystem::path(LOOM_ROOT_DIR) / ".cache" / "procedura" / "textures";
#else
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom-procedura-textures";
#endif
    return root / ("v" + std::to_string(Engine::WeaverProcedura::textureGeneratorVersion));
}

struct ProceduralTextureFiles{ std::string color, metallicRoughness, normal; };

// The material's three maps as PNG files, made the first time they are asked for (512 x 512, one
// meter per repeat). Empty paths when the material has no texture or the files cannot be written.
inline ProceduralTextureFiles proceduralTextureFiles(uint16_t material, uint32_t size = 512){
    static std::mutex lock;
    static std::map<uint16_t, ProceduralTextureFiles> made;
    const std::lock_guard<std::mutex> guard(lock);
    const auto found = made.find(material);
    if(found != made.end()) return found->second;
    ProceduralTextureFiles files;
    const std::string name = Engine::WeaverProcedura::materialName(material);
    if(!name.empty()){
        const std::filesystem::path folder = proceduralTextureDirectory();
        const std::string stem = (folder / (name + "_" + std::to_string(size))).string();
        ProceduralTextureFiles candidate{stem + "_color.png", stem + "_mr.png", stem + "_normal.png"};
        std::error_code missing;
        const bool cached = std::filesystem::exists(candidate.color, missing) && std::filesystem::exists(candidate.metallicRoughness, missing) &&
                            std::filesystem::exists(candidate.normal, missing);
        if(cached) files = candidate;
        else{
            Engine::WeaverProcedura::MaterialTextures maps;
            std::string error;
            if(Engine::WeaverProcedura::makeMaterialTextures(name, size, maps, error)){
                try{
                    auto save = [&](const std::string& path, const std::vector<uint8_t>& pixels){
                        Spool::Image image;
                        image.pixels = pixels;
                        image.width = image.height = maps.size;
                        image.sourceChannels = 4;
                        Spool::savePng(path + ".tmp.png", image);
                        std::filesystem::rename(path + ".tmp.png", path);
                    };
                    save(candidate.color, maps.color);
                    save(candidate.metallicRoughness, maps.metallicRoughness);
                    save(candidate.normal, maps.normal);
                    files = candidate;
                }catch(const std::exception&){}
            }
        }
    }
    made[material] = files;
    return files;
}

// A viewport material with the procedural maps; the flat colour when there are none.
inline Warp::Material proceduralWarpMaterial(uint16_t material){
    Warp::Material look;
    look.name = "procedura:" + Engine::WeaverProcedura::materialName(material);
    const ProceduralTextureFiles files = proceduralTextureFiles(material);
    if(files.color.empty()){
        look.baseColor = glm::vec4(proceduralMaterialColour(material), 1.0f);
        look.roughness = 0.6f;
        return look;
    }
    look.baseColor = glm::vec4(1.0f);
    look.baseColorMap.source = files.color;
    look.metallic = 1.0f;          // the map holds the values; glTF multiplies map by factor
    look.roughness = 1.0f;
    look.metallicRoughnessMap.source = files.metallicRoughness;
    look.normalMap.source = files.normal;
    look.normalMap.amount = 1.0f;
    return look;
}

} // namespace Loom
