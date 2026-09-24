#include "TestHarness.h"
#include "../src/LoomAutoRig.h"
#include "../src/LoomModel.h"
#include <Warp/Project.h>
#include <chrono>
#include <filesystem>
#include <fstream>

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
