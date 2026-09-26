// Redovi timelinea (LoomTimelineRows.h): sto se vidi ispod ravnala za odabrani lik.
//
// Lik Hero: Hips -> Spine -> Head (Head bez kljuceva) i mesh uz njih. Klip 1-100 pokrece Hips
// (skok: y +0.5 m u kadru 50) i Spine; sloj ispravlja Spine u kadrovima 40 i 60.
//   - gusti kljucevi se spoje u traku, rijetki ostanu pojedinacni
//   - redovi kostiju: redom hijerarhije, samo pokretane kosti, ispravci sloja na pravoj kosti
//   - red objekta: traka klipa + rombovi ispravaka i kljuceva samog lika
//   - slicice: FK polozaji kostiju u uzorcima, skok se vidi; otisak se mijenja s tezinom sloja
//   - uzorci slicica padaju samo unutar klipa
#include "TestHarness.h"

#include "../src/LoomTimelineRows.h"

#include <cmath>

namespace{

struct Scene{ Warp::Stage stage; Warp::Id rig = Warp::None, hips = Warp::None, spine = Warp::None, head = Warp::None; };

Scene makeScene(){
    Scene s;
    s.rig = s.stage.create("Hero");
    s.stage.create("Body", s.rig);                   //mesh, nije kost
    s.hips = s.stage.create("Hips", s.rig);
    s.spine = s.stage.create("Spine", s.hips);
    s.head = s.stage.create("Head", s.spine);
    for(Warp::Id joint : {s.hips, s.spine, s.head}) s.stage.get(joint)->joint = Warp::Joint{};
    s.stage.get(s.spine)->local.translation = glm::vec3(0.0f, 0.3f, 0.0f);
    s.stage.get(s.head)->local.translation = glm::vec3(0.0f, 0.4f, 0.0f);
    Warp::AnimationClip clip;
    clip.name = "take";
    clip.startFrame = 1.0;
    clip.endFrame = 100.0;
    Warp::AnimatorTrack hips, spine;
    hips.target = s.hips;
    spine.target = s.spine;
    for(int f = 1; f <= 100; ++f){
        const float jump = f == 50 ? 0.5f : 0.0f;
        hips.translationKeys.set(double(f), glm::vec3(0.0f, 1.0f + jump, 0.0f));
        hips.rotationKeys.set(double(f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        spine.rotationKeys.set(double(f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }
    clip.tracks = {hips, spine};
    Warp::AnimationLayer layer;
    layer.name = "fix";
    Warp::AnimatorTrack edit;
    edit.target = s.spine;
    edit.rotationKeys.set(40.0, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    edit.rotationKeys.set(60.0, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    layer.keys.push_back(edit);
    clip.layers.push_back(layer);
    Warp::Animator animator;
    animator.animations.push_back(clip);
    s.stage.get(s.rig)->animator = animator;
    return s;
}

}

int main(){
    TestReport report("timeline_rows");

    const auto spans = Loom::mergeTimelineKeys({0.0f, 1.0f, 2.0f, 10.0f, 11.0f, 30.0f}, 3.0f);
    report.check("gusti kljucevi postanu traka, rijetki ostanu kljucevi",
                 spans.size() == 3 && spans[0].keys == 3 && spans[0].x1 == 2.0f && spans[1].keys == 2 && !spans[2].bar(),
                 fmt("%zu raspona", spans.size()));

    Scene s = makeScene();
    const auto rows = Loom::timelineBoneRows(s.stage, s.rig);
    report.check("redovi kostiju: redom hijerarhije, samo pokretane, uvuceno",
                 rows.size() == 2 && rows[0].label == "Hips" && rows[1].label == "Spine" && rows[0].depth == 0 && rows[1].depth == 1 &&
                 rows[0].keys.size() == 100,
                 fmt("%zu redova", rows.size()));
    report.check("ispravak sloja je na svojoj kosti",
                 rows.size() == 2 && rows[0].edits.empty() && rows[1].edits == std::vector<double>{40.0, 60.0}, "");

    s.stage.get(s.rig)->translationKeys.set(5.0, glm::vec3(0.0f));
    const Loom::TimelineObjectRow object = Loom::timelineObjectRow(s.stage, s.spine, s.rig);
    report.check("red objekta: klip 1-100, rombovi ispravaka i kljuca lika",
                 object.label == "Hero" && object.hasClip && object.clipStart == 1.0 && object.clipEnd == 100.0 &&
                 object.layers == 1 && object.edits == std::vector<double>{5.0, 40.0, 60.0},
                 fmt("%zu rombova", object.edits.size()));

    const auto channels = Loom::timelineChannelRows(s.stage, s.rig);
    report.check("objekt bez kostiju: P/R/S redovi s kljucevima", channels.size() == 3 &&
                 channels[0].edits == std::vector<double>{5.0} && channels[1].edits.empty(), "");

    Loom::TimelinePoseStrip strip;
    Loom::sampleTimelinePoses(s.stage, s.rig, {10.0, 50.0}, strip);
    const bool sampled = strip.joints.size() == 3 && strip.positions.size() == 2;
    report.check("slicice: sve kosti (i Head bez kljuceva), roditelj prije djeteta",
                 sampled && strip.parents == std::vector<int>{-1, 0, 1} && strip.hips == 0, fmt("%zu kostiju", strip.joints.size()));
    if(sampled){
        const float headRest = strip.positions[0][2].y, headJump = strip.positions[1][2].y;
        report.check("slicice: FK glave 1.7 m, u skoku 2.2 m", std::fabs(headRest - 1.7f) < 1e-4f && std::fabs(headJump - 2.2f) < 1e-4f,
                     fmt("%.3f / %.3f", headRest, headJump));
        report.check("slicice: pod i visina preko svih uzoraka", std::fabs(strip.floor - 1.0f) < 1e-4f && std::fabs(strip.height - 1.2f) < 1e-4f,
                     fmt("pod %.3f visina %.3f", strip.floor, strip.height));
    }

    const uint64_t before = Loom::timelineClipFingerprint(s.stage, s.rig);
    const uint64_t same = Loom::timelineClipFingerprint(s.stage, s.rig);
    s.stage.get(s.rig)->animator->animations[0].layers[0].weight = 0.5f;
    report.check("otisak se mijenja s tezinom sloja, inace stoji", before == same && before != Loom::timelineClipFingerprint(s.stage, s.rig), "");

    const auto frames = Loom::timelinePoseFrames(10.0, 20.0, 0.0f, 300.0f, 20.0f, [](float x){ return double(x / 10.0f); });
    report.check("uzorci slicica samo unutar klipa", frames == std::vector<double>{11.0, 13.0, 15.0, 17.0, 19.0},
                 fmt("%zu uzoraka", frames.size()));
    return report.result();
}
