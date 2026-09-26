#pragma once

#include <Engine/WeaverProcedura.h>
#include "LoomProceduraRecipe.h"
#include <Treadle/Ui.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace Loom{

struct WeaverProceduraPanelState{
    bool open = false;
    float scroll = 0.0f;
    Engine::WeaverProcedura::Graph graph;
    Engine::WeaverProcedura::MeshData previewMesh;
    std::size_t previewPointCount = 0;
    bool previewIsPointCloud = false;
    uint64_t previewRevision = 0;
    bool previewReady = false;
    bool previewVisible = false;
    bool graphDirty = false;
    bool newRecipeArmed = false;
    std::string previewError;
    std::string recipeName = "Untitled Recipe";
    std::string recipeFilePath = "WeaverProcedura/Untitled.loomrecipe.json";
    std::string recipeStatus;
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
    if(std::holds_alternative<Proc::GridNode>(node.payload)) return "Grid";
    if(std::holds_alternative<Proc::SetGridPointHeightNode>(node.payload)) return "Set point height";
    if(std::holds_alternative<Proc::GridToMeshNode>(node.payload)) return "Grid to mesh";
    if(std::holds_alternative<Proc::InteriorBlockoutNode>(node.payload)) return "Interior blockout";
    if(std::holds_alternative<Proc::AddPrimitiveNode>(node.payload)) return "Add Primitive";
    if(std::holds_alternative<Proc::MoveNode>(node.payload)) return "Move";
    if(std::holds_alternative<Proc::RotateNode>(node.payload)) return "Rotate";
    if(std::holds_alternative<Proc::ScaleNode>(node.payload)) return "Scale";
    if(std::holds_alternative<Proc::ExtrudeNode>(node.payload)) return "Extrude";
    if(std::holds_alternative<Proc::BevelNode>(node.payload)) return "Bevel";
    if(std::holds_alternative<Proc::MeshToPointNode>(node.payload)) return "Mesh to Point";
    if(std::holds_alternative<Proc::PointFromMeshNode>(node.payload)) return "Point from Mesh";
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
    if(const auto* grid = std::get_if<Proc::GridNode>(&node.payload))
        return std::to_string(grid->cellsX + 1) + " x " + std::to_string(grid->cellsZ + 1) +
               " points / " + formatSize(grid->width) + " x " + formatSize(grid->depth) + " m";
    if(const auto* height = std::get_if<Proc::SetGridPointHeightNode>(&node.payload))
        return "point (" + std::to_string(height->column) + ", " + std::to_string(height->row) +
               ") / Y " + formatSize(height->height) + " m";
    if(std::holds_alternative<Proc::GridToMeshNode>(node.payload)) return "triangulated surface mesh";
    if(const auto* room = std::get_if<Proc::InteriorBlockoutNode>(&node.payload))
        return std::to_string(room->roomsPerSide * 2) + " rooms / corridor / " +
               formatSize(room->wallHeight) + " m walls";
    if(const auto* primitive = std::get_if<Proc::AddPrimitiveNode>(&node.payload))
        return primitive->size.x == primitive->size.y && primitive->size.y == primitive->size.z
            ? formatSize(primitive->size.x) + " m primitive" : "custom dimensions";
    if(const auto* move = std::get_if<Proc::MoveNode>(&node.payload))
        return "offset " + formatSize(move->offset.x) + ", " + formatSize(move->offset.y) + ", " + formatSize(move->offset.z) + " m";
    if(const auto* rotate = std::get_if<Proc::RotateNode>(&node.payload))
        return "X " + formatSize(rotate->degrees.x) + "° / Y " + formatSize(rotate->degrees.y) + "° / Z " + formatSize(rotate->degrees.z) + "°";
    if(const auto* scale = std::get_if<Proc::ScaleNode>(&node.payload))
        return "factor " + formatSize(scale->factor.x) + ", " + formatSize(scale->factor.y) + ", " + formatSize(scale->factor.z);
    if(const auto* extrude = std::get_if<Proc::ExtrudeNode>(&node.payload))
        return "seed triangle " + std::to_string(extrude->faceIndex) + " / " + formatSize(extrude->distance) + " m";
    if(const auto* bevel = std::get_if<Proc::BevelNode>(&node.payload))
        return formatSize(bevel->amount) + " m / " + std::to_string(bevel->segments) + " segments";
    if(std::holds_alternative<Proc::MeshToPointNode>(node.payload)) return "unique mesh vertex positions";
    if(const auto* sample = std::get_if<Proc::PointFromMeshNode>(&node.payload))
        return std::to_string(sample->count) + " surface points / seed " + std::to_string(sample->seed);
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
    if(std::holds_alternative<Engine::WeaverProcedura::SetGridPointHeightNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::GridToMeshNode>(node.payload))
        return "Point Grid input";
    if(std::holds_alternative<Engine::WeaverProcedura::MoveNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::RotateNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::ScaleNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::ExtrudeNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::BevelNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::MeshToPointNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::PointFromMeshNode>(node.payload)) return "Mesh input";
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
    state.newRecipeArmed = false;
    state.previewError.clear();
}

inline bool isGeometryOutputNode(const Engine::WeaverProcedura::Node& node){
    namespace Proc = Engine::WeaverProcedura;
    return std::holds_alternative<Proc::SweepNode>(node.payload) ||
           std::holds_alternative<Proc::GridToMeshNode>(node.payload) ||
           std::holds_alternative<Proc::InteriorBlockoutNode>(node.payload) ||
           std::holds_alternative<Proc::AddPrimitiveNode>(node.payload) ||
           std::holds_alternative<Proc::MoveNode>(node.payload) ||
           std::holds_alternative<Proc::RotateNode>(node.payload) ||
           std::holds_alternative<Proc::ScaleNode>(node.payload) ||
           std::holds_alternative<Proc::ExtrudeNode>(node.payload) ||
           std::holds_alternative<Proc::BevelNode>(node.payload) ||
           std::holds_alternative<Proc::MeshToPointNode>(node.payload) ||
           std::holds_alternative<Proc::PointFromMeshNode>(node.payload);
}

inline Engine::WeaverProcedura::Node* terminalGeometryNode(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Engine::WeaverProcedura::Node* terminal = nullptr;
    for(Proc::Node& node : state.graph.nodes){
        if(!isGeometryOutputNode(node)) continue;
        const bool hasOutgoing = std::any_of(state.graph.links.begin(),state.graph.links.end(),
            [&](const Proc::Link& link){ return link.from == node.id; });
        if(!hasOutgoing){
            if(terminal) return nullptr;
            terminal = &node;
        }
    }
    return terminal;
}

inline bool appendPrimitiveNode(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    if(state.graph.nodes.size() >= 256){ state.recipeStatus = "Recipe has reached its node limit."; return false; }
    if(terminalGeometryNode(state)){
        state.recipeStatus = "Add Primitive starts a mesh recipe. Save this Recipe, choose New Recipe, then add the primitive.";
        return false;
    }
    Proc::AddPrimitiveNode primitive;
    const Proc::NodeId id = Proc::addNode(state.graph,primitive,24.0f,120.0f);
    if(!id){ state.recipeStatus = "Could not add the primitive node."; return false; }
    state.expandedNodes[id] = true;
    state.recipeName = "Primitive Recipe";
    markGraphChanged(state);
    state.recipeStatus.clear();
    return true;
}

inline bool appendMeshNode(WeaverProceduraPanelState& state, Engine::WeaverProcedura::NodePayload payload){
    namespace Proc = Engine::WeaverProcedura;
    if(state.graph.nodes.size() >= 256){ state.recipeStatus = "Recipe has reached its node limit."; return false; }
    Proc::Node* previous = terminalGeometryNode(state);
    if(!previous || std::holds_alternative<Proc::MeshToPointNode>(previous->payload) ||
       std::holds_alternative<Proc::PointFromMeshNode>(previous->payload)){
        state.recipeStatus = "This operation needs a mesh output. Add a primitive or mesh generator first.";
        return false;
    }
    const Proc::NodeId previousId = previous->id;
    const float x = previous->editorX + 240.0f, y = previous->editorY;
    const Proc::NodeId id = Proc::addNode(state.graph,std::move(payload),x,y);
    if(!id){ state.recipeStatus = "Could not add the mesh operation node."; return false; }
    state.graph.links.push_back({previousId,0,id,0});
    state.expandedNodes[id] = true;
    markGraphChanged(state);
    state.recipeStatus.clear();
    return true;
}

inline const Engine::WeaverProcedura::GridNode* sourceGridSettings(
        const Engine::WeaverProcedura::Graph& graph, Engine::WeaverProcedura::NodeId nodeId){
    namespace Proc = Engine::WeaverProcedura;
    for(const Proc::Node& node : graph.nodes){
        if(node.id != nodeId) continue;
        if(const auto* grid = std::get_if<Proc::GridNode>(&node.payload)) return grid;
        if(!std::holds_alternative<Proc::SetGridPointHeightNode>(node.payload)) return nullptr;
        for(const Proc::Link& link : graph.links)
            if(link.to == nodeId && link.toPort == 0) return sourceGridSettings(graph, link.from);
        return nullptr;
    }
    return nullptr;
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
        if(result.pointCloudOutput){
            Proc::MeshData markers;
            std::string pointError;
            if(!Proc::makePointPreview(result.points,markers,pointError)){
                state.previewError = std::move(pointError);
                state.graphDirty = true;
                return false;
            }
            state.previewPointCount = result.points.size();
            state.previewMesh = std::move(markers);
        }else{
            state.previewPointCount = 0;
            state.previewMesh = std::move(result.mesh);
        }
        state.previewIsPointCloud = result.pointCloudOutput;
        state.previewReady = true;
        state.previewVisible = true;
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
    state.recipeName = "Road Sweep Example";
    state.expandedNodes.clear();
    state.expandedNodes[curveId] = true;
    state.expandedNodes[profileId] = true;
    state.expandedNodes[sweepId] = true;
    state.selectedCurveNodeId = curveId;
    state.selectedControlPoint = 1;
    state.contextCurveNodeId = 0;
    state.contextControlPoint = -1;
    state.graphDirty = true;
    state.previewError.clear();
    return evaluateGraph(state);
}

inline bool createGridExample(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Graph graph;
    Proc::GridNode grid;
    grid.width = 8.0f;
    grid.depth = 8.0f;
    grid.cellsX = 4;
    grid.cellsZ = 4;
    const Proc::NodeId gridId = Proc::addNode(graph, grid, 24.0f, 100.0f);
    Proc::SetGridPointHeightNode height;
    height.column = 2;
    height.row = 2;
    height.height = 2.0f;
    const Proc::NodeId heightId = Proc::addNode(graph, height, 280.0f, 100.0f);
    const Proc::NodeId meshId = Proc::addNode(graph, Proc::GridToMeshNode{}, 540.0f, 100.0f);
    if(!gridId || !heightId || !meshId){
        state.previewError = "Could not create the starter grid recipe.";
        return false;
    }
    graph.links = {{gridId, 0, heightId, 0}, {heightId, 0, meshId, 0}};
    state.graph = std::move(graph);
    state.recipeName = "Raised Grid Surface";
    state.expandedNodes.clear();
    state.expandedNodes[gridId] = true;
    state.expandedNodes[heightId] = true;
    state.selectedCurveNodeId = 0;
    state.selectedControlPoint = -1;
    state.contextCurveNodeId = 0;
    state.contextControlPoint = -1;
    state.graphDirty = true;
    state.previewError.clear();
    return evaluateGraph(state);
}

inline bool createInteriorExample(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Graph graph;
    Proc::InteriorBlockoutNode interior;
    const Proc::NodeId id = Proc::addNode(graph, interior, 120.0f, 120.0f);
    if(!id){
        state.previewError = "Could not create the starter interior recipe.";
        return false;
    }
    state.graph = std::move(graph);
    state.recipeName = "Hallway and Rooms Blockout";
    state.expandedNodes.clear();
    state.expandedNodes[id] = true;
    state.selectedCurveNodeId = 0;
    state.selectedControlPoint = -1;
    state.contextCurveNodeId = 0;
    state.contextControlPoint = -1;
    state.graphDirty = true;
    state.previewError.clear();
    return evaluateGraph(state);
}

inline bool appendGridHeightNode(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Proc::NodeId meshId = 0;
    Proc::Link* meshInput = nullptr;
    for(Proc::Node& node : state.graph.nodes)
        if(std::holds_alternative<Proc::GridToMeshNode>(node.payload)) meshId = node.id;
    if(!meshId) return false;
    for(Proc::Link& link : state.graph.links)
        if(link.to == meshId && link.toPort == 0){ meshInput = &link; break; }
    if(!meshInput) return false;
    const Proc::NodeId previous = meshInput->from;
    const Proc::GridNode* grid = sourceGridSettings(state.graph, previous);
    if(!grid) return false;
    const uint32_t width = grid->cellsX + 1, height = grid->cellsZ + 1;
    uint32_t count = 0;
    for(const Proc::Node& node : state.graph.nodes)
        if(std::holds_alternative<Proc::SetGridPointHeightNode>(node.payload)) ++count;
    Proc::SetGridPointHeightNode point;
    point.column = count % width;
    point.row = (count / width) % height;
    point.height = 1.5f;
    const Proc::NodeId id = Proc::addNode(state.graph, point, 280.0f, 240.0f + 80.0f * float(count));
    if(!id) return false;
    state.graph.links.erase(std::remove_if(state.graph.links.begin(), state.graph.links.end(),
        [&](const Proc::Link& link){ return link.to == meshId && link.toPort == 0; }), state.graph.links.end());
    state.graph.links.push_back({previous, 0, id, 0});
    state.graph.links.push_back({id, 0, meshId, 0});
    state.expandedNodes[id] = true;
    markGraphChanged(state);
    return true;
}

inline bool saveRecipeFile(WeaverProceduraPanelState& state){
    namespace fs = std::filesystem;
    try{
        const fs::path path(state.recipeFilePath);
        if(path.empty() || path.filename().empty()) throw std::runtime_error("Enter a recipe file path.");
        const std::string contents = WeaverProceduraRecipe::serialize({state.recipeName, state.graph});
        if(!path.parent_path().empty()) fs::create_directories(path.parent_path());
        fs::path temporary = path;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if(!file) throw std::runtime_error("Could not create the temporary recipe file.");
            file.write(contents.data(), std::streamsize(contents.size()));
            file.flush();
            if(!file) throw std::runtime_error("Could not write the complete recipe file.");
        }
        std::error_code error;
        fs::rename(temporary, path, error);
        if(error){ fs::remove(temporary); throw std::runtime_error("Could not replace the target recipe file: " + error.message()); }
        state.recipeStatus = "Saved recipe to " + path.string();
        state.newRecipeArmed = false;
        return true;
    }catch(const std::exception& error){
        state.recipeStatus = "Save failed: " + std::string(error.what());
        return false;
    }
}

inline bool loadRecipeFile(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    namespace fs = std::filesystem;
    try{
        const fs::path path(state.recipeFilePath);
        std::error_code error;
        const uintmax_t bytes = fs::file_size(path, error);
        if(error || bytes == 0 || bytes > 1'000'000)
            throw std::runtime_error("Recipe file must exist and contain at most 1000000 bytes.");
        std::ifstream file(path, std::ios::binary);
        if(!file) throw std::runtime_error("Could not open the recipe file.");
        const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        WeaverProceduraRecipe::Document document = WeaverProceduraRecipe::parse(contents);
        state.graph = std::move(document.graph);
        state.recipeName = std::move(document.name);
        state.expandedNodes.clear();
        for(const Proc::Node& node : state.graph.nodes) state.expandedNodes[node.id] = true;
        state.selectedCurveNodeId = 0;
        state.selectedControlPoint = -1;
        state.contextCurveNodeId = 0;
        state.contextControlPoint = -1;
        state.previewReady = false;
        state.previewVisible = false;
        state.previewMesh = {};
        state.previewPointCount = 0;
        state.previewIsPointCloud = false;
        state.graphDirty = true;
        state.previewError.clear();
        const bool evaluated = evaluateGraph(state);
        state.recipeStatus = evaluated ? "Loaded recipe from " + path.string()
                                      : "Loaded recipe; preview needs attention: " + state.previewError;
        state.newRecipeArmed = false;
        return evaluated;
    }catch(const std::exception& error){
        state.recipeStatus = "Load failed: " + std::string(error.what());
        return false;
    }
}

} // namespace WeaverProceduraUi

inline void drawWeaverProceduraPanel(Treadle::Ui& ui, WeaverProceduraPanelState& state,
                                     const Treadle::Rect& area){
    namespace Proc = Engine::WeaverProcedura;
    namespace Panel = WeaverProceduraUi;
    ui.dock("WEAVER PROCEDURA", area, &state.scroll);
    ui.label("PROCEDURAL WORKFLOW");
    ui.hint("Edit a versioned recipe graph and preview its generated mesh.");

    ui.separator();
    ui.caption("RECIPE FILE");
    Treadle::Ui::TextFieldConfig nameConfig;
    nameConfig.lines = 1;
    nameConfig.maxLength = 120;
    nameConfig.placeholder = "Recipe name";
    nameConfig.label = "Name";
    ui.textField("weaverprocedura-recipe-name", &state.recipeName, nameConfig);
    Treadle::Ui::TextFieldConfig pathConfig;
    pathConfig.lines = 1;
    pathConfig.maxLength = 1024;
    pathConfig.placeholder = "WeaverProcedura/Untitled.loomrecipe.json";
    pathConfig.label = "File";
    pathConfig.labelFraction = 0.22f;
    ui.textField("weaverprocedura-recipe-path", &state.recipeFilePath, pathConfig);
    const int recipeButton = ui.buttonRow({"Save Recipe", "Load Recipe",
                                            state.newRecipeArmed ? "Confirm New" : "New Recipe"});
    if(recipeButton == 0) WeaverProceduraUi::saveRecipeFile(state);
    else if(recipeButton == 1) WeaverProceduraUi::loadRecipeFile(state);
    else if(recipeButton == 2 && (!state.graph.nodes.empty() && !state.newRecipeArmed)){
        state.newRecipeArmed = true;
        state.recipeStatus = "Click Confirm New again to discard the current Recipe graph.";
    }else if(recipeButton == 2){
        state.graph = {};
        state.expandedNodes.clear();
        state.previewMesh = {};
        state.previewPointCount = 0;
        state.previewIsPointCloud = false;
        state.previewReady = false;
        state.previewVisible = false;
        state.graphDirty = false;
        state.previewError.clear();
        state.recipeName = "Untitled Recipe";
        state.newRecipeArmed = false;
        state.recipeStatus = "New recipe.";
        state.selectedCurveNodeId = 0;
        state.selectedControlPoint = -1;
        state.contextCurveNodeId = 0;
        state.contextControlPoint = -1;
        ++state.previewRevision;
    }
    if(!state.recipeStatus.empty()) ui.status(state.recipeStatus,
        state.recipeStatus.find("failed") != std::string::npos
            ? Treadle::Color{1.0f, 0.30f, 0.30f, 1.0f}
            : Treadle::Color{0.43f, 0.78f, 0.56f, 1.0f});

    if(state.graph.nodes.empty()){
        ui.separator();
        ui.caption("START WITH A RECIPE");
        ui.label("Interior blockout");
        ui.hint("A corridor with rooms on both sides, a floor slab, walls, dividers, and door openings.");
        if(ui.primaryButton("Create hallway + rooms")) Panel::createInteriorExample(state);
        if(ui.button("Create raised grid surface")) Panel::createGridExample(state);
        if(ui.button("Create road sweep")) Panel::createRoadExample(state);
        ui.separator();
        ui.caption("START A MESH RECIPE");
        ui.hint("Build one primitive, then chain mesh operations onto it.");
        if(ui.primaryButton("Add Primitive")) Panel::appendPrimitiveNode(state);
    }else{
        ui.separator();
        ui.caption("1  GRAPH OVERVIEW");
        ui.value("Graph", std::to_string(state.graph.nodes.size()) + " nodes / " +
                           std::to_string(state.graph.links.size()) + " links");
        ui.value("Seed", std::to_string(state.graph.seed));

        ui.caption("ADD TO THE MESH FLOW");
        if(ui.button("Add Primitive")) Panel::appendPrimitiveNode(state);
        const int firstOperation = ui.buttonRow({"Move","Rotate","Scale"});
        if(firstOperation == 0) Panel::appendMeshNode(state,Proc::MoveNode{});
        else if(firstOperation == 1) Panel::appendMeshNode(state,Proc::RotateNode{});
        else if(firstOperation == 2) Panel::appendMeshNode(state,Proc::ScaleNode{});
        const int secondOperation = ui.buttonRow({"Extrude","Bevel","Mesh to Point"});
        if(secondOperation == 0) Panel::appendMeshNode(state,Proc::ExtrudeNode{});
        else if(secondOperation == 1) Panel::appendMeshNode(state,Proc::BevelNode{});
        else if(secondOperation == 2) Panel::appendMeshNode(state,Proc::MeshToPointNode{});
        if(ui.button("Point from Mesh")) Panel::appendMeshNode(state,Proc::PointFromMeshNode{});
        if(!state.recipeStatus.empty()) ui.status(state.recipeStatus,{0.95f,0.70f,0.26f,1.0f});

        bool hasGridOutput = false;
        for(const Proc::Node& node : state.graph.nodes)
            hasGridOutput |= std::holds_alternative<Proc::GridToMeshNode>(node.payload);
        if(hasGridOutput && state.graph.nodes.size() < 256 && ui.button("Add raised grid point"))
            Panel::appendGridHeightNode(state);

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
            }else if(auto* grid = std::get_if<Proc::GridNode>(&node.payload)){
                changed |= ui.slider("Grid width", &grid->width, 0.5f, 100.0f, "m");
                changed |= ui.slider("Grid depth", &grid->depth, 0.5f, 100.0f, "m");
                const std::vector<std::string> cellOptions = {"2", "4", "8", "16", "32", "64"};
                const std::vector<uint32_t> cellValues = {2, 4, 8, 16, 32, 64};
                auto selectedCellOption = [&](uint32_t value){
                    const auto found = std::find(cellValues.begin(), cellValues.end(), value);
                    return found == cellValues.end() ? 1 : int(found - cellValues.begin());
                };
                int cellsX = selectedCellOption(grid->cellsX);
                if(ui.choice("Cells X", cellOptions, &cellsX) && cellsX >= 0 && cellsX < int(cellValues.size())){
                    grid->cellsX = cellValues[size_t(cellsX)]; changed = true;
                }
                int cellsZ = selectedCellOption(grid->cellsZ);
                if(ui.choice("Cells Z", cellOptions, &cellsZ) && cellsZ >= 0 && cellsZ < int(cellValues.size())){
                    grid->cellsZ = cellValues[size_t(cellsZ)]; changed = true;
                }
            }else if(auto* height = std::get_if<Proc::SetGridPointHeightNode>(&node.payload)){
                const Proc::GridNode* sourceGrid = nullptr;
                for(const Proc::Link& link : state.graph.links)
                    if(link.to == node.id && link.toPort == 0) sourceGrid = Panel::sourceGridSettings(state.graph, link.from);
                if(sourceGrid){
                    float column = float(std::min(height->column, sourceGrid->cellsX));
                    float row = float(std::min(height->row, sourceGrid->cellsZ));
                    if(ui.slider("Grid column", &column, 0.0f, float(sourceGrid->cellsX))){
                        height->column = uint32_t(std::lround(column)); changed = true;
                    }
                    if(ui.slider("Grid row", &row, 0.0f, float(sourceGrid->cellsZ))){
                        height->row = uint32_t(std::lround(row)); changed = true;
                    }
                }
                changed |= ui.slider("Point height", &height->height, -20.0f, 20.0f, "m");
            }else if(std::holds_alternative<Proc::GridToMeshNode>(node.payload)){
                ui.hint("Triangulates the evaluated grid points into a shaded XZ mesh.");
            }else if(auto* interior = std::get_if<Proc::InteriorBlockoutNode>(&node.payload)){
                const std::vector<std::string> roomOptions = {"1", "2", "4", "6", "8"};
                const std::vector<uint32_t> roomValues = {1, 2, 4, 6, 8};
                auto selectedRoomOption = [&]{
                    const auto found = std::find(roomValues.begin(), roomValues.end(), interior->roomsPerSide);
                    return found == roomValues.end() ? 1 : int(found - roomValues.begin());
                };
                int rooms = selectedRoomOption();
                if(ui.choice("Rooms on each side", roomOptions, &rooms) && rooms >= 0 && rooms < int(roomValues.size())){
                    interior->roomsPerSide = roomValues[size_t(rooms)]; changed = true;
                }
                changed |= ui.slider("Room width", &interior->roomWidth, 2.0f, 12.0f, "m");
                changed |= ui.slider("Room depth", &interior->roomDepth, 2.0f, 12.0f, "m");
                changed |= ui.slider("Corridor width", &interior->corridorWidth, 1.0f, 5.0f, "m");
                changed |= ui.slider("Wall height", &interior->wallHeight, 2.0f, 6.0f, "m");
                changed |= ui.slider("Wall thickness", &interior->wallThickness, 0.08f, 0.4f, "m");
                const float maxDoorWidth = std::max(0.2f, std::min(1.8f,
                    interior->roomWidth - 2.0f * interior->wallThickness - 0.02f));
                changed |= ui.slider("Door width", &interior->doorWidth, 0.2f, maxDoorWidth, "m");
                changed |= ui.slider("Floor thickness", &interior->floorThickness, 0.05f, 0.5f, "m");
            }else if(auto* primitive = std::get_if<Proc::AddPrimitiveNode>(&node.payload)){
                const std::vector<std::string> primitiveOptions = {"Cube","Plane","Sphere","Pyramid","Capsule"};
                int selected = int(primitive->primitive);
                if(ui.choice("Primitive",primitiveOptions,&selected) && selected >= 0 && selected < int(primitiveOptions.size())){
                    primitive->primitive = Proc::PrimitiveType(selected);
                    changed = true;
                }
                changed |= ui.slider("Size X",&primitive->size.x,0.1f,20.0f,"m");
                if(primitive->primitive != Proc::PrimitiveType::Plane)
                    changed |= ui.slider("Size Y",&primitive->size.y,0.1f,20.0f,"m");
                else ui.hint("Plane lies flat on XZ and has no Y thickness.");
                changed |= ui.slider("Size Z",&primitive->size.z,0.1f,20.0f,"m");
            }else if(auto* move = std::get_if<Proc::MoveNode>(&node.payload)){
                changed |= ui.slider("Move X",&move->offset.x,-20.0f,20.0f,"m");
                changed |= ui.slider("Move Y",&move->offset.y,-20.0f,20.0f,"m");
                changed |= ui.slider("Move Z",&move->offset.z,-20.0f,20.0f,"m");
            }else if(auto* rotate = std::get_if<Proc::RotateNode>(&node.payload)){
                changed |= ui.slider("Rotate X",&rotate->degrees.x,-360.0f,360.0f,"°");
                changed |= ui.slider("Rotate Y",&rotate->degrees.y,-360.0f,360.0f,"°");
                changed |= ui.slider("Rotate Z",&rotate->degrees.z,-360.0f,360.0f,"°");
                changed |= ui.slider("Pivot X",&rotate->pivot.x,-20.0f,20.0f,"m");
                changed |= ui.slider("Pivot Y",&rotate->pivot.y,-20.0f,20.0f,"m");
                changed |= ui.slider("Pivot Z",&rotate->pivot.z,-20.0f,20.0f,"m");
            }else if(auto* scale = std::get_if<Proc::ScaleNode>(&node.payload)){
                changed |= ui.slider("Scale X",&scale->factor.x,0.01f,10.0f,"x");
                changed |= ui.slider("Scale Y",&scale->factor.y,0.01f,10.0f,"x");
                changed |= ui.slider("Scale Z",&scale->factor.z,0.01f,10.0f,"x");
                changed |= ui.slider("Pivot X",&scale->pivot.x,-20.0f,20.0f,"m");
                changed |= ui.slider("Pivot Y",&scale->pivot.y,-20.0f,20.0f,"m");
                changed |= ui.slider("Pivot Z",&scale->pivot.z,-20.0f,20.0f,"m");
            }else if(auto* extrude = std::get_if<Proc::ExtrudeNode>(&node.payload)){
                const float maximumFace = float(std::max<std::size_t>(1,state.previewMesh.indices.size()/3)-1);
                float face = std::min(float(extrude->faceIndex),maximumFace);
                if(ui.slider("Seed triangle",&face,0.0f,maximumFace)){
                    extrude->faceIndex = uint32_t(std::lround(face));
                    changed = true;
                }
                changed |= ui.slider("Extrude distance",&extrude->distance,-10.0f,10.0f,"m");
                ui.hint("The seed triangle selects its connected coplanar face.");
            }else if(auto* bevel = std::get_if<Proc::BevelNode>(&node.payload)){
                changed |= ui.slider("Bevel amount",&bevel->amount,0.01f,0.5f,"m");
                const std::vector<std::string> segmentOptions = {"1","2","3","4","6","8"};
                const std::vector<uint32_t> segmentValues = {1,2,3,4,6,8};
                int selected = 0;
                for(std::size_t i = 0; i < segmentValues.size(); ++i)
                    if(segmentValues[i] == bevel->segments) selected = int(i);
                if(ui.choice("Segments",segmentOptions,&selected) && selected >= 0 && selected < int(segmentValues.size())){
                    bevel->segments = segmentValues[size_t(selected)];
                    changed = true;
                }
                ui.hint("Bevel insets each planar face along its boundary.");
            }else if(std::holds_alternative<Proc::MeshToPointNode>(node.payload)){
                ui.hint("Creates one point for each unique mesh vertex position.");
            }else if(auto* sample = std::get_if<Proc::PointFromMeshNode>(&node.payload)){
                float count = float(sample->count);
                if(ui.slider("Surface point count",&count,1.0f,20000.0f,"points")){
                    sample->count = uint32_t(std::lround(count));
                    changed = true;
                }
                float seed = float(std::min<uint64_t>(sample->seed,1'000'000));
                if(ui.slider("Seed",&seed,0.0f,1'000'000.0f)){
                    sample->seed = uint64_t(std::lround(seed));
                    changed = true;
                }
                ui.hint("Samples mesh triangles proportionally to their surface area.");
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
            ui.status(state.previewIsPointCloud
                ? "Point cloud preview is up to date in the viewport."
                : "Preview is up to date in the viewport.", {0.43f, 0.78f, 0.56f, 1.0f});
        }else{
            ui.status("No viewport preview yet. Evaluate the graph to see its mesh.",
                      {0.95f, 0.70f, 0.26f, 1.0f});
        }
        if((state.graphDirty || !state.previewReady) && ui.primaryButton("Update viewport preview"))
            Panel::evaluateGraph(state);

        if(state.previewReady){
            if(state.previewIsPointCloud){
                ui.value("Points",std::to_string(state.previewPointCount));
                ui.hint("Markers show the point positions; save the Recipe to keep the node graph.");
            }else{
                ui.value("Vertices", std::to_string(state.previewMesh.vertices.size()));
                ui.value("Triangles", std::to_string(state.previewMesh.indices.size() / 3));
            }
            ui.hint("Temporary preview; this graph is not saved in the scene yet.");
            if(ui.button("Clear viewport preview")){
                state.previewMesh = {};
                state.previewPointCount = 0;
                state.previewIsPointCloud = false;
                state.previewReady = false;
                state.previewVisible = false;
                state.graphDirty = true;
                state.previewError.clear();
                ++state.previewRevision;
            }
        }
    }

    if(ui.button("Close")) state.open = false;
}

}
