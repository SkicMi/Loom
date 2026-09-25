#pragma once
//=============================================================================================
// POZE PO KLJUCEVIMA: umjetnik uredi pozu u dva ili vise kadrova, a IZMEDJU NJIH se poza pretapa iz
// jedne u drugu.
//
// ZASTO. Prvi Smart Blend je imao jedan kadar: pomak poze se ulije, drzi pa ISCURI natrag u
// originalni pokret. Za "podigne ruku u kadru 40 i drzi je do kadra 90 gdje je spusti drukcije"
// trebalo je dvaput raditi Smart Blend, a izmedju dvaju ispravaka se svaki put vracao originalni
// pokret iz Kimoda - bas ono sto je umjetnik htio zamijeniti.
//
//   PRIJE PRVOG KLJUCA   pomak prve poze (uredjeno - original u tom kadru) se ulije kroz "in"
//                        kadrova, preko originalnog pokreta - ulaz iz snimljenog u uredjeno
//   IZMEDJU KLJUCEVA     cista interpolacija poza, SVI zglobovi: kljuc A -> kljuc B, glatko
//                        (smoothstep). Originalni pokret se ovdje ne vidi
//   IZA ZADNJEG KLJUCA   Return: pomak zadnje poze se drzi "hold" kadrova i iscuri kroz "out"
//                        natrag u pokret. Hold: zadnja poza stoji do kraja klipa
//
// Jedan kljuc s Return je tocno stari Smart Blend. Poza se zadaje u lokalnom sustavu zgloba, pa
// kljucevi ne ovise o tome gdje je lik u sceni.
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

inline Warp::Transform mixTransforms(const Warp::Transform& a, const Warp::Transform& b, float t){
    Warp::Transform out;
    out.translation = a.translation + (b.translation - a.translation) * t;
    out.scale = a.scale + (b.scale - a.scale) * t;
    glm::quat to = b.rotation;
    if(glm::dot(a.rotation, to) < 0.0f) to = -to;
    out.rotation = glm::normalize(glm::slerp(a.rotation, to, t));
    return out;
}

//Original + tezina * (uredjeno - original u kljucu): pomak se nosi preko pokreta koji tece
inline Warp::Transform addPoseOffset(const Warp::Transform& motion, const Warp::Transform& edited,
                                     const Warp::Transform& originalAtKey, float weight){
    Warp::Transform out = motion;
    out.translation += (edited.translation - originalAtKey.translation) * weight;
    out.scale += (edited.scale - originalAtKey.scale) * weight;
    const glm::quat delta = glm::normalize(edited.rotation * glm::inverse(originalAtKey.rotation));
    out.rotation = glm::normalize(glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), delta, weight) * motion.rotation);
    return out;
}

inline float easeInOut(float x){
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

//Stage mora u trenutku poziva nositi ORIGINALNI klip (vraceni prije primjene): iz njega se citaju
//pokret prije prvog i iza zadnjeg kljuca. Kljucevi ne moraju biti poredani. Vraca broj
//zapisanih kadrova po zglobu (za poruku i test)
inline size_t applyPoseKeys(Warp::Stage& stage, std::vector<PoseKey> keys, double clipStart, double clipEnd,
                            const PoseKeySettings& settings){
    if(keys.empty()) return 0;
    std::sort(keys.begin(), keys.end(), [](const PoseKey& a, const PoseKey& b){ return a.frame < b.frame; });
    const PoseKey& firstKey = keys.front();
    const PoseKey& lastKey = keys.back();

    //Cijeli plan se racuna PRIJE ijednog zapisa: setLocalAt mijenja klip, a pokret prije i poslije
    //kljuceva se mora citati iz originala
    struct Write{ Warp::Id id; double frame; Warp::Transform value; };
    std::vector<Write> writes;
    auto poseOf = [](const PoseKey& key, Warp::Id id, Warp::Transform& out){
        for(const auto& joint : key.pose) if(joint.first == id){ out = joint.second; return true; }
        return false;
    };

    for(const auto& joint : firstKey.pose){
        const Warp::Id id = joint.first;
        if(!stage.get(id)) continue;
        for(const PoseKey& key : keys){
            Warp::Transform value;
            if(poseOf(key, id, value)) writes.push_back({id, key.frame, value});
        }
        if(!settings.blendBetween) continue;

        //-- izmedju kljuceva: poza u pozu --------------------------------------------------------
        for(size_t k = 0; k + 1 < keys.size(); ++k){
            Warp::Transform a, b;
            if(!poseOf(keys[k], id, a) || !poseOf(keys[k + 1], id, b)) continue;
            const double span = keys[k + 1].frame - keys[k].frame;
            for(double t = std::floor(keys[k].frame) + 1.0; t < keys[k + 1].frame; t += 1.0)
                writes.push_back({id, t, mixTransforms(a, b, easeInOut(float((t - keys[k].frame) / span)))});
        }

        //-- ulaz: pomak prve poze preko pokreta --------------------------------------------------
        const Warp::Transform originalFirst = stage.localAt(id, firstKey.frame);
        const double inStart = std::max(clipStart, firstKey.frame - std::max(0.0, settings.inFrames));
        for(double t = std::ceil(inStart); t < firstKey.frame; t += 1.0){
            const float weight = easeInOut(float((t - inStart) / std::max(1e-6, firstKey.frame - inStart)));
            writes.push_back({id, t, addPoseOffset(stage.localAt(id, t), joint.second, originalFirst, weight)});
        }

        //-- izlaz ----------------------------------------------------------------------------------
        Warp::Transform lastPose;
        if(!poseOf(lastKey, id, lastPose)) continue;
        if(settings.ending == PoseKeyEnding::Hold){
            for(double t = std::floor(lastKey.frame) + 1.0; t <= clipEnd; t += 1.0) writes.push_back({id, t, lastPose});
            continue;
        }
        const Warp::Transform originalLast = stage.localAt(id, lastKey.frame);
        const double holdEnd = std::min(clipEnd, lastKey.frame + std::max(0.0, settings.holdFrames));
        const double outEnd = std::min(clipEnd, holdEnd + std::max(1.0, settings.outFrames));
        for(double t = std::floor(lastKey.frame) + 1.0; t <= outEnd; t += 1.0){
            const float weight = t <= holdEnd ? 1.0f : easeInOut(float((outEnd - t) / std::max(1e-6, outEnd - holdEnd)));
            writes.push_back({id, t, addPoseOffset(stage.localAt(id, t), lastPose, originalLast, weight)});
        }
    }
    for(const Write& write : writes) stage.setLocalAt(write.id, write.frame, write.value);
    return firstKey.pose.empty() ? 0 : writes.size() / firstKey.pose.size();
}

}
