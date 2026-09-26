// Tool (LoomTool.h + Warp::Tool) i prsti oko drske (LoomHandPose.h conformedHandPoseAt).
//
// Drska: valjak polumjera 1.5 cm, dug 20 cm (prsteni svakih 1 cm). Saka iz test_hand_pose: prsti
// duz +x po 3 cm, dlan prema -y, os kaziprst -> mali prst je -z.
//   - projekt spremi i procita Tool s gripom
//   - prvi grip je na najduzoj osi blizu kraja; debljina drske iz vrhova ~1.5 cm
//   - poravnanje: os drske uz os sake, dlan na strani gripa, sredina drske ispod dlana
//   - prsti oko valjka ispod dlana: bez prodora, a vrhovi blizu povrsine (omotani); preset bez
//     collidera kroz drsku prolazi
#include "TestHarness.h"

#include "../src/LoomTool.h"

#include <Warp/Project.h>

#include <cmath>
#include <filesystem>

namespace{

Engine::Physics::TriangleMesh cylinder(float radius, float height, int rings, int segments){
    Engine::Physics::TriangleMesh mesh;
    for(int r = 0; r < rings; ++r)
        for(int s = 0; s < segments; ++s){
            const float a = float(s) / float(segments) * 6.2831853f;
            mesh.vertices.push_back({radius * std::cos(a), -0.5f * height + height * float(r) / float(rings - 1), radius * std::sin(a)});
        }
    for(int r = 0; r + 1 < rings; ++r)
        for(int s = 0; s < segments; ++s){
            const uint32_t a = uint32_t(r * segments + s), b = uint32_t(r * segments + (s + 1) % segments);
            const uint32_t c = a + uint32_t(segments), d = b + uint32_t(segments);
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    //Kape, da je valjak zatvoren (unutra se zna da je unutra)
    const uint32_t bottom = uint32_t(mesh.vertices.size());
    mesh.vertices.push_back({0.0f, -0.5f * height, 0.0f});
    mesh.vertices.push_back({0.0f, 0.5f * height, 0.0f});
    for(int s = 0; s < segments; ++s){
        const uint32_t a = uint32_t(s), b = uint32_t((s + 1) % segments);
        const uint32_t top = uint32_t((rings - 1) * segments);
        mesh.indices.insert(mesh.indices.end(), {bottom, a, b, bottom + 1, top + b, top + a});
    }
    return mesh;
}

struct Hand{ Warp::Stage stage; Warp::Id rig = Warp::None, hand = Warp::None; std::array<std::vector<Warp::Id>, 5> fingers; };

Hand makeHand(){
    Hand s;
    s.rig = s.stage.create("Rig");
    s.hand = s.stage.create("h", s.rig);
    s.stage.get(s.hand)->joint = Warp::Joint{};
    const glm::vec3 bases[5] = {{0.02f, 0.0f, 0.03f}, {0.08f, 0.0f, 0.03f}, {0.085f, 0.0f, 0.01f}, {0.08f, 0.0f, -0.01f}, {0.075f, 0.0f, -0.03f}};
    for(int f = 0; f < 5; ++f){
        Warp::Id parent = s.hand;
        for(int j = 0; j < 3; ++j){
            const Warp::Id id = s.stage.create("f" + std::to_string(f) + "_" + std::to_string(j), parent);
            s.stage.get(id)->joint = Warp::Joint{};
            s.stage.get(id)->local.translation = j == 0 ? bases[f] : glm::vec3(0.03f, 0.0f, 0.0f);
            if(f == 2 && j == 1) s.stage.get(id)->local.rotation = glm::angleAxis(glm::radians(-10.0f), glm::vec3(0, 0, 1));
            s.fingers[size_t(f)].push_back(id);
            parent = id;
        }
    }
    return s;
}

}

int main(){
    TestReport report("tool");

    //-- Warp::Tool u projektu -------------------------------------------------------------------------
    {
        Warp::Stage stage;
        const Warp::Id sword = stage.create("Sword");
        Warp::Tool tool;
        tool.kind = "weapon";
        Warp::Grip grip;
        grip.point = {0.0f, -0.4f, 0.0f};
        grip.axis = {0.0f, 1.0f, 0.0f};
        grip.palm = {0.0f, 0.0f, 1.0f};
        grip.thickness = 0.017f;
        grip.preset = "fist";
        grip.hand = 1;
        tool.grips.push_back(grip);
        stage.get(sword)->tool = tool;
        const std::string path = (std::filesystem::temp_directory_path() / "loom_test_tool.usda").string();
        std::string error;
        Warp::Stage loaded;
        const bool ok = Warp::saveProject(stage, path, error) && Warp::loadProject(path, loaded, error);
        const Warp::Entity* back = ok ? loaded.get(loaded.find("/Sword")) : nullptr;
        report.check("projekt spremi i procita Tool s gripom", back && back->tool && back->tool->kind == "weapon" &&
                     back->tool->grips.size() == 1 && back->tool->grips[0].preset == "fist" && back->tool->grips[0].hand == 1 &&
                     std::fabs(back->tool->grips[0].point.y + 0.4f) < 1e-6f && loaded.fingerprint() == stage.fingerprint(), error);
        std::filesystem::remove(path);
    }

    //-- prvi grip i debljina drske --------------------------------------------------------------------
    Loom::ToolGeometry handle;
    handle.mesh = cylinder(0.015f, 0.20f, 21, 16);
    handle.low = {-0.015f, -0.1f, -0.015f};
    handle.high = {0.015f, 0.1f, 0.015f};
    const Warp::Grip first = Loom::defaultGrip(handle, "grip");
    report.check("valjak (bez stitnika): grip na najduzoj osi (y)", std::fabs(std::fabs(first.axis.y) - 1.0f) < 1e-6f,
                 fmt("y %.3f", first.point.y));
    //Mac: drska y 0-0.2 (polumjer 1.5 cm), stitnik y 0.2-0.23 (sirok 10 cm), ostrica do 1.0 m. Drska je
    //kraca strana od stitnika: grip na y 0.1, os prema ostrici
    {
        Loom::ToolGeometry sword;
        auto addBox = [&](glm::vec3 low, glm::vec3 high){
            Engine::Physics::TriangleMesh box = cylinder(1.0f, 1.0f, 2, 4);
            const uint32_t base = uint32_t(sword.mesh.vertices.size());
            for(glm::vec3 v : box.vertices){
                const glm::vec3 t((v.x + 1.0f) * 0.5f, v.y + 0.5f, (v.z + 1.0f) * 0.5f);
                sword.mesh.vertices.push_back(low + (high - low) * t);
            }
            for(uint32_t i : box.indices) sword.mesh.indices.push_back(base + i);
        };
        addBox({-0.015f, 0.0f, -0.015f}, {0.015f, 0.2f, 0.015f});
        addBox({-0.05f, 0.2f, -0.012f}, {0.05f, 0.23f, 0.012f});
        addBox({-0.025f, 0.23f, -0.003f}, {0.025f, 1.0f, 0.003f});
        sword.low = {-0.05f, 0.0f, -0.015f};
        sword.high = {0.05f, 1.0f, 0.015f};
        const Warp::Grip swordGrip = Loom::defaultGrip(sword, "grip");
        report.check("mac bez mjerila: grip na sredini drske (ispod stitnika), os prema ostrici",
                     std::fabs(swordGrip.point.y - 0.1f) < 0.01f && glm::dot(swordGrip.axis, glm::vec3(0, 1, 0)) > 0.999f,
                     fmt("y %.3f, os %.0f", swordGrip.point.y, swordGrip.axis.y));
        //S mjerilom saka sjedne uz stitnik kao prava: sredina dlana 7 cm ispod ruba stitnika (y 0.2)
        const Warp::Grip nearGuard = Loom::defaultGrip(sword, "grip", 1.0f);
        report.check("mac u metrima: dlan 7 cm ispod stitnika", std::fabs(nearGuard.point.y - 0.13f) < 0.005f,
                     fmt("y %.3f (ocekivano 0.130)", nearGuard.point.y));
    }
    Warp::Grip middle = first;
    middle.point = glm::vec3(0.0f);
    middle.axis = glm::vec3(0.0f, 1.0f, 0.0f);
    const float radius = Loom::handleRadius(handle, middle);
    report.check("debljina drske iz vrhova ~1.5 cm", std::fabs(radius - 0.015f) < 1e-4f, fmt("%.4f m", radius));

    //-- poravnanje drske u dlanu -----------------------------------------------------------------------
    Loom::HandFrame frame;
    frame.palm = {0.05f, 0.0f, 0.0f};
    frame.normal = {0.0f, -1.0f, 0.0f};
    frame.across = {0.0f, 0.0f, 1.0f};
    frame.palmDepth = 0.015f;
    Warp::Grip grip = middle;
    grip.palm = {1.0f, 0.0f, 0.0f};
    const glm::mat4 aligned = Loom::gripAlignedWorld(glm::mat4(1.0f), grip, radius, frame);
    const glm::vec3 axisWorld = glm::vec3(aligned * glm::vec4(0, 1, 0, 0));
    const glm::vec3 palmWorld = glm::vec3(aligned * glm::vec4(1, 0, 0, 0));
    const glm::vec3 centre = glm::vec3(aligned * glm::vec4(0, 0, 0, 1));
    report.check("poravnanje: os drske uz saku, dlan na strani gripa, sredina 3 cm ispod dlana",
                 glm::length(axisWorld - frame.across) < 1e-5f && glm::length(palmWorld - glm::vec3(0, 1, 0)) < 1e-5f &&
                 glm::length(centre - glm::vec3(0.05f, -0.03f, 0.0f)) < 1e-5f,
                 fmt("sredina %.3f %.3f %.3f", centre.x, centre.y, centre.z));

    //-- prsti oko drske ------------------------------------------------------------------------------
    Hand hand = makeHand();
    const Loom::HandFingers fingers = Loom::handFingersOf(hand.stage, hand.hand, 1.0);
    //Drska poprijeko ispod dlana uz zglobove prstiju (os z), 2.8 cm ispod linije kostiju
    glm::mat4 toolWorld = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
    toolWorld[3] = glm::vec4(0.065f, -0.028f, 0.0f, 1.0f);
    const Engine::Physics::Collider collider = Engine::Physics::Collider::fromMesh(handle.mesh);
    auto posedStage = [&](const Loom::JointPose& pose){
        Warp::Stage copy = hand.stage;
        for(const auto& [id, local] : pose) copy.get(id)->local = local;
        return copy;
    };
    const glm::mat4 toTool = glm::inverse(toolWorld);
    auto penetrates = [&](const Warp::Stage& stage){
        for(int f = 1; f < 5; ++f)
            for(size_t j = 0; j + 1 < hand.fingers[size_t(f)].size(); ++j){
                const glm::vec3 a(toTool * stage.worldMatrix(hand.fingers[size_t(f)][j], 1.0)[3]);
                const glm::vec3 b(toTool * stage.worldMatrix(hand.fingers[size_t(f)][j + 1], 1.0)[3]);
                if(collider.capsuleHits(a, b, 0.0045f)) return true;
            }
        return false;
    };
    const Warp::Stage conformed = posedStage(Loom::conformedHandPoseAt(hand.stage, fingers, Loom::gripPreset("fist"), 1.0, collider, toolWorld));
    const Warp::Stage preset = posedStage(Loom::handPoseAt(hand.stage, fingers, Loom::gripPreset("fist"), 1.0));
    float worstGap = 0.0f;
    for(int f = 1; f < 5; ++f){
        const glm::vec3 tip(toTool * conformed.worldMatrix(hand.fingers[size_t(f)].back(), 1.0)[3]);
        worstGap = std::max(worstGap, collider.distance(tip, 0.2f));
    }
    report.check("preset bez collidera prolazi kroz drsku", penetrates(preset), "");
    report.check("prsti oko drske bez prodora", !penetrates(conformed), "");
    report.check("zadnji zglobovi prstiju blizu drske (omotani)", worstGap < 0.02f, fmt("najdalji %.4f m", worstGap));
    return report.result();
}
