// Hvat na pravom liku: drska u dlanu, prsti omotani oko nje, palac s druge strane.
//
// Lik iz desnog klika (tools/autorig/outputs/humanoid-mascott/rigged.glb, 1.80 m, mirne sake) i isti put
// kao editor (grabWithHand u loom_app): gripAlignedWorld -> grabItemAt -> syncHoldHandLayers s
// colliderom toola. Predmet: ~/Downloads/bastard_sword__lowpoly.glb (mac 115 cm, grip iz oblika).
// Bez lika ili maca se test preskace.
// Mjere (desna saka, sredina hvata):
//   dlan      udaljenost osi drske od tocke dlana (holdPalmPoint), prema polumjer + debljina dlana
//   kut       drska prema osi sake (mali prst -> kaziprst)
//   dodir     najdalji vrh kaziprst..mali od povrsine drske (linija kosti; prst je ~5 mm debeo oko nje);
//             prodor clanaka (kapsula 4 mm). Vrh = zadnji clanak nosen rotacijom zadnjeg zgloba
//   omatanje  kut oko osi drske od zgloba prsta do vrha (srednji prst), u presjeku drske
//   palac     vrh palca od povrsine drske
//   ostrica   kut izmedju sirine predmeta (stitnik, ostrica) i normale dlana: plosnati dio lezi u dlanu,
//             ostrica gleda naprijed kao zglobovi (90 st); 0 st = dlan na ostrici
#include "TestHarness.h"

#include "../src/LoomModel.h"
#include "../src/LoomMotionPanel.h"
#include "../src/LoomRelaxedHands.h"
#include "../src/LoomTool.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace{

Warp::Id named(const Warp::Stage& stage, const std::string& name){
    Warp::Id found = Warp::None;
    stage.walk([&](const Warp::Entity& e, int){ if(found == Warp::None && e.joint && e.name == name) found = e.id; });
    return found;
}

glm::vec3 at(const Warp::Stage& stage, Warp::Id id, double frame){ return glm::vec3(stage.worldMatrix(id, frame)[3]); }

struct Result{ float palm = 0, palmExpected = 0, angle = 0, edge = 90, farthest = 0, wrap = 0, thumb = 0; float tips[5]{}; bool penetrates = false; };

}

int main(){
    TestReport report("grab_real");
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path character = root / "tools/autorig/outputs/humanoid-mascott/rigged.glb";
    if(!std::filesystem::is_regular_file(character)){
        std::printf("   preskoceno: nema %s\n", character.string().c_str());
        return report.result();
    }
    //Lik kao importAutoRigModel: 1.80 m, Animator, mirne sake
    Warp::Stage base;
    Spool::GltfScene scene;
    std::string error;
    Spool::loadGltf(character.string(), scene, error);
    const Loom::ModelImportReport imported = Loom::importGltf(base, scene);
    const Warp::Id rig = Loom::motionCharacterForEntity(base, imported.group);
    if(Warp::Entity* group = base.get(imported.group)){
        const float height = (imported.high.y - imported.low.y) * group->local.scale.y;
        if(height > 1e-6f) group->local.scale *= Loom::characterHeightMetres / height;
    }
    Loom::ensureRigAnimator(base, rig);
    Loom::applyRelaxedHandsRestPose(base, rig);
    base.startFrame = 1.0;
    base.endFrame = 100.0;
    const Loom::HoldHand hand{rig, named(base, "hand_r"), named(base, "middle_03_r"), true, named(base, "lowerarm_r")};
    report.check("lik 1.80 m sa sakom", rig != Warp::None && hand.hand != Warp::None, error);
    if(hand.hand == Warp::None) return report.result();

    auto grab = [&](Warp::Stage& stage, Warp::Id item, const std::string& label){
        Result r;
        Loom::ToolColliders colliders;
        const double on = 1.0, sample = 20.0;
        Loom::HandFrame frame;
        Loom::handFrameAt(stage, hand, on, frame);
        Warp::Grip& gripRef = stage.get(item)->tool->grips.front();
        const auto started = std::chrono::steady_clock::now();
        const glm::mat4 world = Loom::heldWorld(stage, item, gripRef, frame, on, &hand);
        std::printf("   hvat %s: polozaj na drsci %.0f ms\n", label.c_str(),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        const Warp::Grip grip = gripRef;
        const float scale = glm::length(glm::vec3(world[0]));
        const float radius = Loom::handleRadius(Loom::toolGeometry(stage, item, on), grip, 1.0f / scale) * scale;
        Loom::grabItemAt(stage, item, hand.hand, on, stage.endFrame, world);
        for(Warp::Hold& hold : stage.get(item)->holds) hold.grip = grip.preset;
        Loom::syncHoldHandLayers(stage, rig, {}, 6.0, [&](Warp::Id id) -> const Engine::Physics::Collider*{
            return stage.get(id) && stage.get(id)->tool ? colliders.of(stage, id, sample) : nullptr;
        });
        const Engine::Physics::Collider* collider = colliders.of(stage, item, sample);
        const glm::mat4 toolWorld = stage.worldMatrix(item, sample), toTool = glm::inverse(toolWorld);
        auto local = [&](glm::vec3 p){ return glm::vec3(toTool * glm::vec4(p, 1.0f)); };
        //Os drske u svijetu
        const glm::vec3 axisPoint(toolWorld * glm::vec4(grip.point, 1.0f));
        const glm::vec3 axis = glm::normalize(glm::vec3(toolWorld * glm::vec4(grip.axis, 0.0f)));
        auto fromAxis = [&](glm::vec3 p){ const glm::vec3 d = p - axisPoint; return d - axis * glm::dot(d, axis); };
        Loom::HandFrame now;
        Loom::handFrameAt(stage, hand, sample, now);
        r.palm = glm::length(fromAxis(Loom::holdPalmPoint(stage, hand, sample))) * 100.0f;
        r.palmExpected = (radius + now.palmDepth) * 100.0f;
        //Os sake bez dijagonale: baza malog prsta -> baza kaziprsta
        const Loom::HandFingers bases = Loom::handFingersOf(stage, hand.hand, sample);
        glm::vec3 across = at(stage, bases.fingers[1].front(), sample) - at(stage, bases.fingers[4].front(), sample);
        across = glm::normalize(across - now.normal * glm::dot(across, now.normal));
        r.angle = glm::degrees(std::acos(std::min(1.0f, std::fabs(glm::dot(axis, across)))));
        //Sirina predmeta (druga glavna os) okomito na drsku, prema normali dlana
        glm::vec3 width = glm::vec3(toolWorld * glm::vec4(Loom::toolAxes(Loom::toolGeometry(stage, item, sample)).axes[1], 0.0f));
        width -= axis * glm::dot(width, axis);
        if(glm::length(width) > 1e-6f)
            r.edge = glm::degrees(std::acos(std::min(1.0f, std::fabs(glm::dot(glm::normalize(width), now.normal)))));
        const Loom::HandFingers fingers = Loom::handFingersOf(stage, hand.hand, sample);
        //Vrh: produzetak srednjeg clanka u mirnoj pozi, nosen rotacijom zadnjeg zgloba (kao u omatanju)
        auto tipOf = [&](const std::vector<Warp::Id>& finger){
            const glm::vec3 restLast = at(base, finger.back(), sample), restBefore = at(base, finger[finger.size() - 2], sample);
            const glm::vec3 tipLocal = glm::inverse(glm::mat3(base.worldMatrix(finger.back(), sample))) * ((restLast - restBefore) * 0.8f);
            return at(stage, finger.back(), sample) + glm::mat3(stage.worldMatrix(finger.back(), sample)) * tipLocal;
        };
        for(int f = 0; f < 5; ++f){
            const std::vector<Warp::Id>& finger = fingers.fingers[size_t(f)];
            if(finger.size() < 2) continue;
            std::vector<glm::vec3> points;
            for(Warp::Id id : finger) points.push_back(at(stage, id, sample));
            points.push_back(tipOf(finger));
            for(size_t j = 0; j + 1 < points.size(); ++j)
                if(collider->capsuleHits(local(points[j]), local(points[j + 1]), 0.004f / scale)) r.penetrates = true;
            const float tipGap = collider->distance(local(points.back()), 1.0f / scale) * scale * 100.0f;
            r.tips[f] = tipGap;
            if(f == 0) r.thumb = tipGap;
            else r.farthest = std::max(r.farthest, tipGap);
            if(f == 2){
                const glm::vec3 a = fromAxis(points.front()), b = fromAxis(points.back());
                r.wrap = glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f)));
            }
        }
        std::printf("   vrhovi %s: palac %.1f kaziprst %.1f srednji %.1f prstenjak %.1f mali %.1f cm\n", label.c_str(), r.tips[0], r.tips[1], r.tips[2], r.tips[3], r.tips[4]);
        std::printf("   MJERA %s: dlan %.1f cm (ocekivano %.1f), kut %.0f st, vrhovi do %.1f cm, omatanje %.0f st, palac %.1f cm, prodor %d, ostrica %.0f st\n",
                    label.c_str(), r.palm, r.palmExpected, r.angle, r.farthest, r.wrap, r.thumb, int(r.penetrates), r.edge);
        return r;
    };
    auto checkGrab = [&](const Result& r, const std::string& label){
        report.check((label + ": drska u dlanu (os do 1 cm od ocekivanog), dijagonalno 10-35 st").c_str(),
                     std::fabs(r.palm - r.palmExpected) < 1.0f && r.angle > 10.0f && r.angle < 35.0f,
                     fmt("dlan %.1f / %.1f cm, kut %.0f st", r.palm, r.palmExpected, r.angle));
        //Omatanje oko osi ovisi o debljini drske: saka je na macu uz stitnik, na kozni omot (polumjer
        //1.7-2.5 cm, izmjereno), ne na tanki prsten uz jabuku (1.3 cm, ondje je bilo 170 st). Vrhovi do
        //drske su glavna mjera; omatanje samo da prsti nisu ostali ispruzeni
        report.check((label + ": prsti omotani (linija kosti vrha do 1.2 cm od drske, srednji preko 100 st oko osi), bez prodora").c_str(),
                     r.farthest < 1.2f && r.wrap > 100.0f && !r.penetrates,
                     fmt("vrhovi %.1f cm, omatanje %.0f st, prodor %d", r.farthest, r.wrap, int(r.penetrates)));
        report.check((label + ": palac uz drsku (do 1.5 cm)").c_str(), r.thumb < 1.5f, fmt("%.1f cm", r.thumb));
        report.check((label + ": plosnati dio u dlanu, ostrica naprijed (sirina 70-90 st od normale dlana)").c_str(),
                     r.edge > 70.0f, fmt("%.0f st", r.edge));
    };

    const std::filesystem::path swordPath = std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / "Downloads/bastard_sword__lowpoly.glb";
    if(std::filesystem::is_regular_file(swordPath)){
        Warp::Stage stage = base;
        Spool::GltfScene swordScene;
        Spool::loadGltf(swordPath.string(), swordScene, error);
        const Loom::ModelImportReport sword = Loom::importGltf(stage, swordScene);
        Warp::Entity* group = stage.get(sword.group);
        const Loom::ToolGeometry geometry = Loom::toolGeometry(stage, sword.group, 1.0);
        const glm::vec3 size = geometry.high - geometry.low;
        group->local.scale *= Loom::suggestedToolLength("bastard_sword", "weapon") / (std::max({size.x, size.y, size.z}) * group->local.scale.x);
        group->local.translation = glm::vec3(0.4f, 0.0f, 0.3f);
        Warp::Tool tool;
        tool.kind = "weapon";
        tool.grips.push_back(Loom::defaultGrip(geometry, "grip", 1.0f / group->local.scale.x));
        group->tool = tool;
        checkGrab(grab(stage, sword.group, "mac"), "mac");
    }else std::printf("   mac preskocen: nema %s\n", swordPath.string().c_str());

    //Pistolj (model nije u repou): LOOM_TEST_PISTOL=<glTF desert eagle ili slicnog>
    if(const char* pistolPath = std::getenv("LOOM_TEST_PISTOL"); pistolPath && std::filesystem::is_regular_file(pistolPath)){
        Warp::Stage stage = base;
        Spool::GltfScene pistolScene;
        Spool::loadGltf(pistolPath, pistolScene, error);
        const Loom::ModelImportReport pistol = Loom::importGltf(stage, pistolScene);
        Warp::Entity* group = stage.get(pistol.group);
        const Loom::ToolGeometry geometry = Loom::toolGeometry(stage, pistol.group, 1.0);
        const glm::vec3 size = geometry.high - geometry.low;
        group->local.scale *= 0.27f / (std::max({size.x, size.y, size.z}) * group->local.scale.x);
        group->local.translation = glm::vec3(0.4f, 0.0f, 0.3f);
        Warp::Tool tool;
        tool.kind = "weapon";
        Warp::Grip grip;
        grip.preset = "pistol";
        const Loom::ToolAxes axes = Loom::toolAxes(geometry);
        const bool found = Loom::pistolGrip(geometry, axes, grip);
        tool.grips.push_back(Loom::defaultGrip(geometry, "pistol"));
        group->tool = tool;
        std::printf("   pistolj: rukohvat %d, os . cijev %.2f, os . visina %.2f\n", int(found),
                    glm::dot(grip.axis, axes.axes[0]), glm::dot(grip.axis, axes.axes[1]));
        grab(stage, pistol.group, "pistolj");
    }
    return report.result();
}
