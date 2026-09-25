#pragma once

#include <Engine/WeaverProcedura.h>
#include <Treadle/Ui.h>

#include <string>

namespace Loom{

struct WeaverProceduraPanelState{
    bool open = false;
    float scroll = 0.0f;
    Engine::WeaverProcedura::Graph graph;
};

inline void drawWeaverProceduraPanel(Treadle::Ui& ui, WeaverProceduraPanelState& state,
                                     const Treadle::Rect& area){
    ui.dock("WEAVER PROCEDURA", area, &state.scroll);
    ui.label("PROCEDURAL GRAPH");
    ui.separator();

    ui.value("Nodes", std::to_string(state.graph.nodes.size()));
    ui.value("Connections", std::to_string(state.graph.links.size()));
    ui.value("Seed", std::to_string(state.graph.seed));
    if(state.graph.nodes.empty()) ui.label("Empty graph. The graph scaffold is ready.");

    ui.separator();
    ui.label("First generator slice: Curve to Sweep.");
    ui.label("This path will support roads, ropes and chains.");
    ui.label("Geometry generation is not wired yet.");
    if(ui.button("Close")) state.open = false;
}

}
