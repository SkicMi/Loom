// Pokret u sceni: klip iz WeaverMotion modula (Kimodo) kao kostur u Warpu.
//
// ZASTO SE OVO TESTIRA. Lik se u editoru postavlja u scenu iz matchmovea - uz kameru s 50 fps
// snimke. Tri stvari mogu tiho promasiti:
//
//   VRIJEME       klip od 30 fps prepisan na timeline scene kadar-za-kadar hoda 5/3 puta
//                 prebrzo; a klip koji preuzme timeline pokvari kameru iz solvea
//   MJESTO        lik mora poceti ondje gdje ga je editor postavio, ne u ishodistu klipa
//   RUPE          zglob koji se pomice mora imati kljuc u SVAKOM kadru; kljuc samo gdje pomak
//                 nije nula ostavi kadar u kojem zglob drzi prosli polozaj
//
// Prvu verziju testa (i uvoza) napisao je GPT; ova provjerava zamjenu
#include "TestHarness.h"
#include "../src/LoomWeaverMotion.h"

#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace{

float distance(const glm::vec3& a, const glm::vec3& b){ return glm::length(a - b); }

Engine::WeaverMotion::Clip twoJointClip(){
    Engine::WeaverMotion::Clip clip;
    clip.framesPerSecond = 30.0;
    clip.joints.push_back({"Pelvis", -1, glm::vec3(0.0f, 1.0f, 0.0f)});
    clip.joints.push_back({"Chest", 0, glm::vec3(0.0f, 0.5f, 0.0f)});
    clip.joints.push_back({"Hand", 1, glm::vec3(0.3f, 0.2f, 0.0f)});
    for(int f = 0; f < 3; ++f){
        Engine::WeaverMotion::Pose pose;
        pose.translations = {glm::vec3(0.1f + 0.1f * f, 0.2f, 0.3f), glm::vec3(0.0f),
                             //Saka se pomakne samo u drugom kadru - u prvom i trecem je nula
                             f == 1 ? glm::vec3(0.0f, 0.05f, 0.0f) : glm::vec3(0.0f)};
        pose.rotations = {f == 0 ? glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1)) : glm::quat(1, 0, 0, 0),
                          glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0)};
        clip.frames.push_back(pose);
    }
    return clip;
}

struct FloorGltfFixture{
    std::filesystem::path directory;
    std::filesystem::path gltf;
    bool ready = false;
};

FloorGltfFixture makeFloorGltfFixture(){
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    FloorGltfFixture fixture;
    fixture.directory = std::filesystem::temp_directory_path() /
        ("loom_motion_floor_test_" + std::to_string(stamp));
    fixture.gltf = fixture.directory / "mesh.gltf";
    std::error_code error;
    std::filesystem::create_directories(fixture.directory, error);
    if(error) return fixture;

    const std::vector<float> vertices{
        0.0f, -0.2f, 0.0f,
        0.0f, 1.7f, 0.0f,
        0.1f, 0.0f, 0.1f
    };
    {
        std::ofstream binary(fixture.directory / "mesh.bin", std::ios::binary);
        if(!binary) return fixture;
        binary.write(reinterpret_cast<const char*>(vertices.data()),
                     std::streamsize(vertices.size() * sizeof(float)));
        if(!binary) return fixture;
    }
    {
        std::ofstream gltf(fixture.gltf);
        if(!gltf) return fixture;
        gltf << "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
                "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
                "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
                "\"buffers\":[{\"uri\":\"mesh.bin\",\"byteLength\":36}],"
                "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
                "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}]}";
        if(!gltf) return fixture;
    }
    fixture.ready = true;
    return fixture;
}

Engine::WeaverMotion::Clip groundedRigMotion(){
    Engine::WeaverMotion::Clip clip;
    clip.framesPerSecond = 30.0;
    clip.joints = {
        {"Root", -1, glm::vec3(0.0f)},
        {"Hips", 0, glm::vec3(0.0f, 0.9f, 0.0f)},
        {"Spine1", 1, glm::vec3(0.0f, 0.3f, 0.0f)},
        {"Chest", 2, glm::vec3(0.0f, 0.2f, 0.0f)},
        {"Neck1", 3, glm::vec3(0.0f, 0.1f, 0.0f)},
        {"Head", 4, glm::vec3(0.0f, 0.2f, 0.0f)},
        {"LeftLeg", 1, glm::vec3(0.12f, -0.5f, 0.0f)},
        {"LeftShin", 6, glm::vec3(0.0f, -0.2f, 0.0f)},
        {"LeftFoot", 7, glm::vec3(0.0f, -0.2f, 0.0f)},
        {"LeftToeBase", 8, glm::vec3(0.0f, 0.0f, 0.1f)},
        {"RightLeg", 1, glm::vec3(-0.12f, -0.5f, 0.0f)},
        {"RightShin", 10, glm::vec3(0.0f, -0.2f, 0.0f)},
        {"RightFoot", 11, glm::vec3(0.0f, -0.2f, 0.0f)},
        {"RightToeBase", 12, glm::vec3(0.0f, 0.0f, 0.1f)}
    };
    Engine::WeaverMotion::Pose pose;
    pose.translations.assign(clip.joints.size(), glm::vec3(0.0f));
    pose.translations[0] = glm::vec3(0.3f, 0.0f, 0.4f);
    pose.rotations.assign(clip.joints.size(), glm::quat(1, 0, 0, 0));
    clip.frames.push_back(pose);
    Engine::WeaverMotion::Pose second = pose;
    second.translations[0].x += 0.2f;
    clip.frames.push_back(second);
    Engine::WeaverMotion::Pose third = second;
    third.translations[0].x += 0.2f;
    clip.frames.push_back(third);
    return clip;
}
}

int main(){
    TestReport report("weaver_motion_stage");
    const Engine::WeaverMotion::Clip clip = twoJointClip();

    //-- 1. prazna scena: klip preuzme timeline, lik pocinje u ishodistu ----------------------
    {
        Warp::Stage stage;
        const Loom::WeaverMotionImportReport imported = Loom::importWeaverMotionClip(stage, clip, "KimodoWalk");
        report.check("uvoz u praznu scenu", imported.problem.empty() && imported.joints == 3 && imported.frames == 3 &&
                                             stage.startFrame == 1.0 && stage.endFrame == 3.0 && stage.framesPerSecond == 30.0,
            imported.problem.empty() ? fmt("%zu zgloba, kadrovi %.0f-%.0f @ %.0f", imported.joints, stage.startFrame,
                                           stage.endFrame, stage.framesPerSecond) : imported.problem);

        const Warp::Id pelvis = stage.find("/KimodoWalk/Pelvis");
        const Warp::Id chest = stage.find("/KimodoWalk/Pelvis/Chest");
        const bool clean = pelvis == imported.root && chest != Warp::None && stage.get(chest)->joint &&
                           stage.get(pelvis)->joint && stage.size() == 4;
        report.check("stablo: samo zglobovi, bez kocke po zglobu", clean, fmt("%zu entiteta", stage.size()));

        //Korijen krece u ishodistu (vodoravno), na svojoj visini; dijete prati rotaciju korijena
        const glm::vec3 pelvisAt1 = glm::vec3(stage.worldMatrix(pelvis, 1.0)[3]);
        const glm::vec3 pelvisAt2 = glm::vec3(stage.worldMatrix(pelvis, 2.0)[3]);
        const glm::vec3 chestAt1 = glm::vec3(stage.worldMatrix(chest, 1.0)[3]);
        report.check("korijen krece na mjestu postavljanja, dijete prati rotaciju",
            distance(pelvisAt1, glm::vec3(0.0f)) < 1e-5f && distance(pelvisAt2, glm::vec3(0.1f, 0.0f, 0.0f)) < 1e-5f &&
            distance(chestAt1, glm::vec3(-0.5f, 0.0f, 0.0f)) < 1e-5f,
            fmt("korijen %.2f %.2f %.2f -> %.2f %.2f %.2f; prsa %.2f %.2f %.2f", pelvisAt1.x, pelvisAt1.y, pelvisAt1.z,
                pelvisAt2.x, pelvisAt2.y, pelvisAt2.z, chestAt1.x, chestAt1.y, chestAt1.z));

        const Warp::Entity* hand = stage.get(stage.find("/KimodoWalk/Pelvis/Chest/Hand"));
        report.check("zglob koji se pomice ima kljuc u svakom kadru",
            hand && hand->translationKeys.size() == 3 && stage.get(chest)->translationKeys.empty() &&
            distance(stage.localAt(hand->id, 3.0).translation, glm::vec3(0.3f, 0.2f, 0.0f)) < 1e-6f,
            hand ? fmt("saka %zu kljuca, u kadru 3 na mjestu mirovanja", hand->translationKeys.size()) : "nema sake");
        report.check("visina je razlika rest-pose bounds, ne od origin-a do vrha",
                      std::fabs(imported.height - 0.7f) < 1e-5f, fmt("%.3f", imported.height));
    }

    //-- 2. scena iz matchmovea: vrijeme scene, mjesto i mjerilo ------------------------------------
    {
        Warp::Stage stage;
        stage.startFrame = 1.0; stage.endFrame = 381.0; stage.framesPerSecond = 50.0;
        Loom::MotionPlacement placement;
        placement.startFrame = 100.0;
        placement.sceneFps = 50.0;
        placement.position = glm::vec3(3.0f, -0.5f, 7.0f);
        placement.scale = 2.0f;
        const Loom::WeaverMotionImportReport imported = Loom::importWeaverMotionClip(stage, clip, "Lik", placement);
        const Warp::Entity* pelvis = stage.get(imported.root);
        const double second = pelvis && pelvis->rotationKeys.size() > 1 ? pelvis->rotationKeys.times[1] : -1.0;
        report.check("klip od 30 fps u sceni od 50: kadar klipa = 5/3 kadra scene, timeline ostaje",
            std::fabs(second - (100.0 + 50.0 / 30.0)) < 1e-9 && stage.endFrame == 381.0 && stage.framesPerSecond == 50.0 &&
            std::fabs(imported.lastFrame - (100.0 + 2.0 * 50.0 / 30.0)) < 1e-9,
            fmt("drugi kljuc u %.4f, klip do %.2f; timeline %.0f-%.0f @ %.0f", second, imported.lastFrame,
                stage.startFrame, stage.endFrame, stage.framesPerSecond));

        const glm::vec3 start = glm::vec3(stage.worldMatrix(imported.root, 100.0)[3]);
        report.check("lik pocinje na mjestu postavljanja, s mjerilom",
            distance(start, glm::vec3(3.0f, -0.5f, 7.0f)) < 1e-5f,
            fmt("korijen u (%.3f, %.3f, %.3f)", start.x, start.y, start.z));
    }

    //-- mapped rig: rest pose, root and feet share the selected character's floor -----------------
    {
        Warp::Stage stage;
        const FloorGltfFixture floorFixture = makeFloorGltfFixture();
        const Warp::Id mascot = stage.create("Mascot");
        stage.get(mascot)->local.translation.y = 0.2f;
        Warp::Model mascotMesh;
        mascotMesh.path = floorFixture.gltf.string();
        stage.get(mascot)->model = mascotMesh;
        const auto addJoint = [&](const std::string& name, Warp::Id parent, const glm::vec3& offset){
            const Warp::Id id = stage.create(name, parent);
            stage.get(id)->local.translation = offset;
            stage.get(id)->joint = Warp::Joint{};
            return id;
        };
        const Warp::Id hips = addJoint("Hips", mascot, glm::vec3(0.0f, 0.9f, 0.0f));
        const Warp::Id spine1 = addJoint("Spine1", hips, glm::vec3(0.0f, 0.3f, 0.0f));
        const Warp::Id chest = addJoint("Chest", spine1, glm::vec3(0.0f, 0.2f, 0.0f));
        const Warp::Id neck = addJoint("Neck1", chest, glm::vec3(0.0f, 0.1f, 0.0f));
        addJoint("Head", neck, glm::vec3(0.0f, 0.2f, 0.0f));
        const Warp::Id leftThigh = addJoint("LeftUpLeg", hips, glm::vec3(0.12f, -0.5f, 0.0f));
        const Warp::Id leftShin = addJoint("LeftLeg", leftThigh, glm::vec3(0.0f, -0.2f, 0.0f));
        const Warp::Id leftFoot = addJoint("LeftFoot", leftShin, glm::vec3(0.0f, -0.2f, 0.0f));
        addJoint("LeftToeBase", leftFoot, glm::vec3(0.0f, 0.0f, 0.1f));
        const Warp::Id rightThigh = addJoint("RightUpLeg", hips, glm::vec3(-0.12f, -0.5f, 0.0f));
        const Warp::Id rightShin = addJoint("RightLeg", rightThigh, glm::vec3(0.0f, -0.2f, 0.0f));
        const Warp::Id rightFoot = addJoint("RightFoot", rightShin, glm::vec3(0.0f, -0.2f, 0.0f));
        addJoint("RightToeBase", rightFoot, glm::vec3(0.0f, 0.0f, 0.1f));

        const Engine::WeaverMotion::Clip fittedClip = groundedRigMotion();
        Loom::MotionRigFit fit;
        std::string fitProblem;
        const bool fitOk = Loom::fitMotionToRigRestPose(fittedClip, stage, mascot, fit, fitProblem);
        const float expectedFitScale = 1.9f / 1.7f;
        report.check("mapiranje Kimodo Hips i named bones", floorFixture.ready && fitOk && fit.mappedJoints >= 10 &&
                     fit.profile == "named bones", fitOk ? fmt("%zu mapped, scale %.3f, floor source %s",
                         fit.mappedJoints, fit.scale, fit.floorSource.c_str()) : fitProblem);
        report.check("fit koristi stvarne mesh bounds, kukove i pod",
                     fitOk && std::fabs(fit.scale - expectedFitScale) < 1e-5f &&
                     distance(fit.translation, glm::vec3(-0.3f * expectedFitScale, -0.2f, -0.4f * expectedFitScale)) < 1e-5f &&
                     fit.floorSource == "GLB mesh bounds",
                     fitOk ? fmt("scale %.3f; translate %.3f %.3f %.3f (%s)", fit.scale,
                                 fit.translation.x, fit.translation.y, fit.translation.z, fit.floorSource.c_str()) : fitProblem);

        const Loom::MotionRigMapping legMapping = Loom::mapMotionBones(
            fittedClip, Loom::motionRigRestPose(stage, mascot));
        report.check("Kimodo LeftLeg/LeftShin map to Manny thigh/calf",
                     legMapping.targetBySource.size() > 11 &&
                     legMapping.targetBySource[6] == leftThigh && legMapping.targetBySource[7] == leftShin &&
                     legMapping.targetBySource[10] == rightThigh && legMapping.targetBySource[11] == rightShin,
                     fmt("left %llu/%llu, right %llu/%llu",
                         static_cast<unsigned long long>(legMapping.targetBySource[6]),
                         static_cast<unsigned long long>(legMapping.targetBySource[7]),
                         static_cast<unsigned long long>(legMapping.targetBySource[10]),
                         static_cast<unsigned long long>(legMapping.targetBySource[11])));

        const size_t entitiesBefore = stage.size();
        const float restLeftFootY = glm::vec3(stage.worldMatrix(leftFoot, 1.0)[3]).y;
        stage.endFrame = 1.0;
        const Warp::Id targetHead = stage.find("/Mascot/Hips/Spine1/Chest/Neck1/Head");
        stage.get(targetHead)->rotationKeys.set(5.0, glm::quat(1,0,0,0));
        stage.get(targetHead)->rotationKeys.set(6.0, glm::angleAxis(0.4f, glm::vec3(0,0,1)));
        Loom::MotionPlacement placement;
        placement.parent = mascot;
        placement.startFrame = 1.0;
        placement.sceneFps = stage.framesPerSecond;
        placement.fitToParentRig = true;
        const Loom::WeaverMotionImportReport imported =
            Loom::importWeaverMotionClip(stage, fittedClip, "FittedMotion", placement);
        const Warp::Entity* retargetedHips = stage.get(hips);
        const Warp::Animator* animator = stage.get(mascot)->animator ? &*stage.get(mascot)->animator : nullptr;
        const double secondFrame = 1.0 + stage.framesPerSecond / fittedClip.framesPerSecond;
        const glm::vec3 hipsAtStart = glm::vec3(stage.worldMatrix(hips, 1.0)[3]);
        const glm::vec3 hipsAfterRootMotion = glm::vec3(stage.worldMatrix(hips, secondFrame)[3]);
        const glm::vec3 actorAtStart = glm::vec3(stage.worldMatrix(mascot, 1.0)[3]);
        const glm::vec3 actorAfterRootMotion = glm::vec3(stage.worldMatrix(mascot, secondFrame)[3]);
        const Warp::Id targetLeftFoot = stage.find("/Mascot/Hips/LeftUpLeg/LeftLeg/LeftFoot");
        const glm::vec3 headAtStart = glm::vec3(stage.worldMatrix(targetHead, 1.0)[3]);
        const float floorY = glm::vec3(stage.worldMatrix(targetLeftFoot, 1.0)[3]).y;
        auto trackFor = [](const Warp::AnimationClip& animation, Warp::Id id) -> const Warp::AnimatorTrack*{
            for(const Warp::AnimatorTrack& track : animation.tracks) if(track.target == id) return &track;
            return nullptr;
        };
        const bool clipStored = animator && animator->animations.size() == 2 && animator->activeAnimation == 1 &&
            animator->animations[0].name == "Imported animation" &&
            trackFor(animator->animations[0], targetHead) && trackFor(animator->animations[0], targetHead)->rotationKeys.size() == 2 &&
            trackFor(animator->animations[1], hips) && trackFor(animator->animations[1], hips)->rotationKeys.size() == fittedClip.frames.size() &&
            trackFor(animator->animations[1], hips)->translationKeys.size() == fittedClip.frames.size() &&
            trackFor(animator->animations[1], mascot) && trackFor(animator->animations[1], mascot)->rootMotion &&
            trackFor(animator->animations[1], mascot)->translationKeys.size() == fittedClip.frames.size() &&
            trackFor(animator->animations[1], stage.find("/Mascot/Hips/Spine1/Chest")) &&
            retargetedHips && retargetedHips->rotationKeys.empty() && retargetedHips->translationKeys.empty() &&
            stage.get(targetHead)->rotationKeys.empty();
        report.check("Kimodo becomes an Animator clip and existing motion is migrated",
                     imported.problem.empty() && imported.group == mascot && imported.root == hips &&
                     imported.mappedJoints >= 10 && clipStored &&
                     stage.endFrame >= imported.lastFrame && stage.find("/Mascot/FittedMotion") == Warp::None && stage.size() == entitiesBefore,
                     imported.problem.empty() ? fmt("%zu bones, %zu clips; no preview hierarchy added", imported.mappedJoints,
                                                     animator ? animator->animations.size() : 0) : imported.problem);
        report.check("retarget moves the character origin with Kimodo root travel",
                     distance(hipsAtStart, glm::vec3(0.0f, 1.1f, 0.0f)) < 1e-5f &&
                     std::fabs(actorAtStart.x) < 1e-5f &&
                     std::fabs(actorAfterRootMotion.x - 0.2f * expectedFitScale) < 1e-5f &&
                     std::fabs(hipsAfterRootMotion.x - actorAfterRootMotion.x) < 1e-5f &&
                     std::fabs(imported.importedScale - expectedFitScale) < 1e-5f &&
                     imported.floorSource == "GLB mesh bounds" && std::fabs(floorY - restLeftFootY) < 1e-5f &&
                     headAtStart.y > floorY,
                     fmt("origin %.3f -> %.3f; hips %.3f; floor %.3f; scale %.3f", actorAtStart.x,
                         actorAfterRootMotion.x, hipsAfterRootMotion.x, floorY, imported.importedScale));
        Engine::WeaverMotion::Clip plantedClip = fittedClip;
        for(Engine::WeaverMotion::Pose& pose : plantedClip.frames)
            pose.translations[0] = glm::vec3(0.3f, 0.0f, 0.4f);
        const Loom::WeaverMotionImportReport planted =
            Loom::importWeaverMotionClip(stage, plantedClip, "PlantedFeet", placement);
        const Warp::Animator* plantedAnimator = stage.get(mascot)->animator ? &*stage.get(mascot)->animator : nullptr;
        const Warp::AnimatorTrack* plantedThighTrack = plantedAnimator && plantedAnimator->activeAnimation < plantedAnimator->animations.size()
            ? trackFor(plantedAnimator->animations[plantedAnimator->activeAnimation], leftThigh) : nullptr;
        report.check("stationary Kimodo feet trigger both-leg contact IK",
                     planted.problem.empty() && planted.footContactFrames == plantedClip.frames.size() * 2 &&
                     plantedThighTrack && plantedThighTrack->rotationKeys.size() == plantedClip.frames.size(),
                     fmt("contact frames %zu; thigh keys %zu", planted.footContactFrames,
                         plantedThighTrack ? plantedThighTrack->rotationKeys.size() : 0));
        std::filesystem::remove_all(floorFixture.directory);
    }

    //-- UniRig mapping is enabled only for the complete verified 52-bone parent signature ---------
    {
        Warp::Stage stage;
        const Warp::Id mascot = stage.create("UniRigMascot");
        const std::array<int, 52> parents = Loom::motionUniRigExpectedParents();
        std::array<Warp::Id, 52> ids{};
        for(size_t i = 0; i < ids.size(); ++i){
            const Warp::Id parent = parents[i] < 0 ? mascot : ids[size_t(parents[i])];
            ids[i] = stage.create("bone_" + std::to_string(i), parent);
            Warp::Entity& entity = *stage.get(ids[i]);
            entity.joint = Warp::Joint{};
            if(i == 6 || i == 44) entity.local.translation.x = 0.1f;
            if(i == 25 || i == 48) entity.local.translation.x = -0.1f;
            if(i == 5) entity.local.translation.y = 1.0f;
        }
        const Loom::MotionRigRestPose rest = Loom::motionRigRestPose(stage, mascot);
        std::array<Warp::Id, 52> verified{};
        const bool profileOk = Loom::motionFindVerifiedUniRig52(rest, verified);
        Engine::WeaverMotion::Clip canonical;
        canonical.joints = {
            {"Hips", -1, glm::vec3(0.0f)},
            {"Spine1", 0, glm::vec3(0.0f, 0.2f, 0.0f)},
            {"Chest", 1, glm::vec3(0.0f, 0.2f, 0.0f)},
            {"LeftShoulder", 2, glm::vec3(0.1f, 0.0f, 0.0f)},
            {"LeftArm", 3, glm::vec3(0.2f, 0.0f, 0.0f)},
            {"RightArm", 2, glm::vec3(-0.2f, 0.0f, 0.0f)},
            {"LeftLeg", 0, glm::vec3(0.1f, -0.3f, 0.0f)},
            {"RightToeBase", 6, glm::vec3(-0.1f, -0.5f, 0.0f)}
        };
        const Loom::MotionRigMapping mapping = Loom::mapMotionBones(canonical, rest);
        const bool boneMatches = mapping.problem.empty() && mapping.profile == "verified UniRig 52" &&
            mapping.targetBySource[0] == ids[0] && mapping.targetBySource[3] == ids[6] &&
            mapping.targetBySource[4] == ids[7] && mapping.targetBySource[5] == ids[26] &&
            mapping.targetBySource[6] == ids[44] && mapping.targetBySource[7] == ids[51];
        report.check("UniRig 52 profile and named Kimodo-to-bone slots", profileOk && boneMatches,
                     fmt("%s; %zu matches", mapping.profile.c_str(), mapping.matched));
        const bool changedTree = stage.reparent(ids[48], ids[44]);
        const Loom::MotionRigRestPose malformed = Loom::motionRigRestPose(stage, mascot);
        std::array<Warp::Id, 52> rejected{};
        report.check("generic bone_N topology change is rejected",
                     changedTree && !Loom::motionFindVerifiedUniRig52(malformed, rejected),
                     changedTree ? "altered leg parent disables profile" : "could not alter test hierarchy");
    }

    //-- prazan klip se odbija ----------------------------------------------------------------------
    {
        Engine::WeaverMotion::Clip empty;
        Warp::Stage badStage;
        const Loom::WeaverMotionImportReport bad = Loom::importWeaverMotionClip(badStage, empty, "Bad");
        report.check("prazan klip ne stvara nista", !bad.problem.empty() && badStage.size() == 0, bad.problem);
    }

    return report.result();
}
