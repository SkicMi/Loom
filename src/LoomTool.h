#pragma once
//=============================================================================================
// TOOL: predmet koji lik moze drzati. Tool se oznaci pri uvozu (Tool / Weapon), ima gripove iz tool
// editora (gdje se drzi i kako) i collider od svojih trokuta, oko kojeg se prsti omotaju.
//
//   toolRootOf        entitet s Toolom za bilo koji dio predmeta (klik na mesh unutar grupe)
//   toolGeometry      trokuti svih modela predmeta u njegovom sustavu - za collider i za procjene
//   defaultGrip       prvi grip: na najduzoj osi, blizu jednog kraja; tool editor ga namjesti
//   handleRadius      debljina drske oko gripa, iz vrhova uz os
//   gripAlignedWorld  gdje predmet mora biti da mu drska lezi u dlanu te sake
//   suggestedLength   stvarna velicina za uvoz (mac ~1.1 m, pistolj ~0.25 m) - model s interneta
//                     je cesto u centimetrima ili "koliko god" velik
//=============================================================================================
#include "LoomHandPose.h"
#include "LoomHold.h"

#include <Engine/Physics.h>
#include <Spool/Gltf.h>
#include <Warp/Stage.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Loom{

inline Warp::Id toolRootOf(const Warp::Stage& stage, Warp::Id id){
    for(Warp::Id walk = id; walk != Warp::None;){
        const Warp::Entity* entity = stage.get(walk);
        if(!entity) break;
        if(entity->tool) return walk;
        walk = entity->parent;
    }
    return Warp::None;
}

struct ToolGeometry{
    Engine::Physics::TriangleMesh mesh;         //u sustavu entiteta s Toolom
    glm::vec3 low{0.0f}, high{0.0f};
    bool empty() const { return mesh.indices.empty(); }
};

//glTF scene po putu, bez slika - geometrija za collider (render ima svoju kopiju na kartici)
inline const Spool::GltfScene* toolGltf(const std::string& path){
    static std::map<std::string, std::shared_ptr<Spool::GltfScene>> cache;
    auto found = cache.find(path);
    if(found != cache.end()) return found->second.get();
    auto scene = std::make_shared<Spool::GltfScene>();
    std::string error;
    Spool::GltfLoadConfig config;
    config.decodeImages = false;
    if(!Spool::loadGltf(path, *scene, error, config)) scene.reset();
    cache[path] = scene;
    return scene.get();
}

inline ToolGeometry toolGeometry(const Warp::Stage& stage, Warp::Id root, double frame){
    ToolGeometry geometry;
    if(!stage.get(root)) return geometry;
    const glm::mat4 toLocal = glm::inverse(stage.worldMatrix(root, frame));
    bool first = true;
    std::vector<Warp::Id> pending{root};
    while(!pending.empty()){
        const Warp::Id id = pending.back();
        pending.pop_back();
        const Warp::Entity* entity = stage.get(id);
        if(!entity) continue;
        for(Warp::Id child : entity->children) pending.push_back(child);
        if(!entity->model || entity->model->mesh < 0) continue;
        const Spool::GltfScene* scene = toolGltf(entity->model->path);
        if(!scene || size_t(entity->model->mesh) >= scene->meshes.size()) continue;
        const glm::mat4 toRoot = toLocal * stage.worldMatrix(id, frame);
        for(const Spool::GltfPrimitive& primitive : scene->meshes[size_t(entity->model->mesh)].primitives){
            const uint32_t base = uint32_t(geometry.mesh.vertices.size());
            for(size_t v = 0; v < primitive.vertexCount(); ++v){
                const glm::vec3 p(toRoot * glm::vec4(primitive.positions[v * 3], primitive.positions[v * 3 + 1], primitive.positions[v * 3 + 2], 1.0f));
                geometry.mesh.vertices.push_back(p);
                geometry.low = first ? p : glm::min(geometry.low, p);
                geometry.high = first ? p : glm::max(geometry.high, p);
                first = false;
            }
            for(uint32_t index : primitive.indices) geometry.mesh.indices.push_back(base + index);
        }
    }
    return geometry;
}

//Najduza os kutije predmeta (0 x, 1 y, 2 z)
inline int toolLongAxis(const ToolGeometry& geometry){
    const glm::vec3 size = geometry.high - geometry.low;
    return size.x >= size.y && size.x >= size.z ? 0 : (size.y >= size.z ? 1 : 2);
}

//Prvi grip: na najduzoj osi, 15 % od donjeg kraja (drska je obicno na kraju), dlan prema drugoj
//najduzoj osi. Tool editor ga pomakne, okrene ili prebaci na drugi kraj
inline Warp::Grip defaultGrip(const ToolGeometry& geometry, const std::string& preset){
    Warp::Grip grip;
    grip.preset = preset;
    if(geometry.empty()) return grip;
    const int along = toolLongAxis(geometry);
    const glm::vec3 size = geometry.high - geometry.low;
    const int side = along == 0 ? (size.y >= size.z ? 1 : 2) : along == 1 ? (size.x >= size.z ? 0 : 2) : (size.x >= size.y ? 0 : 1);
    grip.axis = glm::vec3(0.0f);
    grip.axis[along] = 1.0f;
    grip.palm = glm::vec3(0.0f);
    grip.palm[side] = 1.0f;
    grip.point = 0.5f * (geometry.low + geometry.high);
    grip.point[along] = geometry.low[along] + 0.15f * size[along];
    return grip;
}

//Polumjer drske oko gripa: vrhovi u pojasu +-band uz os, udaljenost od osi prema dlanu (80. centil,
//da stitnik ili okidac ne napuhnu drsku)
inline float handleRadius(const ToolGeometry& geometry, const Warp::Grip& grip, float band = 0.02f){
    if(grip.thickness > 0.0f) return grip.thickness;
    const glm::vec3 axis = glm::normalize(grip.axis);
    std::vector<float> distances;
    for(const glm::vec3& v : geometry.mesh.vertices){
        const glm::vec3 d = v - grip.point;
        const float along = glm::dot(d, axis);
        if(std::fabs(along) > band) continue;
        distances.push_back(glm::length(d - axis * along));
    }
    if(distances.empty()) return 0.015f;
    std::sort(distances.begin(), distances.end());
    return std::max(0.004f, distances[size_t(0.8f * float(distances.size() - 1))]);
}

//Okvir sake: dlan, normala dlana, os od malog prsta prema kaziprstu. false kad sake nema
struct HandFrame{ glm::vec3 palm{0.0f}, normal{0.0f}, across{0.0f}; float palmDepth = 0.02f; };
inline bool handFrameAt(const Warp::Stage& stage, const HoldHand& hand, double frame, HandFrame& out){
    const HandFingers fingers = handFingersOf(stage, hand.hand, frame);
    if(!fingers.valid() || fingers.fingers[1].empty()) return false;
    const std::vector<Warp::Id>& pinky = !fingers.fingers[4].empty() ? fingers.fingers[4] : fingers.fingers[3];
    const glm::vec3 index(stage.worldMatrix(fingers.fingers[1].front(), frame)[3]);
    const glm::vec3 little(stage.worldMatrix(pinky.front(), frame)[3]);
    const glm::vec3 wrist(stage.worldMatrix(hand.hand, frame)[3]);
    out.normal = fingers.palmNormal;
    glm::vec3 across = index - little;
    across -= out.normal * glm::dot(across, out.normal);
    if(glm::length(across) < 1e-6f) return false;
    out.across = glm::normalize(across);
    out.palm = holdPalmPoint(stage, hand, frame);
    //Koza dlana je ispod linije kostiju; ~20 % duljine od zapesca do zglobova prstiju
    out.palmDepth = 0.2f * glm::length(0.5f * (index + little) - wrist);
    return true;
}

//Svijet predmeta kad mu grip lezi u dlanu: os drske uz os sake (prema kaziprstu), strana dlana
//gripa prema dlanu, sredina drske za polumjer drske + debljinu dlana ispred kostiju. Mjerilo ostaje
inline glm::mat4 gripAlignedWorld(const glm::mat4& itemWorld, const Warp::Grip& grip, float radius, const HandFrame& hand){
    glm::vec3 a = glm::normalize(grip.axis);
    glm::vec3 p = grip.palm - a * glm::dot(grip.palm, a);
    p = glm::length(p) > 1e-6f ? glm::normalize(p) : glm::normalize(glm::cross(a, glm::vec3(0.3f, 0.5f, 0.8f)));
    const glm::mat3 toolFrame(a, p, glm::cross(a, p));
    const glm::vec3 towardPalm = -hand.normal;       //od drske prema dlanu
    const glm::mat3 handFrame(hand.across, towardPalm, glm::cross(hand.across, towardPalm));
    const glm::mat3 rotation = handFrame * glm::transpose(toolFrame);
    glm::mat4 world(1.0f);
    for(int c = 0; c < 3; ++c) world[c] = glm::vec4(rotation[c] * glm::length(glm::vec3(itemWorld[c])), 0.0f);
    const glm::vec3 centre = hand.palm + hand.normal * (radius + hand.palmDepth);
    const glm::vec3 gripWorld(world * glm::vec4(grip.point, 1.0f));
    world[3] = glm::vec4(centre - gripWorld, 1.0f);
    return world;
}

//Stvarna velicina predmeta za uvoz (najdulja stranica, metri) iz imena i vrste; 0 = ne zna se
inline float suggestedToolLength(std::string name, const std::string& kind){
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    struct Size{ const char* word; float metres; };
    static const Size sizes[] = {
        {"pistol", 0.25f}, {"eagle", 0.27f}, {"glock", 0.19f}, {"revolver", 0.3f}, {"gun", 0.25f},
        {"rifle", 1.0f}, {"shotgun", 1.05f}, {"sniper", 1.2f},
        {"dagger", 0.35f}, {"knife", 0.25f}, {"katana", 1.0f}, {"bastard", 1.15f}, {"longsword", 1.15f}, {"sword", 1.0f},
        {"axe", 0.75f}, {"hammer", 0.35f}, {"mace", 0.7f}, {"spear", 2.0f}, {"staff", 1.7f}, {"bat", 0.85f},
        {"shield", 0.8f}, {"torch", 0.5f}, {"cup", 0.1f}, {"mug", 0.11f}, {"bottle", 0.28f}, {"phone", 0.15f},
    };
    for(const Size& size : sizes) if(name.find(size.word) != std::string::npos) return size.metres;
    return kind == "weapon" ? 0.9f : 0.0f;
}

//Collideri toolova po entitetu; iznova se grade kad se promijeni broj trokuta ili modeli
struct ToolColliders{
    struct Entry{ Engine::Physics::Collider collider; size_t vertices = 0; float radiusScale = 1.0f; };
    std::map<Warp::Id, Entry> entries;

    const Engine::Physics::Collider* of(const Warp::Stage& stage, Warp::Id root, double frame){
        const ToolGeometry geometry = toolGeometry(stage, root, frame);
        if(geometry.empty()) return nullptr;
        Entry& entry = entries[root];
        if(entry.collider.empty() || entry.vertices != geometry.mesh.vertices.size()){
            entry.collider = Engine::Physics::Collider::fromMesh(geometry.mesh);
            entry.vertices = geometry.mesh.vertices.size();
        }
        return entry.collider.empty() ? nullptr : &entry.collider;
    }
};

}
