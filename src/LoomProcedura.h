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
    int connectFrom = 0, connectTo = 0, connectPort = 0, removeLink = 0;   //odabir u FLOW i CONNECT
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
    if(std::holds_alternative<Proc::CircleProfileNode>(node.payload)) return "Circle profile";
    if(std::holds_alternative<Proc::MergeNode>(node.payload)) return "Merge";
    if(std::holds_alternative<Proc::SetSemanticNode>(node.payload)) return "Set Semantic";
    if(std::holds_alternative<Proc::SetMaterialNode>(node.payload)) return "Set Material";
    if(std::holds_alternative<Proc::SmoothNormalsNode>(node.payload)) return "Smooth Normals";
    if(std::holds_alternative<Proc::UVProjectNode>(node.payload)) return "UV Project";
    if(std::holds_alternative<Proc::CopyToPointsNode>(node.payload)) return "Copy to Points";
    if(std::holds_alternative<Proc::CurveSmoothNode>(node.payload)) return "Curve Smooth";
    if(std::holds_alternative<Proc::CatenaryCurveNode>(node.payload)) return "Catenary curve";
    if(std::holds_alternative<Proc::CopyAlongCurveNode>(node.payload)) return "Copy along Curve";
    if(std::holds_alternative<Proc::FootprintNode>(node.payload)) return "Footprint";
    if(std::holds_alternative<Proc::FootprintFromCurveNode>(node.payload)) return "Footprint from Curve";
    if(std::holds_alternative<Proc::FloorStackNode>(node.payload)) return "Floor Stack";
    if(std::holds_alternative<Proc::WallsNode>(node.payload)) return "Walls";
    if(std::holds_alternative<Proc::SlabNode>(node.payload)) return "Slab";
    if(std::holds_alternative<Proc::RoofNode>(node.payload)) return "Roof";
    if(std::holds_alternative<Proc::StairsNode>(node.payload)) return "Stairs";
    if(std::holds_alternative<Proc::RoadFromCurveNode>(node.payload)) return "Road from Curve";
    if(std::holds_alternative<Proc::RoomSplitNode>(node.payload)) return "Room Split";
    if(std::holds_alternative<Proc::InteriorNode>(node.payload)) return "Interior";
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
        return (extrude->useFilter ? std::string("filtered faces") : "seed triangle " + std::to_string(extrude->faceIndex)) +
               " / " + formatSize(extrude->distance) + " m";
    if(const auto* bevel = std::get_if<Proc::BevelNode>(&node.payload))
        return formatSize(bevel->amount) + " m / " + std::to_string(bevel->segments) + " segments";
    if(std::holds_alternative<Proc::MeshToPointNode>(node.payload)) return "unique mesh vertex positions";
    if(const auto* sample = std::get_if<Proc::PointFromMeshNode>(&node.payload))
        return std::to_string(sample->count) + " surface points / seed " + std::to_string(sample->seed);
    auto filterText = [](const Proc::TriangleFilter& filter){
        std::string text = filter.semantic.empty() ? "all" : filter.semantic;
        if(filter.useDirection) text += " / facing within " + formatSize(filter.maxAngleDegrees) + "°";
        return text;
    };
    if(const auto* circle = std::get_if<Proc::CircleProfileNode>(&node.payload))
        return "radius " + formatSize(circle->radius) + " m / " + std::to_string(circle->sides) + " sides";
    if(std::holds_alternative<Proc::MergeNode>(node.payload)) return "joins up to 8 meshes";
    if(const auto* tag = std::get_if<Proc::SetSemanticNode>(&node.payload))
        return tag->semantic + " on " + filterText(tag->filter);
    if(const auto* paint = std::get_if<Proc::SetMaterialNode>(&node.payload))
        return paint->material + " on " + filterText(paint->filter);
    if(const auto* smooth = std::get_if<Proc::SmoothNormalsNode>(&node.payload))
        return "below " + formatSize(smooth->angleDegrees) + "°";
    if(const auto* uv = std::get_if<Proc::UVProjectNode>(&node.payload))
        return "box / " + formatSize(uv->tileSize) + " m tiles";
    if(const auto* copy = std::get_if<Proc::CopyToPointsNode>(&node.payload))
        return std::string(copy->alignToNormal ? "aligned" : "upright") + " / scale " + formatSize(copy->scale);
    if(const auto* smooth = std::get_if<Proc::CurveSmoothNode>(&node.payload))
        return std::to_string(smooth->subdivisions) + " samples per segment";
    if(const auto* catenary = std::get_if<Proc::CatenaryCurveNode>(&node.payload))
        return "sag " + formatSize(catenary->sag) + " m / " + std::to_string(catenary->samples) + " samples";
    if(const auto* along = std::get_if<Proc::CopyAlongCurveNode>(&node.payload))
        return "every " + formatSize(along->spacing) + " m" +
               (along->alternateRollDegrees != 0.0f ? " / alternate " + formatSize(along->alternateRollDegrees) + "°" : "");
    if(const auto* footprint = std::get_if<Proc::FootprintNode>(&node.payload)){
        static const char* shapes[] = {"rectangle", "L", "U"};
        return std::string(shapes[std::min(int(footprint->shape), 2)]) + " / " + formatSize(footprint->width) + " x " +
               formatSize(footprint->depth) + " m";
    }
    if(const auto* traced = std::get_if<Proc::FootprintFromCurveNode>(&node.payload))
        return traced->rectify ? "closed curve outline / snapped to right angles" : "closed curve outline";
    if(const auto* stack = std::get_if<Proc::FloorStackNode>(&node.payload))
        return std::to_string(stack->floors) + " floors x " + formatSize(stack->floorHeight) + " m";
    if(const auto* walls = std::get_if<Proc::WallsNode>(&node.payload))
        return formatSize(walls->thickness) + " m" + (walls->windows ? " / windows" : "") + (walls->door ? " / door" : "");
    if(const auto* slab = std::get_if<Proc::SlabNode>(&node.payload))
        return formatSize(slab->thickness) + " m slabs" + (slab->foundation ? " / plinth" : "");
    if(const auto* roof = std::get_if<Proc::RoofNode>(&node.payload)){
        static const char* types[] = {"flat", "gable", "hip", "shed"};
        return std::string(types[std::min(int(roof->type), 3)]) +
               (roof->type == Proc::RoofType::Flat ? "" : " / " + formatSize(roof->pitchDegrees) + "°");
    }
    if(const auto* stairs = std::get_if<Proc::StairsNode>(&node.payload))
        return std::to_string(stairs->steps) + " steps / " + formatSize(stairs->totalRise) + " m rise";
    if(const auto* road = std::get_if<Proc::RoadFromCurveNode>(&node.payload))
        return formatSize(road->roadWidth) + " m road" + (road->sidewalks ? " / sidewalks" : "");
    if(const auto* split = std::get_if<Proc::RoomSplitNode>(&node.payload))
        return std::string(split->program == Proc::InteriorProgram::Office ? "office" : "home") + " / seed " + std::to_string(split->seed);
    if(const auto* interior = std::get_if<Proc::InteriorNode>(&node.payload))
        return formatSize(interior->partitionThickness) + " m partitions" + (interior->stairs ? " / stairs" : "");
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
    if(std::holds_alternative<Engine::WeaverProcedura::MergeNode>(node.payload))
        return "Mesh input " + std::to_string(port + 1);
    if(std::holds_alternative<Engine::WeaverProcedura::CopyToPointsNode>(node.payload))
        return port == 0 ? "Instance mesh" : "Points input";
    if(std::holds_alternative<Engine::WeaverProcedura::CopyAlongCurveNode>(node.payload))
        return port == 0 ? "Instance mesh" : "Curve input";
    if(std::holds_alternative<Engine::WeaverProcedura::CurveSmoothNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::FootprintFromCurveNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::RoadFromCurveNode>(node.payload)) return "Curve input";
    if(std::holds_alternative<Engine::WeaverProcedura::FloorStackNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::WallsNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::SlabNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::RoofNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::RoomSplitNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::InteriorNode>(node.payload)) return "Footprint input";
    if(std::holds_alternative<Engine::WeaverProcedura::SetGridPointHeightNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::GridToMeshNode>(node.payload))
        return "Point Grid input";
    if(std::holds_alternative<Engine::WeaverProcedura::MoveNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::RotateNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::ScaleNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::ExtrudeNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::BevelNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::MeshToPointNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::PointFromMeshNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::SetSemanticNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::SetMaterialNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::SmoothNormalsNode>(node.payload) ||
       std::holds_alternative<Engine::WeaverProcedura::UVProjectNode>(node.payload)) return "Mesh input";
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
           std::holds_alternative<Proc::PointFromMeshNode>(node.payload) ||
           std::holds_alternative<Proc::MergeNode>(node.payload) ||
           std::holds_alternative<Proc::SetSemanticNode>(node.payload) ||
           std::holds_alternative<Proc::SetMaterialNode>(node.payload) ||
           std::holds_alternative<Proc::SmoothNormalsNode>(node.payload) ||
           std::holds_alternative<Proc::UVProjectNode>(node.payload) ||
           std::holds_alternative<Proc::CopyToPointsNode>(node.payload) ||
           std::holds_alternative<Proc::CopyAlongCurveNode>(node.payload) ||
           std::holds_alternative<Proc::WallsNode>(node.payload) ||
           std::holds_alternative<Proc::SlabNode>(node.payload) ||
           std::holds_alternative<Proc::RoofNode>(node.payload) ||
           std::holds_alternative<Proc::StairsNode>(node.payload) ||
           std::holds_alternative<Proc::RoadFromCurveNode>(node.payload) ||
           std::holds_alternative<Proc::InteriorNode>(node.payload);
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

inline bool vocabularyChoice(Treadle::Ui& ui, const std::string& label, const std::vector<std::string>& names,
                             std::string& value, bool allowAny){
    std::vector<std::string> options;
    if(allowAny) options.push_back("any");
    options.insert(options.end(), names.begin(), names.end());
    const auto found = std::find(options.begin(), options.end(), value.empty() && allowAny ? "any" : value);
    int selected = found == options.end() ? 0 : int(found - options.begin());
    if(!ui.choice(label, options, &selected) || selected < 0 || selected >= int(options.size())) return false;
    value = allowAny && selected == 0 ? std::string{} : options[std::size_t(selected)];
    return true;
}

inline bool filterControls(Treadle::Ui& ui, Engine::WeaverProcedura::TriangleFilter& filter){
    bool changed = vocabularyChoice(ui, "Only semantic", Engine::WeaverProcedura::semanticVocabulary(), filter.semantic, true);
    changed |= ui.checkbox("Only faces facing a direction", &filter.useDirection);
    if(filter.useDirection){
        const std::vector<std::string> directions = {"up", "down", "+X", "-X", "+Z", "-Z"};
        const glm::vec3 axes[] = {{0,1,0},{0,-1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        int selected = 0;
        for(int i = 0; i < 6; ++i) if(glm::dot(filter.direction, axes[i]) > 0.99f) selected = i;
        if(ui.choice("Direction", directions, &selected) && selected >= 0 && selected < 6){
            filter.direction = axes[selected]; changed = true;
        }
        changed |= ui.slider("Max angle", &filter.maxAngleDegrees, 0.0f, 90.0f, "°");
    }
    return changed;
}

// Adds (or replaces the link on) one input port; validate() decides whether the types
// match, the port exists, and the graph stays acyclic.
inline bool connectNodes(WeaverProceduraPanelState& state, Engine::WeaverProcedura::NodeId from,
                         Engine::WeaverProcedura::NodeId to, uint32_t port){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Graph candidate = state.graph;
    candidate.links.erase(std::remove_if(candidate.links.begin(), candidate.links.end(),
        [&](const Proc::Link& link){ return link.to == to && link.toPort == port; }), candidate.links.end());
    candidate.links.push_back({from, 0, to, port});
    const Proc::ValidationResult valid = Proc::validate(candidate);
    if(!valid){ state.recipeStatus = "Cannot connect: " + valid.error; return false; }
    state.graph = std::move(candidate);
    markGraphChanged(state);
    state.recipeStatus.clear();
    return true;
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

// Adds a node without chaining it to the mesh flow. Nodes that read a footprint are wired
// to the newest footprint producer; the rest are joined with CONNECT or a Merge.
inline bool appendLooseNode(WeaverProceduraPanelState& state, Engine::WeaverProcedura::NodePayload payload){
    namespace Proc = Engine::WeaverProcedura;
    if(state.graph.nodes.size() >= 256){ state.recipeStatus = "Recipe has reached its node limit."; return false; }
    const bool readsFootprint = std::holds_alternative<Proc::FloorStackNode>(payload) || std::holds_alternative<Proc::WallsNode>(payload) ||
                                std::holds_alternative<Proc::SlabNode>(payload) || std::holds_alternative<Proc::RoofNode>(payload) ||
                                std::holds_alternative<Proc::RoomSplitNode>(payload) || std::holds_alternative<Proc::InteriorNode>(payload);
    const Proc::Node* source = nullptr;
    for(const Proc::Node& node : state.graph.nodes)
        if(std::holds_alternative<Proc::FootprintNode>(node.payload) || std::holds_alternative<Proc::FootprintFromCurveNode>(node.payload) ||
           std::holds_alternative<Proc::FloorStackNode>(node.payload) || std::holds_alternative<Proc::RoomSplitNode>(node.payload)) source = &node;
    if(readsFootprint && !source){ state.recipeStatus = "Add a Footprint first."; return false; }
    const float x = source ? source->editorX + 240.0f : 24.0f;
    const float y = 60.0f + 70.0f * float(state.graph.nodes.size() % 8);
    const Proc::NodeId sourceId = source ? source->id : 0;
    const Proc::NodeId id = Proc::addNode(state.graph, std::move(payload), x, y);
    if(!id){ state.recipeStatus = "Could not add the node."; return false; }
    if(readsFootprint) state.graph.links.push_back({sourceId, 0, id, 0});
    state.expandedNodes[id] = true;
    markGraphChanged(state);
    state.recipeStatus = readsFootprint ? std::string() : "Added. Join it to the output with CONNECT or a Merge.";
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

// Replaces the panel graph with a finished starter recipe and previews it.
inline bool installExample(WeaverProceduraPanelState& state, Engine::WeaverProcedura::Graph graph, const std::string& name){
    state.graph = std::move(graph);
    state.recipeName = name;
    state.expandedNodes.clear();
    for(const Engine::WeaverProcedura::Node& node : state.graph.nodes) state.expandedNodes[node.id] = true;
    state.selectedCurveNodeId = 0;
    state.selectedControlPoint = -1;
    state.contextCurveNodeId = 0;
    state.contextControlPoint = -1;
    state.graphDirty = true;
    state.previewError.clear();
    return evaluateGraph(state);
}

// Catenary -> Circle Profile -> Sweep, tagged as rope fiber.
inline bool createRopeExample(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Graph graph;
    const Proc::NodeId path = Proc::addNode(graph, Proc::CatenaryCurveNode{{-3,3,0},{3,2.5f,0},0.8f,48}, 24.0f, 88.0f);
    const Proc::NodeId profile = Proc::addNode(graph, Proc::CircleProfileNode{0.03f,10}, 24.0f, 250.0f);
    Proc::SweepNode sweep;
    sweep.settings.sampleSpacing = 0.1f;
    const Proc::NodeId mesh = Proc::addNode(graph, sweep, 280.0f, 170.0f);
    Proc::SetSemanticNode tag; tag.semantic = "rope";
    const Proc::NodeId tagged = Proc::addNode(graph, tag, 520.0f, 170.0f);
    Proc::SetMaterialNode paint; paint.material = "rope_fiber";
    const Proc::NodeId painted = Proc::addNode(graph, paint, 760.0f, 170.0f);
    graph.links = {{path,0,mesh,0},{profile,0,mesh,1},{mesh,0,tagged,0},{tagged,0,painted,0}};
    return installExample(state, std::move(graph), "Hanging Rope");
}

// Torus link copied along a catenary, every other link rolled 90 degrees.
inline bool createChainExample(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Graph graph;
    const Proc::NodeId path = Proc::addNode(graph, Proc::CatenaryCurveNode{{-3,3,0},{3,3,0},1.0f,96}, 24.0f, 88.0f);
    Proc::AddPrimitiveNode link;
    link.primitive = Proc::PrimitiveType::Torus;
    link.size = {0.16f, 0.16f, 0.10f};
    link.tubeRatio = 0.22f;
    const Proc::NodeId linkMesh = Proc::addNode(graph, link, 24.0f, 250.0f);
    Proc::CopyAlongCurveNode along;
    along.spacing = 0.085f;   // just under the link's inner length, so neighbours interlock
    along.alternateRollDegrees = 90.0f;
    const Proc::NodeId chain = Proc::addNode(graph, along, 280.0f, 170.0f);
    Proc::SetSemanticNode tag; tag.semantic = "chain_link";
    const Proc::NodeId tagged = Proc::addNode(graph, tag, 520.0f, 170.0f);
    Proc::SetMaterialNode paint; paint.material = "steel_chain";
    const Proc::NodeId painted = Proc::addNode(graph, paint, 760.0f, 170.0f);
    graph.links = {{linkMesh,0,chain,0},{path,0,chain,1},{chain,0,tagged,0},{tagged,0,painted,0}};
    return installExample(state, std::move(graph), "Hanging Chain");
}

// L-shaped two-storey house: Footprint -> Floor Stack -> Walls, Slab, Roof -> Merge.
inline bool createHouseExample(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Graph graph;
    Proc::FootprintNode print;
    print.shape = Proc::FootprintShape::LShape;
    print.width = 12.0f; print.depth = 10.0f; print.wingWidth = 5.0f;
    const Proc::NodeId footprint = Proc::addNode(graph, print, 24.0f, 170.0f);
    const Proc::NodeId stack = Proc::addNode(graph, Proc::FloorStackNode{2, 3.0f, 0.4f}, 264.0f, 170.0f);
    const Proc::NodeId rooms = Proc::addNode(graph, Proc::RoomSplitNode{}, 504.0f, 170.0f);
    const Proc::NodeId walls = Proc::addNode(graph, Proc::WallsNode{}, 744.0f, 20.0f);
    Proc::SlabNode slab; slab.topCeiling = true;
    const Proc::NodeId slabs = Proc::addNode(graph, slab, 744.0f, 120.0f);
    const Proc::NodeId roof = Proc::addNode(graph, Proc::RoofNode{}, 744.0f, 220.0f);
    const Proc::NodeId interior = Proc::addNode(graph, Proc::InteriorNode{}, 744.0f, 320.0f);
    const Proc::NodeId merge = Proc::addNode(graph, Proc::MergeNode{}, 984.0f, 170.0f);
    Proc::SetMaterialNode brick; brick.material = "brick"; brick.filter.semantic = "wall_exterior";
    const Proc::NodeId painted = Proc::addNode(graph, brick, 1224.0f, 170.0f);
    graph.links = {{footprint,0,stack,0},{stack,0,rooms,0},{rooms,0,walls,0},{rooms,0,slabs,0},{rooms,0,roof,0},{rooms,0,interior,0},
                   {walls,0,merge,0},{slabs,0,merge,1},{roof,0,merge,2},{interior,0,merge,3},{merge,0,painted,0}};
    return installExample(state, std::move(graph), "L-shaped House");
}

// A curved street with curbs and sidewalks.
inline bool createStreetExample(WeaverProceduraPanelState& state){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Graph graph;
    Proc::CurveNode path;
    path.curve.points = {{-20.0f, 0.0f, -6.0f}, {-6.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 8.0f}};
    const Proc::NodeId curve = Proc::addNode(graph, std::move(path), 24.0f, 170.0f);
    const Proc::NodeId smooth = Proc::addNode(graph, Proc::CurveSmoothNode{8}, 264.0f, 170.0f);
    const Proc::NodeId road = Proc::addNode(graph, Proc::RoadFromCurveNode{}, 504.0f, 170.0f);
    graph.links = {{curve,0,smooth,0},{smooth,0,road,0}};
    return installExample(state, std::move(graph), "Street");
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
        const int hanging = ui.buttonRow({"Create rope","Create chain"});
        if(hanging == 0) Panel::createRopeExample(state);
        else if(hanging == 1) Panel::createChainExample(state);
        ui.caption("BUILDINGS AND STREETS");
        const int built = ui.buttonRow({"Create house","Create street"});
        if(built == 0) Panel::createHouseExample(state);
        else if(built == 1) Panel::createStreetExample(state);
        if(ui.button("Create stairs")) Panel::installExample(state, [] {
            Engine::WeaverProcedura::Graph graph;
            Engine::WeaverProcedura::addNode(graph, Engine::WeaverProcedura::StairsNode{}, 24.0f, 170.0f);
            return graph;
        }(), "Stairs");
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
        const int lookOperation = ui.buttonRow({"Semantic","Material","Smooth","UV"});
        if(lookOperation == 0) Panel::appendMeshNode(state,Proc::SetSemanticNode{});
        else if(lookOperation == 1) Panel::appendMeshNode(state,Proc::SetMaterialNode{});
        else if(lookOperation == 2) Panel::appendMeshNode(state,Proc::SmoothNormalsNode{});
        else if(lookOperation == 3) Panel::appendMeshNode(state,Proc::UVProjectNode{});
        ui.caption("ADD BUILDING PARTS");
        const int buildingPart = ui.buttonRow({"Footprint","Floors","Walls"});
        if(buildingPart == 0) Panel::appendLooseNode(state,Proc::FootprintNode{});
        else if(buildingPart == 1) Panel::appendLooseNode(state,Proc::FloorStackNode{});
        else if(buildingPart == 2) Panel::appendLooseNode(state,Proc::WallsNode{});
        const int interiorPart = ui.buttonRow({"Room Split","Interior"});
        if(interiorPart == 0) Panel::appendLooseNode(state,Proc::RoomSplitNode{});
        else if(interiorPart == 1) Panel::appendLooseNode(state,Proc::InteriorNode{});
        const int buildingTop = ui.buttonRow({"Slab","Roof","Stairs","Merge"});
        if(buildingTop == 0) Panel::appendLooseNode(state,Proc::SlabNode{});
        else if(buildingTop == 1) Panel::appendLooseNode(state,Proc::RoofNode{});
        else if(buildingTop == 2) Panel::appendLooseNode(state,Proc::StairsNode{});
        else if(buildingTop == 3) Panel::appendLooseNode(state,Proc::MergeNode{});
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
        if(!state.graph.links.empty()){
            std::vector<std::string> linkNames;
            for(const Proc::Link& link : state.graph.links)
                linkNames.push_back(Panel::nodeLabel(state.graph, link.from) + " -> " + Panel::nodeLabel(state.graph, link.to));
            state.removeLink = std::clamp(state.removeLink, 0, int(linkNames.size()) - 1);
            ui.choice("Link", linkNames, &state.removeLink);
            if(ui.button("Remove link")){
                state.graph.links.erase(state.graph.links.begin() + state.removeLink);
                Panel::markGraphChanged(state);
            }
        }
        if(state.graph.nodes.size() >= 2){
            ui.caption("CONNECT");
            std::vector<std::string> nodeNames;
            for(const Proc::Node& node : state.graph.nodes) nodeNames.push_back(Panel::nodeLabel(state.graph, node.id));
            state.connectFrom = std::clamp(state.connectFrom, 0, int(nodeNames.size()) - 1);
            state.connectTo = std::clamp(state.connectTo, 0, int(nodeNames.size()) - 1);
            ui.choice("From", nodeNames, &state.connectFrom);
            ui.choice("To", nodeNames, &state.connectTo);
            const Proc::Node& target = state.graph.nodes[std::size_t(state.connectTo)];
            const uint32_t ports = Proc::inputPortCount(target);
            if(ports == 0) ui.hint("This node has no inputs.");
            else{
                std::vector<std::string> portNames;
                for(uint32_t port = 0; port < ports; ++port) portNames.push_back(Panel::inputName(target, port));
                state.connectPort = std::clamp(state.connectPort, 0, int(ports) - 1);
                ui.choice("Input", portNames, &state.connectPort);
                if(ui.button("Connect"))
                    Panel::connectNodes(state, state.graph.nodes[std::size_t(state.connectFrom)].id, target.id, uint32_t(state.connectPort));
            }
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
                const std::vector<std::string> primitiveOptions = {"Cube","Plane","Sphere","Pyramid","Capsule","Cylinder","Torus"};
                int selected = int(primitive->primitive);
                if(ui.choice("Primitive",primitiveOptions,&selected) && selected >= 0 && selected < int(primitiveOptions.size())){
                    primitive->primitive = Proc::PrimitiveType(selected);
                    changed = true;
                }
                changed |= ui.slider("Size X",&primitive->size.x,0.1f,20.0f,"m");
                if(primitive->primitive != Proc::PrimitiveType::Plane)
                    changed |= ui.slider("Size Y",&primitive->size.y,0.1f,20.0f,"m");
                else ui.hint("Plane lies flat on XZ and has no Y thickness.");
                if(primitive->primitive == Proc::PrimitiveType::Torus)
                    changed |= ui.slider("Tube ratio",&primitive->tubeRatio,0.02f,0.49f);
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
                changed |= ui.checkbox("Select faces by filter",&extrude->useFilter);
                if(extrude->useFilter) changed |= Panel::filterControls(ui,extrude->filter);
                ui.hint(extrude->useFilter ? "Every flat face matching the filter moves along its own normal."
                                           : "The seed triangle selects its connected coplanar face.");
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
            }else if(auto* circle = std::get_if<Proc::CircleProfileNode>(&node.payload)){
                changed |= ui.slider("Radius",&circle->radius,0.005f,5.0f,"m");
                float sides = float(circle->sides);
                if(ui.slider("Sides",&sides,3.0f,64.0f)){ circle->sides = uint32_t(std::lround(sides)); changed = true; }
            }else if(std::holds_alternative<Proc::MergeNode>(node.payload)){
                ui.hint("Joins every connected mesh input; each triangle keeps the node that made it.");
            }else if(auto* tag = std::get_if<Proc::SetSemanticNode>(&node.payload)){
                changed |= Panel::vocabularyChoice(ui,"Semantic",Engine::WeaverProcedura::semanticVocabulary(),tag->semantic,false);
                changed |= Panel::filterControls(ui,tag->filter);
            }else if(auto* paint = std::get_if<Proc::SetMaterialNode>(&node.payload)){
                changed |= Panel::vocabularyChoice(ui,"Material",Engine::WeaverProcedura::materialLibrary(),paint->material,false);
                changed |= Panel::filterControls(ui,paint->filter);
            }else if(auto* smooth = std::get_if<Proc::SmoothNormalsNode>(&node.payload)){
                changed |= ui.slider("Smoothing angle",&smooth->angleDegrees,0.0f,180.0f,"°");
            }else if(auto* uv = std::get_if<Proc::UVProjectNode>(&node.payload)){
                changed |= ui.slider("Tile size",&uv->tileSize,0.05f,20.0f,"m");
            }else if(auto* copy = std::get_if<Proc::CopyToPointsNode>(&node.payload)){
                changed |= ui.checkbox("Align to point normal",&copy->alignToNormal);
                changed |= ui.slider("Scale",&copy->scale,0.01f,10.0f);
                changed |= ui.slider("Random yaw",&copy->randomYawDegrees,0.0f,180.0f,"°");
                changed |= ui.slider("Random scale",&copy->randomScale,0.0f,0.9f);
            }else if(auto* smooth = std::get_if<Proc::CurveSmoothNode>(&node.payload)){
                float samples = float(smooth->subdivisions);
                if(ui.slider("Samples per segment",&samples,1.0f,32.0f)){ smooth->subdivisions = uint32_t(std::lround(samples)); changed = true; }
            }else if(auto* catenary = std::get_if<Proc::CatenaryCurveNode>(&node.payload)){
                changed |= ui.slider("Start X",&catenary->start.x,-20.0f,20.0f,"m");
                changed |= ui.slider("Start Y",&catenary->start.y,-10.0f,20.0f,"m");
                changed |= ui.slider("End X",&catenary->end.x,-20.0f,20.0f,"m");
                changed |= ui.slider("End Y",&catenary->end.y,-10.0f,20.0f,"m");
                changed |= ui.slider("End Z",&catenary->end.z,-20.0f,20.0f,"m");
                changed |= ui.slider("Sag",&catenary->sag,0.0f,10.0f,"m");
            }else if(auto* along = std::get_if<Proc::CopyAlongCurveNode>(&node.payload)){
                changed |= ui.slider("Spacing",&along->spacing,0.01f,10.0f,"m");
                changed |= ui.slider("Start offset",&along->startOffset,0.0f,10.0f,"m");
                changed |= ui.slider("Roll",&along->rollDegrees,-180.0f,180.0f,"°");
                changed |= ui.slider("Alternate roll",&along->alternateRollDegrees,-180.0f,180.0f,"°");
            }else if(auto* footprint = std::get_if<Proc::FootprintNode>(&node.payload)){
                int shape = int(footprint->shape);
                if(ui.choice("Shape",{"Rectangle","L shape","U shape"},&shape) && shape >= 0 && shape < 3){
                    footprint->shape = Proc::FootprintShape(shape); changed = true;
                }
                changed |= ui.slider("Width (X)",&footprint->width,1.0f,60.0f,"m");
                changed |= ui.slider("Depth (Z)",&footprint->depth,1.0f,60.0f,"m");
                if(footprint->shape != Proc::FootprintShape::Rectangle)
                    changed |= ui.slider("Wing width",&footprint->wingWidth,1.0f,30.0f,"m");
                changed |= ui.slider("Center X",&footprint->center.x,-50.0f,50.0f,"m");
                changed |= ui.slider("Center Z",&footprint->center.y,-50.0f,50.0f,"m");
                changed |= ui.slider("Rotation",&footprint->rotationDegrees,-180.0f,180.0f,"°");
            }else if(auto* traced = std::get_if<Proc::FootprintFromCurveNode>(&node.payload)){
                changed |= ui.checkbox("Snap to right angles",&traced->rectify);
                ui.hint("Right-angled outlines take pitched roofs and rooms; others a flat roof only.");
            }else if(auto* stack = std::get_if<Proc::FloorStackNode>(&node.payload)){
                float floors = float(stack->floors);
                if(ui.slider("Floors",&floors,1.0f,30.0f)){ stack->floors = uint32_t(std::lround(floors)); changed = true; }
                changed |= ui.slider("Floor height",&stack->floorHeight,2.0f,6.0f,"m");
                changed |= ui.slider("Ground floor elevation",&stack->elevation,0.0f,3.0f,"m");
            }else if(auto* walls = std::get_if<Proc::WallsNode>(&node.payload)){
                changed |= ui.slider("Thickness",&walls->thickness,0.05f,0.8f,"m");
                changed |= ui.checkbox("Windows",&walls->windows);
                if(walls->windows){
                    changed |= ui.slider("Window width",&walls->windowWidth,0.3f,4.0f,"m");
                    changed |= ui.slider("Window height",&walls->windowHeight,0.3f,4.0f,"m");
                    changed |= ui.slider("Sill height",&walls->sillHeight,0.0f,3.0f,"m");
                    changed |= ui.slider("Window spacing",&walls->windowSpacing,walls->windowWidth + 0.2f,12.0f,"m");
                }
                changed |= ui.checkbox("Door",&walls->door);
                if(walls->door){
                    float edge = float(walls->doorEdge);
                    if(ui.slider("Door on edge",&edge,0.0f,11.0f)){ walls->doorEdge = uint32_t(std::lround(edge)); changed = true; }
                    changed |= ui.slider("Door width",&walls->doorWidth,0.5f,3.0f,"m");
                    changed |= ui.slider("Door height",&walls->doorHeight,1.5f,4.0f,"m");
                }
            }else if(auto* slab = std::get_if<Proc::SlabNode>(&node.payload)){
                changed |= ui.slider("Thickness",&slab->thickness,0.02f,0.6f,"m");
                changed |= ui.slider("Inset",&slab->inset,0.0f,1.0f,"m");
                changed |= ui.checkbox("Ceiling under the roof",&slab->topCeiling);
                changed |= ui.checkbox("Foundation plinth",&slab->foundation);
            }else if(auto* roof = std::get_if<Proc::RoofNode>(&node.payload)){
                int type = int(roof->type);
                if(ui.choice("Roof type",{"Flat","Gable","Hip","Shed"},&type) && type >= 0 && type < 4){
                    roof->type = Proc::RoofType(type); changed = true;
                }
                if(roof->type != Proc::RoofType::Flat) changed |= ui.slider("Pitch",&roof->pitchDegrees,2.0f,70.0f,"°");
                changed |= ui.slider("Overhang",&roof->overhang,0.0f,2.0f,"m");
                if(roof->type == Proc::RoofType::Flat){
                    changed |= ui.slider("Slab thickness",&roof->thickness,0.05f,1.0f,"m");
                    changed |= ui.slider("Parapet height",&roof->parapetHeight,0.0f,2.0f,"m");
                }
            }else if(auto* stairs = std::get_if<Proc::StairsNode>(&node.payload)){
                changed |= ui.slider("Width",&stairs->width,0.5f,5.0f,"m");
                changed |= ui.slider("Total rise",&stairs->totalRise,0.2f,10.0f,"m");
                float steps = float(stairs->steps);
                if(ui.slider("Steps",&steps,2.0f,60.0f)){ stairs->steps = uint32_t(std::lround(steps)); changed = true; }
                changed |= ui.slider("Tread depth",&stairs->treadDepth,0.15f,1.0f,"m");
                changed |= ui.checkbox("Railing",&stairs->railing);
            }else if(auto* split = std::get_if<Proc::RoomSplitNode>(&node.payload)){
                int program = int(split->program);
                if(ui.choice("Program",{"Home","Office"},&program) && program >= 0 && program < 2){
                    split->program = Proc::InteriorProgram(program); changed = true;
                }
                float seed = float(std::min<uint64_t>(split->seed, 100000));
                if(ui.slider("Layout seed",&seed,1.0f,1000.0f)){ split->seed = uint64_t(std::lround(seed)); changed = true; }
                changed |= ui.slider("Corridor width",&split->corridorWidth,0.9f,3.0f,"m");
                changed |= ui.slider("Door width",&split->doorWidth,0.7f,1.5f,"m");
                float edge = float(split->entranceEdge);
                if(ui.slider("Front door on edge",&edge,0.0f,11.0f)){ split->entranceEdge = uint32_t(std::lround(edge)); changed = true; }
                ui.hint("Corridors, hall, stairs and rooms follow rules; walls put windows per room and the door here.");
            }else if(auto* interior = std::get_if<Proc::InteriorNode>(&node.payload)){
                changed |= ui.slider("Partition thickness",&interior->partitionThickness,0.05f,0.4f,"m");
                changed |= ui.checkbox("Door leaves",&interior->doorLeaves);
                changed |= ui.checkbox("Floor finish",&interior->floorFinish);
                changed |= ui.checkbox("Stairs",&interior->stairs);
            }else if(auto* road = std::get_if<Proc::RoadFromCurveNode>(&node.payload)){
                changed |= ui.slider("Road width",&road->roadWidth,1.0f,30.0f,"m");
                changed |= ui.checkbox("Sidewalks",&road->sidewalks);
                if(road->sidewalks){
                    changed |= ui.slider("Sidewalk width",&road->sidewalkWidth,0.5f,6.0f,"m");
                    changed |= ui.slider("Curb height",&road->curbHeight,0.0f,0.4f,"m");
                }
                changed |= ui.slider("Sample spacing",&road->sampleSpacing,0.1f,5.0f,"m");
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
