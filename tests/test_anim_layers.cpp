// Slojevi animacije (LoomAnimLayers.h): ispravak kao sloj preko netaknute osnove.
//
// Lik s jednim zglobom koji se u osnovi mice x = kadar. Sloj A: kljucevi u 40 (x 100) i 60 (x 200);
// sloj B: kljucevi u 80 (x 300) i 90 (x 310), ispravak +220 u oba - u vremenu se ne preklapaju.
//   - sloj daje isto sto i pose blend (izmedju 40 i 60 ispravak A -> B)
//   - iskljucen sloj: klip je osnova BIT PO BIT (usporedba svih kljuceva)
//   - tezina 0.5: pola ispravka
//   - slojevi koji se ne preklapaju su neovisni
//   - brisanje zadnjeg sloja vraca osnovu i prazni baseTracks
//   - projekt spremi i procita slojeve i osnovu, i izracun nakon citanja je isti
#include "TestHarness.h"

#include "../src/LoomAnimLayers.h"

#include <Warp/Project.h>

#include <cmath>
#include <filesystem>

namespace{

struct Scene{ Warp::Stage stage; Warp::Id rig = Warp::None, arm = Warp::None; };

Scene makeScene(){
    Scene s;
    s.rig = s.stage.create("Hero");
    s.arm = s.stage.create("Arm", s.rig);
    s.stage.get(s.arm)->joint = Warp::Joint{};
    Warp::AnimationClip clip;
    clip.name = "take";
    clip.startFrame = 1.0;
    clip.endFrame = 100.0;
    Warp::AnimatorTrack track;
    track.target = s.arm;
    track.targetPath = s.stage.path(s.arm);
    for(int f = 1; f <= 100; ++f){
        track.translationKeys.set(double(f), glm::vec3(float(f), 0.0f, 0.0f));
        track.rotationKeys.set(double(f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        track.scaleKeys.set(double(f), glm::vec3(1.0f));
    }
    clip.tracks.push_back(track);
    Warp::Animator animator;
    animator.animations.push_back(clip);
    s.stage.get(s.rig)->animator = animator;
    return s;
}

Loom::PoseKey key(Warp::Id id, double frame, float x){
    Warp::Transform t;
    t.translation = glm::vec3(x, 0.0f, 0.0f);
    return {frame, {{id, t}}};
}

float xAt(const Scene& s, double frame){ return s.stage.localAt(s.arm, frame).translation.x; }

Warp::AnimationClip& clipOf(Scene& s){ return s.stage.get(s.rig)->animator->animations[0]; }

bool sameTracks(const std::vector<Warp::AnimatorTrack>& a, const std::vector<Warp::AnimatorTrack>& b){
    if(a.size() != b.size()) return false;
    for(size_t i = 0; i < a.size(); ++i){
        if(a[i].translationKeys.times != b[i].translationKeys.times) return false;
        for(size_t k = 0; k < a[i].translationKeys.values.size(); ++k)
            if(a[i].translationKeys.values[k] != b[i].translationKeys.values[k]) return false;
        if(a[i].rotationKeys.times != b[i].rotationKeys.times) return false;
    }
    return true;
}

}

int main(){
    TestReport report("anim_layers");
    Scene s = makeScene();
    const std::vector<Warp::AnimatorTrack> original = clipOf(s).tracks;

    Loom::PoseKeySettings settings;
    settings.inFrames = 0.0;
    settings.outFrames = 1.0;
    clipOf(s).layers.push_back(Loom::layerFromPoseKeys(s.stage, "Fix A", {key(s.arm, 40.0, 100.0f), key(s.arm, 60.0, 200.0f)}, settings));
    clipOf(s).layers.push_back(Loom::layerFromPoseKeys(s.stage, "Fix B", {key(s.arm, 80.0, 300.0f), key(s.arm, 90.0, 310.0f)}, settings));
    const size_t applied = Loom::bakeAnimationLayers(s.stage, s.rig, 0, {});
    report.check("dva sloja primijenjena, osnova sacuvana netaknuta",
                 applied == 2 && sameTracks(clipOf(s).baseTracks, original) &&
                 std::fabs(xAt(s, 50.0) - 150.0f) < 1e-3f && std::fabs(xAt(s, 85.0) - (85.0f + 220.0f)) < 1e-2f,
                 fmt("kadar 50: %.2f, kadar 85: %.2f", double(xAt(s, 50.0)), double(xAt(s, 85.0))));

    clipOf(s).layers[0].enabled = false;
    Loom::bakeAnimationLayers(s.stage, s.rig, 0, {});
    const float b85 = xAt(s, 85.0);
    report.check("iskljucen sloj A: kadar 50 je osnova, sloj B nepromijenjen",
                 xAt(s, 50.0) == 50.0f && std::fabs(b85 - (85.0f + 220.0f)) < 1e-2f,
                 fmt("kadar 50: %.2f, kadar 85: %.2f", double(xAt(s, 50.0)), double(b85)));

    clipOf(s).layers[0].enabled = true;
    clipOf(s).layers[0].weight = 0.5f;
    Loom::bakeAnimationLayers(s.stage, s.rig, 0, {});
    report.check("tezina 0.5: pola ispravka", std::fabs(xAt(s, 50.0) - 100.0f) < 1e-3f,
                 fmt("kadar 50: %.2f (osnova 50, puni ispravak 150)", double(xAt(s, 50.0))));

    //Projekt: spremi i procitaj; izracun nakon citanja mora dati isto
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "loom_anim_layers_test.usda";
    std::string error;
    const bool saved = Warp::saveProject(s.stage, file.string(), error);
    Warp::Stage loaded;
    const bool read = saved && Warp::loadProject(file.string(), loaded, error);
    bool roundTrip = false;
    if(read){
        const Warp::Id rig = loaded.find("/Hero"), arm = loaded.find("/Hero/Arm");
        const Warp::AnimationClip& clip = loaded.get(rig)->animator->animations[0];
        roundTrip = clip.layers.size() == 2 && clip.layers[0].name == "Fix A" && clip.layers[0].weight == 0.5f &&
                    clip.layers[1].keys.size() == 1 && clip.layers[1].keys[0].target == arm &&
                    clip.baseTracks.size() == 1 && clip.baseTracks[0].target == arm;
        Loom::bakeAnimationLayers(loaded, rig, 0, {});
        roundTrip = roundTrip && std::fabs(loaded.localAt(arm, 50.0).translation.x - 100.0f) < 1e-3f &&
                    std::fabs(loaded.localAt(arm, 85.0).translation.x - b85) < 1e-2f;
    }
    std::filesystem::remove(file);
    report.check("projekt cuva slojeve i osnovu, izracun isti nakon citanja", roundTrip, error.empty() ? "ok" : error);

    clipOf(s).layers.clear();
    Loom::bakeAnimationLayers(s.stage, s.rig, 0, {});
    report.check("brisanje zadnjeg sloja vraca osnovu bit po bit",
                 sameTracks(clipOf(s).tracks, original) && clipOf(s).baseTracks.empty() && xAt(s, 50.0) == 50.0f,
                 fmt("kadar 50: %.2f", double(xAt(s, 50.0))));
    return report.result();
}
