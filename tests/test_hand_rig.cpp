// Hand rig na pravom liku (tools/autorig/hand_rig.py + LoomHandPose.h + LoomTool.h).
//
// Lik iz desnog klika u Manny profilu (tools/autorig/outputs/mascot-manny/rigged.glb - lokalni izlaz
// auto-riga, nije u gitu; bez njega se test preskace). Sto se brani:
//   - Loom nade prste iz stabla (palac, kaziprst..mali, metakarpali preskoceni)
//   - Loomova strana dlana = os Z hand riga (+X savija prste u dlan): editor i auto-rig govore isto
//   - savijanje oko lokalne X osi svakog zgloba vodi vrh prsta prema dlanu, u ravnini prsta
//   - drska polumjera 1.6 cm u lijevoj saci (gripAlignedWorld): prsti se omotaju bez prodora,
//     a zglobovi su uz drsku
#include "TestHarness.h"

#include "../src/LoomModel.h"
#include "../src/LoomTool.h"

#include <cmath>
#include <filesystem>

namespace{

Warp::Id named(const Warp::Stage& stage, const std::string& name){
    Warp::Id found = Warp::None;
    stage.walk([&](const Warp::Entity& e, int){ if(found == Warp::None && e.joint && e.name == name) found = e.id; });
    return found;
}

glm::vec3 at(const Warp::Stage& stage, Warp::Id id){ return glm::vec3(stage.worldMatrix(id, 1.0)[3]); }

Engine::Physics::TriangleMesh cylinder(float radius, float height){
    Engine::Physics::TriangleMesh mesh;
    const int rings = 25, segments = 20;
    for(int r = 0; r < rings; ++r)
        for(int s = 0; s < segments; ++s){
            const float a = float(s) / float(segments) * 6.2831853f;
            mesh.vertices.push_back({radius * std::cos(a), -0.5f * height + height * float(r) / float(rings - 1), radius * std::sin(a)});
        }
    for(int r = 0; r + 1 < rings; ++r)
        for(int s = 0; s < segments; ++s){
            const uint32_t a = uint32_t(r * segments + s), b = uint32_t(r * segments + (s + 1) % segments);
            mesh.indices.insert(mesh.indices.end(), {a, a + uint32_t(segments), b, b, a + uint32_t(segments), b + uint32_t(segments)});
        }
    const uint32_t bottom = uint32_t(mesh.vertices.size());
    mesh.vertices.push_back({0.0f, -0.5f * height, 0.0f});
    mesh.vertices.push_back({0.0f, 0.5f * height, 0.0f});
    const uint32_t top = uint32_t((rings - 1) * segments);
    for(int s = 0; s < segments; ++s){
        const uint32_t a = uint32_t(s), b = uint32_t((s + 1) % segments);
        mesh.indices.insert(mesh.indices.end(), {bottom, a, b, bottom + 1, top + b, top + a});
    }
    return mesh;
}

}

int main(){
    TestReport report("hand_rig");
    const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                                       "tools/autorig/outputs/mascot-manny/rigged.glb";
    if(!std::filesystem::is_regular_file(path)){
        std::printf("   preskoceno: nema %s (lokalni izlaz auto-riga)\n", path.string().c_str());
        return report.result();
    }
    Spool::GltfScene scene;
    std::string error;
    Warp::Stage stage;
    const bool loaded = Spool::loadGltf(path.string(), scene, error);
    if(loaded) Loom::importGltf(stage, scene);
    const Warp::Id hand = named(stage, "hand_l");
    report.check("Manny mascot se ucita s hand_l", loaded && hand != Warp::None, error);
    if(hand == Warp::None) return report.result();

    const Loom::HandFingers fingers = Loom::handFingersOf(stage, hand, 1.0);
    const char* expected[5] = {"thumb_01_l", "index_01_l", "middle_01_l", "ring_01_l", "pinky_01_l"};
    bool order = fingers.valid();
    for(int f = 0; f < 5 && order; ++f) order = !fingers.fingers[size_t(f)].empty() && stage.get(fingers.fingers[size_t(f)].front())->name == expected[f];
    report.check("prsti iz stabla: palac..mali, metakarpali preskoceni", order, "");

    //Os Z sake iz hand riga = Loomova strana dlana
    const glm::mat4 handWorld = stage.worldMatrix(hand, 1.0);
    const glm::vec3 handZ = glm::normalize(glm::vec3(handWorld[2]));
    report.check("strana dlana u Loomu = os Z hand riga", glm::dot(handZ, fingers.palmNormal) > 0.95f,
                 fmt("cos %.3f", glm::dot(handZ, fingers.palmNormal)));

    //Savijanje oko lokalne X (konvencija hand riga) vodi vrh u dlan, u ravnini prsta
    float worstPalm = 1.0f, worstLateral = 0.0f;
    for(int f = 1; f < 5; ++f){
        const std::vector<Warp::Id>& finger = fingers.fingers[size_t(f)];
        Warp::Stage bent = stage;
        const glm::vec3 base = at(stage, finger.front()), tip0 = at(stage, finger.back());
        for(Warp::Id joint : finger){
            Warp::Transform local = bent.localAt(joint, 1.0);
            local.rotation = glm::normalize(local.rotation * glm::angleAxis(glm::radians(15.0f), glm::vec3(1, 0, 0)));
            bent.get(joint)->local = local;
            if(Warp::AnimatorTrack* track = bent.activeAnimatorTrack(joint)) track->rotationKeys = {};
        }
        const glm::vec3 direction = glm::normalize(tip0 - base);
        glm::vec3 motion = at(bent, finger.back()) - tip0;
        motion -= direction * glm::dot(motion, direction);
        if(glm::length(motion) < 1e-7f) continue;
        motion = glm::normalize(motion);
        glm::vec3 palm = fingers.palmNormal - direction * glm::dot(fingers.palmNormal, direction);
        palm = glm::normalize(palm);
        worstPalm = std::min(worstPalm, glm::dot(motion, palm));
        worstLateral = std::max(worstLateral, std::fabs(glm::dot(motion, glm::normalize(glm::cross(direction, palm)))));
    }
    report.check("+X savija prste u dlan, bez skretanja", worstPalm > 0.99f && worstLateral < 0.05f,
                 fmt("najgore: dlan %.3f, bocno %.3f", worstPalm, worstLateral));

    //Drska u lijevoj saci i prsti oko nje
    const Warp::Id middleEnd = named(stage, "middle_03_l"), forearm = named(stage, "lowerarm_l");
    const Loom::HoldHand holdHand{Warp::None, hand, middleEnd, false, forearm};
    Loom::HandFrame frame;
    const bool framed = Loom::handFrameAt(stage, holdHand, 1.0, frame);
    Loom::ToolGeometry handle;
    handle.mesh = cylinder(0.016f, 0.14f);
    Warp::Grip grip;
    grip.axis = {0, 1, 0};
    grip.palm = {1, 0, 0};
    const glm::mat4 toolWorld = Loom::gripAlignedWorld(glm::mat4(1.0f), grip, 0.016f, frame);
    const Engine::Physics::Collider collider = Engine::Physics::Collider::fromMesh(handle.mesh);
    const Loom::JointPose pose = Loom::conformedHandPoseAt(stage, fingers, Loom::gripPreset("grip"), 1.0, collider, toolWorld);
    Warp::Stage held = stage;
    for(const auto& [id, local] : pose){
        held.get(id)->local = local;
        if(Warp::AnimatorTrack* track = held.activeAnimatorTrack(id)) track->rotationKeys = {};
    }
    const glm::mat4 toTool = glm::inverse(toolWorld);
    bool penetrates = false;
    float farthest = 0.0f;
    for(int f = 0; f < 5; ++f){
        const std::vector<Warp::Id>& finger = fingers.fingers[size_t(f)];
        for(size_t j = 0; j + 1 < finger.size(); ++j){
            const glm::vec3 a(toTool * glm::vec4(at(held, finger[j]), 1.0f)), b(toTool * glm::vec4(at(held, finger[j + 1]), 1.0f));
            if(collider.capsuleHits(a, b, 0.004f)){
                penetrates = true;
                std::printf("   prodor: %s (rest dira: %d)\n", held.get(finger[j])->name.c_str(),
                            int(collider.capsuleHits(glm::vec3(toTool * glm::vec4(at(stage, finger[j]), 1.0f)),
                                                     glm::vec3(toTool * glm::vec4(at(stage, finger[j + 1]), 1.0f)), 0.004f)));
            }
        }
        if(f > 0) farthest = std::max(farthest, collider.distance(glm::vec3(toTool * glm::vec4(at(held, finger.back()), 1.0f)), 0.3f));
    }
    report.check("drska u saci: prsti bez prodora, zadnji zglobovi uz drsku", framed && !penetrates && farthest < 0.025f,
                 fmt("prodor %d, najdalji zglob %.1f cm", int(penetrates), farthest * 100.0f));
    return report.result();
}
