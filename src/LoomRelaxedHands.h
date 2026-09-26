#pragma once
//=============================================================================================
// MIRNE SAKE NA SVAKOM RIGU: prsti u mirnoj pozi blago savijeni prema dlanu, kao opustena ruka.
//
// UniRig-52 ima svoj preset (applyUniRigRelaxedRestPose: ramena, laktovi, zapesca i prsti). Svaki
// drugi rig (Manny iz auto-riga, Mixamo, imenovani rigovi) dobije samo prste, nadjene iz stabla
// kao za hvat (handFingersFrom): saka je kost s "hand" u imenu i barem tri lanca prstiju, a rig bez
// imena - kost s cetiri ili vise lanaca. Svaki zglob se okrene oko svoje osi savijanja (smjer kosti
// x normala dlana), pa rig ne mora imati nikakvu konvenciju osi.
//
// Mijenja se MIRNA POZA (lokalne rotacije prstiju), a postojeci klipovi, osnove i slojevi se
// okrenu za isto - pokret ostaje isti, samo prsti vise nisu kruti. Retarget (retargetMotionToRig)
// racuna prste relativno prema vidljivoj mirnoj pozi, pa Kimodo pokret s konstantnim kanalima
// prstiju zadrzi opustene prste.
//=============================================================================================
#include "LoomHandPose.h"
#include "LoomMotionRetarget.h"

#include <unordered_map>

namespace Loom{

//Savijanje u stupnjevima (baza, srednja, vrh): palac malo, ostali sve vise prema malom prstu
inline constexpr float relaxedHandCurl[5][3] = {{5, 10, 6}, {12, 18, 10}, {15, 21, 12}, {18, 24, 14}, {21, 27, 16}};

//Sake riga u mirnoj pozi: imenovane "hand" kosti s prstima, a bez njih kosti s 4+ lanca
inline std::vector<HandFingers> restHandsOf(const Warp::Stage& stage, const MotionRigRestPose& rest){
    std::unordered_map<Warp::Id, glm::vec3> positions;
    for(const MotionRigJointRest& joint : rest.joints) positions[joint.id] = glm::vec3(joint.matrix[3]);
    const auto positionOf = [&](Warp::Id id){
        const auto found = positions.find(id);
        return found == positions.end() ? glm::vec3(0.0f) : found->second;
    };
    auto chainsUnder = [&](Warp::Id id){
        size_t chains = 0;
        if(const Warp::Entity* entity = stage.get(id))
            for(Warp::Id child : entity->children){
                const Warp::Entity* c = stage.get(child);
                if(!c || !c->joint) continue;
                bool longer = false;
                for(Warp::Id grandchild : c->children) if(stage.get(grandchild) && stage.get(grandchild)->joint) longer = true;
                chains += longer ? 1 : 0;
            }
        return chains;
    };
    std::vector<HandFingers> named, structural;
    for(const MotionRigJointRest& joint : rest.joints){
        const size_t chains = chainsUnder(joint.id);
        if(chains < 3) continue;
        const bool isHand = motionJointKey(joint.name).find("hand") != std::string::npos;
        if(!isHand && chains < 4) continue;
        HandFingers hand = handFingersFrom(stage, joint.id, positionOf);
        if(!hand.valid() || hand.fingers[1].empty()) continue;
        (isHand ? named : structural).push_back(std::move(hand));
    }
    return named.empty() ? structural : named;
}

//Primijeni mirne sake. UniRig-52 ide svojim presetom; ostali rigovi samo prste. Vraca true kad su
//sake opustene (i kad vec jesu); changed = broj okrenutih zglobova
inline bool applyRelaxedHandsRestPose(Warp::Stage& stage, Warp::Id rigRoot, size_t* changed = nullptr){
    if(changed) *changed = 0;
    const MotionRigRestPose rest = motionRigRestPose(stage, rigRoot);
    std::array<Warp::Id, 52> uniRig;
    if(motionFindVerifiedUniRig52(rest, uniRig)) return applyUniRigRelaxedRestPose(stage, rigRoot, changed);
    if(!ensureRigAnimator(stage, rigRoot)) return false;
    if(stage.get(rigRoot)->animator->relaxedHands) return true;
    const std::vector<HandFingers> hands = restHandsOf(stage, rest);
    if(hands.empty()) return false;
    std::unordered_map<Warp::Id, glm::quat> restWorld;
    for(const MotionRigJointRest& joint : rest.joints) restWorld[joint.id] = motionRotationOf(joint.matrix);
    std::unordered_map<Warp::Id, glm::vec3> positions;
    for(const MotionRigJointRest& joint : rest.joints) positions[joint.id] = glm::vec3(joint.matrix[3]);

    //Okret svakog zgloba u njegovom lokalnom sustavu (desno mnozenje, kao handPoseAt): sve iz mirne
    //poze prije okretanja, pa redoslijed ne mijenja rezultat
    std::unordered_map<Warp::Id, glm::quat> turns;
    for(const HandFingers& hand : hands)
        for(size_t f = 0; f < 5; ++f){
            const std::vector<Warp::Id>& finger = hand.fingers[f];
            for(size_t j = 0; j < finger.size() && j < 3; ++j){
                const glm::vec3 from = positions[finger[j]];
                const glm::vec3 to = j + 1 < finger.size() ? positions[finger[j + 1]]
                                                          : from + (from - positions[finger[j > 0 ? j - 1 : 0]]);
                const glm::vec3 axis = glm::cross(to - from, hand.palmNormal);
                if(glm::length(axis) < 1e-8f) continue;
                const glm::vec3 localAxis = glm::normalize(glm::inverse(restWorld[finger[j]]) * glm::normalize(axis));
                turns[finger[j]] = glm::angleAxis(glm::radians(relaxedHandCurl[f][j]), localAxis);
            }
        }
    Warp::Entity* rig = stage.get(rigRoot);
    auto rebase = [&](std::vector<Warp::AnimatorTrack>& tracks){
        for(Warp::AnimatorTrack& track : tracks){
            const auto turn = turns.find(track.target);
            if(turn == turns.end()) continue;
            for(glm::quat& value : track.rotationKeys.values) value = glm::normalize(value * turn->second);
        }
    };
    for(const auto& [id, turn] : turns){
        Warp::Entity* entity = stage.get(id);
        if(!entity) continue;
        entity->local.rotation = glm::normalize(entity->local.rotation * turn);
        for(glm::quat& value : entity->rotationKeys.values) value = glm::normalize(value * turn);
    }
    for(Warp::AnimationClip& clip : rig->animator->animations){
        rebase(clip.tracks);
        rebase(clip.baseTracks);
        for(Warp::AnimationLayer& layer : clip.layers) rebase(layer.keys);
    }
    rig->animator->relaxedHands = !turns.empty();
    if(changed) *changed = turns.size();
    return !turns.empty();
}

inline bool relaxedHandsApplied(const Warp::Stage& stage, Warp::Id rigRoot){
    const Warp::Entity* rig = stage.get(rigRoot);
    return rig && rig->animator && (rig->animator->relaxedHands || rig->animator->relaxedUniRigPose);
}

}
