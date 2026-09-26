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
#include <limits>
#include <array>
#include <cmath>
#include <map>
#include <set>
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
//PISTOLJ: rukohvat strsi poprijeko na cijev (druga os oblika). Uz drugu os se gleda koliko je predmet
//dug duz cijevi: kraj gdje je kratak je dno rukohvata, a gdje je dug je cijev/zatvarac. Rukohvat je od
//dna do prve kriske duge preko 55 % cijevi; os mu ide od dna prema cijevi (s nagibom rukohvata, iz
//sredista kriski na 20 % i 80 %), tocka na 50 %. Dlan je na bocnoj ploci (treca os); na koju stranu,
//odluci se pri hvatu tako da cijev gleda naprijed (pointMuzzleForward)
inline bool pistolGrip(const ToolGeometry& geometry, const ToolAxes& frame, Warp::Grip& grip){
    const glm::vec3 barrel = frame.axes[0], up = frame.axes[1];
    float low0 = 0.0f, high0 = 0.0f, low1 = 0.0f, high1 = 0.0f;
    toolExtentAlong(geometry, frame.centre, barrel, low0, high0);
    toolExtentAlong(geometry, frame.centre, up, low1, high1);
    const float length = std::max(high0 - low0, 1e-9f), height = std::max(high1 - low1, 1e-9f);
    constexpr int bins = 16;
    std::array<float, bins> lo, hi;
    lo.fill(std::numeric_limits<float>::max());
    hi.fill(std::numeric_limits<float>::lowest());
    auto binOf = [&](const glm::vec3& v){ return std::clamp(int((glm::dot(v - frame.centre, up) - low1) / height * float(bins)), 0, bins - 1); };
    for(const glm::vec3& v : geometry.mesh.vertices){
        const int b = binOf(v);
        const float t = glm::dot(v - frame.centre, barrel);
        lo[size_t(b)] = std::min(lo[size_t(b)], t);
        hi[size_t(b)] = std::max(hi[size_t(b)], t);
    }
    auto extent = [&](int b){ return hi[size_t(b)] >= lo[size_t(b)] ? hi[size_t(b)] - lo[size_t(b)] : 0.0f; };
    //Dno rukohvata: kraj druge osi s kracom kriskom
    const bool bottomLow = extent(0) <= extent(bins - 1);
    auto binAt = [&](int k){ return bottomLow ? k : bins - 1 - k; };      //k od dna prema cijevi
    int top = -1;
    for(int k = 0; k < bins; ++k) if(extent(binAt(k)) > 0.55f * length){ top = k; break; }
    if(top < 2 || extent(binAt(0)) > 0.5f * length) return false;
    //Rukohvat je na straznjem kraju cijevi: strana na kojoj su kriske rukohvata prema sredini predmeta
    float handleMid = 0.0f;
    for(int k = 0; k < top; ++k) handleMid += 0.5f * (lo[size_t(binAt(k))] + hi[size_t(binAt(k))]);
    handleMid /= float(top);
    const bool rearLow = handleMid < 0.5f * (low0 + high0);
    //Sredista kriski rukohvata: samo vrhovi do 6 cm (22 % cijevi) od straznjeg ruba kriske - branik
    //okidaca i okidac su ispred i inace nagnu os rukohvata prema naprijed
    auto centreOf = [&](float fraction){
        const int k = std::clamp(int(fraction * float(top)), 0, top - 1);
        const int b = binAt(k);
        const float rear = rearLow ? lo[size_t(b)] : hi[size_t(b)];
        glm::vec3 sum(0.0f);
        int count = 0;
        for(const glm::vec3& v : geometry.mesh.vertices){
            if(binOf(v) != b) continue;
            if(std::fabs(glm::dot(v - frame.centre, barrel) - rear) > 0.22f * length) continue;
            sum += v;
            ++count;
        }
        return count ? sum / float(count) : frame.centre;
    };
    const glm::vec3 bottom = centreOf(0.15f), upper = centreOf(0.6f);
    if(glm::length(upper - bottom) < 1e-9f) return false;
    grip.point = centreOf(0.45f);
    grip.axis = glm::normalize(upper - bottom);
    grip.palm = frame.axes[2];
    return true;
}

inline Warp::Grip defaultGrip(const ToolGeometry& geometry, const std::string& preset){
    Warp::Grip grip;
    grip.preset = preset;
    if(geometry.empty()) return grip;
    const ToolAxes frame = toolAxes(geometry);
    if(preset == "pistol" && pistolGrip(geometry, frame, grip)) return grip;
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
//Mjeri se PREMA DLANU (strana grip.palm): rukohvat pistolja nije okrugao, dlan lezi na bocnoj ploci
inline float handleRadius(const ToolGeometry& geometry, const Warp::Grip& grip, float unitsPerMetre = 1.0f){
    if(grip.thickness > 0.0f) return grip.thickness;
    const float band = 0.02f * unitsPerMetre;
    const glm::vec3 axis = glm::normalize(grip.axis);
    glm::vec3 palm = grip.palm - axis * glm::dot(grip.palm, axis);
    const bool directed = glm::length(palm) > 1e-6f;
    if(directed) palm = glm::normalize(palm);
    std::vector<float> distances;
    for(const glm::vec3& v : geometry.mesh.vertices){
        const glm::vec3 d = v - grip.point;
        const float along = glm::dot(d, axis);
        if(std::fabs(along) > band) continue;
        const glm::vec3 across = d - axis * along;
        //Na strani dlana: vrhovi unutar 20 st od smjera dlana (siri stozac na duguljastom rukohvatu
        //uhvati kutove i napuse debljinu)
        if(!directed || glm::dot(across, palm) > 0.9397f * glm::length(across)) distances.push_back(glm::length(across));
    }
    if(distances.empty()) return 0.015f * unitsPerMetre;
    std::sort(distances.begin(), distances.end());
    return std::max(0.004f * unitsPerMetre, distances[size_t(0.8f * float(distances.size() - 1))]);
}

//MREZA ZA PRSTE: modeli s interneta imaju i stotine tisuca trokuta (pistolj 460k), a prsti trebaju samo
//oblik. Vrhovi se spoje na mrezi od cell (jedinice modela; ~2 mm), trokuti koji se tako izrode otpadnu.
//Collider se gradi 100x brze, a upiti prstiju su jednako brzi za bilo koji model
inline Engine::Physics::TriangleMesh handMesh(const Engine::Physics::TriangleMesh& mesh, float cell){
    if(mesh.indices.size() / 3 < 20000 || cell <= 0.0f) return mesh;
    Engine::Physics::TriangleMesh result;
    std::map<std::array<int64_t, 3>, uint32_t> cells;
    std::vector<uint32_t> remap(mesh.vertices.size());
    std::vector<glm::vec3> sums;
    std::vector<int> counts;
    for(size_t i = 0; i < mesh.vertices.size(); ++i){
        const glm::vec3& v = mesh.vertices[i];
        const std::array<int64_t, 3> key{int64_t(std::floor(v.x / cell)), int64_t(std::floor(v.y / cell)), int64_t(std::floor(v.z / cell))};
        auto [found, inserted] = cells.emplace(key, uint32_t(sums.size()));
        if(inserted){ sums.push_back(glm::vec3(0.0f)); counts.push_back(0); }
        remap[i] = found->second;
        sums[found->second] += v;
        ++counts[found->second];
    }
    for(size_t i = 0; i < sums.size(); ++i) result.vertices.push_back(sums[i] / float(counts[i]));
    std::set<std::array<uint32_t, 3>> seen;
    for(size_t t = 0; t + 2 < mesh.indices.size(); t += 3){
        const uint32_t a = remap[mesh.indices[t]], b = remap[mesh.indices[t + 1]], c = remap[mesh.indices[t + 2]];
        if(a == b || b == c || a == c) continue;
        std::array<uint32_t, 3> key{a, b, c};
        std::sort(key.begin(), key.end());
        if(!seen.insert(key).second) continue;
        result.indices.insert(result.indices.end(), {a, b, c});
    }
    return result;
}

//Okvir sake: dlan, normala dlana, os od malog prsta prema kaziprstu. false kad sake nema
struct HandFrame{ glm::vec3 palm{0.0f}, normal{0.0f}, across{0.0f}, forward{0.0f}; float palmDepth = 0.02f; };
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
    //POWER GRIP: drska ide dijagonalno kroz dlan - kraj uz kaziprst prema zglobovima, kraj uz mali prst
    //prema zapescu (~20 st). Zato mac u saci gleda malo naprijed, a ne okomito na podlakticu
    glm::vec3 forward = 0.5f * (index + little) - wrist;
    forward -= out.normal * glm::dot(forward, out.normal);
    if(glm::length(forward) > 1e-6f){
        forward = glm::normalize(forward - out.across * glm::dot(forward, out.across));
        out.forward = forward;
        out.across = glm::normalize(out.across * std::cos(glm::radians(20.0f)) + forward * std::sin(glm::radians(20.0f)));
    }
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

//Smjer cijevi u sustavu predmeta: duz glavne osi prema strani koja je od gripa dalja (cijev je
//ispred rukohvata)
inline glm::vec3 muzzleDirection(const ToolGeometry& geometry, const Warp::Grip& grip){
    const glm::vec3 barrel = toolAxes(geometry).axes[0];
    float low = 0.0f, high = 0.0f;
    toolExtentAlong(geometry, grip.point, barrel, low, high);
    return high >= -low ? barrel : -barrel;
}

//Pistolj: dlan na onu bocnu plocu uz koju cijev gleda naprijed (smjer prstiju) - ista odluka za
//lijevu i desnu saku. Vraca svijet predmeta
inline glm::mat4 pointMuzzleForward(const glm::mat4& itemWorld, Warp::Grip& grip, float radius, const HandFrame& hand,
                                    const glm::vec3& muzzle){
    glm::mat4 world = gripAlignedWorld(itemWorld, grip, radius, hand);
    if(glm::length(hand.forward) < 0.5f) return world;
    if(glm::dot(glm::normalize(glm::vec3(world * glm::vec4(muzzle, 0.0f))), hand.forward) < 0.0f){
        grip.palm = -grip.palm;
        world = gripAlignedWorld(itemWorld, grip, radius, hand);
    }
    return world;
}

//SVIJET PREDMETA U SACI za grip (drska u dlanu; pistolj s cijevi naprijed - tada se grip.palm moze
//okrenuti, pa ga pozivatelj spremi natrag u tool)
//
//S rukom (holdHand) se jos isproba odmak drske od dlana (0 ili 1 cm) i polozaj sake duz drske (-3..+1.5
//cm), i uzme onaj gdje se prsti najbolje omotaju (ConformReport: nijedan zglob ne krece u predmetu, vrhovi najblize povrsini). Tako
//srednji prst pistolja zavrsi ispod branika okidaca, a saka na macu ne sjedne na stitnik
inline glm::mat4 heldWorld(const Warp::Stage& stage, Warp::Id item, Warp::Grip& grip, const HandFrame& hand, double frame,
                           const HoldHand* holdHand = nullptr){
    const glm::mat4 itemWorld = stage.worldMatrix(item, frame);
    const float scale = glm::length(glm::vec3(itemWorld[0]));
    const ToolGeometry geometry = toolGeometry(stage, item, frame);
    const float radius = handleRadius(geometry, grip, 1.0f / std::max(scale, 1e-9f)) * scale;
    const glm::mat4 world = grip.preset == "pistol" ? pointMuzzleForward(itemWorld, grip, radius, hand, muzzleDirection(geometry, grip))
                                                    : gripAlignedWorld(itemWorld, grip, radius, hand);
    if(!holdHand || geometry.empty()) return world;
    const HandFingers fingers = handFingersOf(stage, holdHand->hand, frame);
    if(!fingers.valid()) return world;
    const Engine::Physics::Collider collider = Engine::Physics::Collider::fromMesh(handMesh(geometry.mesh, 0.002f / std::max(scale, 1e-9f)));
    if(collider.empty()) return world;
    const glm::vec3 axis = glm::normalize(glm::vec3(world * glm::vec4(grip.axis, 0.0f)));
    const GripPreset& preset = gripPreset(grip.preset);
    auto scoreAt = [&](float slide, float push, glm::mat4& candidate){
        candidate = world;
        candidate[3] += glm::vec4(axis * slide + hand.normal * push, 0.0f);
        ConformReport report;
        conformedHandPoseAt(stage, fingers, preset, frame, collider, candidate, 12, &report);
        //Blaga prednost za polozaj iz gripa (i drsku uz dlan): medju jednako dobrima ostaje tamo
        return report.score() + std::fabs(slide) * 0.1f + push * 0.5f;
    };
    glm::mat4 best = world, candidate;
    float bestScore = std::numeric_limits<float>::max();
    for(const float push : {0.0f, 0.01f})
        for(const float slide : {0.0f, -0.015f, -0.03f, 0.015f}){
            const float score = scoreAt(slide, push, candidate);
            if(score < bestScore){ bestScore = score; best = candidate; }
        }
    return best;
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
            const float scale = glm::length(glm::vec3(stage.worldMatrix(root, frame)[0]));
            entry.collider = Engine::Physics::Collider::fromMesh(handMesh(geometry.mesh, 0.002f / std::max(scale, 1e-9f)));
            entry.vertices = geometry.mesh.vertices.size();
        }
        return entry.collider.empty() ? nullptr : &entry.collider;
    }
};

}
