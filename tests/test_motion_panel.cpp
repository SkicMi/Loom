// Pokret iz teksta: naredba za Kimodo, niz radnji, povijest - bez prozora i bez Kimoda.
//
// ZASTO SE OVO TESTIRA. Opis ide u ljusku (popen), pa je navodnik u opisu ili tocka na krivom
// mjestu vec problem: tocka dijeli radnje, pa "stop. then run" postane dvije radnje s jednim
// trajanjem, i Kimodo odbije ili tiho uzme krivo trajanje. Naredba se zato provjerava i pravom
// ljuskom (/bin/sh) - argumenti moraju stici tocno onakvi kakvi su napisani.
#include "TestHarness.h"

#include "../src/LoomMotionPanel.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

int main(){
    TestReport report("pokret iz teksta");

    {
        Warp::Stage stage;
        const Warp::Id group = stage.create("Characters");
        auto addRig = [&](const std::string& name){
            const Warp::Id root = stage.create(name, group);
            const Warp::Id mesh = stage.create("Mesh", root);
            Warp::Model asset;
            asset.path = name + ".glb";
            stage.get(mesh)->model = asset;
            const Warp::Id hip = stage.create("Hip", root);
            stage.get(hip)->joint = Warp::Joint{};
            return root;
        };
        const Warp::Id first = addRig("RigA");
        const Warp::Id second = addRig("RigB");
        const Warp::Id staticId = stage.create("StaticAsset");
        stage.get(staticId)->model = Warp::Model{};
        const Warp::Id previewId = stage.create("SkeletonPreview");
        stage.get(previewId)->joint = Warp::Joint{};
        const std::vector<Loom::MotionCharacter> candidates = Loom::motionCharactersIn(stage);
        report.check("scene selector includes both mesh+joint characters and excludes static mesh/skeleton preview",
            candidates.size() == 2 && candidates[0].id == first && candidates[1].id == second &&
            candidates[0].path == "/Characters/RigA" && candidates[1].path == "/Characters/RigB",
            candidates.size() == 2 ? candidates[0].path + " | " + candidates[1].path : std::to_string(candidates.size()));
    }

    const std::vector<std::string>& models = Loom::kimodoModels();
    report.check("UI model choices match all seven Kimodo registry variants", models.size() == 7 &&
                 std::find(models.begin(), models.end(), "Kimodo-G1-RP-v1") != models.end() &&
                 std::find(models.begin(), models.end(), "Kimodo-SMPLX-RP-v1") != models.end(), std::to_string(models.size()));
    Loom::MotionPanelState panel;
    panel.modelIndex = 4;
    panel.samples = 3.0f; panel.transitionFrames = 11.0f; panel.quality = 250.0f;
    panel.cfgIndex = 3; panel.textGuidance = 1.25f; panel.constraintGuidance = 3.5f;
    panel.firstHeadingAngle = 1.2f; panel.rootMargin = 0.13f;
    panel.rootPathEnabled = true; panel.constrainRootHeading = true;
    panel.rootWaypoints = {{0, 0.0f, 0.0f, 1.2f}, {30, 1.0f, 0.5f, 0.8f}};
    panel.targetCharacter = 42; panel.saveExample = true;
    const Loom::MotionRequest panelRequest = panel.request();
    report.check("UI state reaches model/sampling/CFG/constraints/target/example request",
        panelRequest.model == models[4] && panelRequest.numSamples == 3 && panelRequest.transitionFrames == 11 &&
        panelRequest.diffusionSteps == 250 && panelRequest.cfgType == "separated" && panelRequest.textGuidance == 1.25f &&
        panelRequest.constraintGuidance == 3.5f &&
        panelRequest.targetCharacter == 42 && panelRequest.saveExample && panelRequest.firstHeadingAngle == 1.2f &&
        panelRequest.rootMargin == 0.13f && panelRequest.rootWaypoints.size() == 2 &&
        panelRequest.constrainRootHeading, panelRequest.model);
    Loom::MotionPanelState externalConstraintPanel;
    externalConstraintPanel.constraintsPath = "/tmp/constraints input.json";
    report.check("external constraints JSON path is forwarded without a competing authored root path",
        externalConstraintPanel.request().constraints == "/tmp/constraints input.json" &&
        externalConstraintPanel.request().rootWaypoints.empty(), externalConstraintPanel.request().constraints.string());
    Loom::MotionRequest request;
    Loom::MotionAction frameActions[] = {{"walk", 4.2f}, {"turn", 2.5f}};
    const std::vector<Loom::MotionAction> frameActionList{frameActions[0], frameActions[1]};
    report.check("Kimodo multi-action 30 Hz frame span matches CLI durations and does not add transitions",
                 Loom::kimodoMotionFrameCount(frameActionList) == 201 &&
                 Loom::kimodoMotionLastFrame(frameActionList) == 200,
                 std::to_string(Loom::kimodoMotionFrameCount(frameActionList)));

    std::vector<Loom::MotionRootWaypoint> waypoints{
        {0, 0.0f, 0.0f, 0.0f}, {60, 1.0f, 0.0f, 0.5f}, {120, 1.0f, 2.0f, 1.0f}
    };
    const Loom::MotionRootWaypoint midpoint = Loom::motionRootWaypointAt(waypoints, 30);
    Loom::upsertMotionRootWaypoint(waypoints, midpoint, 120);
    const int movedIndex = Loom::moveMotionRootWaypoint(waypoints, 1, 100, 120);
    const int lockedOrigin = Loom::moveMotionRootWaypoint(waypoints, 0, 30, 120);
    report.check("root waypoint insertion interpolates values, retiming cannot cross neighbors, origin stays locked",
                 waypoints.size() == 4 && movedIndex == 1 && waypoints[1].frame == 59 &&
                 std::fabs(waypoints[1].x - 0.5f) < 1e-6f && lockedOrigin == 0 &&
                 waypoints[0].frame == 0 && waypoints[0].x == 0.0f && waypoints[0].z == 0.0f,
                 std::to_string(waypoints.size()));
    const Loom::MotionRootWaypoint afterLast = Loom::motionRootWaypointAt(waypoints, 149);
    Loom::upsertMotionRootWaypoint(waypoints, afterLast, 150);
    report.check("clicking after the last root key creates a key at the clicked frame with held values",
                 waypoints.back().frame == 149 && std::fabs(waypoints.back().x - 1.0f) < 1e-6f &&
                 std::fabs(waypoints.back().z - 2.0f) < 1e-6f,
                 std::to_string(waypoints.back().frame));
    const std::vector<Loom::MotionRootWaypoint> duplicateFrames{{0, 0.0f, 0.0f, 0.0f}, {10, 0.0f, 0.0f, 0.0f}, {10, 1.0f, 0.0f, 0.0f}};
    const std::vector<Loom::MotionRootWaypoint> movedOrigin{{0, 0.1f, 0.0f, 0.0f}};
    report.check("root path validation rejects duplicate frames and a moved canonical origin",
                 !Loom::motionRootPathProblem(duplicateFrames, 120).empty() &&
                 !Loom::motionRootPathProblem(movedOrigin, 120).empty(), "invalid constraint keys accepted");

    const std::vector<Loom::MotionRootWaypoint> serializedWaypoints{
        {0, 0.0f, 0.0f, 0.0f}, {30, 1.25f, -0.5f, 1.57079632679f}
    };
    const std::filesystem::path constraintFixture = LOOM_WEAVERMOTION_FIXTURE;
    std::string constraintProblem;
    const bool serialized = Loom::writeMotionRootConstraints(constraintFixture, serializedWaypoints, true, 59, constraintProblem);
    std::ifstream fixtureInput(constraintFixture);
    const std::string constraintJson((std::istreambuf_iterator<char>(fixtureInput)), std::istreambuf_iterator<char>());
    const bool jsonMatchesKimodoSchema = serialized &&
        constraintJson.find("\"type\": \"root2d\"") != std::string::npos &&
        constraintJson.find("\"frame_indices\": [0, 30]") != std::string::npos &&
        constraintJson.find("\"smooth_root_2d\": [[0.000000, 0.000000], [1.250000, -0.500000]]") != std::string::npos &&
        constraintJson.find("\"global_root_heading\": [[1.000000, 0.000000], [0.000000, 1.000000]]") != std::string::npos;
    report.check("root2d JSON serializes 0-based frames, XZ meters, and cosine/sine headings", jsonMatchesKimodoSchema,
                 constraintProblem.empty() ? constraintJson : constraintProblem);

    const std::filesystem::path plainConstraintFile = constraintFixture.string() + ".no_heading";
    const bool plainWritten = Loom::writeMotionRootConstraints(plainConstraintFile, serializedWaypoints, false, 59, constraintProblem);
    std::ifstream plainInput(plainConstraintFile);
    const std::string plainJson((std::istreambuf_iterator<char>(plainInput)), std::istreambuf_iterator<char>());
    report.check("root path without heading omits optional global_root_heading",
                 plainWritten && plainJson.find("global_root_heading") == std::string::npos, constraintProblem);
    std::filesystem::remove(plainConstraintFile);

    std::vector<Loom::MotionRootWaypoint> outOfRange = serializedWaypoints;
    outOfRange.back().frame = 60;
    const bool invalidAccepted = Loom::writeMotionRootConstraints(constraintFixture, outOfRange, false, 59, constraintProblem);
    std::ifstream unchangedInput(constraintFixture);
    const std::string unchangedJson((std::istreambuf_iterator<char>(unchangedInput)), std::istreambuf_iterator<char>());
    report.check("out-of-range keyframes are rejected without overwriting the last valid constraints file",
                 !invalidAccepted && unchangedJson == constraintJson && !constraintProblem.empty(), constraintProblem);

    request.actions = {{"a person walks forward. slowly", 3.0f}, {"  ", 2.0f}, {"sits down on it's chair\n", 2.5f}};
    request.seed = 7;
    request.diffusionSteps = 60;
    request.footCleanup = false;

    //-- 1. prazne radnje ispadaju, tocka u opisu postaje zarez ------------------------------------
    const std::vector<Loom::MotionAction> filled = Loom::filledActions(request.actions);
    report.check("prazna radnja ispada, tocka unutar opisa ne dijeli radnju",
        filled.size() == 2 && filled[0].prompt == "a person walks forward, slowly" && filled[1].prompt == "sits down on it's chair",
        filled.empty() ? "" : filled[0].prompt + " | " + filled.back().prompt);

    //-- 2. naredba: pravom ljuskom stizu tocno ti argumenti ----------------------------------------
    {
        //Umjesto kimodo_gen: skripta koja ispise svoje argumente, svaki u svom retku
        const std::filesystem::path echo = std::filesystem::temp_directory_path() / "loom kimodo echo.sh";
        {
            std::ofstream script(echo);
            script << "#!/bin/sh\nfor a in \"$@\"; do printf '%s\\n' \"$a\"; done\n";
        }
        std::filesystem::permissions(echo, std::filesystem::perms::owner_all);
        const std::string command = Loom::buildMotionCommand(echo, request, "/tmp/izlaz s razmakom/motion_1");
        FILE* shell = popen(command.c_str(), "r");
        std::vector<std::string> args;
        char line[512];
        while(shell && std::fgets(line, sizeof(line), shell)){
            std::string a(line);
            if(!a.empty() && a.back() == '\n') a.pop_back();
            args.push_back(a);
        }
        const int status = shell ? pclose(shell) : -1;
        auto has = [&](const std::string& flag, const std::string& value){
            for(size_t i = 0; i + 1 < args.size(); ++i) if(args[i] == flag && args[i + 1] == value) return true;
            return false;
        };
        const bool ok = status == 0 && !args.empty() &&
                        args[0] == "a person walks forward, slowly. sits down on it's chair" &&
                        has("--duration", "3.00 2.50") && has("--seed", "7") && has("--diffusion_steps", "60") &&
                        has("--output", "/tmp/izlaz s razmakom/motion_1") &&
                        std::find(args.begin(), args.end(), "--no-postprocess") != args.end() &&
                        std::find(args.begin(), args.end(), "--bvh") != args.end();
        report.check("naredba kroz /bin/sh: opisi spojeni tockom, trajanja, sjeme, koraci, izlaz s razmakom", ok,
            args.empty() ? "nema izlaza" : args[0]);

        //Bez sjemena i s cistenjem stopala: tih zastavica nema
        Loom::MotionRequest plain;
        plain.actions = {{"a person jumps", 2.0f}};
        const std::string simple = Loom::buildMotionCommand(echo, plain, "/tmp/m");
        report.check("bez sjemena nema --seed, s cistenjem nema --no-postprocess",
            simple.find("--seed") == std::string::npos && simple.find("--no-postprocess") == std::string::npos, simple);
        Loom::MotionRequest advanced;
        advanced.actions = {{"a person walks", 4.0f}};
        advanced.model = "Kimodo-G1-RP-v1"; advanced.numSamples = 3; advanced.diffusionSteps = 275;
        advanced.transitionFrames = 13; advanced.cfgType = "separated";
        advanced.firstHeadingAngle = 1.2f; advanced.rootMargin = 0.13f;
        advanced.textGuidance = 1.25f; advanced.constraintGuidance = 3.5f;
        advanced.constraints = "/tmp/constraints and spaces.json"; advanced.saveExample = true;
        const std::string advancedCommand = Loom::buildMotionCommand(echo, advanced, "/tmp/native output", "tools/weavermotion/kimodo_cli.py");
        FILE* advancedShell = popen(advancedCommand.c_str(), "r");
        std::vector<std::string> advancedArgs;
        char advancedLine[512];
        while(advancedShell && std::fgets(advancedLine, sizeof(advancedLine), advancedShell)){
            std::string value(advancedLine);
            if(!value.empty() && value.back() == '\n') value.pop_back();
            advancedArgs.push_back(value);
        }
        const int advancedStatus = advancedShell ? pclose(advancedShell) : -1;
        auto hasAdvanced = [&](const std::string& flag, const std::string& value){
            for(size_t i = 0; i + 1 < advancedArgs.size(); ++i) if(advancedArgs[i] == flag && advancedArgs[i + 1] == value) return true;
            return false;
        };
        const auto weights = std::find(advancedArgs.begin(), advancedArgs.end(), "--cfg_weight");
        const bool apiOptions = advancedArgs.size() > 1 && advancedArgs[0] == "tools/weavermotion/kimodo_cli.py" &&
            hasAdvanced("--first_heading_angle", "1.200000") && hasAdvanced("--root_margin", "0.130000");
        report.check("real shell argv preserves G1, samples, constraints, separated CFG and example export without illegal BVH",
            advancedStatus == 0 && apiOptions && hasAdvanced("--model", advanced.model) && hasAdvanced("--num_samples", "3") &&
            hasAdvanced("--diffusion_steps", "275") && hasAdvanced("--num_transition_frames", "13") &&
            hasAdvanced("--constraints", advanced.constraints.string()) &&
            weights != advancedArgs.end() && size_t(advancedArgs.end() - weights) > 2 &&
            *(weights + 1) == "1.250000" && *(weights + 2) == "3.500000" &&
            std::find(advancedArgs.begin(), advancedArgs.end(), "--save_example_dir") != advancedArgs.end() &&
            std::find(advancedArgs.begin(), advancedArgs.end(), "--bvh") == advancedArgs.end(), advancedCommand);
        std::filesystem::remove(echo);
    }

    //-- 3. povijest: opis uz BVH, najnoviji prvi ---------------------------------------------------
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "loom_pokreti_test";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        Loom::writeMotionSidecar(directory / "motion_1", request);
        std::ofstream(directory / "motion_1.bvh") << "HIERARCHY";
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::ofstream(directory / "motion_2.bvh") << "HIERARCHY";            //bez opisa
        const std::vector<Loom::MotionHistoryEntry> history = Loom::motionHistory(directory);
        report.check("povijest: najnoviji prvi, opis i trajanja iz .txt, bez opisa ime datoteke",
            history.size() == 2 && history[0].summary == "motion_2.bvh" && history[1].actions.size() == 2 &&
            history[1].actions[1].duration == 2.5f && history[1].summary == "a person walks forward, slowly  (+1)",
            history.size() == 2 ? history[0].summary + " | " + history[1].summary : "krivo");
        std::filesystem::remove_all(directory);
    }

    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "loom_motion_samples_test";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory / "motion_many");
        Loom::MotionRequest sampleRequest;
        sampleRequest.actions = {{"a person waves", 3.0f}};
        Loom::writeMotionSidecar(directory / "motion_many", sampleRequest);
        std::ofstream(directory / "motion_many" / "motion_many_00.bvh") << "HIERARCHY sample 0";
        std::ofstream(directory / "motion_many" / "motion_many_01.bvh") << "HIERARCHY sample 1";
        const std::vector<Loom::MotionHistoryEntry> samples = Loom::motionHistory(directory);
        bool foundFirst = false, foundSecond = false, promptsRestored = samples.size() == 2;
        for(const Loom::MotionHistoryEntry& item : samples){
            foundFirst = foundFirst || item.bvh.filename() == "motion_many_00.bvh";
            foundSecond = foundSecond || item.bvh.filename() == "motion_many_01.bvh";
            promptsRestored = promptsRestored && item.actions.size() == 1 && item.actions[0].prompt == "a person waves" &&
                              item.summary.find(item.bvh.filename().string()) != std::string::npos;
        }
        report.check("recursive sample history restores the shared prompt and distinguishes each generated BVH",
                     foundFirst && foundSecond && promptsRestored, std::to_string(samples.size()));
        std::filesystem::remove_all(directory);
    }

    return report.result();
}
