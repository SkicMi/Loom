#pragma once
//=============================================================================================
// HVAT PREDMETA: predmet se dovuce do sake lika i pusti - saka ga uhvati.
//
// TOK. Dok se predmet vuce gizmom, najbliza saka unutar dosega zasvijetli (nearestHoldHand). Kad se
// mis pusti uz osvijetljenu saku, nastane hvat (grabItem): od tog kadra predmet prati kost sake s
// pomakom izmjerenim u trenutku hvata, pa ostaje tocno gdje je bio kad je pusten - ne skoci u dlan.
// Na timelineu je hvat traka od On do Off; iza Off predmet ostaje gdje ga je saka pustila.
//
// Warp::Hold cuva podatke i primijeni ih u Stage::worldMatrix, pa hvat vide pogled, render,
// spremanje i undo bez ijednog posebnog puta. Ovdje je samo odluka: koja saka, kada, s kojim pomakom.
//=============================================================================================
#include "LoomHandPose.h"
#include "Warp/Stage.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace Loom{

//Saka lika koja moze drzati predmet. Dlan je izmedju zgloba sake i vrha srednjeg prsta
struct HoldHand{
    Warp::Id rig = Warp::None;
    Warp::Id hand = Warp::None;         //kost na koju se predmet veze
    Warp::Id middleEnd = Warp::None;    //vrh srednjeg prsta (ili None) - za tocku dlana
    bool right = false;
    Warp::Id forearm = Warp::None;      //za doseg: pola podlaktice, jer scena ne mora biti u metrima
};

//Doseg hvata: pola duljine podlaktice (lik od 1.8 m: ~13 cm), a bez podlaktice fallback
inline float holdReach(const Warp::Stage& stage, const HoldHand& hand, double frame, float fallback){
    if(hand.forearm == Warp::None || !stage.get(hand.forearm)) return fallback;
    const float forearm = glm::length(glm::vec3(stage.worldMatrix(hand.hand, frame)[3]) -
                                      glm::vec3(stage.worldMatrix(hand.forearm, frame)[3]));
    return forearm > 1e-6f ? forearm * 0.5f : fallback;
}

//TOCKA DLANA gdje lezi drska: ~70 % puta od zapesca prema zglobovima prstiju (kaziprst..mali iz
//stabla, handFingersOf). Rig bez prstiju: 45 % prema vrhu srednjeg prsta, a bez njega zapesce
inline glm::vec3 holdPalmPoint(const Warp::Stage& stage, const HoldHand& hand, double frame){
    const glm::vec3 wrist(stage.worldMatrix(hand.hand, frame)[3]);
    const HandFingers fingers = handFingersOf(stage, hand.hand, frame);
    glm::vec3 knuckles(0.0f);
    int count = 0;
    for(int f = 1; f < 5; ++f)
        if(!fingers.fingers[size_t(f)].empty()){ knuckles += glm::vec3(stage.worldMatrix(fingers.fingers[size_t(f)].front(), frame)[3]); ++count; }
    if(count >= 2) return wrist + (knuckles / float(count) - wrist) * 0.7f;
    if(hand.middleEnd == Warp::None || !stage.get(hand.middleEnd)) return wrist;
    const glm::vec3 tip(stage.worldMatrix(hand.middleEnd, frame)[3]);
    return wrist + (tip - wrist) * 0.45f;
}

inline std::string holdHandLabel(const HoldHand& hand){ return hand.right ? "right hand" : "left hand"; }

//Najbliza saka kojoj je tocka unutar dosega (holdReach, a bez podlaktice radius); -1 kad nijedna.
//Blizina se mjeri relativno prema dosegu, pa veca saka ne "krade" predmet manjoj
inline int nearestHoldHand(const Warp::Stage& stage, const std::vector<HoldHand>& hands, Warp::Id item,
                           const glm::vec3& point, double frame, float radius){
    int best = -1;
    float bestRatio = 1.0f;
    for(size_t i = 0; i < hands.size(); ++i){
        if(!stage.canHold(item, hands[i].hand)) continue;
        const float reach = holdReach(stage, hands[i], frame, radius);
        const float ratio = glm::length(holdPalmPoint(stage, hands[i], frame) - point) / std::max(reach, 1e-6f);
        if(ratio <= bestRatio){ bestRatio = ratio; best = int(i); }
    }
    return best;
}

//ISTO NA EKRANU: vuce se gizmom po jednoj osi, pa se dubina tesko pogodi - predmet je "uz saku"
//kad je uz nju onako kako ga korisnik vidi. Doseg je doseg sake projiciran, a najmanje minPixels.
//project: svijet -> piksel pogleda (false iza kamere)
inline int nearestHoldHandOnScreen(const Warp::Stage& stage, const std::vector<HoldHand>& hands, Warp::Id item,
                                   const glm::vec3& point, double frame, float minPixels,
                                   const std::function<bool(const glm::vec3&, glm::vec2&)>& project){
    glm::vec2 itemPixel;
    if(!project(point, itemPixel)) return -1;
    int best = -1;
    float bestRatio = 1.0f;
    for(size_t i = 0; i < hands.size(); ++i){
        if(!stage.canHold(item, hands[i].hand)) continue;
        const glm::vec3 palm = holdPalmPoint(stage, hands[i], frame);
        glm::vec2 palmPixel, edgePixel;
        if(!project(palm, palmPixel)) continue;
        const glm::vec3 wrist(stage.worldMatrix(hands[i].hand, frame)[3]);
        float reachPixels = minPixels;
        if(project(palm + glm::vec3(0.0f, holdReach(stage, hands[i], frame, glm::length(palm - wrist)), 0.0f), edgePixel))
            reachPixels = std::max(minPixels, glm::length(edgePixel - palmPixel));
        const float ratio = glm::length(palmPixel - itemPixel) / reachPixels;
        if(ratio <= bestRatio){ bestRatio = ratio; best = int(i); }
    }
    return best;
}

//POGLED NA SAKU: smjer od dlana prema kameri s kojeg tijelo ne zaklanja saku. Prvi pogled (sa strane
//palca) je u A-pozi, sa sakom uz bedro, gledao kroz nogu. Isproba se 12 smjerova oko sake na tri
//visine; mjera je najmanji razmak crte pogleda (bez zadnjih 15 % uz dlan) od kostiju lika bez same sake
//i podlaktice. Najslobodniji pobijedi, uz blagu prednost za `preferred`
inline glm::vec3 holdViewDirection(const Warp::Stage& stage, const HoldHand& hand, double frame, float distance,
                                   const glm::vec3& preferred){
    const glm::vec3 palm = holdPalmPoint(stage, hand, frame);
    //Kosti tijela kao odsjecci zglob - roditelj; saka i sve ispod nje, i podlaktica, ne racunaju se
    auto underHand = [&](Warp::Id id){
        for(Warp::Id walk = id; walk != Warp::None;){
            if(walk == hand.hand || walk == hand.forearm) return true;
            const Warp::Entity* e = stage.get(walk);
            walk = e ? e->parent : Warp::None;
        }
        return false;
    };
    std::vector<std::pair<glm::vec3, glm::vec3>> bones;
    stage.walk([&](const Warp::Entity& e, int){
        if(!e.joint || underHand(e.id)) return;
        const Warp::Entity* parent = stage.get(e.parent);
        if(!parent || !parent->joint) return;
        if(hand.rig != Warp::None){
            bool inRig = false;
            for(Warp::Id walk = e.id; walk != Warp::None && !inRig;){ inRig = walk == hand.rig; const Warp::Entity* w = stage.get(walk); walk = w ? w->parent : Warp::None; }
            if(!inRig) return;
        }
        bones.push_back({glm::vec3(stage.worldMatrix(e.id, frame)[3]), glm::vec3(stage.worldMatrix(e.parent, frame)[3])});
    });
    auto pointSegment = [](const glm::vec3& p, const glm::vec3& a, const glm::vec3& b){
        const glm::vec3 ab = b - a;
        const float t = glm::dot(ab, ab) > 1e-12f ? std::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
        return glm::length(p - (a + ab * t));
    };
    const glm::vec3 want = glm::length(preferred) > 1e-6f ? glm::normalize(preferred) : glm::vec3(0.0f, 0.3f, 1.0f);
    glm::vec3 best = want;
    float bestScore = -1e9f;
    for(const float pitch : {0.15f, 0.45f, -0.15f})
        for(int k = 0; k < 12; ++k){
            const float yaw = float(k) / 12.0f * 6.2831853f;
            const glm::vec3 dir = glm::normalize(glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)));
            float clearance = 1e9f;
            for(int i = 0; i <= 10; ++i){
                const glm::vec3 p = palm + dir * distance * (0.15f + 0.85f * float(i) / 10.0f);
                for(const auto& [a, b] : bones) clearance = std::min(clearance, pointSegment(p, a, b));
            }
            //Razmak preko ~pola udaljenosti je dovoljan: dalje odlucuje prednost smjera
            const float score = std::min(clearance, 0.5f * distance) + 0.05f * distance * glm::dot(dir, want);
            if(score > bestScore){ bestScore = score; best = dir; }
        }
    return best;
}

//Hvat koji drzi predmet u ovom kadru (on <= kadar <= off), ili -1
inline int holdAt(const Warp::Entity& item, double frame){
    for(size_t i = 0; i < item.holds.size(); ++i)
        if(item.holds[i].onFrame <= frame + 1e-9 && frame <= item.holds[i].offFrame + 1e-9) return int(i);
    return -1;
}

//HVAT od frame do endFrame (ili do sljedeceg hvata). Pomak se izmjeri sada, pa predmet ostaje gdje
//je. Hvat koji je u ovom kadru vec trajao zavrsi kadar prije (prebacivanje iz ruke u ruku)
//palm: kad je zadan, predmet sjedne ishodistem u tu tocku (u dlan) i zadrzi okret - predmet dovucen
//"uz saku na ekranu" moze biti metar ispred nje po dubini
//itemWorld: gdje predmet mora biti u kadru hvata (npr. drska poravnata u dlan, LoomTool.h)
inline bool grabItemAt(Warp::Stage& stage, Warp::Id item, Warp::Id hand, double frame, double endFrame,
                       const glm::mat4& itemWorld);

inline bool grabItem(Warp::Stage& stage, Warp::Id item, Warp::Id hand, double frame, double endFrame,
                     const glm::vec3* palm = nullptr){
    if(!stage.canHold(item, hand)) return false;
    glm::mat4 itemWorld = stage.worldMatrix(item, frame);
    if(palm) itemWorld[3] = glm::vec4(*palm, 1.0f);
    return grabItemAt(stage, item, hand, frame, endFrame, itemWorld);
}

inline bool grabItemAt(Warp::Stage& stage, Warp::Id item, Warp::Id hand, double frame, double endFrame,
                       const glm::mat4& itemWorld){
    if(!stage.canHold(item, hand)) return false;
    const glm::mat4 handWorld = stage.worldMatrix(hand, frame);
    Warp::Entity* entity = stage.get(item);
    std::vector<Warp::Hold>& holds = entity->holds;
    Warp::Hold hold;
    hold.hand = hand;
    hold.handPath = stage.path(hand);
    hold.onFrame = frame;
    hold.offFrame = std::max(frame, endFrame);
    hold.offset = glm::inverse(handWorld) * itemWorld;
    //Hvatovi se ne preklapaju: onaj koji traje ovdje zavrsi kadar prije, onaj istog kadra se zamijeni,
    //a novi zavrsi prije sljedeceg
    holds.erase(std::remove_if(holds.begin(), holds.end(), [&](const Warp::Hold& h){ return h.onFrame == frame; }), holds.end());
    for(Warp::Hold& h : holds){
        if(h.onFrame < frame && h.offFrame >= frame) h.offFrame = frame - 1.0;
        if(h.onFrame > frame) hold.offFrame = std::min(hold.offFrame, h.onFrame - 1.0);
    }
    holds.push_back(hold);
    std::sort(holds.begin(), holds.end(), [](const Warp::Hold& a, const Warp::Hold& b){ return a.onFrame < b.onFrame; });
    return true;
}

//PUSTI u ovom kadru: hvat koji traje zavrsi ovdje. Predmet ostaje gdje ga je saka pustila
inline bool releaseItem(Warp::Stage& stage, Warp::Id item, double frame){
    Warp::Entity* entity = stage.get(item);
    if(!entity) return false;
    const int index = holdAt(*entity, frame);
    if(index < 0) return false;
    entity->holds[size_t(index)].offFrame = std::max(entity->holds[size_t(index)].onFrame, frame);
    return true;
}

//Rub hvata na timelineu: On (edge 0) ili Off (edge 1) na novi kadar, bez prelaska preko drugog ruba
//ili susjednog hvata. Pomak predmeta u sakama ostaje isti
inline void moveHoldEdge(Warp::Entity& item, size_t index, int edge, double frame){
    if(index >= item.holds.size()) return;
    Warp::Hold& hold = item.holds[index];
    const double low = index > 0 ? item.holds[index - 1].offFrame + 1.0 : -1e18;
    const double high = index + 1 < item.holds.size() ? item.holds[index + 1].onFrame - 1.0 : 1e18;
    frame = std::round(frame);
    if(edge == 0) hold.onFrame = std::clamp(frame, low, hold.offFrame);
    else hold.offFrame = std::clamp(frame, hold.onFrame, high);
}

//Predmet u ruci pomaknut rukom korisnika (gizmo): novi svijet predmeta postane novi pomak u saci,
//a ne kljuc koji hvat ionako nadjacava
inline bool moveHeldItem(Warp::Stage& stage, Warp::Id item, double frame, const glm::mat4& desiredWorld){
    Warp::Entity* entity = stage.get(item);
    if(!entity) return false;
    const int index = holdAt(*entity, frame);
    if(index < 0) return false;
    Warp::Hold& hold = entity->holds[size_t(index)];
    if(!stage.get(hold.hand)) return false;
    hold.offset = glm::inverse(stage.worldMatrix(hold.hand, frame)) * desiredWorld;
    return true;
}

}
