#pragma once
//=============================================================================================
// ISPRAVAK POZE KAO SLOJ: umjetnik ispravi pozu u dva (ili vise) kadra, a izmedju njih se ISPRAVAK
// pretapa preko pokreta - klasicni animacijski sloj.
//
// Primjer zbog kojeg postoji: lik glumi da drzi pistolj, ruke su krive. Ispravi se ruke u kadru
// 40 i u kadru 90; izmedju njih ruke idu od jednog ispravka do drugog, a noge, kukovi i hod
// ostaju onakvi kakvi su bili.
//
// DVIJE ZAMKE KOJE SU OVDJE VEC PRODJENE:
//
//   - PRVI Smart Blend je imao jedan kadar i ispravak je iza njega iscurio natrag u original, pa se
//     izmedju dvaju ispravaka uvijek vracala kriva poza
//   - DRUGI (caf5df8) je izmedju kljuceva interpolirao CIJELE POZE, svih zglobova. Noge i root
//     motion su se izmedju dva kadra ukocili i klizali - korisnik je to vidio kao glitch
//
// Zato je ovo ADITIVNO i SAMO ZA DIRANE ZGLOBOVE: ispravak zgloba je d = original^-1 * uredjeno u
// njegovom lokalnom sustavu (i razlika pomaka); rezultat je pokret(t) * d(t). Zglob koji ni u
// jednom kljucu nije diran uopce se ne pise.
//
//   PRIJE PRVOG KLJUCA   ispravak prvog kljuca se ulije kroz "in" kadrova
//   IZMEDJU KLJUCEVA     ispravak A -> ispravak B (smoothstep, slerp), preko pokreta koji tece
//   IZA ZADNJEG KLJUCA   Return: ispravak se drzi "hold" kadrova i iscuri kroz "out".
//                        Hold: ispravak ostaje do kraja klipa
//=============================================================================================
#include <Warp/Stage.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Loom{

using JointPose = std::vector<std::pair<Warp::Id, Warp::Transform>>;

struct PoseKey{
    double frame = 0.0;
    JointPose pose;                 //uredjena poza u tom kadru
};

enum class PoseKeyEnding{ Return, Hold };

struct PoseKeySettings{
    double inFrames = 8.0;
    double holdFrames = 0.0;
    double outFrames = 12.0;
    PoseKeyEnding ending = PoseKeyEnding::Return;
    bool blendBetween = true;       //false: samo kljucni kadrovi (stari "No Blend")
};

struct PoseCorrection{
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};    //lokalno: original * rotation = uredjeno
    glm::vec3 scale{0.0f};
};

inline PoseCorrection correctionOf(const Warp::Transform& original, const Warp::Transform& edited){
    PoseCorrection c;
    c.translation = edited.translation - original.translation;
    c.rotation = glm::normalize(glm::inverse(original.rotation) * edited.rotation);
    c.scale = edited.scale - original.scale;
    return c;
}

inline bool correctionIsZero(const PoseCorrection& c){
    return glm::length(c.translation) < 1e-5f && glm::length(c.scale) < 1e-5f && std::fabs(c.rotation.w) > 0.999999f;
}

inline PoseCorrection mixCorrections(const PoseCorrection& a, const PoseCorrection& b, float t){
    PoseCorrection out;
    out.translation = a.translation + (b.translation - a.translation) * t;
    out.scale = a.scale + (b.scale - a.scale) * t;
    glm::quat to = b.rotation;
    if(glm::dot(a.rotation, to) < 0.0f) to = -to;
    out.rotation = glm::normalize(glm::slerp(a.rotation, to, t));
    return out;
}

inline Warp::Transform applyCorrection(const Warp::Transform& motion, const PoseCorrection& c, float weight = 1.0f){
    const PoseCorrection w = mixCorrections(PoseCorrection{}, c, weight);
    Warp::Transform out = motion;
    out.translation += w.translation;
    out.scale += w.scale;
    out.rotation = glm::normalize(motion.rotation * w.rotation);
    return out;
}

inline float easeInOut(float x){
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

//Stage mora u trenutku poziva nositi ORIGINALNI klip (vracen prije primjene): iz njega se citaju
//pokret i ispravci. Kljucevi ne moraju biti poredani. Vraca broj ispravljenih zglobova
inline size_t applyPoseKeys(Warp::Stage& stage, std::vector<PoseKey> keys, double clipStart, double clipEnd,
                            const PoseKeySettings& settings){
    if(keys.empty()) return 0;
    std::sort(keys.begin(), keys.end(), [](const PoseKey& a, const PoseKey& b){ return a.frame < b.frame; });
    const double first = keys.front().frame, last = keys.back().frame;

    //Svi zglobovi koji se pojavljuju u bilo kojem kljucu
    std::vector<Warp::Id> joints;
    for(const PoseKey& key : keys)
        for(const auto& joint : key.pose)
            if(std::find(joints.begin(), joints.end(), joint.first) == joints.end()) joints.push_back(joint.first);

    //Cijeli plan PRIJE ijednog zapisa: setLocalAt mijenja klip, a pokret se cita iz originala
    struct Write{ Warp::Id id; double frame; Warp::Transform value; };
    std::vector<Write> writes;
    size_t corrected = 0;
    for(Warp::Id id : joints){
        if(!stage.get(id)) continue;
        std::vector<PoseCorrection> perKey;
        bool touched = false;
        for(const PoseKey& key : keys){
            PoseCorrection c;
            for(const auto& joint : key.pose)
                if(joint.first == id){ c = correctionOf(stage.localAt(id, key.frame), joint.second); break; }
            touched = touched || !correctionIsZero(c);
            perKey.push_back(c);
        }
        if(!touched) continue;
        ++corrected;

        auto correctionAt = [&](double t, float& weight) -> PoseCorrection{
            weight = 1.0f;
            if(t <= first){
                const double inStart = std::max(clipStart, first - std::max(0.0, settings.inFrames));
                weight = t >= first ? 1.0f : easeInOut(float((t - inStart) / std::max(1e-6, first - inStart)));
                return perKey.front();
            }
            if(t >= last){
                if(settings.ending == PoseKeyEnding::Hold) return perKey.back();
                const double holdEnd = std::min(clipEnd, last + std::max(0.0, settings.holdFrames));
                const double outEnd = std::min(clipEnd, holdEnd + std::max(1.0, settings.outFrames));
                weight = t <= holdEnd ? 1.0f : easeInOut(float((outEnd - t) / std::max(1e-6, outEnd - holdEnd)));
                return perKey.back();
            }
            size_t k = 0;
            while(k + 1 < keys.size() && keys[k + 1].frame <= t) ++k;
            if(!settings.blendBetween){ weight = 0.0f; return perKey[k]; }
            const float s = easeInOut(float((t - keys[k].frame) / (keys[k + 1].frame - keys[k].frame)));
            return mixCorrections(perKey[k], perKey[k + 1], s);
        };

        const double from = settings.blendBetween ? std::max(clipStart, first - std::max(0.0, settings.inFrames)) : first;
        double to = last;
        if(settings.blendBetween){
            to = settings.ending == PoseKeyEnding::Hold ? clipEnd
               : std::min(clipEnd, last + std::max(0.0, settings.holdFrames) + std::max(1.0, settings.outFrames));
        }
        //ISPRAVAK IDE NA VREMENA KLJUCEVA KOJA ZGLOB VEC IMA. Kimodov klip je 30 Hz, a scena
        //obicno 25: kljucevi stoje na 1, 1.83, 2.67... Prva verzija je pisala samo cijele kadrove,
        //pa su izmedju njih ostali originalni kljucevi i ruka je u reprodukciji skakala izmedju
        //ispravljene i krive poze - glitch koji je korisnik vidio. Zglob bez kljuceva dobije cijele
        std::vector<double> frames;
        std::vector<double> existing;
        auto collect = [&](const auto& track){
            for(double t : track.times) if(t >= from - 1e-9 && t <= to + 1e-9) existing.push_back(t);
        };
        if(const Warp::AnimatorTrack* track = stage.activeAnimatorTrack(id)){
            collect(track->translationKeys); collect(track->rotationKeys); collect(track->scaleKeys);
        }else if(const Warp::Entity* entity = stage.get(id)){
            collect(entity->translationKeys); collect(entity->rotationKeys); collect(entity->scaleKeys);
        }
        if(existing.empty()) for(double t = std::ceil(from); t <= to; t += 1.0) frames.push_back(t);
        else frames = existing;
        for(const PoseKey& key : keys) frames.push_back(key.frame);
        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
        for(double t : frames){
            const bool isKey = std::any_of(keys.begin(), keys.end(), [&](const PoseKey& k){ return k.frame == t; });
            if(!settings.blendBetween && !isKey) continue;
            float weight = 1.0f;
            const PoseCorrection c = correctionAt(t, weight);
            writes.push_back({id, t, applyCorrection(stage.localAt(id, t), c, isKey ? 1.0f : weight)});
        }
    }
    for(const Write& write : writes) stage.setLocalAt(write.id, write.frame, write.value);
    return corrected;
}

}
