#pragma once
//=============================================================================================
// SLOJEVI ANIMACIJE: ispravci poze kao zasebni slojevi preko netaknutog pokreta.
//
// ZASTO. Svaki ispravak (pose blend, i prije Smart Blend) je prepisivao klip: drugi ispravak se
// radio na vec ispravljenom, a da se prvi makne, trebalo je sve iznova. Sada ispravak postane
// SLOJ (Warp::AnimationLayer): ime, ukljucen, tezina, kljucne poze i postavke ulaza i izlaza.
// Klip cuva netaknutu osnovu (baseTracks), a tracks - ono sto se reproducira i crta - se ponovno
// izracuna iz osnove i ukljucenih slojeva, redom, svaki sa svojom tezinom.
//
//   UKLJUCI/ISKLJUCI   sloj se preskoci; klip bez njega je bit po bit osnova (test)
//   TEZINA             0 je osnova, 1 puni ispravak; izmedju je ispravak razmjerno
//   BRISANJE           sloj nestaje; kad ne ostane nijedan, klip je opet osnova i baseTracks se isprazni
//
// KLJUCEVI SU APSOLUTNE POZE (kako ih je umjetnik postavio), ne razlike: sloj u svom kljucu uvijek
// da tocno tu pozu. Ispravak prema onome ispod racuna se pri svakom izracunu (LoomPoseBlend.h).
// Slojevi koji se u vremenu ne preklapaju zato su potpuno neovisni; kad se preklapaju, gornji u
// svojim kljucevima ima zadnju rijec.
//=============================================================================================
#include "LoomPoseBlend.h"

#include <Warp/Stage.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Loom{

//Kljucne poze sloja: kadrovi su vremena kljuceva njegovih trackova
inline std::vector<PoseKey> poseKeysOf(const Warp::AnimationLayer& layer){
    std::vector<double> frames;
    for(const Warp::AnimatorTrack& track : layer.keys)
        for(double t : track.rotationKeys.times) frames.push_back(t);
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    std::vector<PoseKey> keys;
    for(double frame : frames){
        PoseKey key{frame, {}};
        for(const Warp::AnimatorTrack& track : layer.keys){
            if(track.target == Warp::None) continue;
            auto at = [&](const auto& t, auto fallback){
                for(size_t i = 0; i < t.times.size(); ++i) if(t.times[i] == frame) return t.values[i];
                return fallback;
            };
            Warp::Transform value;
            value.translation = at(track.translationKeys, glm::vec3(0.0f));
            value.rotation = at(track.rotationKeys, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            value.scale = at(track.scaleKeys, glm::vec3(1.0f));
            key.pose.emplace_back(track.target, value);
        }
        keys.push_back(std::move(key));
    }
    return keys;
}

inline Warp::AnimationLayer layerFromPoseKeys(const Warp::Stage& stage, const std::string& name,
                                              const std::vector<PoseKey>& keys, const PoseKeySettings& settings){
    Warp::AnimationLayer layer;
    layer.name = name;
    layer.weight = settings.weight;
    layer.inFrames = settings.inFrames;
    layer.holdFrames = settings.holdFrames;
    layer.outFrames = settings.outFrames;
    layer.holdToEnd = settings.ending == PoseKeyEnding::Hold;
    layer.blendBetween = settings.blendBetween;
    for(const PoseKey& key : keys){
        for(const auto& [id, value] : key.pose){
            auto track = std::find_if(layer.keys.begin(), layer.keys.end(),
                                      [&](const Warp::AnimatorTrack& t){ return t.target == id; });
            if(track == layer.keys.end()){
                Warp::AnimatorTrack created;
                created.target = id;
                created.targetPath = stage.contains(id) ? stage.path(id) : std::string();
                layer.keys.push_back(created);
                track = layer.keys.end() - 1;
            }
            track->translationKeys.set(key.frame, value.translation);
            track->rotationKeys.set(key.frame, value.rotation);
            track->scaleKeys.set(key.frame, value.scale);
        }
    }
    return layer;
}

inline PoseKeySettings settingsOf(const Warp::AnimationLayer& layer, const std::vector<PoseLimb>& limbs){
    PoseKeySettings settings;
    settings.limbs = limbs;
    settings.weight = layer.weight;
    settings.inFrames = layer.inFrames;
    settings.holdFrames = layer.holdFrames;
    settings.outFrames = layer.outFrames;
    settings.ending = layer.holdToEnd ? PoseKeyEnding::Hold : PoseKeyEnding::Return;
    settings.blendBetween = layer.blendBetween;
    return settings;
}

//Ponovno izracuna tracks klipa iz osnove i ukljucenih slojeva. Poziva se nakon svake promjene
//slojeva (dodan, ukljucen, tezina, obrisan). Vraca broj primijenjenih slojeva
inline size_t bakeAnimationLayers(Warp::Stage& stage, Warp::Id rig, size_t clipIndex, const std::vector<PoseLimb>& limbs){
    Warp::Entity* entity = stage.get(rig);
    if(!entity || !entity->animator || clipIndex >= entity->animator->animations.size()) return 0;
    Warp::Animator& animator = *entity->animator;
    Warp::AnimationClip& clip = animator.animations[clipIndex];
    if(clip.layers.empty()){
        //Zadnji sloj je obrisan: klip je opet samo osnova
        if(!clip.baseTracks.empty()){
            clip.tracks = clip.baseTracks;
            clip.baseTracks.clear();
        }
        return 0;
    }
    if(clip.baseTracks.empty()) clip.baseTracks = clip.tracks;
    //setLocalAt i localAt rade nad AKTIVNIM klipom; privremeno je aktivan ovaj
    const size_t active = animator.activeAnimation;
    animator.activeAnimation = clipIndex;
    const double start = clip.startFrame, end = clip.endFrame;
    const std::vector<Warp::AnimationLayer> layers = clip.layers;
    clip.tracks = clip.baseTracks;
    size_t applied = 0;
    for(const Warp::AnimationLayer& layer : layers){
        if(!layer.enabled || layer.weight <= 0.0f || layer.keys.empty()) continue;
        applyPoseKeys(stage, poseKeysOf(layer), start, end, settingsOf(layer, limbs));
        ++applied;
    }
    //setLocalAt smije produljiti raspon klipa kad kljuc padne izvan njega; slojevi ne smiju
    Warp::AnimationClip& after = stage.get(rig)->animator->animations[clipIndex];
    after.startFrame = start;
    after.endFrame = end;
    stage.get(rig)->animator->activeAnimation = active;
    return applied;
}

}
