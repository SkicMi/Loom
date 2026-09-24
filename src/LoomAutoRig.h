#pragma once
// Local auto-rig orchestration and panel. UniRig runs in its own Python process/environment.
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

struct AutoRigAction{ bool generate = false, preview = false, useSelected = false; };

inline AutoRigAction drawAutoRigPanel(Treadle::Ui& ui, AutoRigState& state,
                                      const Treadle::Rect& area, bool ready, bool running,
                                      bool busy, const std::string& lastLine, float& scroll){
    AutoRigAction action;
    ui.dock("WEAVERMOTION / AUTO RIG", area, &scroll);
    ui.label("Unrigged GLB -> skeleton + skin weights");
    ui.label("Local UniRig backend. Original file is preserved.");
    ui.separator();
    Treadle::Ui::TextFieldConfig field;
    field.lines = 2;
    field.maxLength = 4096;
    field.placeholder = "Paste the full path to an unrigged .glb";
    ui.textField("autorig-source", &state.source, field);
    if(ui.button("Use selected scene model")) action.useSelected = true;
    ui.label("Or right-click a GLB in the file browser.");
    ui.separator();
    ui.label("Best starting point: upright character, arms apart.");
    ui.label("AI rig quality varies; inspect the result before animation.");
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
        ui.label("Rig exported and deformation checks passed.");
        ui.label("Rest mesh + bones are shown in the viewport.");
        if(ui.button("Import bend-test snapshot")) action.preview = true;
        ui.label("Snapshot is static; live skinning/Kimodo retargeting is next.");
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
