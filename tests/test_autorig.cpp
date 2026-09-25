#include "TestHarness.h"
#include "../src/LoomAutoRig.h"
#include "../src/LoomModel.h"
#include <Warp/Project.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

void appendFloat(std::vector<uint8_t>& bytes, float value){
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for(uint32_t shift = 0; shift < 32; shift += 8) bytes.push_back(uint8_t((bits >> shift) & 0xffu));
}

std::string base64(const std::vector<uint8_t>& bytes){
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    for(size_t i = 0; i < bytes.size(); i += 3){
        const uint32_t a = bytes[i];
        const uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const uint32_t value = (a << 16u) | (b << 8u) | c;
        encoded.push_back(alphabet[(value >> 18u) & 63u]);
        encoded.push_back(alphabet[(value >> 12u) & 63u]);
        encoded.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6u) & 63u] : '=');
        encoded.push_back(i + 2 < bytes.size() ? alphabet[value & 63u] : '=');
    }
    return encoded;
}

int main(){
    TestReport report("Auto Rig import and panel");
    namespace fs = std::filesystem;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path directory = fs::temp_directory_path() / ("loom-autorig-test-" + std::to_string(stamp));
    fs::create_directory(directory);
    const fs::path path = directory / "rig.gltf";
    const auto write = [&](const std::string& joints){
        std::ofstream(path) << R"({"asset":{"version":"2.0"},"nodes":[{"name":"hips","translation":[0,1,0],"children":[1]},)"
                              R"({"name":"chest","translation":[0,1,0]},{"name":"plain"}],"scenes":[{"nodes":[0,2]}],"skins":[{"joints":)"
                           << joints << "}]}";
    };
    write("[0,1]");
    Spool::GltfScene scene;
    std::string error;
    const bool loaded = Spool::loadGltf(path.string(), scene, error);
    report.check("skin joints are read, ordinary nodes are not marked", loaded && scene.nodes.size() == 3 &&
        scene.nodes[0].joint && scene.nodes[1].joint && !scene.nodes[2].joint, error);
    if(loaded){
        Warp::Stage stage;
        const auto imported = Loom::importGltf(stage, scene);
        const auto hips = stage.find("/rig/hips"), chest = stage.find("/rig/hips/chest");
        const auto plain = stage.find("/rig/plain");
        report.check("joint hierarchy and rest transforms survive scene import", imported.problem.empty() &&
            stage.get(hips) && stage.get(chest) && stage.get(hips)->joint && stage.get(chest)->joint &&
            stage.get(plain) && !stage.get(plain)->joint &&
            glm::length(glm::vec3(stage.worldMatrix(chest, 1)[3]) - glm::vec3(0,2,0)) < 1e-6f, imported.problem);
        const fs::path project = directory / "rig.usda";
        Warp::Stage restored;
        const bool persisted = Warp::saveProject(stage, project.string(), error) && Warp::loadProject(project.string(), restored, error);
        const auto* joint = restored.get(restored.find("/rig/hips/chest"));
        report.check("rig joints survive project save/reopen", persisted && joint && joint->joint, error);
    }
    // A compact GLTF skin with real byte joint indices, float weights, and inverse bind matrices.
    std::vector<uint8_t> skinBytes;
    for(float value : {0.0f,0.0f,0.0f, 1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f}) appendFloat(skinBytes, value);
    skinBytes.insert(skinBytes.end(), {0,0,0,0, 0,1,0,0, 1,0,0,0});
    for(float value : {1.0f,0.0f,0.0f,0.0f, 0.5f,0.5f,0.0f,0.0f, 1.0f,0.0f,0.0f,0.0f}) appendFloat(skinBytes, value);
    for(int matrix = 0; matrix < 2; ++matrix){
        for(int element = 0; element < 16; ++element) appendFloat(skinBytes, element % 5 == 0 ? 1.0f : 0.0f);
    }
    const fs::path skinnedPath = directory / "skinned.gltf";
    {
        std::ofstream gltf(skinnedPath);
        gltf << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Hips","children":[1,2]},{"name":"Chest","translation":[0,1,0]},{"name":"Body","mesh":0,"skin":0}],"skins":[{"skeleton":0,"joints":[0,1],"inverseBindMatrices":3}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]}],"buffers":[{"byteLength":)"
             << skinBytes.size() << R"(,"uri":"data:application/octet-stream;base64,)" << base64(skinBytes)
             << R"("}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":128}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5126,"count":2,"type":"MAT4"}]})";
    }
    Spool::GltfScene skinnedScene;
    error.clear();
    const bool skinnedLoaded = Spool::loadGltf(skinnedPath.string(), skinnedScene, error);
    const Spool::GltfPrimitive* skinPrimitive = skinnedLoaded ? &skinnedScene.meshes[0].primitives[0] : nullptr;
    report.check("GLTF reads joint bytes, weights, skins, and inverse bind matrices",
        skinnedLoaded && skinnedScene.skins.size() == 1 && skinnedScene.nodes[2].skin == 0 &&
        skinnedScene.skins[0].joints == std::vector<int>{0,1} &&
        skinnedScene.skins[0].inverseBindMatrices.size() == 32 &&
        skinPrimitive && skinPrimitive->jointIndices.size() == 12 && skinPrimitive->jointWeights.size() == 12 &&
        skinPrimitive->jointIndices[5] == 1 && std::fabs(skinPrimitive->jointWeights[4] - 0.5f) < 1e-6f, error);
    if(skinnedLoaded){
        Warp::Stage skinStage;
        const auto imported = Loom::importGltf(skinStage, skinnedScene);
        const Warp::Id hips = skinStage.find("/skinned/Hips");
        const Warp::Id chest = skinStage.find("/skinned/Hips/Chest");
        const Warp::Entity* body = skinStage.get(skinStage.find("/skinned/Hips/Body"));
        report.check("GLTF import binds mesh to the corresponding joint entities",
            imported.problem.empty() && body && body->model && body->model->skin == 0 &&
            body->model->skinJoints == std::vector<Warp::Id>{hips,chest} &&
            body->model->skinJointPaths == std::vector<std::string>{skinStage.path(hips),skinStage.path(chest)}, imported.problem);
        Warp::Stage restoredSkin;
        const fs::path skinProject = directory / "skinned.usda";
        const bool skinProjectRoundTrip = Warp::saveProject(skinStage, skinProject.string(), error) &&
                                           Warp::loadProject(skinProject.string(), restoredSkin, error);
        const Warp::Entity* restoredBody = restoredSkin.get(restoredSkin.find("/skinned/Hips/Body"));
        report.check("skin joint links resolve after project reopen", skinProjectRoundTrip && restoredBody && restoredBody->model &&
            restoredBody->model->skin == 0 && restoredBody->model->skinJoints.size() == 2 &&
            restoredBody->model->skinJoints[0] == restoredSkin.find("/skinned/Hips") &&
            restoredBody->model->skinJoints[1] == restoredSkin.find("/skinned/Hips/Chest"), error);
    }
    for(const std::string bad : {"[-1]", "[3]", "[0.5]", "[\"0\"]"}){
        write(bad);
        report.check(("reject invalid joint index " + bad).c_str(), !Spool::loadGltf(path.string(), scene, error) &&
            error.find("joint index") != std::string::npos, error);
    }
    // Click every screen row: unavailable/running states must never submit; ready state must have a usable button.
    auto submissions = [&](bool ready, bool busy, const std::string& source){
        int count = 0;
        for(int y = 0; y < 700; y += 4){
            Treadle::Ui ui;
            Loom::AutoRigState state;
            state.open = true; state.source = source;
            float scroll = 0;
            Treadle::Input input;
            input.mouseX = 100; input.mouseY = float(y);
            ui.begin(input, 900, 800);
            Loom::drawAutoRigPanel(ui, state, {0,0,560,720}, ready, busy, busy, "", scroll);
            ui.end();
            input.down[uint32_t(Treadle::MouseButton::Left)] = true;
            ui.begin(input, 900, 800);
            const auto action = Loom::drawAutoRigPanel(ui, state, {0,0,560,720}, ready, busy, busy, "", scroll);
            ui.end();
            if(action.generate) ++count;
        }
        return count;
    };
    report.check("ready panel has an actionable generate button", submissions(true, false, "/tmp/source.glb") > 0, "actual Treadle input events");
    report.check("missing backend, empty path and busy job cannot submit", submissions(false, false, "/tmp/source.glb") == 0 &&
        submissions(true, false, "") == 0 && submissions(true, true, "/tmp/source.glb") == 0, "negative UI controls");
    fs::remove_all(directory);
    return report.result();
}
