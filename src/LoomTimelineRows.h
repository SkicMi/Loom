#pragma once
//=============================================================================================
// REDOVI TIMELINEA: sto se vidi ispod ravnala kad je odabran objekt s animacijom.
//
// ZASTO. Timeline je imao jednu traku s tri reda crtica od 2 px (P/R/S) i nista o liku: za take od
// 30 Hz to je bio zid crtica, a nije se vidjelo ni KAKO se lik mice ni GDJE je ispravak poze.
// Sad, odozgo prema dolje:
//   - sličice poza: mini figura lika u jednakim razmacima, pa se backflip, cucanj ili hod vide
//     na prvi pogled i odmah se zna koji dio odabrati za Replace
//   - red objekta: traka klipa (gusti kljucevi spojeni u traku) i ROMBOVI za prave kljuceve -
//     ispravke poze (slojeve) i kljuceve samog objekta
//   - otvoren objekt: red za svaku kost (ili P/R/S za objekt bez kostiju), s istim prikazom
// Sve dijeli jednu os s ravnalom, pa odabir raspona povlacenjem pokriva sve redove.
//
// Ovdje je samo logika bez crtanja (redovi, spajanje kljuceva, uzorci poza), da se da testirati.
//=============================================================================================
#include "Warp/Stage.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Loom{

//Kljucevi preblizu na ekranu postanu traka: take od 30 Hz je traka, a ne 266 crtica
struct TimelineKeySpan{
    float x0 = 0.0f, x1 = 0.0f;
    int keys = 0;
    bool bar() const { return keys > 1; }
};

//xs uzlazno (piksela). Kljucevi razmaknuti manje od minGap spoje se u jedan raspon
inline std::vector<TimelineKeySpan> mergeTimelineKeys(const std::vector<float>& xs, float minGap){
    std::vector<TimelineKeySpan> spans;
    for(float x : xs){
        if(!spans.empty() && x - spans.back().x1 < minGap){
            spans.back().x1 = std::max(spans.back().x1, x);
            ++spans.back().keys;
        }else{
            spans.push_back({x, x, 1});
        }
    }
    return spans;
}

inline void addKeyTimes(std::vector<double>& out, const std::vector<double>& times){
    out.insert(out.end(), times.begin(), times.end());
}
inline void sortKeyTimes(std::vector<double>& times){
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
}

//Jedan red ispod reda objekta: kost lika ili kanal (P/R/S) objekta bez kostiju
struct TimelineRow{
    Warp::Id target = Warp::None;   //kost; None za kanal objekta
    int channel = -1;               //0 P, 1 R, 2 S kad je red kanal objekta
    std::string label;
    int depth = 0;                  //uvlaka u hijerarhiji kostiju
    std::vector<double> keys;       //kljucevi klipa (gusti - crtaju se kao traka)
    std::vector<double> edits;      //kljucevi ispravaka (slojevi) - crtaju se kao rombovi
};

//Aktivni klip lika ili nullptr
inline const Warp::AnimationClip* activeTimelineClip(const Warp::Stage& stage, Warp::Id rig){
    const Warp::Entity* entity = stage.get(rig);
    if(!entity || !entity->animator || entity->animator->animations.empty()) return nullptr;
    const size_t index = std::min(entity->animator->activeAnimation, entity->animator->animations.size() - 1);
    return &entity->animator->animations[index];
}

//Kosti lika redom hijerarhije (dubinski), samo one koje klip ili sloj pokrece
inline std::vector<TimelineRow> timelineBoneRows(const Warp::Stage& stage, Warp::Id rig){
    std::vector<TimelineRow> rows;
    const Warp::AnimationClip* clip = activeTimelineClip(stage, rig);
    if(!clip) return rows;
    std::function<void(Warp::Id, int)> visit = [&](Warp::Id id, int depth){
        const Warp::Entity* entity = stage.get(id);
        if(!entity) return;
        TimelineRow row;
        row.target = id;
        row.label = entity->name;
        row.depth = depth;
        for(const Warp::AnimatorTrack& track : clip->tracks){
            if(track.target != id) continue;
            addKeyTimes(row.keys, track.translationKeys.times);
            addKeyTimes(row.keys, track.rotationKeys.times);
            addKeyTimes(row.keys, track.scaleKeys.times);
        }
        for(const Warp::AnimationLayer& layer : clip->layers)
            for(const Warp::AnimatorTrack& track : layer.keys)
                if(track.target == id) addKeyTimes(row.edits, track.rotationKeys.times);
        sortKeyTimes(row.keys);
        sortKeyTimes(row.edits);
        const bool driven = !row.keys.empty() || !row.edits.empty();
        if(driven) rows.push_back(std::move(row));
        for(Warp::Id child : entity->children) visit(child, driven ? depth + 1 : depth);
    };
    const Warp::Entity* root = stage.get(rig);
    for(Warp::Id child : root->children) visit(child, 0);
    return rows;
}

//Kanali objekta bez kostiju (kamera, model...): P, R, S kao redovi
inline std::vector<TimelineRow> timelineChannelRows(const Warp::Stage& stage, Warp::Id id){
    std::vector<TimelineRow> rows;
    const Warp::Entity* entity = stage.get(id);
    if(!entity) return rows;
    const Warp::AnimatorTrack* active = stage.activeAnimatorTrack(id);
    const std::vector<double>* times[3] = {
        active ? &active->translationKeys.times : &entity->translationKeys.times,
        active ? &active->rotationKeys.times : &entity->rotationKeys.times,
        active ? &active->scaleKeys.times : &entity->scaleKeys.times};
    const char* names[3] = {"Position", "Rotation", "Scale"};
    for(int channel = 0; channel < 3; ++channel){
        TimelineRow row;
        row.channel = channel;
        row.label = names[channel];
        row.edits = *times[channel];          //kljucevi objekta su rijetki i svaki je vazan: rombovi
        rows.push_back(std::move(row));
    }
    return rows;
}

//Red objekta: sve sto je ispod njega sazeto - traka klipa i rombovi ispravaka / kljuceva objekta
struct TimelineObjectRow{
    std::string label;
    bool hasClip = false;
    double clipStart = 0.0, clipEnd = 0.0;
    std::string clipName;
    std::vector<double> edits;
    int layers = 0;
};

inline TimelineObjectRow timelineObjectRow(const Warp::Stage& stage, Warp::Id id, Warp::Id rig){
    TimelineObjectRow row;
    const Warp::Entity* entity = stage.get(rig != Warp::None ? rig : id);
    if(!entity) return row;
    row.label = entity->name;
    if(const Warp::AnimationClip* clip = rig != Warp::None ? activeTimelineClip(stage, rig) : nullptr){
        row.hasClip = true;
        row.clipStart = clip->startFrame;
        row.clipEnd = clip->endFrame;
        row.clipName = clip->name;
        for(const Warp::AnimationLayer& layer : clip->layers){
            if(!layer.enabled) continue;
            ++row.layers;
            for(const Warp::AnimatorTrack& track : layer.keys) addKeyTimes(row.edits, track.rotationKeys.times);
        }
    }else{
        for(const TimelineRow& channel : timelineChannelRows(stage, id)) addKeyTimes(row.edits, channel.edits);
    }
    //Kljucevi samog lika (npr. postavljanje u scenu) su takodjer pravi kljucevi
    addKeyTimes(row.edits, entity->translationKeys.times);
    addKeyTimes(row.edits, entity->rotationKeys.times);
    addKeyTimes(row.edits, entity->scaleKeys.times);
    sortKeyTimes(row.edits);
    return row;
}

//=============================================================================================
// SLICICE POZA. Za svaki uzorak svjetski polozaj svake pokretane kosti; crta se kao stick figura
// (kost -> roditelj). Projekcija na ekran je tek pri crtanju (kamera se mijenja, uzorci ne).
//=============================================================================================
struct TimelinePoseStrip{
    //Kad se ovo promijeni, uzorci se racunaju iznova
    Warp::Id rig = Warp::None;
    uint64_t fingerprint = 0;
    std::vector<double> frames;

    std::vector<Warp::Id> joints;
    std::vector<int> parents;                       //indeks u joints ili -1
    std::vector<std::vector<glm::vec3>> positions;  //[uzorak][kost]
    int hips = 0;                                   //kost prema kojoj se figura centrira
    float floor = 0.0f, height = 1.0f;              //najniza tocka i visina figure preko svih uzoraka
};

//Jeftin otisak klipa: promijeni se kad se promijene kljucevi, slojevi ili njihove tezine
inline uint64_t timelineClipFingerprint(const Warp::Stage& stage, Warp::Id rig){
    const Warp::AnimationClip* clip = activeTimelineClip(stage, rig);
    if(!clip) return 0;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v){ h ^= v; h *= 1099511628211ull; };
    auto mixFloat = [&](double v){ int64_t bits = int64_t(std::llround(v * 1000.0)); mix(uint64_t(bits)); };
    mix(uint64_t(rig));
    mixFloat(clip->startFrame);
    mixFloat(clip->endFrame);
    mix(clip->tracks.size());
    for(const Warp::AnimatorTrack& track : clip->tracks){
        mix(track.rotationKeys.size());
        //Srednji kljuc svake kosti: uhvati prepisane vrijednosti (novi bake slojeva) bez citanja svih
        if(!track.rotationKeys.empty()){
            const glm::quat q = track.rotationKeys.values[track.rotationKeys.size() / 2];
            mixFloat(q.x); mixFloat(q.y); mixFloat(q.z);
        }
    }
    for(const Warp::AnimationLayer& layer : clip->layers){
        mix(layer.enabled);
        mixFloat(layer.weight);
        for(const Warp::AnimatorTrack& track : layer.keys) mix(track.rotationKeys.size());
    }
    return h;
}

inline void sampleTimelinePoses(const Warp::Stage& stage, Warp::Id rig, const std::vector<double>& frames,
                                TimelinePoseStrip& strip){
    strip = TimelinePoseStrip{};
    strip.rig = rig;
    strip.frames = frames;
    strip.fingerprint = timelineClipFingerprint(stage, rig);
    const Warp::AnimationClip* clip = activeTimelineClip(stage, rig);
    if(!clip) return;
    //Kosti redom hijerarhije: roditelj je uvijek prije djeteta, pa FK ide u jednom prolazu
    std::vector<Warp::Id> order;
    std::vector<int> parentOf;
    std::function<void(Warp::Id, int)> visit = [&](Warp::Id id, int parent){
        const Warp::Entity* entity = stage.get(id);
        if(!entity) return;
        int self = parent;
        if(entity->joint){
            self = int(order.size());
            order.push_back(id);
            parentOf.push_back(parent);
        }
        for(Warp::Id child : entity->children) visit(child, self);
    };
    const Warp::Entity* root = stage.get(rig);
    if(!root) return;
    for(Warp::Id child : root->children) visit(child, -1);
    if(order.empty()) return;
    strip.joints = order;
    strip.parents = parentOf;
    //Figura je tijelo: kukovi i sve ispod njih. IK kosti (ik_foot_root...), "interaction" i slicne
    //pomocne kosti vise uz korijen lika i na slicici su crtale krakove od poda do saka i stopala
    int hips = -1;
    for(size_t j = 0; j < order.size() && hips < 0; ++j){
        const std::string& name = stage.get(order[j])->name;
        if(name == "Hips" || name == "hips" || name == "pelvis" || name == "Pelvis") hips = int(j);
    }
    if(hips >= 0){
        std::vector<int> remap(order.size(), -1);
        std::vector<Warp::Id> body;
        std::vector<int> bodyParents;
        for(size_t j = 0; j < order.size(); ++j){
            const bool keep = int(j) == hips || (parentOf[j] >= 0 && remap[size_t(parentOf[j])] >= 0);
            if(!keep) continue;
            remap[j] = int(body.size());
            body.push_back(order[j]);
            bodyParents.push_back(int(j) == hips ? -1 : remap[size_t(parentOf[j])]);
        }
        order = body;
        parentOf = bodyParents;
        strip.joints = order;
        strip.parents = parentOf;
    }
    float low = 1e9f, high = -1e9f;
    for(double frame : frames){
        std::vector<glm::mat4> world(order.size());
        std::vector<glm::vec3> points(order.size());
        for(size_t j = 0; j < order.size(); ++j){
            //Kost ciji roditelj nije kost (prva ispod lika ili ispod grupe) uzima svijet od stagea
            world[j] = parentOf[j] < 0 ? stage.worldMatrix(order[j], frame)
                                       : world[size_t(parentOf[j])] * stage.localMatrix(order[j], frame);
            points[j] = glm::vec3(world[j][3]);
            low = std::min(low, points[j].y);
            high = std::max(high, points[j].y);
        }
        strip.positions.push_back(std::move(points));
    }
    if(high > low){ strip.floor = low; strip.height = high - low; }
}

//Uzorci u jednakim razmacima kroz klip, po jedan na celiju sirine cell piksela na osi timelinea
//(xOf/frameAt su os timelinea). Samo celije unutar klipa
inline std::vector<double> timelinePoseFrames(double clipStart, double clipEnd, float left, float right, float cell,
                                              const std::function<double(float)>& frameAtX){
    std::vector<double> frames;
    if(cell < 4.0f || right <= left) return frames;
    for(float x = left + cell * 0.5f; x < right; x += cell){
        const double f = frameAtX(x);
        if(f < clipStart - 1e-6 || f > clipEnd + 1e-6) continue;
        frames.push_back(f);
    }
    return frames;
}

}
