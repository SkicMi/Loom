#pragma once
//=============================================================================================
// POKRET LIKA U SCENI: klip iz WeaverMotion modula (NVIDIA Kimodo, BVH) postaje kostur u Warpu.
//
//   /<klip>                grupa: mjesto i mjerilo lika u sceni
//       Hips               zglob (Warp::Joint): transformacija i kljucevi su njegovi
//           Spine ...      djeca su zglobovi-djeca; pogled crta tocku i kost do roditelja
//
// VRIJEME JE VRIJEME SCENE. Klip ima svoj fps (Kimodo 30), a scena svoj - matchmove s 50 fps
// snimke. Kadar klipa i pada na kadar scene start + i * fpsScene / fpsKlipa, pa lik hoda istom
// brzinom kojom je generiran, a kamera iz solvea ostaje na svojoj snimci. Timeline scene se
// preuzima samo kad je scena prazna.
//
// MJESTO I MJERILO SU NA GRUPI. Kimodo pise metre, a scena iz solvea nema metre - pa editor lik
// postavi na povrsinu snimke i skalira ga iz udaljenosti (kao kocku), a umjetnik ga dotjera na
// grupi. Kljucevi zglobova ostaju tocno oni iz klipa.
//
// Prvu verziju (uvoz s kockom po zglobu, timeline preuzet uvijek) napisao je GPT; ova ju zamjenjuje
//=============================================================================================
#include <Engine/WeaverMotion.h>
#include <Warp/Stage.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "LoomMotionRetarget.h"

namespace Loom{

// Quote one argument for the existing popen based job launcher.
inline std::string shellQuoteArgument(std::string_view argument){
    std::string quoted;
    quoted.reserve(argument.size() + 2);
    quoted.push_back(char(39));
    for(const char character : argument){
        if(character == char(39)) quoted += std::string(1, char(39)) + char(92) + char(39) + char(39);
        else quoted.push_back(character);
    }
    quoted.push_back(char(39));
    return quoted;
}

inline std::string buildWeaverMotionCommand(const std::filesystem::path& executable,
                                           std::string_view prompt,
                                           float durationSeconds,
                                           const std::filesystem::path& outputStem){
    if(!std::isfinite(durationSeconds)) durationSeconds = 5.0f;
    durationSeconds = std::clamp(durationSeconds, 1.0f, 10.0f);
    char duration[32];
    std::snprintf(duration, sizeof(duration), "%.2f", double(durationSeconds));

    return "TEXT_ENCODER_MODE=local TEXT_ENCODER_DEVICE=cpu " +
           shellQuoteArgument(executable.string()) + " " + shellQuoteArgument(prompt) +
           " --model 'Kimodo-SOMA-RP-v1.1' --duration " + duration +
           " --num_samples 1 --output " + shellQuoteArgument(outputStem.string()) +
           " --bvh --bvh_standard_tpose";
}

struct WeaverMotionImportReport{
    Warp::Id group = Warp::None;
    Warp::Id root = Warp::None;
    size_t joints = 0;
    size_t frames = 0;
    double firstFrame = 1.0, lastFrame = 1.0;   //gdje je klip pao na timelineu scene
    float height = 0.0f;                        //visina kostura u mirovanju, u jedinicama klipa
    float importedScale = 1.0f;
    size_t mappedJoints = 0;
    size_t footContactFrames = 0;
    std::string rigProfile;
    std::string floorSource;
    std::string problem;
};

//Gdje i kako klip ulazi u scenu
struct MotionPlacement{
    double startFrame = 1.0;        //kadar scene na koji pada prvi kadar klipa
    double sceneFps = 0.0;          //0: scena je prazna, preuzmi fps i raspon klipa
    glm::vec3 position{0.0f};       //u svijetu: ondje lik pocinje, na tlu
    float scale = 1.0f;
    Warp::Id parent = Warp::None;
    bool fitToParentRig = false;    //primijeni mapiranje i uklapanje u rest pose odabranog lika
    bool footContactIK = true;
    std::string animationName;
};

inline std::vector<std::filesystem::path> weaverMotionFilesIn(const std::filesystem::path& directory){
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for(const auto& entry : std::filesystem::directory_iterator(directory, error)){
        if(error) break;
        if(!entry.is_regular_file(error)) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c){ return char(std::tolower(c)); });
        if(extension == ".bvh") found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());
    return found;
}


inline WeaverMotionImportReport importWeaverMotionClip(Warp::Stage& stage,
                                                       const Engine::WeaverMotion::Clip& clip,
                                                       const std::string& name,
                                                       const MotionPlacement& placement = {}){
    WeaverMotionImportReport report;
    if(clip.joints.empty()){ report.problem = "clip has no joints"; return report; }
    if(clip.frames.empty()){ report.problem = "clip has no frames"; return report; }

    //Kadar klipa -> kadar scene
    const bool adopt = placement.sceneFps <= 0.0;
    const double step = adopt ? 1.0 : placement.sceneFps / std::max(1e-6, clip.framesPerSecond);
    const double first = adopt ? 1.0 : placement.startFrame;

    //Sidri BVH na najnizi zglob prvog kadra, jer root moze biti na kukovima ili na podu.
    //Kad je odabran lik, fit mjeri visinu, mapira kukove i koristi njegov mirni kostur kao referencu.
    const std::vector<glm::vec3> firstPosePositions = motionPoseJointPositions(clip, 0);
    const MotionBounds firstPoseBounds = motionPoseBounds(clip, 0);
    if(!firstPoseBounds.valid){ report.problem = "clip has no finite joint positions"; return report; }
    float importScale = placement.scale;
    glm::vec3 groupTranslation(placement.position.x - importScale * firstPosePositions[0].x,
                               placement.position.y - importScale * firstPoseBounds.low.y,
                               placement.position.z - importScale * firstPosePositions[0].z);
    if(placement.fitToParentRig){
        if(!ensureRigAnimator(stage, placement.parent)){
            report.problem = "selected character root is missing";
            return report;
        }
        const MotionRigRestPose rest = motionRigRestPose(stage, placement.parent);
        struct StashedTracks{
            Warp::Id id;
            Warp::Track<glm::vec3> translation, scale;
            Warp::Track<glm::quat> rotation;
        };
        std::vector<StashedTracks> stashed;
        stashed.reserve(rest.joints.size());
        for(const MotionRigJointRest& joint : rest.joints){
            Warp::Entity* entity = stage.get(joint.id);
            if(!entity) continue;
            stashed.push_back({joint.id, entity->translationKeys, entity->scaleKeys, entity->rotationKeys});
            entity->translationKeys = {};
            entity->rotationKeys = {};
            entity->scaleKeys = {};
        }
        MotionRigFit rigFit;
        MotionRigMapping mapping;
        Warp::Track<glm::vec3> rootMotionKeys;
        std::string problem;
        if(!retargetMotionToRig(stage, clip, placement.parent, first, step, rigFit, mapping, problem,
                                rootMotionKeys, placement.footContactIK)){
            for(const StashedTracks& saved : stashed) if(Warp::Entity* entity = stage.get(saved.id)){
                entity->translationKeys = saved.translation;
                entity->rotationKeys = saved.rotation;
                entity->scaleKeys = saved.scale;
            }
            report.problem = "rig retarget failed: " + problem;
            return report;
        }
        Warp::AnimationClip animation;
        animation.name = placement.animationName.empty() ? (name.empty() ? "Animation" : name) : placement.animationName;
        animation.startFrame = first;
        animation.endFrame = first + double(clip.frames.size() - 1) * step;
        if(!rootMotionKeys.empty()){
            Warp::AnimatorTrack rootTrack;
            rootTrack.target = placement.parent;
            rootTrack.targetPath = stage.path(placement.parent);
            rootTrack.rootMotion = true;
            rootTrack.translationKeys = std::move(rootMotionKeys);
            animation.tracks.push_back(std::move(rootTrack));
        }
        for(const MotionRigJointRest& joint : rest.joints){
            Warp::Entity* entity = stage.get(joint.id);
            if(!entity || (entity->translationKeys.empty() && entity->rotationKeys.empty() && entity->scaleKeys.empty())) continue;
            Warp::AnimatorTrack track;
            track.target = entity->id;
            track.targetPath = stage.path(entity->id);
            track.translationKeys = entity->translationKeys;
            track.rotationKeys = entity->rotationKeys;
            track.scaleKeys = entity->scaleKeys;
            animation.tracks.push_back(std::move(track));
        }
        for(const StashedTracks& saved : stashed) if(Warp::Entity* entity = stage.get(saved.id)){
            entity->translationKeys = saved.translation;
            entity->rotationKeys = saved.rotation;
            entity->scaleKeys = saved.scale;
        }
        Warp::Animator& animator = *stage.get(placement.parent)->animator;
        animator.animations.push_back(std::move(animation));
        animator.activeAnimation = animator.animations.size() - 1;
        report.group = placement.parent;
        report.root = mapping.targetHips;
        report.joints = clip.joints.size();
        report.frames = clip.frames.size();
        report.firstFrame = first;
        report.lastFrame = first + double(clip.frames.size() - 1) * step;
        stage.endFrame = std::max(stage.endFrame, report.lastFrame);
        report.height = motionRestHeight(clip);
        report.importedScale = rigFit.scale;
        report.mappedJoints = mapping.matched;
        report.footContactFrames = mapping.footContactFrames;
        report.rigProfile = mapping.profile;
        report.floorSource = rigFit.floorSource;
        return report;
    }
    const Warp::Id group = stage.create(name.empty() ? "Motion" : name, placement.parent);
    stage.get(group)->local.translation = groupTranslation;
    stage.get(group)->local.scale = glm::vec3(importScale);

    std::vector<Warp::Id> ids(clip.joints.size(), Warp::None);
    for(size_t i = 0; i < clip.joints.size(); ++i){
        const Engine::WeaverMotion::Joint& joint = clip.joints[i];
        const Warp::Id parent = joint.parent >= 0 && size_t(joint.parent) < i ? ids[size_t(joint.parent)] : group;
        ids[i] = stage.create(joint.name.empty() ? "Joint" : joint.name, parent);
        Warp::Entity& entity = *stage.get(ids[i]);
        entity.local.translation = joint.offset;
        entity.joint = Warp::Joint{};
    }

    //Pomak se kljuca za korijen i za svaki zglob koji se u klipu IGDJE pomice - i onda u SVAKOM
    //kadru. Kljuc samo gdje pomak nije nula ostavio bi rupe u kojima zglob drzi prethodni kljuc
    std::vector<uint8_t> moves(clip.joints.size(), 0);
    moves[0] = 1;
    for(const Engine::WeaverMotion::Pose& pose : clip.frames){
        for(size_t j = 0; j < pose.translations.size() && j < moves.size(); ++j){
            if(glm::length(pose.translations[j]) > 0.0f) moves[j] = 1;
        }
    }

    for(size_t frameIndex = 0; frameIndex < clip.frames.size(); ++frameIndex){
        const double time = first + double(frameIndex) * step;
        const Engine::WeaverMotion::Pose& pose = clip.frames[frameIndex];
        for(size_t j = 0; j < ids.size(); ++j){
            Warp::Entity& entity = *stage.get(ids[j]);
            if(j < pose.rotations.size()) entity.rotationKeys.set(time, pose.rotations[j]);
            if(moves[j] && j < pose.translations.size()){
                entity.translationKeys.set(time, clip.joints[j].offset + pose.translations[j]);
            }
        }
    }

    report.firstFrame = first;
    report.lastFrame = first + double(clip.frames.size() - 1) * step;
    //Prazna scena preuzima raspon klipa; zadani kraj timelinea (100) nije nicija odluka i ne smije
    //ostati iza klipa od tri sekunde. Scena koja vec ima sadrzaj samo se produlji
    if(adopt){
        stage.startFrame = report.firstFrame;
        stage.framesPerSecond = clip.framesPerSecond;
        stage.endFrame = std::max(report.lastFrame, report.firstFrame + 1.0);
    }else stage.endFrame = std::max(stage.endFrame, std::max(report.lastFrame, report.firstFrame + 1.0));

    report.group = group;
    report.root = ids.front();
    report.joints = clip.joints.size();
    report.frames = clip.frames.size();
    report.height = motionRestHeight(clip);
    report.importedScale = importScale;
    return report;
}

inline WeaverMotionImportReport importWeaverMotionBvh(Warp::Stage& stage, const std::filesystem::path& path,
                                                      const MotionPlacement& placement = {}){
    Engine::WeaverMotion::Clip clip;
    std::string error;
    if(!Engine::WeaverMotion::readKimodoBvh(path.string(), clip, error)){
        WeaverMotionImportReport report;
        report.problem = error;
        return report;
    }
    return importWeaverMotionClip(stage, clip, path.stem().string(), placement);
}

}
