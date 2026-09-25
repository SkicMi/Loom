// LoomPoseBlend: poza u dva kljuca, izmedju njih pretapanje poza - bez povratka na original.
//
// Zglob se u originalu mice x = kadar. Kljucevi: kadar 40 -> x 100, kadar 60 -> x 200. Izmedju
// njih rezultat mora ici od 100 do 200 (sredina 150), a NE vratiti se na original (x 50 u kadru
// 50) - to je bila greska koju je korisnik prijavio. Ulaz, izlaz i "drzi" imaju svoje brojke.
#include "TestHarness.h"

#include "../src/LoomPoseBlend.h"

#include <cmath>

namespace{

Warp::Stage motionStage(Warp::Id& joint){
    Warp::Stage stage;
    joint = stage.create("Arm");
    stage.get(joint)->joint = Warp::Joint{};
    //Kljuc pomaka i rotacije u svakom kadru, kao Kimodov klip na rigu
    for(int f = 1; f <= 100; ++f){
        stage.get(joint)->translationKeys.set(double(f), glm::vec3(float(f), 0.0f, 0.0f));
        stage.get(joint)->rotationKeys.set(double(f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }
    return stage;
}

Loom::PoseKey key(Warp::Id joint, double frame, float x){
    Warp::Transform pose;
    pose.translation = glm::vec3(x, 0.0f, 0.0f);
    return {frame, {{joint, pose}}};
}

float xAt(const Warp::Stage& stage, Warp::Id joint, double frame){ return stage.localAt(joint, frame).translation.x; }

}

int main(){
    TestReport report("pose_blend");

    {
        Warp::Id arm;
        Warp::Stage stage = motionStage(arm);
        Loom::PoseKeySettings settings;
        settings.inFrames = 8.0;
        settings.holdFrames = 0.0;
        settings.outFrames = 10.0;
        //Kljucevi namjerno obrnutim redom: funkcija ih sama poreda
        Loom::applyPoseKeys(stage, {key(arm, 60.0, 200.0f), key(arm, 40.0, 100.0f)}, 1.0, 100.0, settings);
        report.check("kljucevi drze tocno uredjenu pozu", xAt(stage, arm, 40.0) == 100.0f && xAt(stage, arm, 60.0) == 200.0f,
                     fmt("%.2f / %.2f", double(xAt(stage, arm, 40.0)), double(xAt(stage, arm, 60.0))));
        report.check("izmedju kljuceva poza u pozu, bez originala", std::fabs(xAt(stage, arm, 50.0) - 150.0f) < 1e-3f &&
                     xAt(stage, arm, 45.0) > 100.0f && xAt(stage, arm, 45.0) < 150.0f,
                     fmt("kadar 45: %.2f, kadar 50: %.2f (original bi bio 50)", double(xAt(stage, arm, 45.0)), double(xAt(stage, arm, 50.0))));
        //Ulaz: kadar 32 je original, 36 je na pola puta prema pomaku (+60 u kljucu)
        report.check("ulaz: pomak se ulije preko pokreta", xAt(stage, arm, 32.0) == 32.0f &&
                     std::fabs(xAt(stage, arm, 36.0) - (36.0f + 60.0f * 0.5f)) < 1e-3f,
                     fmt("32: %.2f, 36: %.2f", double(xAt(stage, arm, 32.0)), double(xAt(stage, arm, 36.0))));
        //Izlaz (Return, hold 0, out 10): pomak zadnjeg kljuca je +140 i iscuri do kadra 70
        report.check("izlaz: povratak u pokret iza zadnjeg kljuca", xAt(stage, arm, 70.0) == 70.0f &&
                     std::fabs(xAt(stage, arm, 65.0) - (65.0f + 140.0f * 0.5f)) < 1e-3f && xAt(stage, arm, 80.0) == 80.0f,
                     fmt("65: %.2f, 70: %.2f", double(xAt(stage, arm, 65.0)), double(xAt(stage, arm, 70.0))));
    }

    {
        Warp::Id arm;
        Warp::Stage stage = motionStage(arm);
        Loom::PoseKeySettings settings;
        settings.ending = Loom::PoseKeyEnding::Hold;
        Loom::applyPoseKeys(stage, {key(arm, 40.0, 100.0f), key(arm, 60.0, 200.0f)}, 1.0, 100.0, settings);
        report.check("Hold: zadnja poza stoji do kraja klipa", xAt(stage, arm, 61.0) == 200.0f && xAt(stage, arm, 100.0) == 200.0f,
                     fmt("61: %.2f, 100: %.2f", double(xAt(stage, arm, 61.0)), double(xAt(stage, arm, 100.0))));
    }

    {
        //Jedan kljuc s Return je stari Smart Blend: ulije se, drzi, iscuri
        Warp::Id arm;
        Warp::Stage stage = motionStage(arm);
        Loom::PoseKeySettings settings;
        settings.holdFrames = 5.0;
        settings.outFrames = 10.0;
        Loom::applyPoseKeys(stage, {key(arm, 50.0, 80.0f)}, 1.0, 100.0, settings);
        report.check("jedan kljuc: drzi pomak pa se vrati", xAt(stage, arm, 50.0) == 80.0f &&
                     std::fabs(xAt(stage, arm, 55.0) - 85.0f) < 1e-3f && xAt(stage, arm, 65.0) == 65.0f,
                     fmt("55: %.2f, 65: %.2f", double(xAt(stage, arm, 55.0)), double(xAt(stage, arm, 65.0))));
    }

    {
        //Bez pretapanja: mijenjaju se samo kljucni kadrovi
        Warp::Id arm;
        Warp::Stage stage = motionStage(arm);
        Loom::PoseKeySettings settings;
        settings.blendBetween = false;
        Loom::applyPoseKeys(stage, {key(arm, 40.0, 100.0f), key(arm, 60.0, 200.0f)}, 1.0, 100.0, settings);
        report.check("samo kljucevi: izmedju ostaje original", xAt(stage, arm, 40.0) == 100.0f && xAt(stage, arm, 50.0) == 50.0f,
                     fmt("50: %.2f", double(xAt(stage, arm, 50.0))));
    }

    {
        //Rotacija ide kracim putem i u sredini je tocno na pola kuta
        Warp::Id arm;
        Warp::Stage stage = motionStage(arm);
        Loom::PoseKey a = key(arm, 10.0, 10.0f), b = key(arm, 20.0, 20.0f);
        b.pose[0].second.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
        Loom::applyPoseKeys(stage, {a, b}, 1.0, 100.0, Loom::PoseKeySettings{});
        const float angle = glm::degrees(glm::angle(stage.localAt(arm, 15.0).rotation));
        report.check("rotacija u sredini na pola kuta", std::fabs(angle - 45.0f) < 0.01f, fmt("%.3f st", double(angle)));
    }
    return report.result();
}
