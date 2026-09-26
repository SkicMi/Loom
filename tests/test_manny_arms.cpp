// Ruke Manny lika (HumanoidMascott, direct rig) prate Kimodo izvor u svakom kadru.
//
// Lik iz tools/autorig/outputs/humanoid-mascott/rigged.glb (lokalni izlaz auto-riga, nije u gitu; bez
// njega se test preskace) i Kimodo skok iz WeaverMotion/ (ruke idu visoko iznad glave). Isti put kao
// editor (loom_app: importAutoRigModel pa importMotion): Animator, mirne sake, retarget, detalji radnji.
// Mjera: kut izmedju smjera nadlaktice/podlaktice lika i izvora, nakon poravnanja okvirom trupa iz linije
// kukova i kraljeznice (izvor i lik ne moraju gledati na istu stranu).
#include "TestHarness.h"

#include "../src/LoomModel.h"
#include "../src/LoomMotionPanel.h"
#include "../src/LoomRelaxedHands.h"
#include "../src/LoomWeaverMotion.h"

#include <filesystem>
#include <unordered_map>

int main(int argc, char** argv){
    TestReport report("manny_arms");
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path glb = root / "tools/autorig/outputs/humanoid-mascott/rigged.glb";
    std::filesystem::path bvh = root / "WeaverMotion/motion_1790419879047/motion_1790419879047_00.bvh";
    if(argc > 1) bvh = argv[1];
    if(!std::filesystem::is_regular_file(glb) || !std::filesystem::is_regular_file(bvh)){
        std::printf("   preskoceno: nema %s ili %s (lokalni izlazi)\n", glb.string().c_str(), bvh.string().c_str());
        return report.result();
    }
    Spool::GltfScene scene;
    std::string error;
    Warp::Stage stage;
    Warp::Id rig = Warp::None;
    if(Spool::loadGltf(glb.string(), scene, error)){
        const Loom::ModelImportReport imported = Loom::importGltf(stage, scene);
        rig = Loom::motionCharacterForEntity(stage, imported.group);
    }
    report.check("lik ucitan", rig != Warp::None, error);
    if(rig == Warp::None) return report.result();
    std::unordered_map<std::string, Warp::Id> bone;
    for(const Loom::MotionRigJointRest& joint : Loom::motionRigRestPose(stage, rig).joints) bone[joint.name] = joint.id;

    Loom::ensureRigAnimator(stage, rig);
    Loom::applyRelaxedHandsRestPose(stage, rig);
    Engine::WeaverMotion::Clip clip;
    Engine::WeaverMotion::readKimodoBvh(bvh.string(), clip, error);
    Loom::MotionPlacement placement;
    placement.parent = rig;
    placement.fitToParentRig = true;
    placement.sceneFps = 25.0;
    const Loom::WeaverMotionImportReport motion = Loom::importWeaverMotionClip(stage, clip, "skok", placement);
    std::vector<Loom::MotionActionInterval> intervals;
    size_t first = 0;
    for(const Loom::MotionAction& action : Loom::motionActionsForClip(bvh)){
        const size_t count = size_t(std::max(1, Loom::kimodoMotionFrameCount({action})));
        if(first >= clip.frames.size()) break;
        intervals.push_back({action.prompt, first, std::min(clip.frames.size() - 1, first + count - 1)});
        first += count;
    }
    const double step = placement.sceneFps / std::max(1e-6, clip.framesPerSecond);
    Loom::applyUniRigActionDetails(stage, rig, clip, intervals, motion.firstFrame, step);
    report.check("pokret na liku", motion.problem.empty(), motion.problem);
    if(!motion.problem.empty()) return report.result();

    auto joint = [&](const char* name){
        for(size_t i = 0; i < clip.joints.size(); ++i) if(clip.joints[i].name == name) return int(i);
        return -1;
    };
    struct Segment{ const char *from, *to, *boneFrom, *boneTo; };
    const Segment segments[4] = {{"LeftArm", "LeftForeArm", "upperarm_l", "lowerarm_l"},
                                 {"LeftForeArm", "LeftHand", "lowerarm_l", "hand_l"},
                                 {"RightArm", "RightForeArm", "upperarm_r", "lowerarm_r"},
                                 {"RightForeArm", "RightHand", "lowerarm_r", "hand_r"}};
    const int hipL = joint("LeftLeg"), hipR = joint("RightLeg"), hips = joint("Hips"), chest = joint("Chest");
    float worst[4] = {0, 0, 0, 0};
    size_t worstFrame[4] = {0, 0, 0, 0};
    auto p = [&](const char* name, double frame){ return glm::vec3(stage.worldMatrix(bone.at(name), frame)[3]); };
    for(size_t f = 0; f < clip.frames.size(); ++f){
        const double frame = motion.firstFrame + double(f) * step;
        const std::vector<glm::vec3> source = Loom::motionPoseJointPositions(clip, f);
        //Okvir trupa (kukovi + kraljeznica) izvora i lika: poravnanje cijelim okretom
        auto basis = [](glm::vec3 lateral, glm::vec3 up){
            const glm::vec3 x = glm::normalize(lateral), z = glm::normalize(glm::cross(x, up)), y = glm::cross(z, x);
            return glm::mat3(x, y, z);
        };
        const glm::mat3 from = basis(source[size_t(hipL)] - source[size_t(hipR)], source[size_t(chest)] - source[size_t(hips)]);
        const glm::mat3 to = basis(p("thigh_l", frame) - p("thigh_r", frame), p("spine_05", frame) - p("pelvis", frame));
        const glm::mat3 align = to * glm::transpose(from);
        for(int s = 0; s < 4; ++s){
            const glm::vec3 src = glm::normalize(align * (source[size_t(joint(segments[s].to))] - source[size_t(joint(segments[s].from))]));
            const glm::vec3 dst = glm::normalize(p(segments[s].boneTo, frame) - p(segments[s].boneFrom, frame));
            const float angle = glm::degrees(std::acos(std::clamp(glm::dot(src, dst), -1.0f, 1.0f)));
            if(angle > worst[s]){ worst[s] = angle; worstFrame[s] = f; }
            if(argc > 2 && f % 10 == 0) std::printf("   kadar %zu seg %d: %.0f st src %.2f %.2f %.2f dst %.2f %.2f %.2f\n", f, s, angle, src.x, src.y, src.z, dst.x, dst.y, dst.z);
        }
    }
    const char* names[4] = {"lijeva nadlaktica", "lijeva podlaktica", "desna nadlaktica", "desna podlaktica"};
    float all = 0.0f;
    for(int s = 0; s < 4; ++s){
        std::printf("   MJERA %s: najvise %.0f st (izvorni kadar %zu)\n", names[s], worst[s], worstFrame[s]);
        all = std::max(all, worst[s]);
    }
    report.check("ruke prate izvor (nadlaktica i podlaktica do 15 st u svakom kadru; A-poza lika bila je 60-70 st)", all < 15.0f, fmt("najvise %.0f st", all));
    return report.result();
}
