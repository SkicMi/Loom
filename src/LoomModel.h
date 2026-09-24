#pragma once
//=============================================================================================
// glTF MODEL U SCENI: cvorovi postaju entiteti, mreze komponenta Model, materijali ulaze u
// biblioteku scene.
//
//   /<datoteka>              grupa: mjesto i mjerilo modela u sceni
//       <cvor>               transformacija cvora; Model kad cvor nosi mrezu
//           <dijete> ...
//
// Materijal se KOPIRA u biblioteku ("<datoteka>/<ime>") s mapama koje pokazuju u istu datoteku
// (glTF + redni broj slike) - pa se da uredjivati u editoru bez diranja datoteke, i dva modela s
// istim imenom materijala ne dijele materijal slucajno.
//
// Geometrija ostaje u datoteci: scena pamti samo put i koju mrezu, a editor je cita i drzi na
// kartici (LoomPbr.h). Projekt tako ostaje malen i model se osvjezi kad se datoteka promijeni
//=============================================================================================
#include <Spool/Gltf.h>
#include <Warp/Stage.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace Loom{

struct ModelImportReport{
    Warp::Id group = Warp::None;
    size_t nodes = 0, meshes = 0, materials = 0;
    glm::vec3 low{0.0f}, high{0.0f};        //granice modela u njegovim jedinicama, prije mjerila
    std::string problem;
};

inline std::vector<std::filesystem::path> modelFilesIn(const std::filesystem::path& directory){
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for(const auto& entry : std::filesystem::directory_iterator(directory, error)){
        if(error) break;
        if(!entry.is_regular_file(error)) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return char(std::tolower(c)); });
        if(extension == ".gltf" || extension == ".glb") found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());
    return found;
}

//Lokalna transformacija cvora kao matrica
inline glm::mat4 nodeMatrix(const Spool::GltfNode& node){
    Warp::Transform t;
    t.translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
    t.rotation = glm::normalize(glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
    t.scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
    return t.matrix();
}

//Granice cijele scene modela (svi cvorovi s mrezama, kroz svoje transformacije)
inline void gltfBounds(const Spool::GltfScene& scene, glm::vec3& low, glm::vec3& high){
    low = glm::vec3(1e30f);
    high = glm::vec3(-1e30f);
    std::vector<std::pair<int, glm::mat4>> pending;
    for(int root : scene.roots) pending.push_back({root, glm::mat4(1.0f)});
    size_t guard = 0;
    while(!pending.empty() && guard++ < 100000){
        const auto [index, parent] = pending.back();
        pending.pop_back();
        const Spool::GltfNode& node = scene.nodes[size_t(index)];
        const glm::mat4 world = parent * nodeMatrix(node);
        if(node.mesh >= 0){
            for(const Spool::GltfPrimitive& p : scene.meshes[size_t(node.mesh)].primitives){
                for(size_t v = 0; v < p.vertexCount(); ++v){
                    const glm::vec3 at = glm::vec3(world * glm::vec4(p.positions[v * 3], p.positions[v * 3 + 1], p.positions[v * 3 + 2], 1.0f));
                    low = glm::min(low, at);
                    high = glm::max(high, at);
                }
            }
        }
        for(int child : node.children) pending.push_back({child, world});
    }
    if(low.x > high.x){ low = glm::vec3(0.0f); high = glm::vec3(0.0f); }
}

//Materijal iz glTF-a u Warpov. Mapa pokazuje u istu datoteku, na sliku koju tekstura koristi
inline Warp::Material materialFromGltf(const Spool::GltfScene& scene, const Spool::GltfMaterial& m, const std::string& name){
    auto slot = [&](const Spool::GltfTextureRef& ref){
        Warp::TextureSlot t;
        if(ref.texture < 0 || size_t(ref.texture) >= scene.textures.size()) return t;
        const int image = scene.textures[size_t(ref.texture)].image;
        if(image < 0 || size_t(image) >= scene.images.size()) return t;
        t.source = scene.path;
        t.image = image;
        t.texCoord = ref.texCoord;
        t.amount = ref.scale;
        return t;
    };
    Warp::Material out;
    out.name = name;
    out.baseColor = glm::vec4(m.baseColor[0], m.baseColor[1], m.baseColor[2], m.baseColor[3]);
    out.baseColorMap = slot(m.baseColorTexture);
    out.metallic = m.metallic;
    out.roughness = m.roughness;
    out.metallicRoughnessMap = slot(m.metallicRoughnessTexture);
    out.normalMap = slot(m.normalTexture);
    out.occlusionMap = slot(m.occlusionTexture);
    out.emissive = glm::vec3(m.emissive[0], m.emissive[1], m.emissive[2]);
    out.emissiveStrength = m.emissiveStrength;
    out.emissiveMap = slot(m.emissiveTexture);
    out.alphaMode = m.alphaMode == Spool::GltfMaterial::Alpha::Mask ? Warp::Material::Alpha::Mask
                  : m.alphaMode == Spool::GltfMaterial::Alpha::Blend ? Warp::Material::Alpha::Blend : Warp::Material::Alpha::Opaque;
    out.alphaCutoff = m.alphaCutoff;
    out.doubleSided = m.doubleSided;
    return out;
}

//position: gdje u svijetu stoji dno modela (sredina donje plohe granica); scale: mjerilo grupe
inline ModelImportReport importGltf(Warp::Stage& stage, const Spool::GltfScene& scene, Warp::Id parent = Warp::None,
                                    glm::vec3 position = glm::vec3(0.0f), float scale = 1.0f){
    ModelImportReport report;
    if(scene.roots.empty()){ report.problem = "model nema cvorova za prikaz"; return report; }
    const std::filesystem::path path(scene.path);
    const std::string stem = path.stem().string();

    //Materijali u biblioteku, s imenom datoteke ispred
    std::vector<int> materialIndex(scene.materials.size(), -1);
    for(size_t i = 0; i < scene.materials.size(); ++i){
        const std::string name = stem + "/" + (scene.materials[i].name.empty() ? "materijal" + std::to_string(i) : scene.materials[i].name);
        materialIndex[i] = stage.addMaterial(materialFromGltf(scene, scene.materials[i], name));
    }
    report.materials = scene.materials.size();

    gltfBounds(scene, report.low, report.high);
    report.group = stage.create(stem.empty() ? "Model" : stem, parent);
    Warp::Entity& group = *stage.get(report.group);
    group.local.scale = glm::vec3(scale);
    //Dno modela na zadano mjesto: sredina granica vodoravno, dno granica okomito
    const glm::vec3 anchor(0.5f * (report.low.x + report.high.x), report.low.y, 0.5f * (report.low.z + report.high.z));
    group.local.translation = position - scale * anchor;

    //Cvorovi, s cuvarom protiv petlje (cvor koji je sam sebi predak u losoj datoteci)
    std::vector<uint8_t> visited(scene.nodes.size(), 0);
    std::vector<std::pair<int, Warp::Id>> pending;
    for(auto root = scene.roots.rbegin(); root != scene.roots.rend(); ++root) pending.push_back({*root, report.group});
    std::vector<uint8_t> meshUsed(scene.meshes.size(), 0);
    while(!pending.empty()){
        const auto [index, under] = pending.back();
        pending.pop_back();
        if(index < 0 || size_t(index) >= scene.nodes.size() || visited[size_t(index)]) continue;
        visited[size_t(index)] = 1;
        const Spool::GltfNode& node = scene.nodes[size_t(index)];
        const Warp::Id id = stage.create(node.name.empty() ? "Cvor" + std::to_string(index) : node.name, under);
        Warp::Entity& entity = *stage.get(id);
        entity.local.translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        entity.local.rotation = glm::normalize(glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
        entity.local.scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
        if(node.joint) entity.joint = Warp::Joint{};
        if(node.mesh >= 0){
            Warp::Model model;
            model.path = scene.path;
            model.mesh = node.mesh;
            for(const Spool::GltfPrimitive& p : scene.meshes[size_t(node.mesh)].primitives){
                model.materials.push_back(p.material >= 0 ? materialIndex[size_t(p.material)] : -1);
            }
            entity.model = model;
            meshUsed[size_t(node.mesh)] = 1;
        }
        ++report.nodes;
        for(auto child = node.children.rbegin(); child != node.children.rend(); ++child) pending.push_back({*child, id});
    }
    report.meshes = size_t(std::count(meshUsed.begin(), meshUsed.end(), uint8_t(1)));
    return report;
}

}
