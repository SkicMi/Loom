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
#include <filesystem>
#include <string>
#include <vector>

namespace Loom{

struct WeaverMotionImportReport{
    Warp::Id group = Warp::None;
    Warp::Id root = Warp::None;
    size_t joints = 0;
    size_t frames = 0;
    double firstFrame = 1.0, lastFrame = 1.0;   //gdje je klip pao na timelineu scene
    float height = 0.0f;                        //visina kostura u mirovanju, u jedinicama klipa
    std::string problem;
};

//Gdje i kako klip ulazi u scenu
struct MotionPlacement{
    double startFrame = 1.0;        //kadar scene na koji pada prvi kadar klipa
    double sceneFps = 0.0;          //0: scena je prazna, preuzmi fps i raspon klipa
    glm::vec3 position{0.0f};       //u svijetu: ondje lik pocinje, na tlu
    float scale = 1.0f;
    Warp::Id parent = Warp::None;
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

//Visina kostura u mirovanju (samo pomaci zglobova), u jedinicama klipa - za mjerilo u sceni
inline float motionRestHeight(const Engine::WeaverMotion::Clip& clip){
    std::vector<glm::vec3> at(clip.joints.size(), glm::vec3(0.0f));
    float low = 0.0f, high = 0.0f;
    for(size_t i = 0; i < clip.joints.size(); ++i){
        const int parent = clip.joints[i].parent;
        at[i] = (parent >= 0 && size_t(parent) < i ? at[size_t(parent)] : glm::vec3(0.0f)) + clip.joints[i].offset;
        low = std::min(low, at[i].y);
        high = std::max(high, at[i].y);
    }
    return high - low;
}

inline WeaverMotionImportReport importWeaverMotionClip(Warp::Stage& stage,
                                                       const Engine::WeaverMotion::Clip& clip,
                                                       const std::string& name,
                                                       const MotionPlacement& placement = {}){
    WeaverMotionImportReport report;
    if(clip.joints.empty()){ report.problem = "klip nema zglobova"; return report; }
    if(clip.frames.empty()){ report.problem = "klip nema kadrova"; return report; }

    //Kadar klipa -> kadar scene
    const bool adopt = placement.sceneFps <= 0.0;
    const double step = adopt ? 1.0 : placement.sceneFps / std::max(1e-6, clip.framesPerSecond);
    const double first = adopt ? 1.0 : placement.startFrame;

    //Lik pocinje na zadanom mjestu: vodoravni pomak korijena u prvom kadru se ponisti na grupi,
    //visina ostaje - tlo klipa je y = 0
    const glm::vec3 rootStart = clip.joints[0].offset + (clip.frames[0].translations.empty()
                                                         ? glm::vec3(0.0f) : clip.frames[0].translations[0]);
    const Warp::Id group = stage.create(name.empty() ? "Pokret" : name, placement.parent);
    stage.get(group)->local.translation = placement.position - placement.scale * glm::vec3(rootStart.x, 0.0f, rootStart.z);
    stage.get(group)->local.scale = glm::vec3(placement.scale);

    std::vector<Warp::Id> ids(clip.joints.size(), Warp::None);
    for(size_t i = 0; i < clip.joints.size(); ++i){
        const Engine::WeaverMotion::Joint& joint = clip.joints[i];
        const Warp::Id parent = joint.parent >= 0 && size_t(joint.parent) < i ? ids[size_t(joint.parent)] : group;
        ids[i] = stage.create(joint.name.empty() ? "Zglob" : joint.name, parent);
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
    if(adopt){
        stage.startFrame = report.firstFrame;
        stage.endFrame = std::max(report.lastFrame, report.firstFrame + 1.0);
        stage.framesPerSecond = clip.framesPerSecond;
    }

    report.group = group;
    report.root = ids.front();
    report.joints = clip.joints.size();
    report.frames = clip.frames.size();
    report.height = motionRestHeight(clip);
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
