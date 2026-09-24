// Undo/redo of the whole scene and autosave (LoomUndo.h, LoomAutosave.h).
//
// WHY THIS IS TESTED. Undo is watched from the scene fingerprint, not recorded by each action, so
// the things that can go wrong are about WHEN a step is taken:
//
//   DRAG          a gizmo drag changes the scene every frame and must be ONE step, not a hundred
//   REDO          a new edit after undo must drop the redo branch, or redo jumps to a lost future
//   MEMORY        a big point cloud per step must not grow the stack without bound
//   AUTOSAVE      must never write the project itself, must be loadable, and must not rewrite
//                 the same state over and over
#include "TestHarness.h"

#include "../src/LoomAutosave.h"
#include "../src/LoomUndo.h"

#include <Warp/Project.h>

#include <filesystem>
#include <fstream>
#include <thread>

int main(){
    TestReport report("E6 undo and autosave");

    Warp::Stage stage;
    const Warp::Id cube = stage.create("Cube");
    stage.get(cube)->mesh = Warp::Mesh{Warp::Shape::Cube};
    Loom::UndoHistory history;
    history.reset(stage);

    //-- 1. a drag over many frames is one step ----------------------------------------------------
    for(int f = 0; f < 30; ++f){
        stage.get(cube)->local.translation.x = 0.1f * float(f + 1);
        history.track(stage, true);                       //mouse held
    }
    history.track(stage, false);                          //released
    history.track(stage, false);                          //nothing new: no step
    const bool oneStep = history.undoSteps() == 1;
    const bool undone = history.undo(stage) && stage.get(cube)->local.translation.x == 0.0f;
    const bool redone = history.redo(stage) && std::fabs(stage.get(cube)->local.translation.x - 3.0f) < 1e-6f;
    report.check("a 30-frame drag is one undo step, and undo/redo restore it exactly", oneStep && undone && redone,
        fmt("steps %zu, x after redo %.3f", history.undoSteps(), stage.get(cube)->local.translation.x));

    //-- 2. created entity goes away on undo; a new edit drops redo ---------------------------------
    const Warp::Id sphere = stage.create("Null");
    history.track(stage, false);
    const bool gone = history.undo(stage) && stage.get(sphere) == nullptr && stage.size() == 1;
    stage.get(cube)->visible = false;                     //new edit after undo
    history.track(stage, false);
    const bool redoDropped = history.redoSteps() == 0 && !history.redo(stage) && !stage.get(cube)->visible;
    report.check("undo removes a created entity; a new edit clears redo", gone && redoDropped,
        fmt("entities after undo %zu, redo steps %zu", stage.size(), history.redoSteps()));

    //-- 3. undo of an edit still in progress undoes that edit --------------------------------------
    stage.get(cube)->name = "Box";
    const bool pending = history.undo(stage) && stage.get(cube)->name == "Cube";
    report.check("undo pressed mid-edit undoes that edit", pending, stage.get(cube)->name);

    //-- 4. memory: big clouds do not grow the stack past its budget --------------------------------
    {
        Warp::Stage heavy;
        const Warp::Id cloud = heavy.create("Points");
        heavy.get(cloud)->points = Warp::Points{};
        heavy.get(cloud)->points->positions.assign(100000, glm::vec3(0.0f));   //~1.2 MB each
        Loom::UndoHistory limited;
        limited.limit(5, size_t(8) << 20);
        limited.reset(heavy);
        for(int i = 0; i < 40; ++i){
            heavy.get(cloud)->local.translation.y = float(i + 1);
            limited.track(heavy, false);
        }
        size_t steps = 0;
        while(limited.undo(heavy)) ++steps;
        report.check("stack is trimmed to the budget but keeps the minimum", limited.bytesHeld() < (size_t(20) << 20) &&
                     steps >= 5 && steps < 40 && heavy.get(cloud)->local.translation.y > 0.0f,
            fmt("%zu steps kept of 40, oldest reachable y = %.0f", steps, heavy.get(cloud)->local.translation.y));
    }

    //-- 5. autosave -------------------------------------------------------------------------------
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "loom_autosave_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path project = dir / "shot.usda";
    std::string error;
    Warp::Stage saved;
    saved.create("Camera");
    Warp::saveProject(saved, project.string(), error);
    const auto projectBytes = std::filesystem::file_size(project);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    Loom::Autosave autosave(0.0);                         //no waiting in the test
    const bool clean = !autosave.tick(stage, project, false);
    const bool started = autosave.tick(stage, project, true);
    while(autosave.isWriting()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const bool again = !autosave.tick(stage, project, true);           //same state: not rewritten
    std::filesystem::path found;
    const bool offered = Loom::newerAutosave(project, found);
    Warp::Stage back;
    const bool loads = offered && Warp::loadProject(found.string(), back, error) && back.fingerprint() == stage.fingerprint();
    const bool untouched = std::filesystem::file_size(project) == projectBytes;
    const bool hidden = found.filename().string().rfind(".shot.autosave", 0) == 0;
    report.check("autosave: only when dirty, once per state, hidden, loads back, project untouched",
        clean && started && again && offered && loads && untouched && hidden,
        fmt("%s %s", found.filename().string().c_str(), autosave.lastError().c_str()));

    autosave.discard(project);
    report.check("save discards the autosave", !Loom::newerAutosave(project, found) && !std::filesystem::exists(Loom::autosavePathFor(project)),
        "removed");
    std::filesystem::remove_all(dir);

    return report.result();
}
