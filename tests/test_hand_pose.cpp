// Poza prstiju za hvat (LoomHandPose.h): prsti iz stabla, savijanje prema dlanu, sloj od On do Off.
//
// Saka na ishodistu, prsti duz +x, dlan gleda prema -y (srednji prst je u mirnoj pozi blago savijen
// prema dolje - odatle se zna strana dlana). Palac je blizu zapesca. Imena su namjerno bez znacenja
// (f0_1...), kao kod UniRiga.
//   - palac, kaziprst..mali prst nadjeni iz polozaja; normala dlana -y
//   - grip savije vrhove prema dlanu; pistol ostavi kaziprst gotovo ravan
//   - hvat 10-30 postane jedan sloj: u 20 prsti savijeni, prije ulaza i iza izlaza nista
//   - promjena preseta ne dodaje sloj nego ga zamijeni; brisanje hvata brise sloj
#include "TestHarness.h"

#include "../src/LoomHandPose.h"

#include <cmath>

namespace{

struct Scene{ Warp::Stage stage; Warp::Id rig = Warp::None, hand = Warp::None, item = Warp::None; std::array<std::vector<Warp::Id>, 5> fingers; };

Scene makeScene(){
    Scene s;
    s.rig = s.stage.create("Rig");
    s.hand = s.stage.create("h", s.rig);
    s.stage.get(s.hand)->joint = Warp::Joint{};
    //Baze: palac blizu zapesca, ostali na zglobovima sake. Mjesani redoslijed djece u stablu
    const glm::vec3 bases[5] = {{0.02f, 0.0f, 0.03f}, {0.08f, 0.0f, 0.03f}, {0.085f, 0.0f, 0.01f}, {0.08f, 0.0f, -0.01f}, {0.075f, 0.0f, -0.03f}};
    for(int f : {3, 0, 4, 1, 2}){
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
    Warp::AnimationClip clip;
    clip.name = "take";
    clip.startFrame = 1.0;
    clip.endFrame = 50.0;
    Warp::AnimatorTrack track;
    track.target = s.hand;
    for(int f = 1; f <= 50; ++f) track.rotationKeys.set(double(f), glm::quat(1, 0, 0, 0));
    clip.tracks.push_back(track);
    Warp::Animator animator;
    animator.animations.push_back(clip);
    s.stage.get(s.rig)->animator = animator;
    s.item = s.stage.create("Desert Eagle");
    Warp::Hold hold;
    hold.hand = s.hand;
    hold.onFrame = 10.0;
    hold.offFrame = 30.0;
    hold.grip = "grip";
    s.stage.get(s.item)->holds.push_back(hold);
    return s;
}

glm::vec3 at(const Warp::Stage& stage, Warp::Id id, double frame){ return glm::vec3(stage.worldMatrix(id, frame)[3]); }

//Vrh prsta: kraj zadnje kosti (tri kosti po 3 cm, zadnja nema dijete - uzme se njen polozaj)
float tipY(const Scene& s, int finger, double frame){ return at(s.stage, s.fingers[size_t(finger)].back(), frame).y; }

}

int main(){
    TestReport report("hand_pose");
    Scene s = makeScene();

    const Loom::HandFingers hand = Loom::handFingersOf(s.stage, s.hand, 1.0);
    bool order = hand.valid();
    for(int f = 0; f < 5 && order; ++f) order = hand.fingers[size_t(f)] == s.fingers[size_t(f)];
    report.check("prsti iz polozaja: palac, kaziprst..mali", order, "");
    report.check("normala dlana -y (iz savijenog srednjeg prsta)", glm::length(hand.palmNormal - glm::vec3(0, -1, 0)) < 1e-3f,
                 fmt("%.2f %.2f %.2f", hand.palmNormal.x, hand.palmNormal.y, hand.palmNormal.z));

    //Poza iz preseta primijenjena privremeno na kopiju: vrhovi prema dlanu
    //Kopija s pozom preseta; prsti nemaju trackove, pa vrijedi local
    auto posed = [&](const char* preset){
        Warp::Stage copy = s.stage;
        for(const auto& [id, local] : Loom::handPoseAt(s.stage, hand, Loom::gripPreset(preset), 5.0)) copy.get(id)->local = local;
        return copy;
    };
    //Savijenost prsta: koliko se vrh priblizio bazi (ravan prst 6 cm). Visina vrha nije dobra mjera -
    //jako savijen prst (72 + 85 st) vrhom opet ide gore
    auto reach = [&](const Warp::Stage& stage, int finger){
        return glm::length(at(stage, s.fingers[size_t(finger)].back(), 5.0) - at(stage, s.fingers[size_t(finger)].front(), 5.0));
    };
    const float restMiddle = tipY(s, 2, 5.0);
    const Warp::Stage grip = posed("grip"), pistol = posed("pistol");
    report.check("grip: vrh srednjeg prsta ide prema dlanu", at(grip, s.fingers[2].back(), 5.0).y < restMiddle - 0.02f,
                 fmt("y %.3f -> %.3f", restMiddle, at(grip, s.fingers[2].back(), 5.0).y));
    report.check("pistol: kaziprst gotovo ravan, srednji savijen", reach(pistol, 1) > 0.055f && reach(pistol, 2) < 0.045f,
                 fmt("kaziprst %.3f m, srednji %.3f m (ravno 0.060)", reach(pistol, 1), reach(pistol, 2)));
    report.check("preset iz imena: Desert Eagle je pistol, Mug cup, Sword grip",
                 Loom::gripForItemName("Desert Eagle") == "pistol" && Loom::gripForItemName("Coffee Mug") == "cup" &&
                 Loom::gripForItemName("bastard_sword") == "grip", "");

    const size_t layers = Loom::syncHoldHandLayers(s.stage, s.rig, {}, 6.0);
    const Warp::AnimationClip& clip = s.stage.get(s.rig)->animator->animations[0];
    report.check("hvat 10-30 je jedan sloj", layers == 1 && clip.layers.size() == 1 && clip.layers[0].name.rfind("Hold: Desert", 0) == 0,
                 fmt("%zu slojeva, %s", clip.layers.size(), clip.layers.empty() ? "" : clip.layers[0].name.c_str()));
    const float inside = tipY(s, 2, 20.0), before = tipY(s, 2, 2.0), after = tipY(s, 2, 45.0);
    report.check("u 20 prsti savijeni, prije ulaza i iza izlaza kao u pokretu",
                 inside < restMiddle - 0.02f && std::fabs(before - restMiddle) < 1e-5f && std::fabs(after - restMiddle) < 1e-5f,
                 fmt("prije %.4f u %.4f iza %.4f (mirno %.4f)", before, inside, after, restMiddle));

    s.stage.get(s.item)->holds[0].grip = "pistol";
    Loom::syncHoldHandLayers(s.stage, s.rig, {}, 6.0);
    report.check("novi preset zamijeni sloj", s.stage.get(s.rig)->animator->animations[0].layers.size() == 1 &&
                 s.stage.get(s.rig)->animator->animations[0].layers[0].name.find("Pistol") != std::string::npos, "");
    s.stage.get(s.item)->holds.clear();
    Loom::syncHoldHandLayers(s.stage, s.rig, {}, 6.0);
    report.check("bez hvata nema sloja, prsti kao u pokretu", s.stage.get(s.rig)->animator->animations[0].layers.empty() &&
                 std::fabs(tipY(s, 2, 20.0) - restMiddle) < 1e-5f, "");
    return report.result();
}
