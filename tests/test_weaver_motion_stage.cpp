// Pokret u sceni: klip iz WeaverMotion modula (Kimodo) kao kostur u Warpu.
//
// ZASTO SE OVO TESTIRA. Lik se u editoru postavlja u scenu iz matchmovea - uz kameru s 50 fps
// snimke. Tri stvari mogu tiho promasiti:
//
//   VRIJEME       klip od 30 fps prepisan na timeline scene kadar-za-kadar hoda 5/3 puta
//                 prebrzo; a klip koji preuzme timeline pokvari kameru iz solvea
//   MJESTO        lik mora poceti ondje gdje ga je editor postavio, ne u ishodistu klipa
//   RUPE          zglob koji se pomice mora imati kljuc u SVAKOM kadru; kljuc samo gdje pomak
//                 nije nula ostavi kadar u kojem zglob drzi prosli polozaj
//
// Prvu verziju testa (i uvoza) napisao je GPT; ova provjerava zamjenu
#include "TestHarness.h"
#include "../src/LoomWeaverMotion.h"

#include <cmath>

namespace{

float distance(const glm::vec3& a, const glm::vec3& b){ return glm::length(a - b); }

Engine::WeaverMotion::Clip twoJointClip(){
    Engine::WeaverMotion::Clip clip;
    clip.framesPerSecond = 30.0;
    clip.joints.push_back({"Pelvis", -1, glm::vec3(0.0f, 1.0f, 0.0f)});
    clip.joints.push_back({"Chest", 0, glm::vec3(0.0f, 0.5f, 0.0f)});
    clip.joints.push_back({"Hand", 1, glm::vec3(0.3f, 0.2f, 0.0f)});
    for(int f = 0; f < 3; ++f){
        Engine::WeaverMotion::Pose pose;
        pose.translations = {glm::vec3(0.1f + 0.1f * f, 0.2f, 0.3f), glm::vec3(0.0f),
                             //Saka se pomakne samo u drugom kadru - u prvom i trecem je nula
                             f == 1 ? glm::vec3(0.0f, 0.05f, 0.0f) : glm::vec3(0.0f)};
        pose.rotations = {f == 0 ? glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1)) : glm::quat(1, 0, 0, 0),
                          glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0)};
        clip.frames.push_back(pose);
    }
    return clip;
}

}

int main(){
    TestReport report("weaver_motion_stage");
    const Engine::WeaverMotion::Clip clip = twoJointClip();

    //-- 1. prazna scena: klip preuzme timeline, lik pocinje u ishodistu ----------------------
    {
        Warp::Stage stage;
        const Loom::WeaverMotionImportReport imported = Loom::importWeaverMotionClip(stage, clip, "KimodoWalk");
        report.check("uvoz u praznu scenu", imported.problem.empty() && imported.joints == 3 && imported.frames == 3 &&
                                             stage.startFrame == 1.0 && stage.endFrame == 3.0 && stage.framesPerSecond == 30.0,
            imported.problem.empty() ? fmt("%zu zgloba, kadrovi %.0f-%.0f @ %.0f", imported.joints, stage.startFrame,
                                           stage.endFrame, stage.framesPerSecond) : imported.problem);

        const Warp::Id pelvis = stage.find("/KimodoWalk/Pelvis");
        const Warp::Id chest = stage.find("/KimodoWalk/Pelvis/Chest");
        const bool clean = pelvis == imported.root && chest != Warp::None && stage.get(chest)->joint &&
                           stage.get(pelvis)->joint && stage.size() == 4;
        report.check("stablo: samo zglobovi, bez kocke po zglobu", clean, fmt("%zu entiteta", stage.size()));

        //Korijen krece u ishodistu (vodoravno), na svojoj visini; dijete prati rotaciju korijena
        const glm::vec3 pelvisAt1 = glm::vec3(stage.worldMatrix(pelvis, 1.0)[3]);
        const glm::vec3 pelvisAt2 = glm::vec3(stage.worldMatrix(pelvis, 2.0)[3]);
        const glm::vec3 chestAt1 = glm::vec3(stage.worldMatrix(chest, 1.0)[3]);
        report.check("korijen krece na mjestu postavljanja, dijete prati rotaciju",
            distance(pelvisAt1, glm::vec3(0.0f, 1.2f, 0.0f)) < 1e-5f && distance(pelvisAt2, glm::vec3(0.1f, 1.2f, 0.0f)) < 1e-5f &&
            distance(chestAt1, glm::vec3(-0.5f, 1.2f, 0.0f)) < 1e-5f,
            fmt("korijen %.2f %.2f %.2f -> %.2f %.2f %.2f; prsa %.2f %.2f %.2f", pelvisAt1.x, pelvisAt1.y, pelvisAt1.z,
                pelvisAt2.x, pelvisAt2.y, pelvisAt2.z, chestAt1.x, chestAt1.y, chestAt1.z));

        const Warp::Entity* hand = stage.get(stage.find("/KimodoWalk/Pelvis/Chest/Hand"));
        report.check("zglob koji se pomice ima kljuc u svakom kadru",
            hand && hand->translationKeys.size() == 3 && stage.get(chest)->translationKeys.empty() &&
            distance(stage.localAt(hand->id, 3.0).translation, glm::vec3(0.3f, 0.2f, 0.0f)) < 1e-6f,
            hand ? fmt("saka %zu kljuca, u kadru 3 na mjestu mirovanja", hand->translationKeys.size()) : "nema sake");
        report.check("visina kostura u mirovanju", std::fabs(imported.height - 1.7f) < 1e-5f, fmt("%.3f", imported.height));
    }

    //-- 2. scena iz matchmovea: vrijeme scene, mjesto i mjerilo ------------------------------------
    {
        Warp::Stage stage;
        stage.startFrame = 1.0; stage.endFrame = 381.0; stage.framesPerSecond = 50.0;
        Loom::MotionPlacement placement;
        placement.startFrame = 100.0;
        placement.sceneFps = 50.0;
        placement.position = glm::vec3(3.0f, -0.5f, 7.0f);
        placement.scale = 2.0f;
        const Loom::WeaverMotionImportReport imported = Loom::importWeaverMotionClip(stage, clip, "Lik", placement);
        const Warp::Entity* pelvis = stage.get(imported.root);
        const double second = pelvis && pelvis->rotationKeys.size() > 1 ? pelvis->rotationKeys.times[1] : -1.0;
        report.check("klip od 30 fps u sceni od 50: kadar klipa = 5/3 kadra scene, timeline ostaje",
            std::fabs(second - (100.0 + 50.0 / 30.0)) < 1e-9 && stage.endFrame == 381.0 && stage.framesPerSecond == 50.0 &&
            std::fabs(imported.lastFrame - (100.0 + 2.0 * 50.0 / 30.0)) < 1e-9,
            fmt("drugi kljuc u %.4f, klip do %.2f; timeline %.0f-%.0f @ %.0f", second, imported.lastFrame,
                stage.startFrame, stage.endFrame, stage.framesPerSecond));

        const glm::vec3 start = glm::vec3(stage.worldMatrix(imported.root, 100.0)[3]);
        report.check("lik pocinje na mjestu postavljanja, s mjerilom",
            distance(start, glm::vec3(3.0f, -0.5f + 2.0f * 1.2f, 7.0f)) < 1e-5f,
            fmt("korijen u (%.3f, %.3f, %.3f)", start.x, start.y, start.z));
    }

    //-- prazan klip se odbija ----------------------------------------------------------------------
    {
        Engine::WeaverMotion::Clip empty;
        Warp::Stage badStage;
        const Loom::WeaverMotionImportReport bad = Loom::importWeaverMotionClip(badStage, empty, "Bad");
        report.check("prazan klip ne stvara nista", !bad.problem.empty() && badStage.size() == 0, bad.problem);
    }

    return report.result();
}
