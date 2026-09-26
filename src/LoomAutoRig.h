#pragma once
// Local auto-rig orchestration and panel. run.py runs in its own Python process/environment:
// joints are measured from the mesh (direct_rig.py); UniRig is the fallback when that fails.
#include "LoomWeaverMotion.h"
#include <Treadle/Ui.h>
#include <filesystem>
#include <string>

namespace Loom{

struct AutoRigState{
    bool open = false;
    std::string source;
    std::filesystem::path output;
};

inline std::string autoRigCommand(const std::filesystem::path& root,
                                  const std::filesystem::path& input,
                                  const std::filesystem::path& output){
    return shellQuoteArgument((root / "tools/autorig/.venv/bin/python").string()) + " -u " +
           shellQuoteArgument((root / "tools/autorig/run.py").string()) + " --input " +
           shellQuoteArgument(input.string()) + " --output " + shellQuoteArgument(output.string());
}

//Direct backend needs only the venv and Blender; UniRig (vendor/) is an optional fallback
inline bool autoRigBackendReady(const std::filesystem::path& root){
    std::error_code error;
    return std::filesystem::is_regular_file(root / "tools/autorig/.venv/bin/python", error) &&
           std::filesystem::is_regular_file(root / "tools/autorig/direct_rig.py", error);
}

struct AutoRigAction{ bool generate = false, preview = false, useSelected = false; };

inline AutoRigAction drawAutoRigPanel(Treadle::Ui& ui, AutoRigState& state,
                                      const Treadle::Rect& area, bool ready, bool running,
                                      bool busy, const std::string& lastLine, float& scroll){
    AutoRigAction action;
    ui.dock("WEAVERMOTION / AUTO RIG", area, &scroll);
    ui.label("GLB/glTF -> UE5 Manny humanoid rig + skin weights");
    ui.label("Joints measured from the mesh; UniRig if that fails.");
    ui.label("Manny is the output skeleton. Original is preserved.");
    ui.separator();
    Treadle::Ui::TextFieldConfig field;
    field.lines = 2;
    field.maxLength = 4096;
    field.placeholder = "Paste the full path to an unrigged .glb or .gltf";
    ui.textField("autorig-source", &state.source, field);
    if(ui.button("Use selected scene model")) action.useSelected = true;
    ui.label("Right-click a Media model to import as humanoid.");
    ui.separator();
    ui.label("Best starting point: upright character, arms apart.");
    ui.label("Rig quality varies; inspect the result before animation.");
    if(!ready){
        ui.label("Install backend: bash tools/autorig/setup.sh");
    }
    if(busy) ui.label(running ? "Auto-rig running..." : "Another job is running.");
    if(running) ui.label(Treadle::fitText(lastLine, area.width - 30.0f, ui.style().textScale));
    if(ui.button(running ? "RIGGING..." : "GENERATE AUTO RIG"))
        action.generate = ready && !busy && !state.source.empty();
    std::error_code error;
    const bool complete = !state.output.empty() && std::filesystem::is_regular_file(state.output / "complete.json", error);
    if(complete){
        ui.separator();
        ui.label("Manny rig exported and deformation checks passed.");
        ui.label("Rest mesh + bones are shown in the viewport.");
        if(ui.button("Import bend-test snapshot")) action.preview = true;
        ui.label("Live skinning and Kimodo retargeting are ready for this rig.");
        ui.label(Treadle::fitText(state.output.string(), area.width - 30.0f, ui.style().textScale));
    }else if(!running && !state.output.empty() &&
             std::filesystem::is_regular_file(state.output / "autorig.log", error)){
        ui.separator();
        ui.label("Last run failed or stopped; complete trace saved at:");
        ui.label(Treadle::fitText((state.output / "autorig.log").string(), area.width - 30.0f, ui.style().textScale));
    }
    if(ui.button("Close Auto Rig (Esc)")) state.open = false;
    return action;
}

}
