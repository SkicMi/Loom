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
#include <array>
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

//Glavne osi oblika (PCA vrhova): modeli s interneta cesto leze dijagonalno u svom sustavu (rotacija
//unutar cvorova), pa najduza os kutije nije os maca. axes[0] je najdulja
struct ToolAxes{ glm::vec3 centre{0.0f}; glm::vec3 axes[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; };
inline ToolAxes toolAxes(const ToolGeometry& geometry){
    ToolAxes result;
    const auto& vertices = geometry.mesh.vertices;
    if(vertices.size() < 3) return result;
    glm::dvec3 mean(0.0);
    for(const glm::vec3& v : vertices) mean += glm::dvec3(v);
    mean /= double(vertices.size());
    glm::dmat3 covariance(0.0);
    for(const glm::vec3& v : vertices){
        const glm::dvec3 d = glm::dvec3(v) - mean;
        covariance += glm::outerProduct(d, d);
    }
    result.centre = glm::vec3(mean);
    //Potencijska iteracija: najveca os, pa druga okomita na nju
    auto dominant = [&](const glm::dmat3& m, glm::dvec3 start){
        glm::dvec3 x = glm::normalize(start);
        for(int i = 0; i < 64; ++i){
            const glm::dvec3 next = m * x;
            if(glm::length(next) < 1e-30) break;
            x = glm::normalize(next);
        }
        return x;
    };
    const glm::dvec3 first = dominant(covariance, glm::dvec3(0.31, 0.83, 0.47));
    const double firstValue = glm::dot(first, covariance * first);
    const glm::dmat3 deflated = covariance - firstValue * glm::outerProduct(first, first);
    glm::dvec3 second = dominant(deflated, glm::cross(first, glm::dvec3(0.57, 0.21, 0.79)));
    second = glm::normalize(second - first * glm::dot(second, first));
    result.axes[0] = glm::vec3(first);
    result.axes[1] = glm::vec3(second);
    result.axes[2] = glm::normalize(glm::cross(result.axes[0], result.axes[1]));
    return result;
}

//Raspon projekcija vrhova na os (za "Along the tool" u tool editoru)
inline void toolExtentAlong(const ToolGeometry& geometry, const glm::vec3& origin, const glm::vec3& axis, float& low, float& high){
    low = 0.0f; high = 0.0f;
    bool first = true;
    for(const glm::vec3& v : geometry.mesh.vertices){
        const float t = glm::dot(v - origin, axis);
        low = first ? t : std::min(low, t);
        high = first ? t : std::max(high, t);
        first = false;
    }
}

//PRVI GRIP iz oblika: profil sirine duz glavne osi (24 odsjecka). Najsiri odsjecak je stitnik maca
//ili glava sjekire/cekica:
//  - glava na samom kraju (sjekira, cekic, bat): drska je suprotni kraj, grip 15 % od njega
//  - stitnik unutar predmeta (mac, noz): drska je KRACA strana od stitnika, grip na njenoj sredini
//Os gripa gleda prema stitniku/glavi (od malog prsta prema palcu); tocka je na sredini presjeka
//drske. Pistolj i cudni oblici se namjeste u tool editoru (Along the tool, Flip, Turn palm)
inline Warp::Grip defaultGrip(const ToolGeometry& geometry, const std::string& preset){
    Warp::Grip grip;
    grip.preset = preset;
    if(geometry.empty()) return grip;
    const ToolAxes frame = toolAxes(geometry);
    const glm::vec3 axis = frame.axes[0];
    float low = 0.0f, high = 0.0f;
    toolExtentAlong(geometry, frame.centre, axis, low, high);
    const float length = std::max(high - low, 1e-9f);
    constexpr int bins = 24;
    std::array<float, bins> width{};
    std::array<glm::vec3, bins> sum{};
    std::array<int, bins> count{};
    for(const glm::vec3& v : geometry.mesh.vertices){
        const float t = glm::dot(v - frame.centre, axis);
        const int bin = std::clamp(int((t - low) / length * float(bins)), 0, bins - 1);
        const glm::vec3 across = (v - frame.centre) - axis * t;
        width[size_t(bin)] = std::max(width[size_t(bin)], glm::length(across));
        sum[size_t(bin)] += across;
        ++count[size_t(bin)];
    }
    const int widest = int(std::max_element(width.begin(), width.end()) - width.begin());
    float at = 0.15f;
    int toward = +1;
    if(widest <= 1){ at = 0.85f; toward = -1; }                 //glava na donjem kraju: drska na gornjem
    else if(widest >= bins - 2){ at = 0.15f; toward = +1; }     //glava na gornjem kraju: drska na donjem
    else{
        const float guard = (float(widest) + 0.5f) / float(bins);
        if(guard <= 0.5f){ at = 0.5f * (guard - 0.5f / float(bins)); toward = +1; }
        else{ at = 0.5f * (guard + 0.5f / float(bins) + 1.0f); toward = -1; }
    }
    const int bin = std::clamp(int(at * float(bins)), 0, bins - 1);
    const glm::vec3 offset = count[size_t(bin)] > 0 ? sum[size_t(bin)] / float(count[size_t(bin)]) : glm::vec3(0.0f);
    grip.point = frame.centre + axis * (low + at * length) + offset;
    grip.axis = axis * float(toward);
    grip.palm = frame.axes[1];
    return grip;
}

//Polumjer drske oko gripa: vrhovi u pojasu +-2 cm uz os, udaljenost od osi (80. centil, da stitnik ili
//okidac ne napuhnu drsku). U jedinicama modela; unitsPerMetre = 1 / mjerilo toola (model s interneta
//je cesto u centimetrima ili "koliko god" velik, a pojas i granice su u metrima scene)
inline float handleRadius(const ToolGeometry& geometry, const Warp::Grip& grip, float unitsPerMetre = 1.0f){
    if(grip.thickness > 0.0f) return grip.thickness;
    const float band = 0.02f * unitsPerMetre;
    const glm::vec3 axis = glm::normalize(grip.axis);
    std::vector<float> distances;
    for(const glm::vec3& v : geometry.mesh.vertices){
        const glm::vec3 d = v - grip.point;
        const float along = glm::dot(d, axis);
        if(std::fabs(along) > band) continue;
        distances.push_back(glm::length(d - axis * along));
    }
    if(distances.empty()) return 0.015f * unitsPerMetre;
    std::sort(distances.begin(), distances.end());
    return std::max(0.004f * unitsPerMetre, distances[size_t(0.8f * float(distances.size() - 1))]);
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
