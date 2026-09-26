// Prsti na UniRig-52 liku kroz Kimodo pokret (isti put kao editor: mirna poza, retarget, action details).
//
// Lik iz tools/autorig/outputs/mascot-03/rigged.glb (lokalni izlaz auto-riga, nije u gitu; bez njega
// se test preskace) i WeaverMotion/mvp_walk_wave_peace_roll.bvh (hod s mahanjem, peace sign, kolut).
// Sto se brani:
//   - usred peace signa desne ruke kaziprst i srednji prst su ispruzeni, prstenjak i mali savijeni
//   - u hodu prije toga savijanje svakog prsta desne sake = mirna poza lika + savijanje SOMA izvora
// Mjera je ukupno savijanje prsta (curl): dlan prema zadnjem clanku, iz rotacija zglobova
#include "TestHarness.h"

#include "../src/LoomModel.h"
#include "../src/LoomMotionPanel.h"
#include "../src/LoomRelaxedHands.h"
#include "../src/LoomWeaverMotion.h"

#include <filesystem>
#include <unordered_map>

namespace{

//UKUPNO SAVIJANJE PRSTA u stupnjevima: kut izmedju smjera dlana prema bazi prsta i smjera zadnjeg
//clanka. Smjerovi su vektori iz mirne poze (bind) koje nose rotacije sake i zadnjeg zgloba u kadru -
//UniRig nema kost vrha, pa se zadnji zglob vidi samo iz rotacije
struct Bind{ std::unordered_map<Warp::Id, glm::mat4> world; };
float curl(const Warp::Stage& stage, const Bind& bind, Warp::Id hand, const std::array<Warp::Id, 52>& ids, int first, double frame){
    auto rotation = [](const glm::mat4& m){ return Loom::motionRotationOf(m); };
    auto moved = [&](Warp::Id id, glm::vec3 restVector){
        return glm::normalize(rotation(stage.worldMatrix(id, frame)) * glm::inverse(rotation(bind.world.at(id))) * restVector);
    };
    auto p = [&](Warp::Id id){ return glm::vec3(bind.world.at(id)[3]); };
    const Warp::Id a = ids[size_t(first)], b = ids[size_t(first + 1)], c = ids[size_t(first + 2)];
    const glm::vec3 palm = moved(hand, p(a) - p(hand)), distal = moved(c, p(c) - p(b));
    return glm::degrees(std::acos(std::clamp(glm::dot(palm, distal), -1.0f, 1.0f)));
}

}

int main(){
    TestReport report("unirig_fingers");
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path glb = root / "tools/autorig/outputs/mascot-03/rigged.glb";
    const std::filesystem::path bvh = root / "WeaverMotion/mvp_walk_wave_peace_roll.bvh";
    if(!std::filesystem::is_regular_file(glb) || !std::filesystem::is_regular_file(bvh)){
        std::printf("   preskoceno: nema %s ili %s (lokalni izlazi)\n", glb.string().c_str(), bvh.string().c_str());
        return report.result();
    }
    Spool::GltfScene scene;
    std::string error;
    Warp::Stage stage;
    const bool loaded = Spool::loadGltf(glb.string(), scene, error);
    Warp::Id rig = Warp::None;
    if(loaded){
        const Loom::ModelImportReport imported = Loom::importGltf(stage, scene);
        rig = Loom::motionCharacterForEntity(stage, imported.group);
    }
    std::array<Warp::Id, 52> ids;
    const bool uniRig = rig != Warp::None && Loom::motionFindVerifiedUniRig52(Loom::motionRigRestPose(stage, rig), ids);
    report.check("mascot-03 je UniRig-52", uniRig, error);
    if(!uniRig) return report.result();

    Bind bind;
    for(const Loom::MotionRigJointRest& joint : Loom::motionRigRestPose(stage, rig).joints) bind.world[joint.id] = joint.matrix;
    //Kao editor (loom_app: uvoz pokreta na lik)
    Loom::ensureRigAnimator(stage, rig);
    Loom::applyRelaxedHandsRestPose(stage, rig);
    Engine::WeaverMotion::Clip clip;
    Engine::WeaverMotion::readKimodoBvh(bvh.string(), clip, error);
    Loom::MotionPlacement placement;
    placement.parent = rig;
    placement.fitToParentRig = true;
    placement.sceneFps = 30.0;
    const Loom::WeaverMotionImportReport motion = Loom::importWeaverMotionClip(stage, clip, "mvp", placement);
    std::vector<Loom::MotionActionInterval> intervals;
    size_t first = 0;
    for(const Loom::MotionAction& action : Loom::motionActionsForClip(bvh)){
        const size_t count = size_t(std::max(1, Loom::kimodoMotionFrameCount({action})));
        if(first >= clip.frames.size()) break;
        intervals.push_back({action.prompt, first, std::min(clip.frames.size() - 1, first + count - 1)});
        first += count;
    }
    const double step = placement.sceneFps / std::max(1e-6, clip.framesPerSecond);
    const size_t details = Loom::applyUniRigActionDetails(stage, rig, clip, intervals, motion.firstFrame, step);
    report.check("pokret na liku, peace i mahanje dodani", motion.problem.empty() && intervals.size() == 3 && details >= 2,
                 fmt("%s, %zu intervala, %zu detalja", motion.problem.c_str(), intervals.size(), details));
    if(intervals.size() < 2) return report.result();

    const double peace = motion.firstFrame + 0.5 * double(intervals[1].first + intervals[1].last) * step;
    const double walk = motion.firstFrame + 0.5 * double(intervals[0].first + intervals[0].last) * step;
    //Desna saka 28: kaziprst 32, srednji 35, prstenjak 38, mali 41
    const Warp::Id hand = ids[28];
    const float index = curl(stage, bind, hand, ids, 32, peace), middle = curl(stage, bind, hand, ids, 35, peace),
                ring = curl(stage, bind, hand, ids, 38, peace), pinky = curl(stage, bind, hand, ids, 41, peace);
    std::printf("   MJERA peace savijanje st: kaziprst %.0f srednji %.0f prstenjak %.0f mali %.0f\n", index, middle, ring, pinky);
    report.check("peace sign: kaziprst i srednji ispruzeni (do 25 st), prstenjak i mali savijeni (preko 70 st)",
                 index < 25.0f && middle < 25.0f && ring > 70.0f && pinky > 70.0f,
                 fmt("kaziprst %.0f srednji %.0f | prstenjak %.0f mali %.0f", index, middle, ring, pinky));
    //Hod: savijanje svakog prsta = savijanje u mirnoj pozi lika + savijanje SOMA izvora (od T-poze).
    //SOMA: metakarpal (Index1 -> Index2) prema zadnjem clanku (Index4 -> End)
    const size_t walkSource = (intervals[0].first + intervals[0].last) / 2;
    Engine::WeaverMotion::Clip tPose = clip;
    tPose.frames.resize(1);
    for(glm::quat& rotation : tPose.frames[0].rotations) rotation = glm::quat(1, 0, 0, 0);
    const std::vector<glm::vec3> source = Loom::motionPoseJointPositions(clip, walkSource), sourceRest = Loom::motionPoseJointPositions(tPose, 0);
    auto angle = [](glm::vec3 a, glm::vec3 b){ return glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f))); };
    auto p = [&](Warp::Id id){ return glm::vec3(bind.world.at(id)[3]); };
    float worst = 0.0f, lowest = 999.0f, highest = 0.0f;
    const char* names[4] = {"Index", "Middle", "Ring", "Pinky"};
    for(int f = 0; f < 4; ++f){
        const int finger = 32 + 3 * f;
        int j[5] = {-1, -1, -1, -1, -1};
        const std::string stem = std::string("RightHand") + names[f];
        for(size_t i = 0; i < clip.joints.size(); ++i)
            for(int k = 0; k < 5; ++k) if(clip.joints[i].name == stem + (k < 4 ? std::to_string(k + 1) : std::string("End"))) j[k] = int(i);
        if(std::min({j[0], j[1], j[3], j[4]}) < 0) continue;
        const float sourceCurl = angle(source[size_t(j[1])] - source[size_t(j[0])], source[size_t(j[4])] - source[size_t(j[3])]) -
                                 angle(sourceRest[size_t(j[1])] - sourceRest[size_t(j[0])], sourceRest[size_t(j[4])] - sourceRest[size_t(j[3])]);
        const float bindCurl = angle(p(ids[size_t(finger)]) - p(hand), p(ids[size_t(finger + 2)]) - p(ids[size_t(finger + 1)]));
        const float walking = curl(stage, bind, hand, ids, finger, walk);
        worst = std::max(worst, std::fabs(walking - bindCurl - sourceCurl));
        lowest = std::min(lowest, walking);
        highest = std::max(highest, walking);
    }
    std::printf("   MJERA hod savijanje st %.0f - %.0f, najvise %.1f st od mirne poze + SOMA\n", lowest, highest, worst);
    report.check("hod: savijanje prstiju = mirna poza lika + SOMA (do 10 st po prstu; kutovi bez predznaka)", worst < 10.0f && highest < 90.0f,
                 fmt("%.0f - %.0f st, najvise %.1f st", lowest, highest, worst));
    return report.result();
}
