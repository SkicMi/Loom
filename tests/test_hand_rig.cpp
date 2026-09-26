// Hand rig na pravom liku (tools/autorig/hand_rig.py + LoomHandPose.h + LoomTool.h).
//
// Lik iz desnog klika u Manny profilu (tools/autorig/outputs/humanoid-mascott/rigged.glb - lokalni izlaz
// auto-riga, nije u gitu; bez njega se test preskace). Sto se brani:
//   - Loom nade prste iz stabla (palac, kaziprst..mali, metakarpali preskoceni)
//   - Loomova strana dlana = os Z hand riga (+X savija prste u dlan): editor i auto-rig govore isto
//   - savijanje oko lokalne X osi svakog zgloba vodi vrh prsta prema dlanu, u ravnini prsta
//   - drska polumjera 1.6 cm u lijevoj saci (gripAlignedWorld): prsti se omotaju bez prodora,
//     a zglobovi su uz drsku
//   - mirne sake (applyRelaxedHandsRestPose): 30 zglobova, prsti se saviju prema dlanu (Z hand riga),
//     a Kimodo pokret (WeaverMotion/motion_1790274564101.bvh) kroz retarget zadrzi opustene prste
#include "TestHarness.h"

#include "../src/LoomModel.h"
#include "../src/LoomTool.h"
#include "../src/LoomRelaxedHands.h"
#include "../src/LoomWeaverMotion.h"
#include "../src/LoomMotionPanel.h"

#include <cmath>
#include <filesystem>

namespace{

Warp::Id named(const Warp::Stage& stage, const std::string& name){
    Warp::Id found = Warp::None;
    stage.walk([&](const Warp::Entity& e, int){ if(found == Warp::None && e.joint && e.name == name) found = e.id; });
    return found;
}

glm::vec3 at(const Warp::Stage& stage, Warp::Id id){ return glm::vec3(stage.worldMatrix(id, 1.0)[3]); }

Engine::Physics::TriangleMesh cylinder(float radius, float height){
    Engine::Physics::TriangleMesh mesh;
    const int rings = 25, segments = 20;
    for(int r = 0; r < rings; ++r)
        for(int s = 0; s < segments; ++s){
            const float a = float(s) / float(segments) * 6.2831853f;
            mesh.vertices.push_back({radius * std::cos(a), -0.5f * height + height * float(r) / float(rings - 1), radius * std::sin(a)});
        }
    for(int r = 0; r + 1 < rings; ++r)
        for(int s = 0; s < segments; ++s){
            const uint32_t a = uint32_t(r * segments + s), b = uint32_t(r * segments + (s + 1) % segments);
            mesh.indices.insert(mesh.indices.end(), {a, a + uint32_t(segments), b, b, a + uint32_t(segments), b + uint32_t(segments)});
        }
    const uint32_t bottom = uint32_t(mesh.vertices.size());
    mesh.vertices.push_back({0.0f, -0.5f * height, 0.0f});
    mesh.vertices.push_back({0.0f, 0.5f * height, 0.0f});
    const uint32_t top = uint32_t((rings - 1) * segments);
    for(int s = 0; s < segments; ++s){
        const uint32_t a = uint32_t(s), b = uint32_t((s + 1) % segments);
        mesh.indices.insert(mesh.indices.end(), {bottom, a, b, bottom + 1, top + b, top + a});
    }
    return mesh;
}

}

int main(){
    TestReport report("hand_rig");
    const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                                       "tools/autorig/outputs/humanoid-mascott/rigged.glb";
    if(!std::filesystem::is_regular_file(path)){
        std::printf("   preskoceno: nema %s (lokalni izlaz auto-riga)\n", path.string().c_str());
        return report.result();
    }
    Spool::GltfScene scene;
    std::string error;
    Warp::Stage stage;
    const bool loaded = Spool::loadGltf(path.string(), scene, error);
    if(loaded) Loom::importGltf(stage, scene);
    const Warp::Id hand = named(stage, "hand_l");
    report.check("Manny mascot se ucita s hand_l", loaded && hand != Warp::None, error);
    if(hand == Warp::None) return report.result();

    const Loom::HandFingers fingers = Loom::handFingersOf(stage, hand, 1.0);
    const char* expected[5] = {"thumb_01_l", "index_01_l", "middle_01_l", "ring_01_l", "pinky_01_l"};
    bool order = fingers.valid();
    for(int f = 0; f < 5 && order; ++f) order = !fingers.fingers[size_t(f)].empty() && stage.get(fingers.fingers[size_t(f)].front())->name == expected[f];
    report.check("prsti iz stabla: palac..mali, metakarpali preskoceni", order, "");

    //Os Z sake iz hand riga = Loomova strana dlana
    const glm::mat4 handWorld = stage.worldMatrix(hand, 1.0);
    const glm::vec3 handZ = glm::normalize(glm::vec3(handWorld[2]));
    report.check("strana dlana u Loomu = os Z hand riga", glm::dot(handZ, fingers.palmNormal) > 0.95f,
                 fmt("cos %.3f", glm::dot(handZ, fingers.palmNormal)));

    //Savijanje oko lokalne X (konvencija hand riga) vodi vrh u dlan, u ravnini prsta
    float worstPalm = 1.0f, worstLateral = 0.0f;
    for(int f = 1; f < 5; ++f){
        const std::vector<Warp::Id>& finger = fingers.fingers[size_t(f)];
        Warp::Stage bent = stage;
        const glm::vec3 base = at(stage, finger.front()), tip0 = at(stage, finger.back());
        for(Warp::Id joint : finger){
            Warp::Transform local = bent.localAt(joint, 1.0);
            local.rotation = glm::normalize(local.rotation * glm::angleAxis(glm::radians(15.0f), glm::vec3(1, 0, 0)));
            bent.get(joint)->local = local;
            if(Warp::AnimatorTrack* track = bent.activeAnimatorTrack(joint)) track->rotationKeys = {};
        }
        const glm::vec3 direction = glm::normalize(tip0 - base);
        glm::vec3 motion = at(bent, finger.back()) - tip0;
        motion -= direction * glm::dot(motion, direction);
        if(glm::length(motion) < 1e-7f) continue;
        motion = glm::normalize(motion);
        glm::vec3 palm = fingers.palmNormal - direction * glm::dot(fingers.palmNormal, direction);
        palm = glm::normalize(palm);
        worstPalm = std::min(worstPalm, glm::dot(motion, palm));
        worstLateral = std::max(worstLateral, std::fabs(glm::dot(motion, glm::normalize(glm::cross(direction, palm)))));
    }
    report.check("+X savija prste u dlan, bez skretanja", worstPalm > 0.99f && worstLateral < 0.05f,
                 fmt("najgore: dlan %.3f, bocno %.3f", worstPalm, worstLateral));

    //Drska u lijevoj saci i prsti oko nje
    const Warp::Id middleEnd = named(stage, "middle_03_l"), forearm = named(stage, "lowerarm_l");
    const Loom::HoldHand holdHand{Warp::None, hand, middleEnd, false, forearm};
    Loom::HandFrame frame;
    const bool framed = Loom::handFrameAt(stage, holdHand, 1.0, frame);
    Loom::ToolGeometry handle;
    handle.mesh = cylinder(0.016f, 0.14f);
    Warp::Grip grip;
    grip.axis = {0, 1, 0};
    grip.palm = {1, 0, 0};
    const glm::mat4 toolWorld = Loom::gripAlignedWorld(glm::mat4(1.0f), grip, 0.016f, frame);
    const Engine::Physics::Collider collider = Engine::Physics::Collider::fromMesh(handle.mesh);
    const Loom::JointPose pose = Loom::conformedHandPoseAt(stage, fingers, Loom::gripPreset("grip"), 1.0, collider, toolWorld);
    Warp::Stage held = stage;
    for(const auto& [id, local] : pose){
        held.get(id)->local = local;
        if(Warp::AnimatorTrack* track = held.activeAnimatorTrack(id)) track->rotationKeys = {};
    }
    const glm::mat4 toTool = glm::inverse(toolWorld);
    bool penetrates = false;
    float farthest = 0.0f;
    for(int f = 0; f < 5; ++f){
        const std::vector<Warp::Id>& finger = fingers.fingers[size_t(f)];
        for(size_t j = 0; j + 1 < finger.size(); ++j){
            const glm::vec3 a(toTool * glm::vec4(at(held, finger[j]), 1.0f)), b(toTool * glm::vec4(at(held, finger[j + 1]), 1.0f));
            if(collider.capsuleHits(a, b, 0.004f)){
                penetrates = true;
                std::printf("   prodor: %s (rest dira: %d)\n", held.get(finger[j])->name.c_str(),
                            int(collider.capsuleHits(glm::vec3(toTool * glm::vec4(at(stage, finger[j]), 1.0f)),
                                                     glm::vec3(toTool * glm::vec4(at(stage, finger[j + 1]), 1.0f)), 0.004f)));
            }
        }
        //Vrh: zadnji clanak (produzetak srednjeg u mirnoj pozi) nosen rotacijom zadnjeg zgloba
        const glm::vec3 restTip = (at(stage, finger.back()) - at(stage, finger[finger.size() - 2])) * 0.8f;
        const glm::vec3 tip = at(held, finger.back()) + glm::mat3(held.worldMatrix(finger.back(), 1.0)) *
                              (glm::inverse(glm::mat3(stage.worldMatrix(finger.back(), 1.0))) * restTip);
        if(f > 0) farthest = std::max(farthest, collider.distance(glm::vec3(toTool * glm::vec4(tip, 1.0f)), 0.3f));
    }
    report.check("drska u saci: prsti bez prodora, vrhovi uz drsku (linija kosti do 1.2 cm)", framed && !penetrates && farthest < 0.012f,
                 fmt("prodor %d, najdalji zglob %.1f cm", int(penetrates), farthest * 100.0f));

    //Mirne sake na Manny mascotu
    const Warp::Id rig = Loom::motionCharacterForEntity(stage, stage.get(hand)->parent);
    Warp::Stage relaxed = stage;
    size_t changed = 0;
    const bool applied = Loom::applyRelaxedHandsRestPose(relaxed, rig, &changed);
    const glm::vec3 wrist = at(stage, hand);
    const glm::vec3 tip0 = at(stage, named(stage, "middle_03_l")), tip1 = at(relaxed, named(relaxed, "middle_03_l"));
    const float towardPalm = glm::dot(tip1 - tip0, handZ);
    report.check("mirne sake: 30 zglobova, vrh srednjeg prsta prema dlanu", applied && changed == 30 && towardPalm > 0.002f,
                 fmt("%zu zglobova, vrh %.1f mm prema dlanu (saka na %.2f m)", changed, towardPalm * 1000.0f, wrist.y));

    //Kimodo pokret na opustenom liku: mirna poza prstiju se zadrzi kao pomak preko pokreta - prsti u
    //pokretu = prsti istog pokreta na neopustenom liku + savijanje mirne poze (SOMA sama savija prste)
    const std::filesystem::path bvh = std::filesystem::path(__FILE__).parent_path().parent_path() / "WeaverMotion/motion_1790274564101.bvh";
    auto retargeted = [&](Warp::Stage& target){
        Loom::MotionPlacement placement;
        placement.parent = rig;
        placement.fitToParentRig = true;
        placement.sceneFps = 30.0;
        return Loom::importWeaverMotionBvh(target, bvh, placement);
    };
    Warp::Stage plain = stage;
    const Loom::WeaverMotionImportReport motion = retargeted(relaxed), plainMotion = retargeted(plain);
    auto degrees = [](glm::quat a, glm::quat b){ return glm::degrees(2.0f * std::acos(std::min(1.0f, std::fabs(glm::dot(a, b))))); };
    float worst = 0.0f, relaxation = 0.0f;
    const double sample = 0.5 * (motion.firstFrame + motion.lastFrame);
    for(const char* name : {"index_01_l", "middle_02_l", "ring_01_l", "pinky_02_r", "thumb_02_r"}){
        const Warp::Id id = named(relaxed, name);
        const glm::quat turn = glm::inverse(plain.get(id)->local.rotation) * relaxed.get(id)->local.rotation;
        worst = std::max(worst, degrees(relaxed.localAt(id, sample).rotation, plain.localAt(id, sample).rotation * turn));
        relaxation = std::max(relaxation, degrees(turn, glm::quat(1, 0, 0, 0)));
    }
    report.check("retarget zadrzi mirnu pozu prstiju preko pokreta (do 3 st)",
                 motion.problem.empty() && plainMotion.problem.empty() && worst < 3.0f && relaxation > 10.0f,
                 fmt("%s odstupanje %.1f st, mirna poza do %.0f st, kadar %.0f", motion.problem.c_str(), worst, relaxation, sample));

    //SOMA prsti: Index1 je metakarpal, Index2..4 clanci; palac 1..3
    Engine::WeaverMotion::Clip soma;
    std::string bvhError;
    Engine::WeaverMotion::readKimodoBvh(bvh.string(), soma, bvhError);
    const Loom::MotionRigMapping mapping = Loom::mapMotionBones(soma, Loom::motionRigRestPose(stage, rig));
    auto targetOf = [&](const char* source) -> std::string{
        for(size_t i = 0; i < soma.joints.size(); ++i)
            if(soma.joints[i].name == source && mapping.targetBySource[i] != Warp::None) return stage.get(mapping.targetBySource[i])->name;
        return "-";
    };
    const std::string m1 = targetOf("LeftHandIndex1"), m2 = targetOf("LeftHandIndex2"), m4 = targetOf("RightHandPinky4"), t1 = targetOf("LeftHandThumb1");
    report.check("SOMA prsti na Manny: Index1 metakarpal, Index2 index_01, Pinky4 pinky_03, Thumb1 thumb_01",
                 m1 == "index_metacarpal_l" && m2 == "index_01_l" && m4 == "pinky_03_r" && t1 == "thumb_01_l",
                 fmt("%s %s %s %s", m1.c_str(), m2.c_str(), m4.c_str(), t1.c_str()));

    //Tocnost prstiju: SAVIJANJE s predznakom (kut kosti prema roditeljskoj u ravnini savijanja, tj.
    //kost + normala dlana) u korijenu prsta (metakarpal/clanak 1) i srednjem zglobu (clanak 1/2):
    //promjena od mirne poze na Mannyju prati istu promjenu u SOMA pokretu. Rasirenost ne ulazi (mirni
    //oblici saka se razlikuju). Prosjek po prstima i svakom petom kadru
    const char* sides[2] = {"Left", "Right"};
    const char* somaNames[4] = {"Index", "Middle", "Ring", "Pinky"};
    const char* mannyNames[4] = {"index", "middle", "ring", "pinky"};
    auto sourceIndexOf = [&](const std::string& name){
        for(size_t i = 0; i < soma.joints.size(); ++i) if(soma.joints[i].name == name) return int(i);
        return -1;
    };
    auto flexion = [](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 normal){
        const glm::vec3 u = glm::normalize(b - a), v = glm::normalize(c - b);
        const glm::vec3 side = glm::normalize(normal - u * glm::dot(normal, u));
        return glm::degrees(std::atan2(glm::dot(v, side), glm::dot(v, u)));
    };
    const size_t sourceFrames = soma.frames.size();
    //SOMA mirna poza: kanonska T-poza (sve rotacije jedinicne)
    Engine::WeaverMotion::Clip somaRest = soma;
    somaRest.frames.resize(1);
    for(glm::quat& rotation : somaRest.frames[0].rotations) rotation = glm::quat(1, 0, 0, 0);
    const std::vector<glm::vec3> sourceRest = Loom::motionPoseJointPositions(somaRest, 0);
    auto flexionError = [&](const Warp::Stage& plain, const Warp::Stage& restStage, const Loom::WeaverMotionImportReport& plainMotion, float& splayError){
    float sumError = 0.0f, sumSplay = 0.0f;
    size_t samples = 0;
    auto at = [](const Warp::Stage& st, Warp::Id id){ return glm::vec3(st.worldMatrix(id, 1.0)[3]); };
    for(size_t frameIndex = 0; frameIndex < sourceFrames; frameIndex += 5){
        const std::vector<glm::vec3> source = Loom::motionPoseJointPositions(soma, frameIndex);
        const double time = plainMotion.firstFrame + double(frameIndex) * (plainMotion.lastFrame - plainMotion.firstFrame) / double(std::max<size_t>(1, sourceFrames - 1));
        for(int side = 0; side < 2; ++side){
            const char suffix = side == 0 ? 'l' : 'r';
            const std::string hand = std::string(sides[side]) + "Hand";
            const int sh = sourceIndexOf(hand), si = sourceIndexOf(hand + "Index1"), sp = sourceIndexOf(hand + "Pinky1");
            const Warp::Id th = named(plain, std::string("hand_") + suffix), ti = named(plain, std::string("index_metacarpal_") + suffix),
                           tp = named(plain, std::string("pinky_metacarpal_") + suffix);
            auto w = [&](Warp::Id id){ return glm::vec3(plain.worldMatrix(id, time)[3]); };
            auto r = [&](Warp::Id id){ return at(restStage, id); };   //mirna poza (ucitani lik bez pokreta)
            const auto& S = source; const auto& R = sourceRest;
            const glm::vec3 sn = glm::cross(S[size_t(si)] - S[size_t(sh)], S[size_t(sp)] - S[size_t(sh)]);
            const glm::vec3 rn = glm::cross(R[size_t(si)] - R[size_t(sh)], R[size_t(sp)] - R[size_t(sh)]);
            const glm::vec3 wn = glm::cross(w(ti) - w(th), w(tp) - w(th)), qn = glm::cross(r(ti) - r(th), r(tp) - r(th));
            for(int f = 0; f < 4; ++f){
                const std::string stem = hand + somaNames[f];
                const int s1 = sourceIndexOf(stem + "1"), s2 = sourceIndexOf(stem + "2"), s3 = sourceIndexOf(stem + "3"), s4 = sourceIndexOf(stem + "4");
                const std::string m = mannyNames[f];
                const Warp::Id meta = named(plain, m + "_metacarpal_" + suffix), p1 = named(plain, m + "_01_" + suffix),
                               p2 = named(plain, m + "_02_" + suffix), p3 = named(plain, m + "_03_" + suffix);
                if(s1 < 0 || s2 < 0 || s3 < 0 || s4 < 0 || meta == Warp::None || p3 == Warp::None) continue;
                const float sourceRoot = flexion(S[size_t(s1)], S[size_t(s2)], S[size_t(s3)], sn) - flexion(R[size_t(s1)], R[size_t(s2)], R[size_t(s3)], rn);
                const float sourceMiddle = flexion(S[size_t(s2)], S[size_t(s3)], S[size_t(s4)], sn) - flexion(R[size_t(s2)], R[size_t(s3)], R[size_t(s4)], rn);
                const float targetRoot = flexion(w(meta), w(p1), w(p2), wn) - flexion(r(meta), r(p1), r(p2), qn);
                const float targetMiddle = flexion(w(p1), w(p2), w(p3), wn) - flexion(r(p1), r(p2), r(p3), qn);
                sumError += std::fabs(sourceRoot - targetRoot) + std::fabs(sourceMiddle - targetMiddle);
                const float sourceSplay = flexion(S[size_t(s1)], S[size_t(s2)], S[size_t(s3)], glm::cross(sn, S[size_t(s2)] - S[size_t(s1)])) -
                                          flexion(R[size_t(s1)], R[size_t(s2)], R[size_t(s3)], glm::cross(rn, R[size_t(s2)] - R[size_t(s1)]));
                const float targetSplay = flexion(w(meta), w(p1), w(p2), glm::cross(wn, w(p1) - w(meta))) -
                                          flexion(r(meta), r(p1), r(p2), glm::cross(qn, r(p1) - r(meta)));
                sumSplay += std::fabs(sourceSplay - targetSplay);
                samples += 2;
            }
        }
    }
    splayError = samples ? 2.0f * sumSplay / float(samples) : 999.0f;
    return samples ? sumError / float(samples) : 999.0f;
    };
    float splay = 0.0f;
    const float meanError = flexionError(plain, stage, plainMotion, splay);
    std::printf("   MJERA prsti_savijanje_greska_st %.4f, rasirenost %.4f (T-poza)\n", meanError, splay);
    const size_t samples = 672;
    report.check("savijanje prstiju prati SOMA pokret (prosjek do 8 st)", samples > 0 && meanError < 8.0f,
                 fmt("prosjek %.1f st, %zu uzoraka", meanError, samples));

    //Isti pokret na liku u A-pozi sa zakrenutim dlanom (ruke 45 st dolje, saka 60 st oko podlaktice):
    //mirni oblik sake vise nije SOMA T-poza, a savijanje prstiju mora ostati savijanje
    Warp::Stage bent = stage;
    for(const char suffix : {'l', 'r'}){
        const float sign = suffix == 'l' ? 1.0f : -1.0f;
        for(const auto& [bone, degrees] : {std::pair<std::string, float>{"upperarm_", 45.0f}, {"hand_", 60.0f}}){
            const Warp::Id id = named(bent, bone + suffix);
            const Warp::Entity* parent = bent.get(bent.get(id)->parent);
            const glm::quat parentWorld = Loom::motionRotationOf(bent.worldMatrix(parent->id, 1.0));
            const glm::vec3 worldAxis = bone == "hand_" ? glm::normalize(at(bent, id) - at(bent, parent->id)) : glm::vec3(0, 0, 1);
            const glm::quat turn = glm::angleAxis(glm::radians(-sign * degrees), glm::inverse(parentWorld) * worldAxis);
            bent.get(id)->local.rotation = glm::normalize(turn * bent.get(id)->local.rotation);
        }
    }
    const Warp::Stage bentRest = bent;
    const Loom::WeaverMotionImportReport bentMotion = retargeted(bent);
    float bentSplay = 0.0f;
    const float bentError = flexionError(bent, bentRest, bentMotion, bentSplay);
    std::printf("   MJERA prsti_savijanje_greska_st %.4f, rasirenost %.4f (A-poza, zakrenut dlan)\n", bentError, bentSplay);
    report.check("A-poza i zakrenut dlan: savijanje i rasirenost prate SOMA (do 2 st)", bentError < 2.0f && bentSplay < 2.0f,
                 fmt("savijanje %.1f st, rasirenost %.1f st", bentError, bentSplay));
    return report.result();
}
