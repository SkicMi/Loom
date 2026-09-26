#pragma once

#include <Engine/WeaverMotion.h>
#include <Warp/Stage.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <filesystem>

#include <Spool/Gltf.h>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/gtc/quaternion.hpp>

namespace Loom{

struct MotionBounds{
    glm::vec3 low{std::numeric_limits<float>::max()};
    glm::vec3 high{-std::numeric_limits<float>::max()};
    bool valid = false;

    void include(const glm::vec3& point){
        if(!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return;
        low = glm::min(low, point);
        high = glm::max(high, point);
        valid = true;
    }
    float height() const { return valid ? high.y - low.y : 0.0f; }
};

//Neutralni BVH/rest pose bounds. Root moze biti na kukovima ili na podu; y=0 nije pretpostavka.
inline MotionBounds motionRestBounds(const Engine::WeaverMotion::Clip& clip){
    MotionBounds bounds;
    std::vector<glm::vec3> at(clip.joints.size(), glm::vec3(0.0f));
    for(size_t i = 0; i < clip.joints.size(); ++i){
        const int parent = clip.joints[i].parent;
        at[i] = (parent >= 0 && size_t(parent) < i ? at[size_t(parent)] : glm::vec3(0.0f)) +
                clip.joints[i].offset;
        bounds.include(at[i]);
    }
    return bounds;
}

inline std::vector<glm::vec3> motionPoseJointPositions(const Engine::WeaverMotion::Clip& clip, size_t frameIndex){
    std::vector<glm::mat4> world(clip.joints.size(), glm::mat4(1.0f));
    std::vector<glm::vec3> positions(clip.joints.size(), glm::vec3(0.0f));
    if(frameIndex >= clip.frames.size()) return positions;
    const Engine::WeaverMotion::Pose& pose = clip.frames[frameIndex];
    for(size_t i = 0; i < clip.joints.size(); ++i){
        const int parent = clip.joints[i].parent;
        const glm::quat rotation = i < pose.rotations.size() ? glm::normalize(pose.rotations[i]) : glm::quat(1, 0, 0, 0);
        glm::mat4 local = glm::mat4_cast(rotation);
        const glm::vec3 translation = clip.joints[i].offset +
            (i < pose.translations.size() ? pose.translations[i] : glm::vec3(0.0f));
        local[3] = glm::vec4(translation, 1.0f);
        world[i] = parent >= 0 && size_t(parent) < i ? world[size_t(parent)] * local : local;
        positions[i] = glm::vec3(world[i][3]);
    }
    return positions;
}

inline MotionBounds motionPoseBounds(const Engine::WeaverMotion::Clip& clip, size_t frameIndex){
    MotionBounds bounds;
    for(const glm::vec3& position : motionPoseJointPositions(clip, frameIndex)) bounds.include(position);
    return bounds;
}

inline float motionRestHeight(const Engine::WeaverMotion::Clip& clip){
    return motionRestBounds(clip).height();
}

inline std::string motionJointKey(std::string_view name){
    std::string key;
    for(unsigned char character : name){
        if(std::isalnum(character)) key.push_back(char(std::tolower(character)));
    }
    for(const std::string_view prefix : {"mixamorig", "armature", "skeleton", "jbipc", "bip001"}){
        if(key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0){
            key.erase(0, prefix.size());
            break;
        }
    }
    return key;
}

struct MotionRigJointRest{
    Warp::Id id = Warp::None;
    Warp::Id parentJoint = Warp::None;
    std::string name;
    glm::mat4 matrix{1.0f};       //Rest matrix relativno odabranom character rootu
    glm::mat4 directParentMatrix{1.0f};
    glm::mat4 localMatrix{1.0f};
};

struct MotionRigRestPose{
    std::vector<MotionRigJointRest> joints;
    MotionBounds bounds;
};

//Koristi samo mirne TRS transformacije, ne playback kljuceve. Rootova vlastita transformacija
//je vanjski koordinatni sustav novog childa.
inline MotionRigRestPose motionRigRestPose(const Warp::Stage& stage, Warp::Id root){
    MotionRigRestPose result;
    if(!stage.contains(root)) return result;
    struct Pending{ Warp::Id id; Warp::Id parentJoint; glm::mat4 parentMatrix; };
    std::vector<Pending> pending;
    const Warp::Entity* rootEntity = stage.get(root);
    if(rootEntity->joint){
        result.joints.push_back({root, Warp::None, rootEntity->name, glm::mat4(1.0f)});
        result.bounds.include(glm::vec3(0.0f));
    }
    for(auto child = rootEntity->children.rbegin(); child != rootEntity->children.rend(); ++child){
        pending.push_back({*child, rootEntity->joint ? root : Warp::None, glm::mat4(1.0f)});
    }
    size_t visited = 0;
    while(!pending.empty() && visited++ < stage.size()){
        const Pending item = pending.back();
        pending.pop_back();
        const Warp::Entity* entity = stage.get(item.id);
        if(!entity) continue;
        const glm::mat4 matrix = item.parentMatrix * entity->local.matrix();
        Warp::Id parentJoint = item.parentJoint;
        if(entity->joint){
            MotionRigJointRest joint;
            joint.id = entity->id;
            joint.parentJoint = item.parentJoint;
            joint.name = entity->name;
            joint.matrix = matrix;
            joint.directParentMatrix = item.parentMatrix;
            joint.localMatrix = entity->local.matrix();
            result.joints.push_back(std::move(joint));
            result.bounds.include(glm::vec3(matrix[3]));
            parentJoint = entity->id;
        }
        for(auto child = entity->children.rbegin(); child != entity->children.rend(); ++child){
            pending.push_back({*child, parentJoint, matrix});
        }
    }
    return result;
}

inline glm::mat4 motionGltfNodeMatrix(const Spool::GltfNode& node){
    Warp::Transform transform;
    transform.translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
    transform.rotation = glm::normalize(glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
    transform.scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
    return transform.matrix();
}

inline MotionBounds motionGltfBounds(const Spool::GltfScene& scene){
    MotionBounds bounds;
    struct Pending{ int node; glm::mat4 parent; };
    std::vector<Pending> pending;
    for(int root : scene.roots) pending.push_back({root, glm::mat4(1.0f)});
    size_t visited = 0;
    while(!pending.empty() && visited++ < 100000){
        const Pending item = pending.back();
        pending.pop_back();
        if(item.node < 0 || size_t(item.node) >= scene.nodes.size()) continue;
        const Spool::GltfNode& node = scene.nodes[size_t(item.node)];
        const glm::mat4 world = item.parent * motionGltfNodeMatrix(node);
        if(node.mesh >= 0 && size_t(node.mesh) < scene.meshes.size()){
            for(const Spool::GltfPrimitive& primitive : scene.meshes[size_t(node.mesh)].primitives){
                for(size_t vertex = 0; vertex < primitive.vertexCount(); ++vertex){
                    const glm::vec3 position = glm::vec3(world * glm::vec4(
                        primitive.positions[vertex * 3], primitive.positions[vertex * 3 + 1],
                        primitive.positions[vertex * 3 + 2], 1.0f));
                    bounds.include(position);
                }
            }
        }
        for(int child : node.children) pending.push_back({child, world});
    }
    return bounds;
}

//Use actual static mesh bounds when the selected rig came from a readable glTF. This is more
//accurate than bone-head bounds: UniRig's terminal toe joint stops above the mesh's sole.
inline bool motionRigMeshBounds(const Warp::Stage& stage, Warp::Id root, bool& hasModel,
                                MotionBounds& meshBounds, std::string& problem){
    hasModel = false;
    std::unordered_set<std::string> paths;
    std::vector<Warp::Id> pending{root};
    size_t visited = 0;
    while(!pending.empty() && visited++ < stage.size()){
        const Warp::Entity* entity = stage.get(pending.back());
        pending.pop_back();
        if(!entity) continue;
        if(entity->model && !entity->model->path.empty()){
            hasModel = true;
            paths.insert(entity->model->path);
        }
        pending.insert(pending.end(), entity->children.begin(), entity->children.end());
    }
    if(!hasModel) return true;
    if(paths.size() != 1){
        problem = "character subtree contains multiple model files; floor bounds are ambiguous";
        return false;
    }

    Spool::GltfScene scene;
    std::string error;
    Spool::GltfLoadConfig config;
    config.decodeImages = false;
    if(!Spool::loadGltf(*paths.begin(), scene, error, config)){
        problem = error;
        return false;
    }
    const MotionBounds bounds = motionGltfBounds(scene);
    if(!bounds.valid){
        problem = "selected glTF has no finite mesh positions";
        return false;
    }
    meshBounds = bounds;
    return true;
}

inline std::array<int, 52> motionUniRigExpectedParents(){
    return {{-1,0,1,2,3,4,3,6,7,8,9,10,11,9,13,14,9,16,17,9,19,20,9,22,23,
             3,25,26,27,28,29,30,28,32,33,28,35,36,28,38,39,28,41,42,0,44,45,46,0,48,49,50}};
}

//Generic bone_N names only acquire meaning when the entire verified UniRig-52 parent tree matches.
inline bool motionFindVerifiedUniRig52(const MotionRigRestPose& pose, std::array<Warp::Id, 52>& ids){
    ids.fill(Warp::None);
    if(pose.joints.size() != ids.size()) return false;
    for(const MotionRigJointRest& joint : pose.joints){
        const std::string key = motionJointKey(joint.name);
        if(key.size() < 5 || key.compare(0, 4, "bone") != 0) return false;
        int index = 0;
        for(size_t i = 4; i < key.size(); ++i){
            if(key[i] < '0' || key[i] > '9') return false;
            const int digit = key[i] - '0';
            if(index > (51 - digit) / 10) return false;
            index = index * 10 + digit;
        }
        if(index < 0 || index >= int(ids.size()) || ids[size_t(index)] != Warp::None) return false;
        ids[size_t(index)] = joint.id;
    }
    const auto expected = motionUniRigExpectedParents();
    for(size_t i = 0; i < ids.size(); ++i){
        if(ids[i] == Warp::None) return false;
        const auto joint = std::find_if(pose.joints.begin(), pose.joints.end(),
            [&](const MotionRigJointRest& value){ return value.id == ids[i]; });
        if(joint == pose.joints.end()) return false;
        const Warp::Id expectedParent = expected[i] < 0 ? Warp::None : ids[size_t(expected[i])];
        if(joint->parentJoint != expectedParent) return false;
    }
    const auto positionOf = [&](Warp::Id id) -> glm::vec3{
        const auto found = std::find_if(pose.joints.begin(), pose.joints.end(),
            [&](const MotionRigJointRest& value){ return value.id == id; });
        return found == pose.joints.end() ? glm::vec3(0.0f) : glm::vec3(found->matrix[3]);
    };
    return positionOf(ids[6]).x > positionOf(ids[25]).x &&
           positionOf(ids[44]).x > positionOf(ids[48]).x;
}

inline int motionUniRigSlot(std::string_view sourceKey){
    static const std::unordered_map<std::string, int> slots{
        {"hips",0},{"pelvis",0},{"spine1",1},{"spine2",2},{"chest",3},{"neck1",4},{"head",5},
        {"leftshoulder",6},{"leftarm",7},{"leftforearm",8},{"lefthand",9},
        {"lefthandthumb1",10},{"lefthandthumb2",11},{"lefthandthumb3",12},
        {"lefthandindex1",13},{"lefthandindex2",14},{"lefthandindex3",15},
        {"lefthandmiddle1",16},{"lefthandmiddle2",17},{"lefthandmiddle3",18},
        {"lefthandring1",19},{"lefthandring2",20},{"lefthandring3",21},
        {"lefthandpinky1",22},{"lefthandpinky2",23},{"lefthandpinky3",24},
        {"rightshoulder",25},{"rightarm",26},{"rightforearm",27},{"righthand",28},
        {"righthandthumb1",29},{"righthandthumb2",30},{"righthandthumb3",31},
        {"righthandindex1",32},{"righthandindex2",33},{"righthandindex3",34},
        {"righthandmiddle1",35},{"righthandmiddle2",36},{"righthandmiddle3",37},
        {"righthandring1",38},{"righthandring2",39},{"righthandring3",40},
        {"righthandpinky1",41},{"righthandpinky2",42},{"righthandpinky3",43},
        {"leftleg",44},{"leftshin",45},{"leftfoot",46},{"lefttoebase",47},
        {"rightleg",48},{"rightshin",49},{"rightfoot",50},{"righttoebase",51}
    };
    const auto found = slots.find(std::string(sourceKey));
    return found == slots.end() ? -1 : found->second;
}

inline std::vector<std::string> motionTargetAliases(std::string_view sourceKey){
    if(sourceKey == "hips" || sourceKey == "pelvis") return {"hips", "hip", "pelvis", "pelvisbone"};
    if(sourceKey == "spine1") return {"spine1", "spine01", "spine", "lowerback"};
    if(sourceKey == "spine2") return {"spine2", "spine02", "midspine"};
    if(sourceKey == "chest") return {"chest", "upperchest", "spine05", "spine03", "spine04"};
    if(sourceKey == "neck1") return {"neck1", "neck01", "neck"};
    if(sourceKey == "neck2") return {"neck2", "neck02"};
    if(sourceKey == "head") return {"head", "headtop"};
    if(sourceKey == "leftshoulder") return {"leftshoulder", "leftclavicle", "claviclel"};
    if(sourceKey == "rightshoulder") return {"rightshoulder", "rightclavicle", "clavicler"};
    if(sourceKey == "leftarm") return {"leftarm", "leftupperarm", "upperarml"};
    if(sourceKey == "rightarm") return {"rightarm", "rightupperarm", "upperarmr"};
    if(sourceKey == "leftforearm") return {"leftforearm", "leftlowerarm", "lowerarml", "forearml"};
    if(sourceKey == "rightforearm") return {"rightforearm", "rightlowerarm", "lowerarmr", "forearmr"};
    if(sourceKey == "lefthand") return {"lefthand", "handl"};
    if(sourceKey == "righthand") return {"righthand", "handr"};
    if(sourceKey == "leftleg") return {"leftupleg", "leftleg", "leftthigh", "thighl"};
    if(sourceKey == "rightleg") return {"rightupleg", "rightleg", "rightthigh", "thighr"};
    if(sourceKey == "leftshin") return {"leftshin", "leftcalf", "leftlowerleg", "calfl", "leftleg"};
    if(sourceKey == "rightshin") return {"rightshin", "rightcalf", "rightlowerleg", "calfr", "rightleg"};
    if(sourceKey == "leftfoot") return {"leftfoot", "footl"};
    if(sourceKey == "rightfoot") return {"rightfoot", "footr"};
    if(sourceKey == "lefttoebase") return {"lefttoebase", "lefttoe", "balll"};
    if(sourceKey == "righttoebase") return {"righttoebase", "righttoe", "ballr"};
    // UE4 Mannequin and UE5 Manny/Quinn fingers use thumb_01_l, index_01_l, etc.
    const std::string source(sourceKey);
    for(const std::string_view side : {"left", "right"}){
        if(source.compare(0, side.size(), side) != 0) continue;
        const std::string_view rest(source.data() + side.size(), source.size() - side.size());
        if(rest.compare(0, 4, "hand") != 0) break;
        const std::string_view finger = rest.substr(4);
        for(const std::string_view name : {"thumb", "index", "middle", "ring", "pinky"}){
            if(finger.compare(0, name.size(), name) != 0) continue;
            const std::string_view digit = finger.substr(name.size());
            if(digit.size() != 1 || digit[0] < '0' || digit[0] > '3') break;
            const char suffix = side == "left" ? 'l' : 'r';
            //0: metakarpal (motionSourceJointKeys) - Manny index_metacarpal_l, rig bez njega ga nema
            if(digit[0] == '0') return {std::string(name) + "metacarpal" + suffix};
            const std::string number(1, digit[0]);
            return {source, std::string(name) + "0" + number + suffix,
                    std::string(name) + number + suffix};
        }
    }
    return {source};
}

//KLJUCEVI IZVORNIH ZGLOBOVA za mapiranje. Kimodo SOMA ima cetiri kosti po prstu prije vrha
//(LeftHandIndex1..4 + LeftHandIndexEnd): 1 je METAKARPAL (od zapesca do zgloba sake), 2..4 su
//clanci. Mixamo ima 1..3 clanke i list 4. Bez ovoga je SOMA metakarpal isao na Mannyjev index_01, a
//svaki clanak jedan zglob dalje (savijanje korijena prsta na srednjem clanku). Lanac s cetvrtom
//kosti koja ima dijete se prebroji: 1 -> 0 (metakarpal), 2..4 -> 1..3. Palac ima tri kosti u oba
inline std::vector<std::string> motionSourceJointKeys(const Engine::WeaverMotion::Clip& clip){
    std::vector<std::string> keys;
    keys.reserve(clip.joints.size());
    for(const Engine::WeaverMotion::Joint& joint : clip.joints) keys.push_back(motionJointKey(joint.name));
    std::vector<bool> hasChild(clip.joints.size(), false);
    for(const Engine::WeaverMotion::Joint& joint : clip.joints)
        if(joint.parent >= 0 && size_t(joint.parent) < hasChild.size()) hasChild[size_t(joint.parent)] = true;
    for(const char* side : {"lefthand", "righthand"})
        for(const char* finger : {"index", "middle", "ring", "pinky"}){
            const std::string stem = std::string(side) + finger;
            const auto fourth = std::find(keys.begin(), keys.end(), stem + "4");
            if(fourth == keys.end() || !hasChild[size_t(fourth - keys.begin())]) continue;
            for(std::string& key : keys){
                if(key.size() != stem.size() + 1 || key.compare(0, stem.size(), stem) != 0) continue;
                const char digit = key.back();
                if(digit >= '1' && digit <= '4') key.back() = char(digit - 1);
            }
        }
    return keys;
}

inline bool motionIsUnrealMannequin(const MotionRigRestPose& pose){
    std::unordered_set<std::string> keys;
    for(const MotionRigJointRest& joint : pose.joints) keys.insert(motionJointKey(joint.name));
    return keys.count("pelvis") && keys.count("spine01") &&
           keys.count("claviclel") && keys.count("clavicler") &&
           keys.count("upperarml") && keys.count("upperarmr") &&
           keys.count("thighl") && keys.count("thighr");
}

struct MotionRigMapping{
    std::vector<Warp::Id> targetBySource;
    int sourceHips = -1;
    Warp::Id targetHips = Warp::None;
    size_t matched = 0;
    size_t footContactFrames = 0;
    std::string profile;
    std::string problem;
};

inline MotionRigMapping mapMotionBones(const Engine::WeaverMotion::Clip& clip,
                                       const MotionRigRestPose& restPose){
    MotionRigMapping mapping;
    mapping.targetBySource.assign(clip.joints.size(), Warp::None);
    std::array<Warp::Id, 52> uniRig{};
    const bool verifiedUniRig = motionFindVerifiedUniRig52(restPose, uniRig);
    mapping.profile = verifiedUniRig ? "verified UniRig 52" :
                      motionIsUnrealMannequin(restPose) ? "Unreal Mannequin" : "named bones";

    std::unordered_set<Warp::Id> usedTargets;
    const std::vector<std::string> sourceKeys = motionSourceJointKeys(clip);
    for(size_t source = 0; source < clip.joints.size(); ++source){
        const std::string& key = sourceKeys[source];
        if(key == "hips" || key == "hip" || key == "pelvis") mapping.sourceHips = int(source);
        Warp::Id target = Warp::None;
        if(verifiedUniRig){
            const int slot = motionUniRigSlot(key);
            if(slot >= 0) target = uniRig[size_t(slot)];
        }else{
            for(const std::string& alias : motionTargetAliases(key)){
                Warp::Id candidate = Warp::None;
                for(const MotionRigJointRest& joint : restPose.joints){
                    if(motionJointKey(joint.name) != alias) continue;
                    if(candidate != Warp::None){ candidate = Warp::None; break; }
                    candidate = joint.id;
                }
                if(candidate != Warp::None){ target = candidate; break; }
            }
        }
        if(target != Warp::None && usedTargets.insert(target).second){
            mapping.targetBySource[source] = target;
            ++mapping.matched;
        }
    }
    if(mapping.sourceHips >= 0) mapping.targetHips = mapping.targetBySource[size_t(mapping.sourceHips)];
    if(mapping.sourceHips < 0 || mapping.targetHips == Warp::None){
        mapping.problem = "no unambiguous Hips/Pelvis bone match; rig was not modified";
    }
    return mapping;
}

struct MotionRigFit{
    float scale = 1.0f;
    glm::vec3 translation{0.0f};
    size_t mappedJoints = 0;
    std::string profile;
    std::string floorSource;
    float floorY = 0.0f;             //Mesh floor relative to the character root
};

inline bool fitMotionToRigRestPose(const Engine::WeaverMotion::Clip& clip,
                                   const Warp::Stage& stage, Warp::Id rigRoot,
                                   MotionRigFit& fit, std::string& problem);

inline bool ensureRigAnimator(Warp::Stage& stage, Warp::Id rigRoot, const std::string& migrationName = "Imported animation"){
    Warp::Entity* root = stage.get(rigRoot);
    if(!root) return false;
    if(!root->animator) root->animator = Warp::Animator{};
    Warp::Animator& animator = *root->animator;
    const MotionRigRestPose pose = motionRigRestPose(stage, rigRoot);
    Warp::AnimationClip imported;
    imported.name = migrationName;
    double first = std::numeric_limits<double>::infinity();
    double last = -std::numeric_limits<double>::infinity();
    for(const MotionRigJointRest& joint : pose.joints){
        Warp::Entity* entity = stage.get(joint.id);
        if(!entity || (!entity->animated() && entity->rotationKeys.empty() && entity->translationKeys.empty() && entity->scaleKeys.empty())) continue;
        if(entity->translationKeys.empty() && entity->rotationKeys.empty() && entity->scaleKeys.empty()) continue;
        Warp::AnimatorTrack track;
        track.target = entity->id;
        track.targetPath = stage.path(entity->id);
        track.translationKeys = entity->translationKeys;
        track.rotationKeys = entity->rotationKeys;
        track.scaleKeys = entity->scaleKeys;
        auto includeTimes = [&](const std::vector<double>& times){
            if(!times.empty()){ first = std::min(first, times.front()); last = std::max(last, times.back()); }
        };
        includeTimes(track.translationKeys.times);
        includeTimes(track.rotationKeys.times);
        includeTimes(track.scaleKeys.times);
        imported.tracks.push_back(std::move(track));
        entity->translationKeys = {};
        entity->rotationKeys = {};
        entity->scaleKeys = {};
    }
    if(!imported.tracks.empty()){
        imported.startFrame = first;
        imported.endFrame = last;
        animator.animations.push_back(std::move(imported));
        animator.activeAnimation = animator.animations.size() - 1;
    }
    return true;
}

inline glm::quat motionRotationOf(const glm::mat4& matrix){
    glm::mat3 basis(matrix);
    for(int column = 0; column < 3; ++column){
        const float length = glm::length(basis[column]);
        if(!std::isfinite(length) || length <= 1e-10f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        basis[column] /= length;
    }
    return glm::normalize(glm::quat_cast(basis));
}


//Rotate one joint about a world-space axis while retaining its rest position. This makes the
//natural-pose preset independent of UniRig's per-bone roll and local axis conventions.
inline bool motionApplyRestWorldRotation(Warp::Stage& stage, Warp::Id rigRoot, Warp::Id id,
                                         glm::vec3 axis, float degrees, bool rebaseAnimations = true){
    const MotionRigRestPose pose = motionRigRestPose(stage, rigRoot);
    const auto found = std::find_if(pose.joints.begin(), pose.joints.end(),
        [&](const MotionRigJointRest& joint){ return joint.id == id; });
    Warp::Entity* entity = stage.get(id);
    if(found == pose.joints.end() || !entity || !entity->joint) return false;
    const glm::quat parentWorld = motionRotationOf(found->directParentMatrix);
    const glm::quat worldDelta = glm::angleAxis(glm::radians(degrees), glm::normalize(axis));
    const glm::quat localDelta = glm::normalize(glm::inverse(parentWorld) * worldDelta * parentWorld);
    const glm::quat newRest = glm::normalize(localDelta * entity->local.rotation);
    entity->local.rotation = newRest;

    if(rebaseAnimations){
        auto rebase = [&](Warp::Track<glm::quat>& keys, double sampleFrame){
            if(keys.empty()) return;
            const glm::quat oldAtStart = glm::normalize(keys.at(sampleFrame));
            const glm::quat correction = glm::normalize(newRest * glm::inverse(oldAtStart));
            for(glm::quat& value : keys.values) value = glm::normalize(correction * value);
        };
        rebase(entity->rotationKeys, entity->rotationKeys.empty() ? 0.0 : entity->rotationKeys.times.front());
        if(Warp::Entity* owner = stage.get(rigRoot); owner && owner->animator){
            for(Warp::AnimationClip& clip : owner->animator->animations){
                for(Warp::AnimatorTrack& track : clip.tracks){
                    if(track.target == id) rebase(track.rotationKeys, clip.startFrame);
                }
            }
        }
    }
    return true;
}

//A comfortable idle stance for Loom's verified 52-joint UniRig: arms hang slightly below the
//shoulder line, elbows and wrists soften the silhouette, and all finger chains curl naturally.
//Existing Animator clips are rebased so their source motion remains relative to the new rest pose.
inline bool applyUniRigRelaxedRestPose(Warp::Stage& stage, Warp::Id rigRoot, size_t* changed = nullptr){
    const MotionRigRestPose rest = motionRigRestPose(stage, rigRoot);
    std::array<Warp::Id, 52> ids;
    if(!motionFindVerifiedUniRig52(rest, ids)) return false;
    if(!ensureRigAnimator(stage, rigRoot)) return false;
    Warp::Entity* rig = stage.get(rigRoot);
    if(rig && rig->animator && rig->animator->relaxedUniRigPose){ if(changed) *changed = 0; return true; }
    size_t count = 0;
    auto turn = [&](int slot, glm::vec3 axis, float degrees){
        if(slot < 0 || slot >= int(ids.size())) return;
        count += motionApplyRestWorldRotation(stage, rigRoot, ids[size_t(slot)], axis, degrees) ? 1u : 0u;
    };
    for(int slot : {6,25}){
        const MotionRigRestPose current = motionRigRestPose(stage, rigRoot);
        const auto joint = std::find_if(current.joints.begin(), current.joints.end(),
            [&](const MotionRigJointRest& item){ return item.id == ids[size_t(slot)]; });
        const float side = joint == current.joints.end() ? (slot == 6 ? 1.0f : -1.0f) :
                           (glm::vec3(joint->matrix[3]).x >= 0.0f ? 1.0f : -1.0f);
        turn(slot, glm::vec3(0,0,1), -side * 60.0f);
    }
    for(int slot : {8,27}){
        const MotionRigRestPose current = motionRigRestPose(stage, rigRoot);
        const auto joint = std::find_if(current.joints.begin(), current.joints.end(),
            [&](const MotionRigJointRest& item){ return item.id == ids[size_t(slot)]; });
        const float side = joint == current.joints.end() ? (slot == 8 ? 1.0f : -1.0f) :
                           (glm::vec3(joint->matrix[3]).x >= 0.0f ? 1.0f : -1.0f);
        turn(slot, glm::vec3(0,0,1), -side * 12.0f);
    }
    for(int slot : {9,28}){
        const MotionRigRestPose current = motionRigRestPose(stage, rigRoot);
        const auto joint = std::find_if(current.joints.begin(), current.joints.end(),
            [&](const MotionRigJointRest& item){ return item.id == ids[size_t(slot)]; });
        const float side = joint == current.joints.end() ? (slot == 9 ? 1.0f : -1.0f) :
                           (glm::vec3(joint->matrix[3]).x >= 0.0f ? 1.0f : -1.0f);
        turn(slot, glm::vec3(0,0,1), -side * 4.0f);
    }
    for(const std::array<int,3>& chain : {std::array<int,3>{10,11,12}, {29,30,31}}){
        for(size_t i = 0; i < chain.size(); ++i){
            const int slot = chain[i];
            const MotionRigRestPose current = motionRigRestPose(stage, rigRoot);
            const auto joint = std::find_if(current.joints.begin(), current.joints.end(),
                [&](const MotionRigJointRest& item){ return item.id == ids[size_t(slot)]; });
            const float side = joint == current.joints.end() ? (slot < 25 ? 1.0f : -1.0f) :
                               (glm::vec3(joint->matrix[3]).x >= 0.0f ? 1.0f : -1.0f);
            const float curl[] = {16.0f, 20.0f, 14.0f};
            turn(slot, glm::vec3(0,0,1), -side * curl[i]);
        }
    }
    for(const std::array<int,3>& chain : {std::array<int,3>{13,14,15}, {16,17,18}, {19,20,21}, {22,23,24},
                                          {32,33,34}, {35,36,37}, {38,39,40}, {41,42,43}}){
        for(size_t i = 0; i < chain.size(); ++i){
            const int slot = chain[i];
            const MotionRigRestPose current = motionRigRestPose(stage, rigRoot);
            const auto joint = std::find_if(current.joints.begin(), current.joints.end(),
                [&](const MotionRigJointRest& item){ return item.id == ids[size_t(slot)]; });
            const float side = joint == current.joints.end() ? (slot < 25 ? 1.0f : -1.0f) :
                               (glm::vec3(joint->matrix[3]).x >= 0.0f ? 1.0f : -1.0f);
            const float curl[] = {18.0f, 24.0f, 16.0f};
            turn(slot, glm::vec3(0,0,1), -side * curl[i]);
        }
    }
    if(rig && rig->animator) rig->animator->relaxedUniRigPose = count > 0;
    if(changed) *changed = count;
    return count > 0;
}

//Retarget against the imported bind pose, not the optional relaxed display pose. The relaxed
//preset is a reversible set of world-space arm/finger offsets, so undo it temporarily, snapshot
//the bind transforms, then restore the visible rig without touching any existing animation keys.
inline MotionRigRestPose motionRetargetRestPose(Warp::Stage& stage, Warp::Id rigRoot){
    MotionRigRestPose visibleRest = motionRigRestPose(stage, rigRoot);
    Warp::Entity* rig = stage.get(rigRoot);
    if(!rig || !rig->animator || !rig->animator->relaxedUniRigPose) return visibleRest;
    std::array<Warp::Id, 52> ids;
    if(!motionFindVerifiedUniRig52(visibleRest, ids)) return visibleRest;

    std::vector<std::pair<Warp::Id, glm::quat>> savedRotations;
    savedRotations.reserve(visibleRest.joints.size());
    for(const MotionRigJointRest& joint : visibleRest.joints){
        if(const Warp::Entity* entity = stage.get(joint.id))
            savedRotations.emplace_back(joint.id, entity->local.rotation);
    }

    auto undoTurn = [&](int slot, float degrees){
        if(slot < 0 || slot >= int(ids.size())) return;
        const MotionRigRestPose current = motionRigRestPose(stage, rigRoot);
        const auto found = std::find_if(current.joints.begin(), current.joints.end(),
            [&](const MotionRigJointRest& joint){ return joint.id == ids[size_t(slot)]; });
        const float side = found == current.joints.end() ? (slot < 25 ? 1.0f : -1.0f) :
                           (glm::vec3(found->matrix[3]).x >= 0.0f ? 1.0f : -1.0f);
        motionApplyRestWorldRotation(stage, rigRoot, ids[size_t(slot)], glm::vec3(0,0,1), side * degrees, false);
    };

    //Reverse the preset's exact application order so each inverse sees its original parent frame.
    const std::array<std::array<int,3>,8> fingerChains{{
        {{13,14,15}}, {{16,17,18}}, {{19,20,21}}, {{22,23,24}},
        {{32,33,34}}, {{35,36,37}}, {{38,39,40}}, {{41,42,43}}
    }};
    const float fingerCurl[] = {18.0f, 24.0f, 16.0f};
    for(auto chain = fingerChains.rbegin(); chain != fingerChains.rend(); ++chain)
        for(int i = 2; i >= 0; --i) undoTurn((*chain)[size_t(i)], fingerCurl[i]);

    const std::array<std::array<int,3>,2> thumbChains{{{{10,11,12}}, {{29,30,31}}}};
    const float thumbCurl[] = {16.0f, 20.0f, 14.0f};
    for(auto chain = thumbChains.rbegin(); chain != thumbChains.rend(); ++chain)
        for(int i = 2; i >= 0; --i) undoTurn((*chain)[size_t(i)], thumbCurl[i]);
    for(int slot : {28,9}) undoTurn(slot, 4.0f);
    for(int slot : {27,8}) undoTurn(slot, 12.0f);
    for(int slot : {25,6}) undoTurn(slot, 60.0f);

    MotionRigRestPose bindRest = motionRigRestPose(stage, rigRoot);
    for(const auto& [id, rotation] : savedRotations)
        if(Warp::Entity* entity = stage.get(id)) entity->local.rotation = rotation;
    return bindRest.bounds.valid ? bindRest : visibleRest;
}

inline std::vector<glm::quat> motionWorldRotations(const Engine::WeaverMotion::Clip& clip, size_t frameIndex){
    std::vector<glm::quat> world(clip.joints.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    if(frameIndex >= clip.frames.size()) return world;
    const Engine::WeaverMotion::Pose& pose = clip.frames[frameIndex];
    for(size_t i = 0; i < clip.joints.size(); ++i){
        const glm::quat local = i < pose.rotations.size() ? glm::normalize(pose.rotations[i]) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const int parent = clip.joints[i].parent;
        world[i] = glm::normalize(parent >= 0 && size_t(parent) < i ? world[size_t(parent)] * local : local);
    }
    return world;
}

struct MotionActionInterval{
    std::string prompt;
    size_t first = 0; //zero-based source BVH frame
    size_t last = 0;
};

//Kimodo's SOMA BVH can keep finger channels constant even when text requests a hand sign.
//These authored details are applied only to explicit action intervals on a verified UniRig.
inline size_t applyUniRigActionDetails(Warp::Stage& stage, Warp::Id rigRoot,
                                      const Engine::WeaverMotion::Clip& source,
                                      const std::vector<MotionActionInterval>& actions,
                                      double firstFrame, double frameStep){
    if(frameStep <= 0.0 || source.frames.empty()) return 0;
    const MotionRigRestPose rest = motionRigRestPose(stage, rigRoot);
    std::array<Warp::Id, 52> ids;
    if(!motionFindVerifiedUniRig52(rest, ids)) return 0;
    Warp::Entity* root = stage.get(rigRoot);
    if(!root || !root->animator || root->animator->activeAnimation >= root->animator->animations.size()) return 0;
    Warp::AnimationClip& animation = root->animator->animations[root->animator->activeAnimation];

    auto trackFor = [&](int slot) -> Warp::AnimatorTrack*{
        for(Warp::AnimatorTrack& track : animation.tracks)
            if(track.target == ids[size_t(slot)]) return &track;
        return nullptr;
    };
    auto localAxis = [&](int slot, glm::vec3 worldAxis) -> glm::vec3{
        const auto found = std::find_if(rest.joints.begin(), rest.joints.end(),
            [&](const MotionRigJointRest& joint){ return joint.id == ids[size_t(slot)]; });
        if(found == rest.joints.end()) return worldAxis;
        return glm::normalize(glm::inverse(motionRotationOf(found->directParentMatrix)) * worldAxis);
    };
    auto progress = [&](double sceneTime, const MotionActionInterval& action) -> float{
        const double sourceFrame = (sceneTime - firstFrame) / frameStep;
        return std::clamp(float((sourceFrame - double(action.first)) /
                                double(std::max<size_t>(1, action.last - action.first))), 0.0f, 1.0f);
    };
    auto envelope = [](float u) -> float{
        const float edge = std::clamp(std::min(u, 1.0f - u) / 0.12f, 0.0f, 1.0f);
        return edge * edge * (3.0f - 2.0f * edge);
    };
    auto rollProgress = [](float u) -> float{
        const float at = std::clamp((u - 0.08f) / 0.84f, 0.0f, 1.0f);
        return at * at * (3.0f - 2.0f * at);
    };
    size_t applied = 0;
    for(const MotionActionInterval& action : actions){
        if(action.first >= source.frames.size() || action.last <= action.first) continue;
        std::string prompt = action.prompt;
        std::transform(prompt.begin(), prompt.end(), prompt.begin(),
                       [](unsigned char c){ return char(std::tolower(c)); });
        const bool peace = prompt.find("peace sign") != std::string::npos ||
                           prompt.find("victory sign") != std::string::npos ||
                           prompt.find("v sign") != std::string::npos;
        const bool wave = prompt.find("wave") != std::string::npos ||
                          prompt.find("waving") != std::string::npos;
        const bool forwardRoll = prompt.find("roll") != std::string::npos &&
                                 prompt.find("forward") != std::string::npos;
        if(!peace && !wave && !forwardRoll) continue;

        auto rotateSlot = [&](int slot, glm::vec3 worldAxis, const auto& degreesAt){
            Warp::AnimatorTrack* track = trackFor(slot);
            if(!track) return;
            const glm::vec3 axis = localAxis(slot, worldAxis);
            for(size_t i = 0; i < track->rotationKeys.values.size(); ++i){
                const double time = track->rotationKeys.times[i];
                const double sourceIndex = (time - firstFrame) / frameStep;
                if(sourceIndex < double(action.first) - 0.01 || sourceIndex > double(action.last) + 0.01) continue;
                const float angle = degreesAt(progress(time, action));
                if(std::fabs(angle) < 1e-5f) continue;
                track->rotationKeys.values[i] = glm::normalize(
                    glm::angleAxis(glm::radians(angle), axis) * track->rotationKeys.values[i]);
            }
        };

        if(peace || wave){
            // Kimodo often leaves upper-body channels static for a requested gesture. Raise one
            // arm from the relaxed rest pose and bend the elbow so the hand sits near the head.
            const bool left = prompt.find("left hand") != std::string::npos ||
                              prompt.find("left arm") != std::string::npos;
            const float side = left ? 1.0f : -1.0f;
            const int shoulder = left ? 6 : 25;
            const int upperArm = left ? 7 : 26;
            const int forearm = left ? 8 : 27;
            const int hand = left ? 9 : 28;
            rotateSlot(shoulder, glm::vec3(0,0,1),
                       [&](float u){ return side * (peace ? 70.0f : 108.0f) * envelope(u); });
            rotateSlot(upperArm, glm::vec3(0,0,1),
                       [&](float u){ return side * 12.0f * envelope(u); });
            rotateSlot(forearm, glm::vec3(0,0,1),
                       [&](float u){ return side * (wave ? 78.0f : 86.0f) * envelope(u); });
            if(wave){
                // Wrist motion stays visible even when the source finger channels are constant.
                rotateSlot(hand, glm::vec3(0,1,0), [&](float u){
                    const float phase = 2.0f * 3.14159265f * 3.0f * std::clamp(u, 0.0f, 1.0f);
                    return 24.0f * std::sin(phase) * envelope(u);
                });
                ++applied;
            }
        }

        if(peace){
            const bool left = prompt.find("left hand") != std::string::npos ||
                              prompt.find("left arm") != std::string::npos;
            const float side = left ? 1.0f : -1.0f;
            const std::array<int, 3> index = left ? std::array<int, 3>{13,14,15} : std::array<int, 3>{32,33,34};
            const std::array<int, 3> middle = left ? std::array<int, 3>{16,17,18} : std::array<int, 3>{35,36,37};
            const std::array<int, 3> ring = left ? std::array<int, 3>{19,20,21} : std::array<int, 3>{38,39,40};
            const std::array<int, 3> pinky = left ? std::array<int, 3>{22,23,24} : std::array<int, 3>{41,42,43};
            const std::array<int, 3> thumb = left ? std::array<int, 3>{10,11,12} : std::array<int, 3>{29,30,31};
            const float existingCurl[3] = {18.0f, 24.0f, 16.0f};
            const float extraCurl[3] = {28.0f, 32.0f, 20.0f};
            for(size_t fingerJoint = 0; fingerJoint < 3; ++fingerJoint){
                const float straighten = root->animator->relaxedUniRigPose
                    ? side * existingCurl[fingerJoint] : 0.0f;
                rotateSlot(index[fingerJoint], glm::vec3(0,0,1),
                           [&](float u){ return straighten * envelope(u); });
                rotateSlot(middle[fingerJoint], glm::vec3(0,0,1),
                           [&](float u){ return straighten * envelope(u); });
                rotateSlot(ring[fingerJoint], glm::vec3(0,0,1),
                           [&](float u){ return -side * extraCurl[fingerJoint] * envelope(u); });
                rotateSlot(pinky[fingerJoint], glm::vec3(0,0,1),
                           [&](float u){ return -side * extraCurl[fingerJoint] * envelope(u); });
                rotateSlot(thumb[fingerJoint], glm::vec3(0,0,1),
                           [&](float u){ return -side * 8.0f * envelope(u); });
            }
            rotateSlot(index[0], glm::vec3(0,1,0), [&](float u){ return side * 10.0f * envelope(u); });
            rotateSlot(middle[0], glm::vec3(0,1,0), [&](float u){ return -side * 10.0f * envelope(u); });
            ++applied;
        }

        if(forwardRoll){
            int sourceHips = -1;
            for(size_t i = 0; i < source.joints.size(); ++i)
                if(motionJointKey(source.joints[i].name) == "hips"){ sourceHips = int(i); break; }
            float sourceTurn = 0.0f;
            if(sourceHips >= 0){
                const glm::quat first = motionWorldRotations(source, action.first)[size_t(sourceHips)];
                for(size_t i = action.first; i <= std::min(action.last, source.frames.size() - 1); ++i){
                    const glm::quat at = motionWorldRotations(source, i)[size_t(sourceHips)];
                    const float dot = std::clamp(std::fabs(glm::dot(first, at)), 0.0f, 1.0f);
                    sourceTurn = std::max(sourceTurn, 2.0f * std::acos(dot));
                }
            }
            if(sourceTurn < 2.1f){
                const auto tuck = [&](float u){
                    const float sine = std::sin(3.14159265f * rollProgress(u));
                    return sine * sine;
                };
                rotateSlot(0, glm::vec3(1,0,0),
                           [&](float u){ return 360.0f * rollProgress(u); });
                for(int slot : {1,2}) rotateSlot(slot, glm::vec3(1,0,0),
                    [&](float u){ return 24.0f * tuck(u); });
                for(int slot : {6,25}) rotateSlot(slot, glm::vec3(1,0,0),
                    [&](float u){ return -35.0f * tuck(u); });
                for(int slot : {44,48}) rotateSlot(slot, glm::vec3(1,0,0),
                    [&](float u){ return -70.0f * tuck(u); });
                for(int slot : {45,49}) rotateSlot(slot, glm::vec3(1,0,0),
                    [&](float u){ return 95.0f * tuck(u); });
                if(Warp::AnimatorTrack* hips = trackFor(0)){
                    const float lift = 0.22f * rest.bounds.height();
                    for(size_t i = 0; i < hips->translationKeys.values.size(); ++i){
                        const double time = hips->translationKeys.times[i];
                        const double sourceIndex = (time - firstFrame) / frameStep;
                        if(sourceIndex < double(action.first) - 0.01 ||
                           sourceIndex > double(action.last) + 0.01) continue;
                        hips->translationKeys.values[i].y += lift * tuck(progress(time, action));
                    }
                }
                ++applied;
            }
        }
    }
    return applied;
}

inline glm::quat motionFromToRotation(glm::vec3 from, glm::vec3 to){
    const float fromLength = glm::length(from), toLength = glm::length(to);
    if(!std::isfinite(fromLength) || !std::isfinite(toLength) || fromLength < 1e-7f || toLength < 1e-7f)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    from /= fromLength; to /= toLength;
    const float cosine = std::clamp(glm::dot(from, to), -1.0f, 1.0f);
    if(cosine > 0.99999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if(cosine < -0.99999f){
        glm::vec3 axis = glm::cross(from, glm::vec3(1,0,0));
        if(glm::dot(axis, axis) < 1e-8f) axis = glm::cross(from, glm::vec3(0,0,1));
        return glm::angleAxis(3.14159265358979323846f, glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(from, to);
    return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
}

inline bool motionSetWorldRotationKey(Warp::Stage& stage, Warp::Id id, double time,
                                      const glm::quat& worldDelta){
    Warp::Entity* entity = stage.get(id);
    if(!entity || !entity->joint) return false;
    const glm::mat4 world = stage.worldMatrix(id, time);
    const glm::quat currentWorld = motionRotationOf(world);
    const glm::quat desiredWorld = glm::normalize(worldDelta * currentWorld);
    const glm::mat4 parentWorld = entity->parent != Warp::None
        ? stage.worldMatrix(entity->parent, time) : glm::mat4(1.0f);
    const glm::quat local = glm::normalize(glm::inverse(motionRotationOf(parentWorld)) * desiredWorld);
    entity->rotationKeys.set(time, local);
    return true;
}

inline std::vector<uint8_t> motionDetectFootContacts(const Engine::WeaverMotion::Clip& clip, size_t footIndex){
    const size_t frames = clip.frames.size();
    std::vector<uint8_t> contact(frames, 0);
    if(frames < 2 || footIndex >= clip.joints.size() || clip.framesPerSecond <= 0.0) return contact;
    std::vector<glm::vec3> positions(frames);
    float lowest = std::numeric_limits<float>::max();
    for(size_t frame = 0; frame < frames; ++frame){
        const std::vector<glm::vec3> pose = motionPoseJointPositions(clip, frame);
        if(footIndex >= pose.size()) return {};
        positions[frame] = pose[footIndex];
        lowest = std::min(lowest, positions[frame].y);
    }
    const float height = std::max(0.5f, motionRestHeight(clip));
    //Dodir je do 2 % visine tijela iznad najnize tocke (~3.4 cm na 1.7 m). Prvi prag (3.5 %, ~6 cm)
    //brojao je petu koja se u odrazu vec dize kao dodir, pa je IK stopalo u skoku drzao na podu -
    //izmjereno na motion_1790382707996: do 55 kadrova zalijepljeno do 10 cm ispod pokreta
    const float heightLimit = std::max(0.02f, height * 0.02f);
    const float speedLimit = std::max(0.08f, height * 0.20f);
    for(size_t frame = 0; frame < frames; ++frame){
        const size_t before = frame == 0 ? 0 : frame - 1;
        const size_t after = std::min(frames - 1, frame + 1);
        const double elapsed = double(after - before) / clip.framesPerSecond;
        const float speed = elapsed > 0.0
            ? glm::length(positions[after] - positions[before]) / float(elapsed) : 0.0f;
        if(positions[frame].y - lowest <= heightLimit && speed <= speedLimit) contact[frame] = 1;
    }
    //Bridge one-frame threshold flicker so the planted foot does not jitter at toe-off.
    for(size_t frame = 1; frame + 1 < frames; ++frame)
        if(!contact[frame] && contact[frame - 1] && contact[frame + 1]) contact[frame] = 1;
    return contact;
}

inline size_t motionApplyFootContactIk(Warp::Stage& stage, const Engine::WeaverMotion::Clip& clip,
                                      Warp::Id rigRoot, double firstFrame, double frameStep,
                                      const MotionRigFit& fit, const MotionRigMapping& mapping,
                                      const Warp::Track<glm::vec3>& rootMotionKeys){
    if(frameStep <= 0.0 || clip.frames.empty()) return 0;
    const MotionRigRestPose rest = motionRigRestPose(stage, rigRoot);
    struct Leg{ std::string side; Warp::Id hip = Warp::None, knee = Warp::None, foot = Warp::None;
              int sourceFoot = -1; const MotionRigJointRest* restFoot = nullptr; };
    std::vector<Leg> legs;
    auto sourceIndex = [&](const std::vector<std::string_view>& names) -> int{
        for(std::string_view name : names){
            for(size_t i = 0; i < clip.joints.size(); ++i){
                if(motionJointKey(clip.joints[i].name) == name && i < mapping.targetBySource.size() &&
                   mapping.targetBySource[i] != Warp::None) return int(i);
            }
        }
        return -1;
    };
    for(const char* side : {"left", "right"}){
        const std::string prefix(side);
        const bool left = prefix == "left";
        const int sourceThigh = sourceIndex(left
            ? std::vector<std::string_view>{"leftupleg", "leftleg", "leftthigh"}
            : std::vector<std::string_view>{"rightupleg", "rightleg", "rightthigh"});
        const int sourceCalf = sourceIndex(left
            ? std::vector<std::string_view>{"leftshin", "leftcalf", "leftlowerleg"}
            : std::vector<std::string_view>{"rightshin", "rightcalf", "rightlowerleg"});
        const int sourceFoot = sourceIndex(left
            ? std::vector<std::string_view>{"leftfoot"} : std::vector<std::string_view>{"rightfoot"});
        if(sourceThigh < 0 || sourceCalf < 0 || sourceFoot < 0) continue;
        const Warp::Id thigh = mapping.targetBySource[size_t(sourceThigh)];
        const Warp::Id calf = mapping.targetBySource[size_t(sourceCalf)];
        const Warp::Id foot = mapping.targetBySource[size_t(sourceFoot)];
        const auto footRest = std::find_if(rest.joints.begin(), rest.joints.end(),
            [&](const MotionRigJointRest& joint){ return joint.id == foot; });
        if(thigh == calf || calf == foot || thigh == foot || footRest == rest.joints.end()) continue;
        legs.push_back({prefix, thigh, calf, foot, sourceFoot, &*footRest});
    }
    if(legs.empty()) return 0;

    Warp::Entity* root = stage.get(rigRoot);
    if(!root) return 0;
    const Warp::Track<glm::vec3> savedRootKeys = root->translationKeys;
    const bool hadAnimator = bool(root->animator);
    const bool savedAnimatorEnabled = hadAnimator ? root->animator->enabled : false;
    root->translationKeys = rootMotionKeys;
    if(hadAnimator) root->animator->enabled = false;
    //Evaluate the newly authored joint tracks together with this clip's horizontal root travel.

    size_t contactFrames = 0;
    for(const Leg& leg : legs){
        const std::vector<uint8_t> contact = motionDetectFootContacts(clip, size_t(leg.sourceFoot));
        if(contact.size() != clip.frames.size()) continue;
        const glm::mat4 rootAtStart = stage.worldMatrix(rigRoot, firstFrame);
        const float floorY = glm::vec3(rootAtStart * glm::vec4(0.0f, fit.floorY, 0.0f, 1.0f)).y;
        const glm::vec3 restFootWorld = glm::vec3(rootAtStart * glm::vec4(glm::vec3(leg.restFoot->matrix[3]), 1.0f));
        const float footClearance = std::max(0.0f, restFootWorld.y - floorY);
        //ZAKLJUCAVA SE SAMO VODORAVNO. Stopalo u dodiru ne smije kliziti, ali visinu i dalje vodi
        //pokret (nikad ispod poda). Prva verzija je u dodiru visinu uvijek stavljala na pod, pa je
        //stopalo koje se u odrazu vec dizalo bilo povuceno dolje - "noge se zalijepe za pod kad
        //skace". Na kraju dodira stopalo se pusti postupno kroz releaseFrames, bez skoka
        constexpr size_t releaseFrames = 3;
        bool locked = false;
        glm::vec3 lockPosition(0.0f);
        size_t previousFrame = clip.frames.size();
        size_t sinceRelease = releaseFrames + 1;
        for(size_t frame = 0; frame < clip.frames.size(); ++frame){
            if(!contact[frame]){
                if(locked){ locked = false; sinceRelease = 0; }
                previousFrame = clip.frames.size();
                if(++sinceRelease > releaseFrames) continue;
            }
            const double time = firstFrame + double(frame) * frameStep;
            const glm::vec3 hip = glm::vec3(stage.worldMatrix(leg.hip, time)[3]);
            const glm::vec3 knee = glm::vec3(stage.worldMatrix(leg.knee, time)[3]);
            const glm::vec3 ankle = glm::vec3(stage.worldMatrix(leg.foot, time)[3]);
            const glm::mat4 rootWorld = stage.worldMatrix(rigRoot, time);
            const float contactFloorY = glm::vec3(rootWorld * glm::vec4(0.0f, fit.floorY, 0.0f, 1.0f)).y;
            glm::vec3 target;
            if(contact[frame]){
                if(!locked || previousFrame + 1 != frame){
                    lockPosition = ankle;
                    locked = true;
                }
                previousFrame = frame;
                target = glm::vec3(lockPosition.x, std::max(ankle.y, contactFloorY + footClearance), lockPosition.z);
            }else{
                const float weight = float(sinceRelease) / float(releaseFrames + 1);
                const glm::vec3 held(lockPosition.x, ankle.y, lockPosition.z);
                target = held + (ankle - held) * weight;
            }

            glm::vec3 toTarget = target - hip;
            const float distance = glm::length(toTarget);
            const float upperLength = glm::length(knee - hip);
            const float lowerLength = glm::length(ankle - knee);
            if(!std::isfinite(distance) || upperLength < 1e-5f || lowerLength < 1e-5f || distance < 1e-5f) continue;
            const float maxReach = upperLength + lowerLength - 1e-4f;
            const float minReach = std::fabs(upperLength - lowerLength) + 1e-4f;
            const float clampedDistance = std::clamp(distance, minReach, maxReach);
            const glm::vec3 direction = toTarget / distance;
            const float along = (upperLength * upperLength - lowerLength * lowerLength +
                                 clampedDistance * clampedDistance) / (2.0f * clampedDistance);
            const float bendLength = std::sqrt(std::max(0.0f, upperLength * upperLength - along * along));
            glm::vec3 bend = knee - hip - direction * glm::dot(knee - hip, direction);
            if(glm::dot(bend, bend) < 1e-8f){
                bend = glm::cross(direction, glm::vec3(0.0f, 0.0f, 1.0f));
                if(glm::dot(bend, bend) < 1e-8f) bend = glm::cross(direction, glm::vec3(1.0f, 0.0f, 0.0f));
            }
            bend = glm::normalize(bend);
            const glm::vec3 desiredKnee = hip + direction * along + bend * bendLength;
            if(!motionSetWorldRotationKey(stage, leg.hip, time,
                                           motionFromToRotation(knee - hip, desiredKnee - hip))) continue;
            const glm::vec3 updatedKnee = glm::vec3(stage.worldMatrix(leg.knee, time)[3]);
            const glm::vec3 updatedAnkle = glm::vec3(stage.worldMatrix(leg.foot, time)[3]);
            motionSetWorldRotationKey(stage, leg.knee, time,
                                      motionFromToRotation(updatedAnkle - updatedKnee,
                                                           target - updatedKnee));
            if(contact[frame]) ++contactFrames;
        }
    }
    root->translationKeys = savedRootKeys;
    if(hadAnimator) root->animator->enabled = savedAnimatorEnabled;
    return contactFrames;
}

inline bool retargetMotionToRig(Warp::Stage& stage, const Engine::WeaverMotion::Clip& clip, Warp::Id rigRoot,
                                double firstFrame, double frameStep, MotionRigFit& fit,
                                MotionRigMapping& mapping, std::string& problem,
                                Warp::Track<glm::vec3>& rootMotionKeys, bool applyFootContactIk = true){
    const MotionRigRestPose rest = motionRetargetRestPose(stage, rigRoot);
    if(!rest.bounds.valid || rest.joints.empty()){ problem = "selected rig has no readable rest-pose joints"; return false; }
    mapping = mapMotionBones(clip, rest);
    if(!mapping.problem.empty()){ problem = mapping.problem; return false; }
    if(!fitMotionToRigRestPose(clip, stage, rigRoot, fit, problem)) return false;

    const size_t sourceHip = size_t(mapping.sourceHips);
    const std::vector<glm::vec3> sourceFirstPositions = motionPoseJointPositions(clip, 0);
    if(sourceHip >= sourceFirstPositions.size()){
        problem = "mapped source hips are missing from the first frame"; return false;
    }
    const glm::quat rigWorldRotation = motionRotationOf(stage.worldMatrix(rigRoot, firstFrame));
    const glm::quat rigWorldInverse = glm::inverse(rigWorldRotation);
    const Warp::Entity* rootEntity = stage.get(rigRoot);
    if(!rootEntity){ problem = "selected character root disappeared during retargeting"; return false; }
    const glm::mat4 parentWorld = rootEntity->parent != Warp::None
        ? stage.worldMatrix(rootEntity->parent, firstFrame) : glm::mat4(1.0f);
    const glm::mat4 parentWorldInverse = glm::inverse(parentWorld);
    const glm::vec3 rootBaseTranslation = rootEntity->local.translation;

    std::unordered_map<Warp::Id, size_t> targetIndices;
    std::unordered_map<Warp::Id, size_t> sourceForTarget;
    for(size_t i = 0; i < rest.joints.size(); ++i) targetIndices.emplace(rest.joints[i].id, i);
    for(size_t source = 0; source < mapping.targetBySource.size(); ++source){
        const Warp::Id target = mapping.targetBySource[source];
        if(target != Warp::None) sourceForTarget.emplace(target, source);
    }

    for(size_t frameIndex = 0; frameIndex < clip.frames.size(); ++frameIndex){
        const double time = firstFrame + double(frameIndex) * frameStep;
        const std::vector<glm::quat> sourceWorld = motionWorldRotations(clip, frameIndex);
        const std::vector<glm::vec3> sourcePositions = motionPoseJointPositions(clip, frameIndex);
        std::vector<glm::quat> desiredJointWorld(rest.joints.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        for(size_t targetIndex = 0; targetIndex < rest.joints.size(); ++targetIndex){
            const MotionRigJointRest& target = rest.joints[targetIndex];
            const auto parentFound = targetIndices.find(target.parentJoint);
            const bool hasParentJoint = target.parentJoint != Warp::None && parentFound != targetIndices.end();
            const size_t parentIndex = hasParentJoint ? parentFound->second : 0;
            const glm::quat parentDesiredWorld = hasParentJoint ? desiredJointWorld[parentIndex]
                                                                : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            const glm::mat4 helperFromParentJoint = hasParentJoint
                ? glm::inverse(rest.joints[parentIndex].matrix) * target.directParentMatrix
                : target.directParentMatrix;
            const glm::quat helperRotation = motionRotationOf(helperFromParentJoint);
            const glm::quat desiredDirectParent = glm::normalize(parentDesiredWorld * helperRotation);
            const auto sourceFound = sourceForTarget.find(target.id);
            const bool mapped = sourceFound != sourceForTarget.end() && sourceFound->second < sourceWorld.size();
            const glm::quat targetRestWorld = motionRotationOf(target.matrix);
            glm::quat desiredWorld = glm::normalize(parentDesiredWorld * helperRotation * motionRotationOf(target.localMatrix));
            if(mapped){
                const size_t source = sourceFound->second;
                // BVH rotations are the NVIDIA/Kimodo pose relative to its canonical T-pose.
                // Apply every sampled source pose directly to the target's original bind frame;
                // do not rebase arms to frame 0, which pins them to the display rest pose.
                desiredWorld = glm::normalize(sourceWorld[source] * targetRestWorld);
            }
            desiredJointWorld[targetIndex] = desiredWorld;

            const glm::quat localRotation = glm::normalize(glm::inverse(desiredDirectParent) * desiredWorld);
            Warp::Entity* entity = stage.get(target.id);
            if(!entity) continue;
            if(mapped) entity->rotationKeys.set(time, localRotation);
            if(target.id == mapping.targetHips && sourceHip < sourcePositions.size()) {
                const glm::vec3 sourceDelta = sourcePositions[sourceHip] - sourceFirstPositions[sourceHip];
                const glm::vec3 rigDelta = rigWorldInverse * sourceDelta * fit.scale;
                const glm::vec3 rootDelta(rigDelta.x, 0.0f, rigDelta.z);
                const glm::vec3 rootWorldDelta = rigWorldRotation * rootDelta;
                const glm::vec3 rootParentDelta = glm::vec3(parentWorldInverse * glm::vec4(rootWorldDelta, 0.0f));
                rootMotionKeys.set(time, rootBaseTranslation + rootParentDelta);

                //Move horizontal locomotion onto the character root. Keep pelvis height and bounce
                //on the hips so the whole skinned model travels without counting the stride twice.
                const glm::vec3 hipsWorldDelta = rigWorldRotation * rigDelta;
                const glm::vec3 localDelta = glm::inverse(desiredDirectParent) * (hipsWorldDelta - rootWorldDelta);
                const glm::vec3 localRestTranslation = glm::vec3(target.localMatrix[3]);
                entity->translationKeys.set(time, localRestTranslation + localDelta);
            }
        }
    }
    mapping.footContactFrames = applyFootContactIk
        ? motionApplyFootContactIk(stage, clip, rigRoot, firstFrame, frameStep, fit, mapping, rootMotionKeys) : 0;
    return true;
}

inline bool fitMotionToRigRestPose(const Engine::WeaverMotion::Clip& clip,
                                   const Warp::Stage& stage, Warp::Id rigRoot,
                                   MotionRigFit& fit, std::string& problem){
    const MotionRigRestPose restPose = motionRigRestPose(stage, rigRoot);
    if(!restPose.bounds.valid || restPose.joints.empty()){
        problem = "selected rig has no readable rest-pose joints";
        return false;
    }
    const MotionRigMapping mapping = mapMotionBones(clip, restPose);
    if(!mapping.problem.empty()){ problem = mapping.problem; return false; }
    const float sourceHeight = motionRestHeight(clip);
    float targetHeight = restPose.bounds.height();
    float targetFloorY = restPose.bounds.low.y;
    bool hasMeshFloor = false;
    MotionBounds targetMeshBounds;
    std::string floorProblem;
    if(!motionRigMeshBounds(stage, rigRoot, hasMeshFloor, targetMeshBounds, floorProblem)){
        problem = "cannot determine selected character mesh bounds: " + floorProblem;
        return false;
    }
    if(hasMeshFloor){
        targetHeight = targetMeshBounds.height();
        targetFloorY = targetMeshBounds.low.y;
    }
    if(!std::isfinite(sourceHeight) || !std::isfinite(targetHeight) || sourceHeight <= 1e-5f || targetHeight <= 1e-5f){
        problem = "source or target rig has zero/invalid rest-pose height";
        return false;
    }
    const std::vector<glm::vec3> sourcePositions = motionPoseJointPositions(clip, 0);
    const MotionBounds sourceFrameBounds = motionPoseBounds(clip, 0);
    const auto targetHip = std::find_if(restPose.joints.begin(), restPose.joints.end(),
        [&](const MotionRigJointRest& joint){ return joint.id == mapping.targetHips; });
    if(targetHip == restPose.joints.end() || size_t(mapping.sourceHips) >= sourcePositions.size() ||
       !sourceFrameBounds.valid){
        problem = "mapped pelvis is missing from one of the rest poses";
        return false;
    }

    fit.scale = targetHeight / sourceHeight;
    const glm::vec3 targetHips = glm::vec3(targetHip->matrix[3]);
    const glm::vec3 sourceHips = sourcePositions[size_t(mapping.sourceHips)];
    fit.translation = glm::vec3(targetHips.x - fit.scale * sourceHips.x,
                                targetFloorY - fit.scale * sourceFrameBounds.low.y,
                                targetHips.z - fit.scale * sourceHips.z);
    fit.mappedJoints = mapping.matched;
    fit.profile = mapping.profile;
    fit.floorSource = hasMeshFloor ? "GLB mesh bounds" : "joint bounds";
    fit.floorY = targetFloorY;
    if(!std::isfinite(fit.scale) || !std::isfinite(fit.translation.x) ||
       !std::isfinite(fit.translation.y) || !std::isfinite(fit.translation.z)){
        problem = "rest-pose fit produced a non-finite transform";
        return false;
    }
    return true;
}

}
