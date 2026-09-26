#pragma once
//=============================================================================================
// UNDO AND REDO FOR THE WHOLE SCENE.
//
// WHY SNAPSHOTS AND NOT COMMANDS. The editor changes the scene from dozens of places - gizmo,
// property fields, hierarchy menu, material panel, surface tool, motion import, solve import.
// A command per action means every one of those has to remember to record itself, and the one
// that forgets is the one the user needs to undo. Here the scene itself is watched instead: when
// its fingerprint (Stage::fingerprint, the same one that drives "unsaved") changes, the previous
// state goes on the undo stack. Nothing that edits the scene has to know undo exists.
//
// ONE STEP PER GESTURE, not per frame. A gizmo drag changes the scene every frame; recording each
// frame would make one drag take a hundred undos. So a change is only recorded once the user is
// no longer busy - mouse released, text field left - and a whole drag is one step.
//
// MEMORY. A snapshot is a full copy of the scene. The heavy part is the point cloud from the
// solve (C0257: 266 k points, about 6 MB); cameras with 2301 keys are ~100 KB. The stack keeps
// at least 20 steps and drops the oldest past 768 MB, so a huge cloud cannot eat the machine.
//
// View navigation and selection are not document edits. Large splat cuts enter this same ordered
// history through a reversible callback, without copying the file-sized Gaussian buffer.
//=============================================================================================
#include <Warp/Stage.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <utility>

namespace Loom{

//Rough bytes held by a scene copy: enough to keep the stack under a memory budget
inline size_t sceneBytes(const Warp::Stage& stage){
    size_t bytes = 4096 + stage.materials.size() * 512 + stage.media.size() * 256;
    stage.walk([&](const Warp::Entity& e, int){
        bytes += 256 + e.name.size() + e.children.size() * sizeof(Warp::Id);
        bytes += e.translationKeys.size() * (sizeof(double) + sizeof(glm::vec3));
        bytes += e.rotationKeys.size() * (sizeof(double) + sizeof(glm::quat));
        bytes += e.scaleKeys.size() * (sizeof(double) + sizeof(glm::vec3));
        if(e.points) bytes += e.points->positions.size() * sizeof(glm::vec3) + e.points->colours.size() * sizeof(e.points->colours[0]);
    });
    return bytes;
}

class UndoHistory{
public:
    //After New, Open or anything that is not an edit: forget history, this is the starting point
    void reset(const Warp::Stage& stage){
        undoStack.clear();
        redoStack.clear();
        settled = stage;
        settledPrint = stage.fingerprint();
        used = 0;
    }

    //Once per frame. busy = the user is in the middle of a gesture (mouse button held, text field
    //focused); the change is recorded when the gesture ends. true when a step was recorded
    bool track(const Warp::Stage& stage, bool busy){
        if(busy) return false;
        const uint64_t print = stage.fingerprint();
        if(print == settledPrint) return false;
        pushScene(undoStack, settled, settledPrint);
        clear(redoStack);
        settled = stage;
        settledPrint = print;
        trim();
        return true;
    }

    //Non-Stage edits (currently splat cuts) share the same ordered undo/redo history.
    //The callback receives true for redo and false for undo.
    bool recordExternal(const Warp::Stage& stage, std::function<bool(bool)> apply, size_t bytes = 0){
        if(!apply) return false;
        track(stage, false);
        clear(redoStack);
        Step step;
        step.external = std::move(apply);
        step.bytes = bytes;
        undoStack.push_back(std::move(step));
        used += bytes;
        trim();
        return true;
    }

    //Back one step. A change still in progress is settled first, so it is what gets undone
    bool undo(Warp::Stage& stage){
        track(stage, false);
        if(undoStack.empty()) return false;
        if(undoStack.back().external){
            if(!undoStack.back().external(false)) return false;
            redoStack.push_back(std::move(undoStack.back()));
            undoStack.pop_back();
            return true;
        }
        pushScene(redoStack, settled, settledPrint);
        restore(undoStack, stage);
        return true;
    }

    bool redo(Warp::Stage& stage){
        track(stage, false);
        if(redoStack.empty()) return false;
        if(redoStack.back().external){
            if(!redoStack.back().external(true)) return false;
            undoStack.push_back(std::move(redoStack.back()));
            redoStack.pop_back();
            return true;
        }
        pushScene(undoStack, settled, settledPrint);
        restore(redoStack, stage);
        return true;
    }

    //The dedicated Undo Cut controls should only consume the latest event when it is a cut.
    bool undoExternal(Warp::Stage& stage){
        track(stage, false);
        if(undoStack.empty() || !undoStack.back().external) return false;
        return undo(stage);
    }

    size_t undoSteps() const {return undoStack.size();}
    size_t redoSteps() const {return redoStack.size();}
    size_t bytesHeld() const {return used;}

    //Keep at least 'steps', and drop the oldest beyond 'bytes'
    void limit(size_t steps, size_t bytes){ minimumSteps = steps; budget = bytes; trim(); }

private:
    struct Step{
        std::optional<Warp::Stage> stage;
        uint64_t print = 0;
        size_t bytes = 0;
        std::function<bool(bool)> external;
    };

    void pushScene(std::deque<Step>& to, const Warp::Stage& stage, uint64_t print){
        const size_t bytes = sceneBytes(stage);
        Step step;
        step.stage = stage;
        step.print = print;
        step.bytes = bytes;
        to.push_back(std::move(step));
        used += bytes;
    }

    void restore(std::deque<Step>& from, Warp::Stage& stage){
        Step step = std::move(from.back());
        from.pop_back();
        used -= std::min(used, step.bytes);
        if(!step.stage) return;
        stage = *step.stage;
        settled = std::move(*step.stage);
        settledPrint = step.print;
    }

    void clear(std::deque<Step>& steps){
        for(const Step& step : steps) used -= std::min(used, step.bytes);
        steps.clear();
    }

    //Oldest undo steps go first; redo is short-lived and cleared by the next edit anyway
    void trim(){
        while(undoStack.size() > minimumSteps && used > budget){
            used -= std::min(used, undoStack.front().bytes);
            undoStack.pop_front();
        }
    }

    size_t minimumSteps = 20;
    size_t budget = size_t(768) << 20;
    std::deque<Step> undoStack, redoStack;
    Warp::Stage settled;
    uint64_t settledPrint = 0;
    size_t used = 0;
};

}
