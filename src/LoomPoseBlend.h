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

//Ruka (ili noga) kojoj se pretapa POLOZAJ SAKE u sustavu reference (prsa), a zglobovi se u svakom
//kadru rijese IK-om. Vidi applyPoseKeys, odjeljak UDOVI
struct PoseLimb{
    Warp::Id upper = Warp::None, middle = Warp::None, end = Warp::None;
    Warp::Id reference = Warp::None;    //predak u cijem se sustavu sake drze (Chest)
};

struct PoseKeySettings{
    std::vector<PoseLimb> limbs;
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

namespace detail{

inline glm::quat rotationOfMatrix(const glm::mat4& m){
    glm::mat3 r(m);
    for(int c = 0; c < 3; ++c){
        const float length = glm::length(r[c]);
        if(length > 1e-8f) r[c] /= length;
    }
    return glm::normalize(glm::quat_cast(r));
}

inline glm::quat rotationBetween(const glm::vec3& from, const glm::vec3& to){
    const float fromLength2 = glm::dot(from, from), toLength2 = glm::dot(to, to);
    if(fromLength2 < 1e-12f || toLength2 < 1e-12f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 a = from / std::sqrt(fromLength2), b = to / std::sqrt(toLength2);
    const float cosine = std::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if(cosine < -0.9999f){
        glm::vec3 axis = glm::cross(a, std::fabs(a.x) < 0.8f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
        return glm::angleAxis(3.14159265359f, glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(a, b);
    return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
}

inline glm::quat slerpShort(const glm::quat& a, glm::quat b, float t){
    if(glm::dot(a, b) < 0.0f) b = -b;
    return glm::normalize(glm::slerp(a, b, t));
}

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

    auto keyedLocal = [&](const PoseKey& key, Warp::Id id){
        for(const auto& joint : key.pose) if(joint.first == id) return joint.second;
        return stage.localAt(id, key.frame);
    };

    //-- ispravak po zglobu u svakom kljucu ---------------------------------------------------------
    struct JointPlan{ std::vector<PoseCorrection> perKey; bool touched = false; };
    std::vector<std::pair<Warp::Id, JointPlan>> plans;
    auto planOf = [&](Warp::Id id) -> const JointPlan*{
        for(const auto& p : plans) if(p.first == id) return &p.second;
        return nullptr;
    };
    for(Warp::Id id : joints){
        if(!stage.get(id)) continue;
        JointPlan plan;
        for(const PoseKey& key : keys){
            const PoseCorrection c = correctionOf(stage.localAt(id, key.frame), keyedLocal(key, id));
            plan.touched = plan.touched || !correctionIsZero(c);
            plan.perKey.push_back(c);
        }
        plans.emplace_back(id, plan);
    }

    //Gdje je kadar t: segment izmedju kljuceva (k, s) ili ulaz/izlaz s tezinom. Ista funkcija
    //vodi i obicne zglobove i udove, pa oba dijela uvijek znaju isti raspored
    struct Phase{ size_t from = 0, to = 0; float s = 0.0f, weight = 1.0f; };
    auto phaseAt = [&](double t) -> Phase{
        Phase p;
        if(t <= first){
            const double inStart = std::max(clipStart, first - std::max(0.0, settings.inFrames));
            p.weight = t >= first ? 1.0f : easeInOut(float((t - inStart) / std::max(1e-6, first - inStart)));
            return p;
        }
        if(t >= last){
            p.from = p.to = keys.size() - 1;
            if(settings.ending == PoseKeyEnding::Hold) return p;
            const double holdEnd = std::min(clipEnd, last + std::max(0.0, settings.holdFrames));
            const double outEnd = std::min(clipEnd, holdEnd + std::max(1.0, settings.outFrames));
            p.weight = t <= holdEnd ? 1.0f : easeInOut(float((outEnd - t) / std::max(1e-6, outEnd - holdEnd)));
            return p;
        }
        size_t k = 0;
        while(k + 1 < keys.size() && keys[k + 1].frame <= t) ++k;
        p.from = k;
        p.to = k + 1;
        p.s = easeInOut(float((t - keys[k].frame) / (keys[k + 1].frame - keys[k].frame)));
        return p;
    };
    auto correctionAt = [&](const JointPlan& plan, const Phase& p){
        return mixCorrections(plan.perKey[p.from], plan.perKey[p.to], p.s);
    };
    auto isKey = [&](double t){
        return std::any_of(keys.begin(), keys.end(), [&](const PoseKey& k){ return k.frame == t; });
    };

    const double from = settings.blendBetween ? std::max(clipStart, first - std::max(0.0, settings.inFrames)) : first;
    const double to = !settings.blendBetween ? last : settings.ending == PoseKeyEnding::Hold ? clipEnd
                    : std::min(clipEnd, last + std::max(0.0, settings.holdFrames) + std::max(1.0, settings.outFrames));

    //ISPRAVAK IDE NA VREMENA KLJUCEVA KOJA ZGLOB VEC IMA. Kimodov klip je 30 Hz, a scena
    //obicno 25: kljucevi stoje na 1, 1.83, 2.67... Prva verzija je pisala samo cijele kadrove,
    //pa su izmedju njih ostali originalni kljucevi i ruka je u reprodukciji skakala izmedju
    //ispravljene i krive poze - glitch koji je korisnik vidio. Zglob bez kljuceva dobije cijele
    auto framesFor = [&](std::initializer_list<Warp::Id> ids){
        std::vector<double> frames;
        auto collect = [&](const auto& track){
            for(double t : track.times) if(t >= from - 1e-9 && t <= to + 1e-9) frames.push_back(t);
        };
        for(Warp::Id id : ids){
            if(const Warp::AnimatorTrack* track = stage.activeAnimatorTrack(id)){
                collect(track->translationKeys); collect(track->rotationKeys); collect(track->scaleKeys);
            }else if(const Warp::Entity* entity = stage.get(id)){
                collect(entity->translationKeys); collect(entity->rotationKeys); collect(entity->scaleKeys);
            }
        }
        if(frames.empty()) for(double t = std::ceil(from); t <= to; t += 1.0) frames.push_back(t);
        for(const PoseKey& key : keys) frames.push_back(key.frame);
        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
        if(!settings.blendBetween) frames.erase(std::remove_if(frames.begin(), frames.end(),
                                                               [&](double t){ return !isKey(t); }), frames.end());
        return frames;
    };

    //Planirana lokalna vrijednost obicnog zgloba u kadru t (original + ispravak)
    auto plannedLocal = [&](Warp::Id id, double t){
        const Warp::Transform motion = stage.localAt(id, t);
        const JointPlan* plan = planOf(id);
        if(!plan || !plan->touched) return motion;
        const Phase p = phaseAt(t);
        return applyCorrection(motion, correctionAt(*plan, p), isKey(t) ? 1.0f : p.weight);
    };

    //-- UDOVI: polozaj sake u sustavu prsa, pa IK ----------------------------------------------------
    //
    //Aditivni ispravak kutova je tocan samo u kljucu. Izmedju kljuceva ispod ruke tece pokret
    //ramena i trupa, pa isti kutovi daju saku malo drugdje - korisnik je vidio da se ruka oko
    //kljuca "udalji" i da se razmak medju sakama (dvije ruke na pistolju) mijenja. Umjetnik je
    //postavio POLOZAJ SAKE, pa se pretapa bas on: saka i lakat u sustavu reference (prsa), iz
    //uredjene poze svakog kljuca; u svakom kadru nadlaktica i podlaktica se rijese IK-om prema
    //tome, a zakret nadlaktice oko svoje osi uzme se iz kljuceva da se ruka ne uvrce
    struct LimbPlan{ PoseLimb limb; std::vector<Warp::Id> path; };
    std::vector<LimbPlan> limbs;
    for(const PoseLimb& limb : settings.limbs){
        const JointPlan *u = planOf(limb.upper), *m = planOf(limb.middle), *e = planOf(limb.end);
        const bool touched = (u && u->touched) || (m && m->touched) || (e && e->touched);
        const Warp::Entity* upper = stage.get(limb.upper);
        if(!touched || !upper || !stage.get(limb.middle) || !stage.get(limb.end)) continue;
        //Put od reference do roditelja nadlaktice (npr. klavikula); bez reference - od roditelja
        LimbPlan plan{limb, {}};
        bool found = false;
        for(Warp::Id id = upper->parent; id != Warp::None; id = stage.get(id) ? stage.get(id)->parent : Warp::None){
            if(id == limb.reference){ found = true; break; }
            plan.path.insert(plan.path.begin(), id);
        }
        if(!found) plan.path.clear();
        limbs.push_back(plan);
    }
    auto isLimbJoint = [&](Warp::Id id){
        for(const LimbPlan& l : limbs) if(id == l.limb.upper || id == l.limb.middle || id == l.limb.end) return true;
        return false;
    };

    struct Write{ Warp::Id id; double frame; Warp::Transform value; };
    std::vector<Write> writes;
    size_t corrected = 0;

    //-- obicni zglobovi --------------------------------------------------------------------------------
    for(const auto& [id, plan] : plans){
        if(!plan.touched) continue;
        ++corrected;
        if(isLimbJoint(id)) continue;
        for(double t : framesFor({id})) writes.push_back({id, t, plannedLocal(id, t)});
    }

    //-- udovi ------------------------------------------------------------------------------------------
    for(const LimbPlan& lp : limbs){
        const PoseLimb& limb = lp.limb;
        auto parentMatrix = [&](auto localOf){
            glm::mat4 m(1.0f);
            for(Warp::Id id : lp.path) m = m * localOf(id).matrix();
            return m;
        };
        //Ciljevi iz uredjenih poza kljuceva, u sustavu reference
        struct Target{ glm::vec3 hand, elbow; glm::quat handRotation; Warp::Transform upper, middle; };
        std::vector<Target> targets;
        for(const PoseKey& key : keys){
            const glm::mat4 P = parentMatrix([&](Warp::Id id){ return keyedLocal(key, id); });
            const Warp::Transform u = keyedLocal(key, limb.upper), m = keyedLocal(key, limb.middle), e = keyedLocal(key, limb.end);
            const glm::mat4 U = P * u.matrix(), M = U * m.matrix(), E = M * e.matrix();
            targets.push_back({glm::vec3(E[3]), glm::vec3(M[3]), detail::rotationOfMatrix(E), u, m});
        }
        for(double t : framesFor({limb.upper, limb.middle, limb.end})){
            const Phase p = phaseAt(t);
            const float weight = isKey(t) ? 1.0f : p.weight;
            const glm::mat4 P = parentMatrix([&](Warp::Id id){ return plannedLocal(id, t); });
            Warp::Transform u = stage.localAt(limb.upper, t), m = stage.localAt(limb.middle, t), e = stage.localAt(limb.end, t);
            //Original u sustavu reference - za ulaz i izlaz, gdje se cilj pretapa iz pokreta
            const glm::mat4 U0 = P * u.matrix(), M0 = U0 * m.matrix(), E0 = M0 * e.matrix();
            const Target& a = targets[p.from];
            const Target& b = targets[p.to];
            glm::vec3 hand = a.hand + (b.hand - a.hand) * p.s;
            glm::vec3 elbow = a.elbow + (b.elbow - a.elbow) * p.s;
            glm::quat handRotation = detail::slerpShort(a.handRotation, b.handRotation, p.s);
            glm::quat upperBase = detail::slerpShort(a.upper.rotation, b.upper.rotation, p.s);
            glm::quat middleBase = detail::slerpShort(a.middle.rotation, b.middle.rotation, p.s);
            hand = glm::vec3(E0[3]) + (hand - glm::vec3(E0[3])) * weight;
            elbow = glm::vec3(M0[3]) + (elbow - glm::vec3(M0[3])) * weight;
            handRotation = detail::slerpShort(detail::rotationOfMatrix(E0), handRotation, weight);
            u.rotation = detail::slerpShort(u.rotation, upperBase, weight);
            m.rotation = detail::slerpShort(m.rotation, middleBase, weight);

            //Dvije kosti: lakat na krugu oko osi rame-saka, na strani lakta iz kljuceva
            const glm::quat parentRotation = detail::rotationOfMatrix(P);
            glm::mat4 U = P * u.matrix();
            glm::mat4 M = U * m.matrix();
            const glm::vec3 root(U[3]);
            const glm::vec3 middle(M[3]);
            const glm::vec3 end(M * glm::vec4(e.translation, 1.0f));
            const float upperLength = glm::length(middle - root), lowerLength = glm::length(end - middle);
            glm::vec3 toTarget = hand - root;
            float distance = glm::length(toTarget);
            if(upperLength > 1e-6f && lowerLength > 1e-6f && distance > 1e-6f){
                const glm::vec3 direction = toTarget / distance;
                distance = std::clamp(distance, std::fabs(upperLength - lowerLength) + 1e-5f, upperLength + lowerLength - 1e-5f);
                glm::vec3 bend = elbow - root;
                bend -= direction * glm::dot(bend, direction);
                if(glm::dot(bend, bend) < 1e-10f){ bend = middle - root; bend -= direction * glm::dot(bend, direction); }
                if(glm::dot(bend, bend) > 1e-10f){
                    bend = glm::normalize(bend);
                    const float along = (upperLength * upperLength - lowerLength * lowerLength + distance * distance) / (2.0f * distance);
                    const float height = std::sqrt(std::max(0.0f, upperLength * upperLength - along * along));
                    const glm::vec3 middleTarget = root + direction * along + bend * height;
                    const glm::quat upperDelta = detail::rotationBetween(middle - root, middleTarget - root);
                    u.rotation = glm::normalize(glm::inverse(parentRotation) * upperDelta * parentRotation * u.rotation);
                    U = P * u.matrix();
                    M = U * m.matrix();
                    const glm::vec3 newMiddle(M[3]);
                    const glm::vec3 newEnd(M * glm::vec4(e.translation, 1.0f));
                    const glm::quat upperRotation = detail::rotationOfMatrix(U);
                    const glm::quat middleDelta = detail::rotationBetween(newEnd - newMiddle, hand - newMiddle);
                    m.rotation = glm::normalize(glm::inverse(upperRotation) * middleDelta * upperRotation * m.rotation);
                    M = U * m.matrix();
                }
            }
            e.rotation = glm::normalize(glm::inverse(detail::rotationOfMatrix(M)) * handRotation);
            writes.push_back({limb.upper, t, u});
            writes.push_back({limb.middle, t, m});
            writes.push_back({limb.end, t, e});
        }
    }
    for(const Write& write : writes) stage.setLocalAt(write.id, write.frame, write.value);
    return corrected;
}
}
