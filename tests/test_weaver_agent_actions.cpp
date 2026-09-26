#include "TestHarness.h"

#include "../src/LoomAgentActions.h"
#include "../src/LoomAgentJson.h"

#include <cmath>
#include <limits>

int main(){
    TestReport report("E1 Weaver agent actions");
    Warp::Stage stage;
    Loom::AgentControlState state;

    Loom::AgentAction create;
    create.tool = "scene.create_primitive";
    create.primitive = "cube";
    create.name = "Hero";
    create.translation = glm::vec3(1.0f, 2.0f, 3.0f);
    const Loom::AgentActionResult created = Loom::executeAgentAction(stage, state, create);
    const Warp::Entity* hero = stage.get(state.selected);
    report.check("create primitive and select it", created.succeeded && hero && hero->mesh &&
                 hero->mesh->shape == Warp::Shape::Cube && hero->local.translation == glm::vec3(1, 2, 3), created.message);

    Loom::AgentAction transform;
    transform.tool = "scene.set_transform";
    transform.target = "selected";
    transform.translation = glm::vec3(-2.0f, 0.5f, 4.0f);
    const Loom::AgentActionResult moved = Loom::executeAgentAction(stage, state, transform);
    report.check("set transform targets the current selection", moved.succeeded &&
                 stage.localAt(state.selected, state.frame).translation == glm::vec3(-2, 0.5f, 4), moved.message);

    Loom::AgentAction list;
    list.tool = "scene.list_entities";
    const Loom::AgentActionResult listed = Loom::executeAgentAction(stage, state, list);
    report.check("list entities returns exact stable scene paths", listed.succeeded && listed.entities.size() == 1 &&
                 listed.entities[0].path == "/Hero" && listed.entities[0].type == "cube", listed.message);

    Loom::AgentAction frame;
    frame.tool = "viewport.frame_entity";
    frame.target = "/Hero";
    const Loom::AgentActionResult framed = Loom::executeAgentAction(stage, state, frame);
    report.check("frame entity changes viewport orbit target", framed.succeeded &&
                 state.orbit.target == glm::vec3(-2, 0.5f, 4) && state.orbit.distance > 0.5f, framed.message);

    Warp::Id child = stage.create("Child", state.selected);
    Loom::AgentAction remove;
    remove.tool = "scene.delete";
    remove.target = "/Hero";
    const Loom::AgentActionResult needsConfirmation = Loom::executeAgentAction(stage, state, remove);
    report.check("delete requires confirmation and preserves scene", needsConfirmation.confirmationRequired &&
                 !needsConfirmation.succeeded && stage.contains(state.selected) && stage.contains(child), needsConfirmation.message);
    const Loom::AgentActionResult deleted = Loom::executeAgentAction(stage, state, remove, true);
    report.check("confirmed delete removes the full subtree", deleted.succeeded && !stage.contains(state.selected) &&
                 !stage.contains(child) && state.selected == Warp::None, deleted.message);

    Loom::AgentAction seek;
    seek.tool = "timeline.set_playhead";
    seek.frame = 150.0;
    const Loom::AgentActionResult seekResult = Loom::executeAgentAction(stage, state, seek);
    report.check("timeline frame is clamped to project range", seekResult.succeeded && state.frame == stage.endFrame,
                 seekResult.message);

    Loom::AgentAction unsafeCreate;
    unsafeCreate.tool = "scene.create_primitive";
    unsafeCreate.primitive = "cube";
    unsafeCreate.name = "Unsafe";
    unsafeCreate.translation = glm::vec3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f);
    const size_t beforeUnsafe = stage.size();
    const Loom::AgentActionResult unsafeResult = Loom::executeAgentAction(stage, state, unsafeCreate);
    report.check("non-finite create transform is rejected before mutation", !unsafeResult.succeeded &&
                 stage.size() == beforeUnsafe, unsafeResult.message);

    Loom::AgentAction outOfRangeFrame;
    outOfRangeFrame.tool = "timeline.set_playhead";
    outOfRangeFrame.frame = 10'000'001.0;
    const Loom::AgentActionResult badFrame = Loom::executeAgentAction(stage, state, outOfRangeFrame);
    report.check("out-of-range frame is rejected", !badFrame.succeeded && state.frame == stage.endFrame,
                 badFrame.message);

    const std::string apiJson = "{\"protocol\":1,\"result\":{\"version\":1,\"reply\":\"Kreiram Probe.\",\"actions\":[{\"tool\":\"scene.create_primitive\",\"arguments\":{\"primitive\":\"cube\",\"name\":\"Probe\",\"translation\":[1,2,3]},\"confirmation_required\":false}]}}";
    const Loom::AgentApiResponse parsed = Loom::agentResponseFromJson(apiJson);
    report.check("local API JSON maps into a typed native action", parsed.error.empty() && parsed.reply == "Kreiram Probe." &&
                 parsed.actions.size() == 1 && parsed.actions[0].action.tool == "scene.create_primitive" &&
                 parsed.actions[0].action.translation == glm::vec3(1, 2, 3) &&
                 !parsed.actions[0].confirmationRequired, parsed.reply);
    const Loom::AgentApiResponse parsedRename = Loom::agentResponseFromJson(
        "{\"protocol\":1,\"result\":{\"version\":1,\"reply\":\"Renaming.\",\"actions\":[{\"tool\":\"scene.rename\",\"arguments\":{\"target\":\"/Scene/Old\",\"name\":\"New\"},\"confirmation_required\":false}]}}");
    report.check("local API JSON maps the rename tool and exact target", parsedRename.error.empty() &&
                 parsedRename.actions.size() == 1 && parsedRename.actions[0].action.tool == "scene.rename" &&
                 parsedRename.actions[0].action.target == "/Scene/Old" && parsedRename.actions[0].action.name == "New",
                 parsedRename.reply);
    const Loom::AgentApiResponse parsedVisibility = Loom::agentResponseFromJson(
        "{\"protocol\":1,\"result\":{\"version\":1,\"reply\":\"Hiding.\",\"actions\":[{\"tool\":\"scene.set_visibility\",\"arguments\":{\"target\":\"/Scene/Old\",\"visible\":false},\"confirmation_required\":false}]}}");
    report.check("local API JSON maps the visibility tool and boolean state", parsedVisibility.error.empty() &&
                 parsedVisibility.actions.size() == 1 && parsedVisibility.actions[0].action.tool == "scene.set_visibility" &&
                 parsedVisibility.actions[0].action.visible && !*parsedVisibility.actions[0].action.visible,
                 parsedVisibility.reply);
    const Loom::AgentApiResponse parsedDelete = Loom::agentResponseFromJson(
        "{\"protocol\":1,\"result\":{\"version\":1,\"reply\":\"Confirm deletion.\",\"actions\":[{\"tool\":\"scene.delete\",\"arguments\":{\"target\":\"/Scene/Old\"},\"confirmation_required\":true}]}}");
    report.check("local API JSON preserves the delete confirmation requirement", parsedDelete.error.empty() &&
                 parsedDelete.actions.size() == 1 && parsedDelete.actions[0].confirmationRequired &&
                 parsedDelete.actions[0].action.tool == "scene.delete", parsedDelete.reply);
    const std::string gridActionJson = "{\"protocol\":1,\"result\":{\"version\":1,\"reply\":\"Izrađujem grid.\",\"actions\":[{\"tool\":\"procedura.create_recipe\",\"arguments\":{\"generator\":\"grid_surface\",\"name\":\"Test Grid\",\"width\":6,\"depth\":4,\"cells_x\":2,\"cells_z\":2,\"point_heights\":[{\"column\":1,\"row\":1,\"height\":2.5}]},\"confirmation_required\":false}]}}";
    const Loom::AgentApiResponse parsedGrid = Loom::agentResponseFromJson(gridActionJson);
    report.check("procedural tool JSON maps to a typed grid recipe request",
                 parsedGrid.error.empty() && parsedGrid.actions.size() == 1 &&
                 parsedGrid.actions[0].action.proceduraRecipe &&
                 parsedGrid.actions[0].action.proceduraRecipe->generator == "grid_surface" &&
                 parsedGrid.actions[0].action.proceduraRecipe->grid.cellsX == 2 &&
                 parsedGrid.actions[0].action.proceduraRecipe->pointHeights.size() == 1 &&
                 parsedGrid.actions[0].action.proceduraRecipe->pointHeights[0].height == 2.5f,
                 parsedGrid.reply);
    const std::string escaped = Loom::agentJsonEscape("Croatian č\nquote \" slash");
    report.check("chat text JSON escaping is reversible", Loom::AgentJsonParser(escaped).parse().string == "Croatian č\nquote \" slash", escaped);

    Loom::AgentAction unnamed;
    unnamed.tool = "scene.create_primitive";
    unnamed.primitive = "cube";
    unnamed.color = glm::vec3(0.10f, 0.78f, 0.18f);
    const Loom::AgentActionResult firstCube = Loom::executeAgentAction(stage, state, unnamed);
    report.check("unnamed colored cube gets a default name and viewport color", firstCube.succeeded &&
                 stage.get(state.selected)->name == "Cube" &&
                 stage.get(state.selected)->mesh->colour == *unnamed.color, firstCube.message);
    const Loom::AgentActionResult secondCube = Loom::executeAgentAction(stage, state, unnamed);
    report.check("default cube names remain unique", secondCube.succeeded &&
                 stage.get(state.selected)->name == "Cube1", secondCube.message);

    Loom::AgentAction gridRecipe;
    gridRecipe.tool = "procedura.create_recipe";
    gridRecipe.name = "Test Grid";
    Loom::AgentProceduraRecipeRequest gridRequest;
    gridRequest.generator = "grid_surface";
    gridRequest.grid.width = 6.0f;
    gridRequest.grid.depth = 4.0f;
    gridRequest.grid.cellsX = 2;
    gridRequest.grid.cellsZ = 2;
    gridRequest.pointHeights.push_back({1, 1, 2.5f});
    gridRecipe.proceduraRecipe = gridRequest;
    const size_t stageSizeBeforeRecipe = stage.size();
    const Warp::Id selectionBeforeRecipe = state.selected;
    const Loom::AgentActionResult madeGrid = Loom::executeAgentAction(stage, state, gridRecipe);
    report.check("grid tool creates and evaluates an editable recipe without mutating scene entities",
                 madeGrid.succeeded && madeGrid.proceduraGraph && madeGrid.proceduraPreview &&
                 madeGrid.proceduraRecipeName == "Test Grid" && madeGrid.proceduraGraph->nodes.size() == 3 &&
                 madeGrid.proceduraPreview->vertices.size() == 9 &&
                 madeGrid.proceduraPreview->vertices[4].position.y == 2.5f &&
                 stage.size() == stageSizeBeforeRecipe && state.selected == selectionBeforeRecipe,
                 madeGrid.message);

    Loom::AgentAction interiorRecipe;
    interiorRecipe.tool = "procedura.create_recipe";
    Loom::AgentProceduraRecipeRequest interiorRequest;
    interiorRequest.generator = "interior_blockout";
    interiorRequest.interior.roomsPerSide = 3;
    interiorRecipe.proceduraRecipe = interiorRequest;
    const Loom::AgentActionResult madeInterior = Loom::executeAgentAction(stage, state, interiorRecipe);
    report.check("hallway tool creates a one-node interior Recipe with evaluated geometry",
                 madeInterior.succeeded && madeInterior.proceduraGraph && madeInterior.proceduraPreview &&
                 madeInterior.proceduraGraph->nodes.size() == 1 &&
                 madeInterior.proceduraPreview->vertices.size() > 24,
                 madeInterior.message);

    gridRecipe.proceduraRecipe->pointHeights[0].column = 9;
    const Loom::AgentActionResult invalidGrid = Loom::executeAgentAction(stage, state, gridRecipe);
    report.check("grid recipe rejects an out-of-range point without returning a preview",
                 !invalidGrid.succeeded && !invalidGrid.proceduraGraph && !invalidGrid.proceduraPreview &&
                 stage.size() == stageSizeBeforeRecipe, invalidGrid.message);

    bool unknownProceduraFieldRejected = false;
    try{
        Loom::agentResponseFromJson("{\"protocol\":1,\"result\":{\"version\":1,\"reply\":\"Create.\",\"actions\":[{\"tool\":\"procedura.create_recipe\",\"arguments\":{\"generator\":\"grid_surface\",\"rooms_per_side\":3}}]}}");
    }catch(const std::runtime_error&){ unknownProceduraFieldRejected = true; }
    report.check("native procedural tool parser rejects generator-specific unknown fields",
                 unknownProceduraFieldRejected, "rooms_per_side cannot be used with grid_surface");
    const Loom::AgentJsonValue context = Loom::AgentJsonParser(
        Loom::agentSceneContextJson(stage, state.selected, state.frame)).parse();
    const Loom::AgentJsonValue* contextEntities = context.get("entities");
    const Loom::AgentJsonValue* contextSchema = context.get("schema");
    const Loom::AgentJsonValue* contextVersion = context.get("version");
    const Loom::AgentJsonValue* localPosition = contextEntities && !contextEntities->array.empty()
        ? contextEntities->array.front().get("local_position") : nullptr;
    const Loom::AgentJsonValue* localRotation = contextEntities && !contextEntities->array.empty()
        ? contextEntities->array.front().get("local_rotation_xyzw") : nullptr;
    const Loom::AgentJsonValue* localScale = contextEntities && !contextEntities->array.empty()
        ? contextEntities->array.front().get("local_scale") : nullptr;
    report.check("live scene snapshot includes the current selected path and entity types",
                 context.get("selected_path") && context.get("selected_path")->string == "/Cube1" &&
                 contextEntities && contextEntities->array.size() == 2 &&
                 contextEntities->array.front().get("type") &&
                 contextEntities->array.front().get("type")->string == "cube", "selected-first live snapshot");
    report.check("scene snapshot versions and describes the selected local transform",
                 contextSchema && contextSchema->string == "loom.scene-context" &&
                 contextVersion && contextVersion->number == 1.0 &&
                 localPosition && localPosition->array.size() == 3 && localPosition->array[0].number == 0.0 &&
                 localRotation && localRotation->array.size() == 4 && localRotation->array[3].number == 1.0 &&
                 localScale && localScale->array.size() == 3 && localScale->array[0].number == 1.0,
                 "semantic transforms use local position, XYZW quaternion, and local scale");

    Loom::AgentAction unknown;
    unknown.tool = "system.shell";
    const Loom::AgentActionResult rejected = Loom::executeAgentAction(stage, state, unknown);
    report.check("unknown action is rejected", !rejected.succeeded && rejected.entities.empty(), rejected.message);

    Loom::AgentAction hide;
    hide.tool = "scene.set_visibility";
    hide.target = "/Cube1";
    hide.visible = false;
    const Loom::AgentActionResult hidden = Loom::executeAgentAction(stage, state, hide);
    report.check("visibility action hides the exact entity", hidden.succeeded && !stage.get(state.selected)->visible,
                 hidden.message);
    hide.visible = true;
    const Loom::AgentActionResult shown = Loom::executeAgentAction(stage, state, hide);
    report.check("visibility action restores the exact entity", shown.succeeded && stage.get(state.selected)->visible,
                 shown.message);

    Loom::AgentAction rename;
    rename.tool = "scene.rename";
    rename.target = "/Cube1";
    rename.name = "RenamedCube";
    const Loom::AgentActionResult renamed = Loom::executeAgentAction(stage, state, rename);
    report.check("rename action changes the entity name", renamed.succeeded &&
                 stage.get(state.selected)->name == "RenamedCube", renamed.message);

    Loom::AgentAction rotateScale;
    rotateScale.tool = "scene.set_transform";
    rotateScale.target = "selected";
    rotateScale.rotationDegrees = glm::vec3(0.0f, 90.0f, 0.0f);
    rotateScale.scale = glm::vec3(2.0f, 2.0f, 2.0f);
    const Loom::AgentActionResult rotatedAndScaled = Loom::executeAgentAction(stage, state, rotateScale);
    const Warp::Transform rotatedTransform = stage.localAt(state.selected, state.frame);
    report.check("transform action applies rotation and scale while preserving position", rotatedAndScaled.succeeded &&
                 rotatedTransform.translation == glm::vec3(0) && rotatedTransform.scale == glm::vec3(2) &&
                 std::abs(glm::angle(rotatedTransform.rotation) - glm::radians(90.0f)) < 0.001f,
                 rotatedAndScaled.message);

    Loom::AgentAction plane;
    plane.tool = "scene.create_primitive";
    plane.primitive = "plane";
    plane.name = "ChildPlane";
    plane.parent = "/RenamedCube";
    const Loom::AgentActionResult planeCreated = Loom::executeAgentAction(stage, state, plane);
    const Warp::Entity* childPlane = stage.get(state.selected);
    report.check("plane creation supports an exact parent path", planeCreated.succeeded && childPlane &&
                 childPlane->mesh && childPlane->mesh->shape == Warp::Shape::Plane &&
                 childPlane->parent == stage.find("/RenamedCube"), planeCreated.message);
    return report.result();
}
