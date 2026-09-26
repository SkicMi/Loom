#pragma once
//=============================================================================================
// LOOM AGENT HOST API
//
// Model output is untrusted. The HTTP side validates JSON against data/tools.json; this native
// boundary independently validates values and exposes only typed Loom scene operations. A host
// should map the validated JSON action into AgentAction and execute it on the editor thread.
//=============================================================================================
#include "LoomViewport.h"
#include <Engine/WeaverProcedura.h>
#include <Warp/Stage.h>

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Loom{

namespace Proc = Engine::WeaverProcedura;

struct AgentGridHeightEdit{
    uint32_t column = 0;
    uint32_t row = 0;
    float height = 0.0f;
};

struct AgentProceduraRecipeRequest{
    std::string generator;
    Proc::GridNode grid;
    std::vector<AgentGridHeightEdit> pointHeights;
    Proc::InteriorBlockoutNode interior;
};

struct AgentAction{
    std::string tool;
    std::string target;
    std::string name;
    std::string primitive;
    std::optional<glm::vec3> color;
    std::optional<std::string> parent;
    std::optional<glm::vec3> translation;
    std::optional<glm::vec3> rotationDegrees;
    std::optional<glm::vec3> scale;
    std::optional<bool> visible;
    std::optional<double> frame;
    std::optional<AgentProceduraRecipeRequest> proceduraRecipe;
};

struct AgentEntityInfo{
    std::string path;
    std::string name;
    std::string type;
    std::string parentPath;
    bool visible = true;
};

struct AgentControlState{
    Warp::Id selected = Warp::None;
    double frame = 1.0;
    Orbit orbit;
    Warp::Id lookThrough = Warp::None;
};

struct AgentActionResult{
    bool succeeded = false;
    bool confirmationRequired = false;
    std::string message;
    std::vector<AgentEntityInfo> entities;
    std::string proceduraRecipeName;
    std::optional<Proc::Graph> proceduraGraph;
    std::optional<Proc::MeshData> proceduraPreview;
};

inline bool finiteVector(const glm::vec3& value){
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool safeAgentVector(const glm::vec3& value, bool positive = false){
    if(!finiteVector(value)) return false;
    for(int axis = 0; axis < 3; ++axis){
        if(std::abs(value[axis]) > 1e6f || (positive && value[axis] <= 0.0f)) return false;
    }
    return true;
}

inline Warp::Id resolveAgentTarget(const Warp::Stage& stage, const AgentControlState& state,
                                   const std::string& target){
    if(target == "selected") return stage.contains(state.selected) ? state.selected : Warp::None;
    return stage.find(target);
}

inline bool agentDescendsFrom(const Warp::Stage& stage, Warp::Id candidate, Warp::Id ancestor){
    for(const Warp::Entity* entity = stage.get(candidate); entity; entity = stage.get(entity->parent)){
        if(entity->id == ancestor) return true;
    }
    return false;
}

inline std::string agentEntityType(const Warp::Entity& entity){
    if(entity.mesh) return Warp::shapeToken(entity.mesh->shape);
    if(entity.camera) return "camera";
    if(entity.points) return "points";
    if(entity.splat) return "splat";
    if(entity.model) return "model";
    if(entity.joint) return "joint";
    if(entity.animator) return "animator";
    return "group";
}

inline AgentActionResult executeAgentAction(Warp::Stage& stage, AgentControlState& state,
                                           const AgentAction& action, bool confirmed = false){
    AgentActionResult result;
    if(action.tool == "scene.list_entities"){
        Warp::Id parent = Warp::None;
        if(action.parent){
            parent = stage.find(*action.parent);
            if(parent == Warp::None){ result.message = "Parent path does not exist."; return result; }
        }
        stage.walk([&](const Warp::Entity& entity, int){
            if(parent != Warp::None && !agentDescendsFrom(stage, entity.id, parent)) return;
            const Warp::Entity* parentEntity = stage.get(entity.parent);
            result.entities.push_back({stage.path(entity.id), entity.name, agentEntityType(entity),
                                       parentEntity ? stage.path(parentEntity->id) : std::string(), entity.visible});
        });
        result.succeeded = true;
        result.message = "Listed " + std::to_string(result.entities.size()) + " scene entities.";
        return result;
    }

    if(action.tool == "scene.create_primitive"){
        const bool cube = action.primitive == "cube";
        if(!cube && action.primitive != "plane"){ result.message = "Only cube and plane primitives are supported."; return result; }
        const std::string name = action.name.empty() ? (cube ? "Cube" : "Plane") : action.name;
        if(name.size() > 80){ result.message = "Entity name must contain at most 80 characters."; return result; }
        Warp::Id parent = Warp::None;
        if(action.parent){
            parent = stage.find(*action.parent);
            if(parent == Warp::None){ result.message = "Parent path does not exist."; return result; }
        }
        if(action.translation && !safeAgentVector(*action.translation)){ result.message = "Translation exceeds the safe finite range."; return result; }
        if(action.color && (!safeAgentVector(*action.color) || action.color->x < 0.0f || action.color->y < 0.0f ||
                            action.color->z < 0.0f || action.color->x > 1.0f || action.color->y > 1.0f ||
                            action.color->z > 1.0f)){
            result.message = "Color channels must be finite values from 0 to 1."; return result;
        }
        const Warp::Id id = stage.create(name, parent);
        Warp::Entity* entity = stage.get(id);
        entity->mesh = Warp::Mesh{cube ? Warp::Shape::Cube : Warp::Shape::Plane};
        if(action.translation) entity->local.translation = *action.translation;
        if(action.color) entity->mesh->colour = *action.color;
        state.selected = id;
        result.succeeded = true;
        result.message = "Created " + stage.path(id) + ".";
        return result;
    }

    if(action.tool == "scene.set_transform"){
        const Warp::Id id = resolveAgentTarget(stage, state, action.target);
        if(id == Warp::None){ result.message = "Target entity does not exist."; return result; }
        if(!action.translation && !action.rotationDegrees && !action.scale){
            result.message = "At least one transform channel is required."; return result;
        }
        if((action.translation && !safeAgentVector(*action.translation)) ||
           (action.rotationDegrees && !safeAgentVector(*action.rotationDegrees)) ||
           (action.scale && !safeAgentVector(*action.scale, true))){
            result.message = "Transform values must be finite and within range; scale must be positive."; return result;
        }
        Warp::Transform transform = stage.localAt(id, state.frame);
        if(action.translation) transform.translation = *action.translation;
        if(action.rotationDegrees){
            const glm::vec3 radians = glm::radians(*action.rotationDegrees);
            const glm::quat yaw = glm::angleAxis(radians.y, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::quat pitch = glm::angleAxis(radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::quat roll = glm::angleAxis(radians.z, glm::vec3(0.0f, 0.0f, 1.0f));
            transform.rotation = glm::normalize(yaw * pitch * roll);
        }
        if(action.scale) transform.scale = *action.scale;
        stage.setLocalAt(id, state.frame, transform);
        result.succeeded = true;
        result.message = "Updated transform for " + stage.path(id) + ".";
        return result;
    }

    if(action.tool == "scene.rename"){
        const Warp::Id id = resolveAgentTarget(stage, state, action.target);
        if(id == Warp::None){ result.message = "Target entity does not exist."; return result; }
        if(action.name.empty() || action.name.size() > 80){ result.message = "Entity name must contain 1 to 80 characters."; return result; }
        if(!stage.rename(id, action.name)){ result.message = "Rename failed."; return result; }
        result.succeeded = true;
        result.message = "Renamed entity to " + stage.path(id) + ".";
        return result;
    }

    if(action.tool == "scene.set_visibility"){
        const Warp::Id id = resolveAgentTarget(stage, state, action.target);
        if(id == Warp::None){ result.message = "Target entity does not exist."; return result; }
        if(!action.visible){ result.message = "visible must be supplied."; return result; }
        stage.get(id)->visible = *action.visible;
        result.succeeded = true;
        result.message = std::string(*action.visible ? "Shown " : "Hidden ") + stage.path(id) + ".";
        return result;
    }

    if(action.tool == "scene.delete"){
        const Warp::Id id = resolveAgentTarget(stage, state, action.target);
        if(id == Warp::None){ result.message = "Target entity does not exist."; return result; }
        if(!confirmed){
            result.confirmationRequired = true;
            result.message = "Deleting " + stage.path(id) + " also removes its descendants; confirm to continue.";
            return result;
        }
        const bool selectionRemoved = agentDescendsFrom(stage, state.selected, id);
        const std::string path = stage.path(id);
        const size_t removed = stage.remove(id);
        if(selectionRemoved) state.selected = Warp::None;
        result.succeeded = removed > 0;
        result.message = "Deleted " + path + " and " + std::to_string(removed - (removed > 0 ? 1 : 0)) + " descendants.";
        return result;
    }

    if(action.tool == "timeline.set_playhead"){
        if(!action.frame || !std::isfinite(*action.frame) || *action.frame < 0.0 || *action.frame > 10'000'000.0){
            result.message = "Frame must be finite and within the supported range."; return result;
        }
        state.frame = std::clamp(*action.frame, stage.startFrame, stage.endFrame);
        result.succeeded = true;
        result.message = "Moved playhead to frame " + std::to_string(state.frame) + ".";
        return result;
    }

    if(action.tool == "viewport.frame_entity"){
        const Warp::Id id = resolveAgentTarget(stage, state, action.target);
        if(id == Warp::None){ result.message = "Target entity does not exist."; return result; }
        const Warp::Entity* entity = stage.get(id);
        const glm::mat4 world = stage.worldMatrix(id, state.frame);
        state.orbit.target = glm::vec3(world[3]);
        float radius = 0.5f;
        if(entity->mesh){
            const Warp::Transform local = stage.localAt(id, state.frame);
            radius = 0.8660254f * std::max({std::abs(local.scale.x), std::abs(local.scale.y), std::abs(local.scale.z)});
        }
        state.orbit.distance = std::clamp(radius * 3.2f, 0.5f, 100000.0f);
        state.lookThrough = Warp::None;
        result.succeeded = true;
        result.message = "Framed " + stage.path(id) + ".";
        return result;
    }

    if(action.tool == "procedura.create_recipe"){
        if(!action.proceduraRecipe){ result.message = "A valid Procedura generator is required."; return result; }
        const AgentProceduraRecipeRequest& request = *action.proceduraRecipe;
        Proc::Graph graph;
        if(request.generator == "grid_surface"){
            const Proc::NodeId grid = Proc::addNode(graph, request.grid, 24.0f, 88.0f);
            if(grid == 0){ result.message = "Could not create the grid source node."; return result; }
            Proc::NodeId previous = grid;
            for(std::size_t i = 0; i < request.pointHeights.size(); ++i){
                const AgentGridHeightEdit& edit = request.pointHeights[i];
                Proc::SetGridPointHeightNode height;
                height.column = edit.column;
                height.row = edit.row;
                height.height = edit.height;
                const Proc::NodeId id = Proc::addNode(graph, height, 260.0f + float(i) * 180.0f, 88.0f);
                if(id == 0){ result.message = "Could not create a point-height node."; return result; }
                graph.links.push_back({previous, 0, id, 0});
                previous = id;
            }
            const Proc::NodeId output = Proc::addNode(graph, Proc::GridToMeshNode{},
                                                       280.0f + float(request.pointHeights.size()) * 180.0f, 88.0f);
            if(output == 0){ result.message = "Could not create the grid-to-mesh output node."; return result; }
            graph.links.push_back({previous, 0, output, 0});
        }else if(request.generator == "interior_blockout"){
            if(Proc::addNode(graph, request.interior, 24.0f, 88.0f) == 0){
                result.message = "Could not create the interior blockout node.";
                return result;
            }
        }else{
            result.message = "Procedura generator is not supported by this action.";
            return result;
        }

        const std::string recipeName = action.name.empty()
            ? (request.generator == "grid_surface" ? "Grid Surface" : "Hallway and Rooms Blockout")
            : action.name;
        if(recipeName.size() > 120){ result.message = "Recipe name must contain at most 120 characters."; return result; }
        const Proc::ValidationResult validation = Proc::validate(graph);
        if(!validation){ result.message = "Recipe validation failed: " + validation.error; return result; }
        Proc::EvaluationResult evaluated = Proc::evaluate(graph);
        if(!evaluated.succeeded){ result.message = "Recipe preview failed: " + evaluated.error; return result; }

        result.succeeded = true;
        result.proceduraRecipeName = recipeName;
        result.proceduraGraph = std::move(graph);
        result.proceduraPreview = std::move(evaluated.mesh);
        result.message = "Created Procedura Recipe '" + recipeName + "' with " +
            std::to_string(result.proceduraGraph->nodes.size()) + " nodes and previewed " +
            std::to_string(result.proceduraPreview->vertices.size()) + " vertices.";
        return result;
    }

    result.message = "Unknown Loom agent action: " + action.tool;
    return result;
}

}
