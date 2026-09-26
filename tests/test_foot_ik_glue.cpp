// Foot IK na pravom liku i pravom Kimodovom klipu: stopalo u zraku ne smije biti zakucano na pod.
//
// Korisnik: "zbog feet constrainta kad skace se noge zalijepe za pod". Izmjereno na Mascot_Manny +
// motion_1790369311054_00 (trci, vadi pistolj, baca se na pod): stari IK je lijevo stopalo drzao na
// podu 13 kadrova u kojima je bez IK-a bilo do 10 cm iznad njega; na 6 od 8 commitanih varijanti
// skoka bilo je lijepljenja. Uzrok: prag dodira ~6 cm i zakljucavanje koje visinu stavlja NA POD.
//
// Test usporedi isti klip uvezen s IK-om i bez njega:
//   zalijepljeno   kadrovi u kojima je stopalo bez IK-a > 5 cm iznad poda, a s IK-om < 2 cm - mora 0
//   klizanje       vodoravna brzina stopala uz pod (kao MotionQuality) - IK je ne smije povecati
#include "TestHarness.h"
#include "../src/LoomModel.h"
#include "../src/LoomMotionPanel.h"
#include "../src/LoomWeaverMotion.h"

#include <Spool/Gltf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace{

struct Imported{ Warp::Stage stage; Warp::Id feet[2] = {Warp::None, Warp::None}; double first = 1.0, last = 1.0; bool ok = false; };

Imported importTake(const std::filesystem::path& root, bool footIk){
    Imported r;
    Spool::GltfScene scene;
    std::string error;
    if(!Spool::loadGltf((root / "WeaverMotion/Mascot_Manny.glb").string(), scene, error)) return r;
    Loom::importGltf(r.stage, scene);
    const auto characters = Loom::motionCharactersIn(r.stage);
    Engine::WeaverMotion::Clip clip;
    if(characters.empty() || !Engine::WeaverMotion::readKimodoBvh(
           (root / "WeaverMotion/motion_1790369311054/motion_1790369311054_00.bvh").string(), clip, error)) return r;
    r.stage.framesPerSecond = 30.0;
    Loom::MotionPlacement placement;
    placement.parent = characters.front().id;
    placement.fitToParentRig = true;
    placement.sceneFps = 30.0;
    placement.startFrame = 1.0;
    placement.footContactIK = footIk;
    const Loom::WeaverMotionImportReport report = Loom::importWeaverMotionClip(r.stage, clip, "take", placement);
    r.first = report.firstFrame;
    r.last = report.lastFrame;
    r.stage.walk([&](const Warp::Entity& e, int){
        if(e.name == "foot_l") r.feet[0] = e.id;
        if(e.name == "foot_r") r.feet[1] = e.id;
    });
    r.ok = report.problem.empty() && r.feet[0] != Warp::None && r.feet[1] != Warp::None;
    return r;
}

float lowest(const Imported& r, Warp::Id foot){
    float low = 1e9f;
    for(double t = r.first; t <= r.last; t += 1.0) low = std::min(low, r.stage.worldMatrix(foot, t)[3].y);
    return low;
}

double skate(const Imported& r, Warp::Id foot, float floor){
    double sum = 0.0;
    int samples = 0;
    for(double t = r.first + 1.0; t <= r.last; t += 1.0){
        const glm::vec3 a(r.stage.worldMatrix(foot, t - 1.0)[3]), b(r.stage.worldMatrix(foot, t)[3]);
        const float h = std::min(a.y, b.y) - floor;
        if(h >= 0.03f) continue;
        const float w = std::clamp(2.0f - std::pow(2.0f, std::max(0.0f, h) / 0.03f), 0.0f, 1.0f);
        sum += glm::length(glm::vec2(b.x - a.x, b.z - a.z)) * 30.0 * 100.0 * w;
        ++samples;
    }
    return samples ? sum / samples : 0.0;
}

}

int main(){
    TestReport report("foot_ik_glue");
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const Imported free = importTake(root, false), ik = importTake(root, true);
    if(!free.ok || !ik.ok){
        report.check("Manny i take ucitani", false, "WeaverMotion/Mascot_Manny.glb ili BVH nedostaje");
        return report.result();
    }
    const char* side[2] = {"lijevo", "desno"};
    for(int i = 0; i < 2; ++i){
        const float floorFree = lowest(free, free.feet[i]), floorIk = lowest(ik, ik.feet[i]);
        int glued = 0;
        float worst = 0.0f;
        for(double t = free.first; t <= free.last; t += 1.0){
            const float up = free.stage.worldMatrix(free.feet[i], t)[3].y - floorFree;
            const float held = ik.stage.worldMatrix(ik.feet[i], t)[3].y - floorIk;
            if(up > 0.05f && held < 0.02f){ ++glued; worst = std::max(worst, up); }
        }
        const std::string what = std::string(side[i]) + " stopalo: nijedan kadar u zraku zakucan na pod";
        report.check(what.c_str(), glued == 0, fmt("%d kadrova, do %.0f cm (stari IK: lijevo 13 kadrova do 10 cm)", glued, double(worst) * 100.0));
        const double without = skate(free, free.feet[i], floorFree), with = skate(ik, ik.feet[i], floorIk);
        const std::string slide = std::string(side[i]) + " stopalo: IK ne povecava klizanje";
        report.check(slide.c_str(), with <= without + 0.05, fmt("bez IK-a %.1f cm/s, s IK-om %.1f cm/s", without, with));
    }
    return report.result();
}
