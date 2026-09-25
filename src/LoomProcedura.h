#pragma once

#include <Engine/WeaverProcedura.h>
#include <Treadle/Ui.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace Loom{

struct WeaverProceduraPanelState{
    bool open = false;
    float scroll = 0.0f;
    Engine::WeaverProcedura::Graph graph;
    Engine::WeaverProcedura::MeshData previewMesh;
    uint64_t previewRevision = 0;
    bool previewReady = false;
    bool graphDirty = false;
    std::string previewError;
    std::unordered_map<Engine::WeaverProcedura::NodeId, bool> expandedNodes;
    Engine::WeaverProcedura::NodeId selectedCurveNodeId = 0;
    int selectedControlPoint = -1;
    Engine::WeaverProcedura::NodeId contextCurveNodeId = 0;
    int contextControlPoint = -1;
};

namespace WeaverProceduraUi{

inline std::string formatSize(float value){
    std::ostringstream text;
    text << std::fixed << std::setprecision(1) << value;
    return text.str();
}

inline std::string nodeType(const Engine::WeaverProcedura::Node& node){
    namespace Proc = Engine::WeaverProcedura;
    if(std::holds_alternative<Proc::CurveNode>(node.payload)) return "Curve path";
    if(std::holds_alternative<Proc::RectangleProfileNode>(node.payload)) return "Rectangle profile";
    if(std::holds_alternative<Proc::SweepNode>(node.payload)) return "Sweep mesh";
    return "Unknown node";
}

inline std::string nodeSummary(const Engine::WeaverProcedura::Node& node){
    namespace Proc = Engine::WeaverProcedura;
    if(const auto* curve = std::get_if<Proc::CurveNode>(&node.payload)){
        return std::to_string(curve->curve.points.size()) + " points" + (curve->curve.closed ? " / closed" : " / open");
    }
    if(const auto* profile = std::get_if<Proc::RectangleProfileNode>(&node.payload)){
        return formatSize(profile->width) + " x " + formatSize(profile->height) + " m";
    }
    if(const auto* sweep = std::get_if<Proc::SweepNode>(&node.payload)){
        return "step " + formatSize(sweep->settings.sampleSpacing) + " m" + (sweep->settings.capEnds ? " / capped" : "");
    }
    return {};
}

inline std::string nodeLabel(const Engine::WeaverProcedura::Graph& graph,
                             Engine::WeaverProcedura::NodeId id){
    for(std::size_t i = 0; i < graph.nodes.size(); ++i){
        if(graph.nodes[i].id == id)
            return std::to_string(i + 1) + "  " + nodeType(graph.nodes[i]);
    }
    return "Missing node";
}

inline std::string inputName(const Engine::WeaverProcedura::Node& node, uint32_t port){
    if(std::holds_alternative<Engine::WeaverProcedura::SweepNode>(node.payload))
        return port == 0 ? "Curve input" : "Profile input";
    return "Input";
}

inline Engine::WeaverProcedura::Node* activeCurveNode(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    for(Proc::Node& node : state.graph.nodes){
        if(node.id == state.selectedCurveNodeId && std::holds_alternative<Proc::CurveNode>(node.payload))
            return &node;
    }
    for(Proc::Node& node : state.graph.nodes){
        if(std::holds_alternative<Proc::CurveNode>(node.payload)){
            state.selectedCurveNodeId = node.id;
            return &node;
        }
    }
    state.selectedCurveNodeId = 0;
    state.selectedControlPoint = -1;
    return nullptr;
}

inline Engine::WeaverProcedura::Node* findCurveNode(WeaverProceduraPanelState& state,
                                                     Engine::WeaverProcedura::NodeId id){
    namespace Proc = Engine::WeaverProcedura;
    for(Proc::Node& node : state.graph.nodes)
        if(node.id == id && std::holds_alternative<Proc::CurveNode>(node.payload)) return &node;
    return nullptr;
}

inline void markGraphChanged(WeaverProceduraPanelState& state){
    state.graphDirty = true;
    state.previewError.clear();
}

inline bool insertControlPointAt(WeaverProceduraPanelState& state,
                                 Engine::WeaverProcedura::NodeId nodeId,
                                 std::size_t index, const glm::vec3& position){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Node* node = findCurveNode(state, nodeId);
    if(!node) return false;
    Proc::Curve& curve = std::get<Proc::CurveNode>(node->payload).curve;
    if(curve.points.size() >= 64) return false;
    index = std::min(index, curve.points.size());
    curve.points.insert(curve.points.begin() + std::ptrdiff_t(index), position);
    state.selectedCurveNodeId = nodeId;
    state.selectedControlPoint = int(index);
    markGraphChanged(state);
    return true;
}

inline bool insertControlPoint(WeaverProceduraPanelState& state,
                               Engine::WeaverProcedura::NodeId nodeId, std::size_t index){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Node* node = findCurveNode(state, nodeId);
    if(!node) return false;
    const Proc::Curve& curve = std::get<Proc::CurveNode>(node->payload).curve;
    const std::size_t count = curve.points.size();
    if(count >= 64) return false;
    index = std::min(index, count);
    glm::vec3 position(0.0f);
    if(count == 0){
        position = glm::vec3(0.0f);
    }else if(curve.closed && (index == 0 || index == count)){
        position = 0.5f * (curve.points.back() + curve.points.front());
    }else if(index == 0){
        position = count > 1 ? curve.points.front() + 0.5f * (curve.points.front() - curve.points[1])
                             : curve.points.front() + glm::vec3(0.0f, 0.0f, 1.0f);
    }else if(index == count){
        position = count > 1 ? curve.points.back() + 0.5f * (curve.points.back() - curve.points[count - 2])
                             : curve.points.back() + glm::vec3(0.0f, 0.0f, 1.0f);
    }else{
        position = 0.5f * (curve.points[index - 1] + curve.points[index]);
    }
    return insertControlPointAt(state, nodeId, index, position);
}

inline bool appendControlPoint(WeaverProceduraPanelState& state){
    Engine::WeaverProcedura::Node* node = activeCurveNode(state);
    if(!node) return false;
    const auto& points = std::get<Engine::WeaverProcedura::CurveNode>(node->payload).curve.points;
    return insertControlPoint(state, node->id, points.size());
}

inline bool eraseControlPoint(WeaverProceduraPanelState& state,
                              Engine::WeaverProcedura::NodeId nodeId, std::size_t index){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Node* node = findCurveNode(state, nodeId);
    if(!node) return false;
    Proc::Curve& curve = std::get<Proc::CurveNode>(node->payload).curve;
    const std::size_t minimumPoints = curve.closed ? 3 : 2;
    if(curve.points.size() <= minimumPoints || index >= curve.points.size()) return false;
    curve.points.erase(curve.points.begin() + std::ptrdiff_t(index));
    state.selectedCurveNodeId = nodeId;
    state.selectedControlPoint = curve.points.empty() ? -1 : int(std::min(index, curve.points.size() - 1));
    if(state.contextCurveNodeId == nodeId) state.contextControlPoint = -1;
    markGraphChanged(state);
    return true;
}

inline bool evaluateGraph(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Proc::EvaluationResult result = Proc::evaluate(state.graph);
    if(result.succeeded){
        state.previewMesh = std::move(result.mesh);
        state.previewReady = true;
        state.graphDirty = false;
        state.previewError.clear();
        ++state.previewRevision;
        return true;
    }

    // Keep the last successful viewport result available while the current graph is invalid.
    state.previewError = std::move(result.error);
    state.graphDirty = true;
    return false;
}

inline bool createRoadExample(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Graph graph;
    Proc::CurveNode curve;
    curve.curve.points = {{-6.0f, 0.0f, -4.0f}, {-2.0f, 0.0f, 0.0f},
                          {4.0f, 0.0f, 2.0f}, {10.0f, 0.0f, 0.0f}};
    const Proc::NodeId curveId = Proc::addNode(graph, std::move(curve), 24.0f, 88.0f);
    Proc::RectangleProfileNode profile;
    profile.width = 4.0f;
    profile.height = 0.12f;
    const Proc::NodeId profileId = Proc::addNode(graph, std::move(profile), 24.0f, 250.0f);
    Proc::SweepNode sweep;
    sweep.settings.sampleSpacing = 1.0f;
    const Proc::NodeId sweepId = Proc::addNode(graph, std::move(sweep), 300.0f, 170.0f);

    if(curveId == 0 || profileId == 0 || sweepId == 0){
        state.previewError = "Could not create the starter graph.";
        return false;
    }

    graph.links.push_back({curveId, 0, sweepId, 0});
    graph.links.push_back({profileId, 0, sweepId, 1});
    state.graph = std::move(graph);
    state.expandedNodes.clear();
    state.selectedCurveNodeId = curveId;
    state.selectedControlPoint = 1;
    state.contextCurveNodeId = 0;
    state.contextControlPoint = -1;
    state.graphDirty = true;
    state.previewError.clear();
    return evaluateGraph(state);
}

} // namespace WeaverProceduraUi

inline void drawWeaverProceduraPanel(Treadle::Ui& ui, WeaverProceduraPanelState& state,
                                     const Treadle::Rect& area){
    namespace Proc = Engine::WeaverProcedura;
    namespace Panel = WeaverProceduraUi;
    ui.dock("WEAVER PROCEDURA", area, &state.scroll);
    ui.label("PROCEDURAL WORKFLOW");
    ui.hint("Build reusable paths and shapes, then preview the generated mesh.");

    if(state.graph.nodes.empty()){
        ui.separator();
        ui.caption("1  START WITH A PRESET");
        ui.label("Curved road segment");
        ui.hint("A curve, a rectangular road profile, and a sweep connected for you.");
        if(ui.primaryButton("Create and preview road")) Panel::createRoadExample(state);
    }else{
        ui.separator();
        ui.caption("1  GRAPH OVERVIEW");
        ui.value("Graph", std::to_string(state.graph.nodes.size()) + " nodes / " +
                           std::to_string(state.graph.links.size()) + " links");
        ui.value("Seed", std::to_string(state.graph.seed));

        ui.caption("FLOW");
        for(const Proc::Link& link : state.graph.links){
            const Proc::Node* target = nullptr;
            for(const Proc::Node& node : state.graph.nodes)
                if(node.id == link.to){ target = &node; break; }
            const std::string targetPort = target ? Panel::inputName(*target, link.toPort) : "Input";
            ui.value(Panel::nodeLabel(state.graph, link.from),
                     Panel::nodeLabel(state.graph, link.to) + " / " + targetPort);
        }

        ui.separator();
        ui.caption("2  TUNE THE NODES");
        ui.hint("Viewport: drag a point to move it; right-click a point for quick actions.");
        bool changed = false;
        for(std::size_t nodeIndex = 0; nodeIndex < state.graph.nodes.size(); ++nodeIndex){
            Proc::Node& node = state.graph.nodes[nodeIndex];
            bool& expanded = state.expandedNodes[node.id];
            const std::string title = std::to_string(nodeIndex + 1) + "  " + Panel::nodeType(node);
            ui.disclosure(title, Panel::nodeSummary(node), &expanded);
            if(!expanded) continue;

            if(auto* curve = std::get_if<Proc::CurveNode>(&node.payload)){
                for(std::size_t i = 0; i < curve->curve.points.size(); ++i){
                    glm::vec3& point = curve->curve.points[i];
                    const std::string prefix = "Point " + std::to_string(i + 1);
                    changed |= ui.slider(prefix + " X", &point.x, -20.0f, 20.0f, "m");
                    changed |= ui.slider(prefix + " Y", &point.y, -10.0f, 10.0f, "m");
                    changed |= ui.slider(prefix + " Z", &point.z, -20.0f, 20.0f, "m");
                }
                changed |= ui.checkbox("Closed path", &curve->curve.closed);
                if(curve->curve.points.size() < 64 && ui.button("Add control point")){
                    if(Panel::insertControlPoint(state, node.id, curve->curve.points.size())) changed = true;
                }
                const std::size_t minimumPoints = curve->curve.closed ? 3 : 2;
                if(curve->curve.points.size() > minimumPoints && ui.button("Remove last point")){
                    if(Panel::eraseControlPoint(state, node.id, curve->curve.points.size() - 1)) changed = true;
                }
            }else if(auto* profile = std::get_if<Proc::RectangleProfileNode>(&node.payload)){
                changed |= ui.slider("Width", &profile->width, 0.1f, 20.0f, "m");
                changed |= ui.slider("Height", &profile->height, 0.02f, 5.0f, "m");
            }else if(auto* sweep = std::get_if<Proc::SweepNode>(&node.payload)){
                changed |= ui.slider("Sample spacing", &sweep->settings.sampleSpacing, 0.05f, 5.0f, "m");
                changed |= ui.checkbox("Cap ends", &sweep->settings.capEnds);
            }
        }
        if(changed){
            state.graphDirty = true;
            state.previewError.clear();
        }

        ui.separator();
        ui.caption("3  PREVIEW IN VIEWPORT");
        if(!state.previewError.empty()){
            ui.status("Update failed: " + state.previewError +
                      (state.previewReady ? " Showing the last successful preview." : ""),
                      {1.0f, 0.30f, 0.30f, 1.0f});
        }else if(state.previewReady && state.graphDirty){
            ui.status("Changes pending. Viewport still shows the last successful preview.",
                      {0.95f, 0.70f, 0.26f, 1.0f});
        }else if(state.previewReady){
            ui.status("Preview is up to date in the viewport.", {0.43f, 0.78f, 0.56f, 1.0f});
        }else{
            ui.status("No viewport preview yet. Evaluate the graph to see its mesh.",
                      {0.95f, 0.70f, 0.26f, 1.0f});
        }
        if((state.graphDirty || !state.previewReady) && ui.primaryButton("Update viewport preview"))
            Panel::evaluateGraph(state);

        if(state.previewReady){
            ui.value("Vertices", std::to_string(state.previewMesh.vertices.size()));
            ui.value("Triangles", std::to_string(state.previewMesh.indices.size() / 3));
            ui.hint("Temporary preview; this graph is not saved in the scene yet.");
            if(ui.button("Clear viewport preview")){
                state.previewMesh = {};
                state.previewReady = false;
                state.graphDirty = true;
                state.previewError.clear();
                ++state.previewRevision;
            }
        }
    }

    if(ui.button("Close")) state.open = false;
}

}
