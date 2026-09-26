#pragma once
//=============================================================================================
// POZA PRSTIJU ZA HVAT: preset (grip, pistol, cup...) postane sloj animacije na liku, od On do Off
// hvata, s kratkim zatvaranjem sake prije i otvaranjem poslije.
//
// PRSTI SE NADJU IZ STABLA, ne po imenima: lanci kostiju ispod kosti sake. Palac je lanac cija je
// baza najbliza zapescu; ostali su poredani po udaljenosti od palca (kaziprst najblizi, mali prst
// najdalji). Lanac s metakarpalom (Manny: index_metacarpal -> index_01...) savija samo zadnje tri.
// Tako radi i UniRig (bone_N, bez imena) i imenovani rigovi.
//
// SAVIJANJE PREMA DLANU: svaki zglob se okrene oko osi (smjer kosti x normala dlana) - vrh prsta
// ide prema normali. Normala je okomita na ravninu zapesce/baza kaziprsta/baza malog prsta, a
// strana se uzme iz vec blago savijenog srednjeg prsta u mirnoj pozi (Relaxed hands), jer je to
// jedino sto pouzdano kaze gdje je dlan na bilo kojem rigu.
//
// Poza je ADITIVNA preko pokreta u tom kadru (kao svaki sloj, LoomPoseBlend.h), pa se prsti koje je
// Kimodo mozda pomaknuo ne izgube, samo se dodatno saviju.
//=============================================================================================
#include "LoomAnimLayers.h"
#include "LoomPoseBlend.h"

#include <Engine/Physics.h>
#include <Warp/Stage.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <string>
#include <vector>

namespace Loom{

//Prsti jedne sake: 0 palac, 1 kaziprst, 2 srednji, 3 prstenjak, 4 mali. Svaki do tri kosti (od baze),
//prazan kad ga rig nema
struct HandFingers{
    Warp::Id hand = Warp::None;
    std::array<std::vector<Warp::Id>, 5> fingers;
    glm::vec3 palmNormal{0.0f};             //svijet, u kadru u kojem su prsti nadjeni
    bool valid() const { return hand != Warp::None && !fingers[2].empty() && glm::length(palmNormal) > 0.5f; }
};

namespace handpose{
inline glm::vec3 at(const Warp::Stage& stage, Warp::Id id, double frame){ return glm::vec3(stage.worldMatrix(id, frame)[3]); }
inline glm::quat worldRotation(const Warp::Stage& stage, Warp::Id id, double frame){
    const glm::mat4 m = stage.worldMatrix(id, frame);
    return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                                                   glm::normalize(glm::vec3(m[2])))));
}
}

inline HandFingers handFingersOf(const Warp::Stage& stage, Warp::Id hand, double frame){
    HandFingers result;
    const Warp::Entity* entity = stage.get(hand);
    if(!entity) return result;
    result.hand = hand;
    const glm::vec3 wrist = handpose::at(stage, hand, frame);
    //Lanci: svako dijete sake, pa prvo dijete-kost dok ih ima
    std::vector<std::vector<Warp::Id>> chains;
    for(Warp::Id child : entity->children){
        std::vector<Warp::Id> chain;
        for(Warp::Id walk = child; walk != Warp::None;){
            const Warp::Entity* joint = stage.get(walk);
            if(!joint || !joint->joint) break;
            chain.push_back(walk);
            Warp::Id next = Warp::None;
            for(Warp::Id grandchild : joint->children) if(stage.get(grandchild) && stage.get(grandchild)->joint){ next = grandchild; break; }
            walk = next;
        }
        if(chain.size() >= 2) chains.push_back(chain);
    }
    if(chains.size() < 3) return result;
    //Metakarpal (lanac od 4+) se ne savija: zadnje tri kosti su prst
    for(auto& chain : chains) if(chain.size() > 3) chain.erase(chain.begin(), chain.end() - 3);
    auto base = [&](const std::vector<Warp::Id>& chain){ return handpose::at(stage, chain.front(), frame); };
    size_t thumb = 0;
    for(size_t i = 1; i < chains.size(); ++i)
        if(glm::length(base(chains[i]) - wrist) < glm::length(base(chains[thumb]) - wrist)) thumb = i;
    std::vector<std::vector<Warp::Id>> others;
    for(size_t i = 0; i < chains.size(); ++i) if(i != thumb) others.push_back(chains[i]);
    const glm::vec3 thumbBase = base(chains[thumb]);
    std::sort(others.begin(), others.end(), [&](const auto& a, const auto& b){
        return glm::length(base(a) - thumbBase) < glm::length(base(b) - thumbBase);
    });
    result.fingers[0] = chains[thumb];
    for(size_t i = 0; i < others.size() && i < 4; ++i) result.fingers[i + 1] = others[i];
    //Bez petog prsta (4 lanca: palac + 3) srednji je drugi po redu - [2] ostaje popunjen
    const std::vector<Warp::Id>& index = result.fingers[1];
    const std::vector<Warp::Id>& pinky = !result.fingers[4].empty() ? result.fingers[4] : result.fingers[3];
    if(index.empty() || pinky.empty()) return result;
    glm::vec3 normal = glm::cross(base(index) - wrist, base(pinky) - wrist);
    if(glm::length(normal) < 1e-8f) return result;
    normal = glm::normalize(normal);
    //Strana dlana iz savijenosti srednjeg prsta: vrh odstupa od pravca prve kosti prema dlanu
    const std::vector<Warp::Id>& middle = result.fingers[2];
    const glm::vec3 m0 = handpose::at(stage, middle.front(), frame);
    const glm::vec3 m1 = handpose::at(stage, middle[1], frame);
    const glm::vec3 tip = handpose::at(stage, middle.back(), frame);
    const glm::vec3 direction = glm::normalize(m1 - m0);
    const glm::vec3 bend = (tip - m0) - direction * glm::dot(tip - m0, direction);
    float side = glm::dot(bend, normal);
    //Ravan srednji prst: palac je na strani dlana
    if(std::fabs(side) < 1e-6f * glm::length(tip - m0)) side = glm::dot(handpose::at(stage, result.fingers[0].back(), frame) - wrist, normal);
    result.palmNormal = side >= 0.0f ? normal : -normal;
    return result;
}

//Preset: savijanje u stupnjevima po prstu (palac..mali) i po kosti (baza, srednja, vrh)
struct GripPreset{
    const char* name;
    const char* label;
    float curl[5][3];
};

inline const std::vector<GripPreset>& gripPresets(){
    static const std::vector<GripPreset> presets{
        {"grip",    "Grip",    {{25, 30, 25}, {70, 85, 55}, {72, 85, 55}, {74, 85, 55}, {76, 85, 55}}},
        {"pistol",  "Pistol",  {{25, 30, 20}, {25, 30, 15}, {72, 85, 55}, {74, 85, 55}, {76, 85, 55}}},
        {"cup",     "Cup",     {{15, 20, 15}, {35, 40, 25}, {35, 40, 25}, {35, 40, 25}, {35, 40, 25}}},
        {"fist",    "Fist",    {{35, 45, 35}, {90, 100, 70}, {90, 100, 70}, {90, 100, 70}, {90, 100, 70}}},
        {"point",   "Point",   {{35, 40, 30}, {0, 0, 0}, {90, 100, 70}, {90, 100, 70}, {90, 100, 70}}},
        {"relaxed", "Relaxed", {{5, 5, 5}, {15, 20, 12}, {15, 20, 12}, {15, 20, 12}, {15, 20, 12}}},
        {"open",    "Open",    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
    };
    return presets;
}

inline const GripPreset& gripPreset(const std::string& name){
    for(const GripPreset& preset : gripPresets()) if(name == preset.name) return preset;
    return gripPresets().front();
}

//Preset iz imena predmeta: pistolj po imenu, salica/case, inace drska (mac, palica, alat)
inline std::string gripForItemName(std::string name){
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    for(const char* word : {"gun", "pistol", "eagle", "revolver", "glock", "beretta", "colt", "rifle", "blaster"})
        if(name.find(word) != std::string::npos) return "pistol";
    for(const char* word : {"cup", "mug", "glass", "can", "bottle"})
        if(name.find(word) != std::string::npos) return "cup";
    return "grip";
}

//Lokalne poze prstiju u kadru s presetom (za PoseKey). Aditivno: svaki zglob se okrene oko svoje
//osi savijanja preko onoga sto pokret ima u tom kadru
inline JointPose handPoseAt(const Warp::Stage& stage, const HandFingers& hand, const GripPreset& preset, double frame){
    JointPose pose;
    if(!hand.valid()) return pose;
    for(size_t f = 0; f < 5; ++f){
        const std::vector<Warp::Id>& finger = hand.fingers[f];
        for(size_t j = 0; j < finger.size() && j < 3; ++j){
            const Warp::Id id = finger[j];
            Warp::Transform local = stage.localAt(id, frame);
            const float degrees = preset.curl[f][j];
            if(std::fabs(degrees) > 1e-4f){
                //Os u svijetu: smjer kosti x normala dlana; vrh ide prema dlanu
                const glm::vec3 from = handpose::at(stage, id, frame);
                const glm::vec3 to = j + 1 < finger.size() ? handpose::at(stage, finger[j + 1], frame)
                                                          : from + (from - handpose::at(stage, finger[j > 0 ? j - 1 : 0], frame));
                glm::vec3 axis = glm::cross(to - from, hand.palmNormal);
                if(glm::length(axis) > 1e-8f){
                    axis = glm::normalize(axis);
                    const glm::quat world = handpose::worldRotation(stage, id, frame);
                    const glm::vec3 localAxis = glm::normalize(glm::inverse(world) * axis);
                    local.rotation = glm::normalize(local.rotation * glm::angleAxis(glm::radians(degrees), localAxis));
                }
            }
            pose.emplace_back(id, local);
        }
    }
    return pose;
}

//PRSTI OKO PREDMETA: svaki zglob, od baze prema vrhu, savija se dok kapsula njegovog clanka (ili
//bilo kojeg clanka dalje prema vrhu, jos ravnog) ne dotakne collider - tada stane tik prije dodira.
//Preset daje najvise savijanje (x1.3, da se tanka drska moze obuhvatiti), a pistol ostavlja
//kaziprst uz okidac. collider je u sustavu predmeta; toolWorld je svijet predmeta u tom kadru
inline JointPose conformedHandPoseAt(const Warp::Stage& stage, const HandFingers& hand, const GripPreset& preset, double frame,
                                     const Engine::Physics::Collider& collider, const glm::mat4& toolWorld, int steps = 24){
    JointPose pose;
    if(!hand.valid()) return pose;
    const glm::mat4 toTool = glm::inverse(toolWorld);
    for(size_t f = 0; f < 5; ++f){
        const std::vector<Warp::Id>& finger = hand.fingers[f];
        const size_t count = std::min<size_t>(finger.size(), 3);
        if(count == 0) continue;
        //Lokalne transformacije i osi savijanja (u sustavu svakog zgloba) iz pokreta u ovom kadru
        std::vector<Warp::Transform> locals(count);
        std::vector<glm::vec3> axes(count, glm::vec3(0.0f));
        std::vector<float> lengths(count, 0.0f);
        const Warp::Entity* first = stage.get(finger[0]);
        const glm::mat4 parentWorld = first && first->parent != Warp::None ? stage.worldMatrix(first->parent, frame) : glm::mat4(1.0f);
        for(size_t j = 0; j < count; ++j){
            locals[j] = stage.localAt(finger[j], frame);
            const glm::vec3 from = handpose::at(stage, finger[j], frame);
            const glm::vec3 to = j + 1 < finger.size() ? handpose::at(stage, finger[j + 1], frame)
                                                      : from + (from - handpose::at(stage, finger[j > 0 ? j - 1 : 0], frame)) * 0.8f;
            lengths[j] = glm::length(to - from);
            const glm::vec3 axis = glm::cross(to - from, hand.palmNormal);
            if(glm::length(axis) > 1e-8f)
                axes[j] = glm::normalize(glm::inverse(handpose::worldRotation(stage, finger[j], frame)) * glm::normalize(axis));
        }
        std::vector<float> angles(count, 0.0f);
        //Tocke clanaka (u sustavu predmeta) za zadane kutove: zglob j do j+1, zadnji produzen
        auto segments = [&](const std::vector<float>& a){
            std::vector<glm::vec3> points;
            glm::mat4 world = parentWorld;
            for(size_t j = 0; j < count; ++j){
                Warp::Transform local = locals[j];
                if(glm::length(axes[j]) > 0.5f) local.rotation = glm::normalize(local.rotation * glm::angleAxis(glm::radians(a[j]), axes[j]));
                world = world * local.matrix();
                points.push_back(glm::vec3(toTool * world[3]));
            }
            //Vrh: iz zadnjeg zgloba duz njegove kosti (smjer zadnjeg clanka)
            const glm::vec3 last = points.back();
            const glm::vec3 previous = count > 1 ? points[count - 2] : glm::vec3(toTool * glm::vec4(glm::vec3(parentWorld[3]), 1.0f));
            const glm::vec3 direction = glm::length(last - previous) > 1e-6f ? glm::normalize(last - previous) : glm::vec3(0.0f);
            const float tipScale = glm::length(glm::vec3(toTool[0]));
            points.push_back(last + direction * lengths[count - 1] * tipScale);
            return points;
        };
        const float toolScale = glm::length(glm::vec3(toTool[0]));
        auto touches = [&](const std::vector<float>& a, size_t fromJoint){
            const std::vector<glm::vec3> points = segments(a);
            for(size_t j = fromJoint; j < count; ++j){
                //Debljina clanka ~22 % njegove duljine (prst odrasle osobe: 4.5 cm clanak, ~1 cm polumjer)
                const float radius = std::max(0.004f, 0.22f * lengths[j]) * toolScale;
                if(collider.capsuleHits(points[j], points[j + 1], radius)) return true;
            }
            return false;
        };
        for(size_t j = 0; j < count; ++j){
            const float maximum = std::min(115.0f, preset.curl[f][j] * 1.3f);
            if(maximum <= 0.0f) continue;
            //Vec u dodiru na nuli: ne savija se dalje (clanak je na drsci)
            if(touches(angles, j)) break;
            float low = 0.0f, high = maximum;
            std::vector<float> trial = angles;
            trial[j] = maximum;
            if(!touches(trial, j)){ angles[j] = maximum; continue; }
            for(int step = 0; step < steps; ++step){
                const float mid = 0.5f * (low + high);
                trial[j] = mid;
                if(touches(trial, j)) high = mid; else low = mid;
            }
            angles[j] = low;
        }
        for(size_t j = 0; j < count; ++j){
            Warp::Transform local = locals[j];
            if(glm::length(axes[j]) > 0.5f) local.rotation = glm::normalize(local.rotation * glm::angleAxis(glm::radians(angles[j]), axes[j]));
            pose.emplace_back(finger[j], local);
        }
    }
    return pose;
}

//SLOJEVI HVATA: za lik se obrisu svi slojevi "Hold: ..." aktivnog klipa i izgrade iznova iz hvatova
//u sceni cije su sake na tom liku. Zove se nakon svake promjene hvata (novi, rub, pusti, preset,
//brisanje), pa sloj uvijek odgovara traci na timelineu. Vraca broj slojeva hvata
//colliderFor: collider predmeta (u njegovom sustavu) ili nullptr - s njim se prsti omotaju oko
//predmeta (conformedHandPoseAt), bez njega preset
inline size_t syncHoldHandLayers(Warp::Stage& stage, Warp::Id rig, const std::vector<PoseLimb>& limbs,
                                 double closeFrames = 6.0,
                                 const std::function<const Engine::Physics::Collider*(Warp::Id item)>& colliderFor = {}){
    Warp::Entity* rigEntity = stage.get(rig);
    if(!rigEntity || !rigEntity->animator || rigEntity->animator->animations.empty()) return 0;
    const size_t clipIndex = std::min(rigEntity->animator->activeAnimation, rigEntity->animator->animations.size() - 1);
    auto isHoldLayer = [](const Warp::AnimationLayer& layer){ return layer.name.rfind("Hold: ", 0) == 0; };
    {
        Warp::AnimationClip& clip = stage.get(rig)->animator->animations[clipIndex];
        clip.layers.erase(std::remove_if(clip.layers.begin(), clip.layers.end(), isHoldLayer), clip.layers.end());
    }
    //Osnova + ostali slojevi, pa se poza prstiju racuna preko onoga sto ce se stvarno vidjeti
    bakeAnimationLayers(stage, rig, clipIndex, limbs);
    auto onRig = [&](Warp::Id bone){
        for(Warp::Id walk = bone; walk != Warp::None;){
            if(walk == rig) return true;
            const Warp::Entity* e = stage.get(walk);
            walk = e ? e->parent : Warp::None;
        }
        return false;
    };
    const double clipStart = stage.get(rig)->animator->animations[clipIndex].startFrame;
    const double clipEnd = stage.get(rig)->animator->animations[clipIndex].endFrame;
    std::vector<Warp::AnimationLayer> built;
    stage.walk([&](const Warp::Entity& item, int){
        for(const Warp::Hold& hold : item.holds){
            if(!stage.get(hold.hand) || !onRig(hold.hand)) continue;
            const double on = std::clamp(hold.onFrame, clipStart, clipEnd);
            const double off = std::clamp(hold.offFrame, clipStart, clipEnd);
            if(hold.offFrame < clipStart || hold.onFrame > clipEnd) continue;
            const HandFingers fingers = handFingersOf(stage, hold.hand, on);
            if(!fingers.valid()) continue;
            const GripPreset& preset = gripPreset(hold.grip);
            const Engine::Physics::Collider* collider = colliderFor ? colliderFor(item.id) : nullptr;
            auto poseAt = [&](double frame){
                return collider ? conformedHandPoseAt(stage, fingers, preset, frame, *collider, stage.worldMatrix(item.id, frame))
                                : handPoseAt(stage, fingers, preset, frame);
            };
            std::vector<PoseKey> keys{{on, poseAt(on)}};
            if(off > on) keys.push_back({off, poseAt(off)});
            PoseKeySettings settings;
            settings.inFrames = closeFrames;
            settings.outFrames = closeFrames;
            settings.holdFrames = 0.0;
            settings.blendBetween = true;
            built.push_back(layerFromPoseKeys(stage, "Hold: " + item.name + " (" + preset.label + ")", keys, settings));
        }
    });
    if(built.empty()){
        bakeAnimationLayers(stage, rig, clipIndex, limbs);
        return 0;
    }
    Warp::AnimationClip& clip = stage.get(rig)->animator->animations[clipIndex];
    for(Warp::AnimationLayer& layer : built) clip.layers.push_back(std::move(layer));
    bakeAnimationLayers(stage, rig, clipIndex, limbs);
    return built.size();
}

}
