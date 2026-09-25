// LoomPlateFloor: pod iz riješenog oblaka, metar iz kamere, klizanje stopala po snimci.
//
// Sinteticka scena s poznatim odgovorom: pod na y = 0.3 (sum 3 mm), zid, stol daleko od lika i
// sum ispod poda - dvije stvari koje "5 % najnizih" i "najveci vrh" hvataju krivo. Kamera ide
// vodoravno 5 cm po kadru na visini 3.0 nad podom, pa je metar (uz snimatelja od 1.5 m) tocno
// 2 jedinice. Negativna kontrola za zakljucanost: stopalo koje stoji mora dati nulu iako se kamera
// mice, a stopalo koje klizi 1 cm po kadru mora dati piksele.
#include "TestHarness.h"

#include "../src/LoomPlateFloor.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <random>

int main(){
    TestReport report("plate_floor");

    Warp::Stage stage;
    const Warp::Id group = stage.create("Solve");
    const Warp::Id pointsId = stage.create("Points", group);
    std::mt19937 random(7);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::vector<glm::vec3> points;
    for(int i = 0; i < 3000; ++i) points.push_back({2.0f * unit(random), 0.3f + 0.003f * unit(random), 2.0f * unit(random)});
    for(int i = 0; i < 2500; ++i) points.push_back({2.0f, 0.3f + 1.1f + 1.1f * unit(random), 2.0f * unit(random)});
    for(int i = 0; i < 400; ++i) points.push_back({8.0f + 0.3f * unit(random), 1.0f, 8.0f + 0.3f * unit(random)});
    for(int i = 0; i < 80; ++i) points.push_back({2.0f * unit(random), -0.1f + 0.2f * unit(random), 2.0f * unit(random)});
    stage.get(pointsId)->points = Warp::Points{points, {}};

    const Warp::Id cameraId = stage.create("Camera", group);
    Warp::Entity& camera = *stage.get(cameraId);
    camera.camera = Warp::Camera{};
    for(int f = 1; f <= 30; ++f){
        const glm::vec3 at(-0.75f + 0.05f * float(f), 3.3f, 3.0f);
        camera.translationKeys.set(double(f), at);
        camera.rotationKeys.set(double(f), glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.3f, -1.0f) - at), glm::vec3(0, 1, 0)));
    }

    //-- pod ----------------------------------------------------------------------------------------
    const Loom::PlateFloor floor = Loom::findPlateFloor(stage, 1.0, glm::vec2(0.0f), 3.0f);
    report.check("pod je ploha, ne najniza tocka ni zid", floor.valid && std::fabs(floor.height - 0.3f) < 0.01f,
                 fmt("y %.4f, raspon %.4f, %zu tocaka", double(floor.height), double(floor.spread), floor.support));
    report.check("kamera iznad poda", floor.camera == cameraId && std::fabs(floor.cameraHeight - 3.0f) < 0.02f,
                 fmt("%.3f", double(floor.cameraHeight)));
    report.check("metar iz visine kamere", std::fabs(Loom::plateUnitsPerMetre(floor, 1.5f) - 2.0f) < 0.02f,
                 fmt("%.3f jedinica/m", double(Loom::plateUnitsPerMetre(floor, 1.5f))));

    //Grupa solvea smije biti skalirana: lik mora u svijetu i dalje dobiti tocno metar
    {
        Warp::Stage scaled = stage;
        scaled.get(group)->local.scale = glm::vec3(2.0f);
        const Loom::PlateFloor big = Loom::findPlateFloor(scaled, 1.0, glm::vec2(0.0f), 6.0f);
        const Warp::Id hero = scaled.create("Hero", group);
        scaled.get(hero)->local.translation = glm::vec3(0.2f, 1.5f, -0.4f);
        const bool placed = Loom::standOnPlateFloor(scaled, hero, 1.0, big, 1.5f);
        const glm::mat4 world = scaled.worldMatrix(hero, 1.0);
        const float worldScale = glm::length(glm::vec3(world[0]));
        report.check("lik na podu skalirane grupe, metar u svijetu", placed &&
                     std::fabs(world[3].y - big.height) < 1e-4f && std::fabs(worldScale - 4.0f) < 0.05f &&
                     std::fabs(world[3].x - 0.4f) < 1e-4f,
                     fmt("y %.3f (pod %.3f), mjerilo %.3f", double(world[3].y), double(big.height), double(worldScale)));
    }

    //-- lik i zakljucanost ----------------------------------------------------------------------------
    const Warp::Id hero = stage.create("Hero");
    stage.get(hero)->local.translation = glm::vec3(0.0f, 1.2f, -1.0f);
    const Warp::Id toe = stage.create("LeftToeBase", hero);
    stage.get(toe)->joint = Warp::Joint{};
    stage.get(toe)->local.translation = glm::vec3(0.1f, 0.02f, 0.0f);
    const bool stood = Loom::standOnPlateFloor(stage, hero, 1.0, floor, 1.5f);
    report.check("lik stoji na podu u metrima", stood && std::fabs(stage.worldMatrix(hero, 1.0)[3].y - floor.height) < 1e-5f &&
                 std::fabs(stage.get(hero)->local.scale.x - 2.0f) < 0.02f,
                 fmt("y %.4f, mjerilo %.3f", double(stage.worldMatrix(hero, 1.0)[3].y), double(stage.get(hero)->local.scale.x)));

    //Stoji: kamera se mice, stopalo ne - mjera mora biti nula
    Loom::PlateLockReport still = Loom::measurePlateLock(stage, hero, floor, 1.0, 30.0, 0.05f);
    report.check("nepomicno stopalo: 0 px iako se kamera mice", still.valid && still.worstPixels < 0.01f,
                 fmt("%zu dodira, najgore %.4f px", still.contacts, double(still.worstPixels)));

    //Klizi 1 cm (lokalno, 2 cm u svijetu) po kadru
    for(int f = 1; f <= 30; ++f) stage.get(toe)->translationKeys.set(double(f), glm::vec3(0.1f + 0.01f * float(f - 1), 0.02f, 0.0f));
    Loom::PlateLockReport slide = Loom::measurePlateLock(stage, hero, floor, 1.0, 30.0, 0.05f);
    report.check("stopalo koje klizi daje piksele", slide.valid && slide.worstPixels > 20.0f,
                 fmt("najgore %.1f px", double(slide.worstPixels)));

    //Sam zid (C0257, kameni zid): visine jednoliko razmazane, nijedan sloj nije istaknut - poda
    //nema, i detektor to mora reci umjesto da izabere slucajan pojas (prvi pokusaj je izabrao)
    {
        Warp::Stage wall;
        const Warp::Id id = wall.create("Points");
        std::vector<glm::vec3> bricks;
        for(int i = 0; i < 6000; ++i) bricks.push_back({3.0f * unit(random), 2.0f + 2.0f * unit(random), 0.02f * unit(random)});
        wall.get(id)->points = Warp::Points{bricks, {}};
        const Loom::PlateFloor none = Loom::findPlateFloor(wall, 1.0);
        report.check("sam zid: poda nema", !none.valid && !none.problem.empty(), none.valid ? fmt("pod na %.3f", double(none.height)) : none.problem);
    }

    //Rig s root motionom: postavlja se grupa modela iznad njega, ne rig koji Animator pregazi
    {
        Warp::Stage scene = stage;
        const Warp::Id model = scene.create("Mascot");
        const Warp::Id rig = scene.create("Armature", model);
        scene.get(rig)->translationKeys.set(1.0, glm::vec3(0.0f, 0.0f, 0.5f));
        scene.get(model)->local.translation = glm::vec3(1.0f, 2.0f, 0.0f);
        const bool placed = Loom::standOnPlateFloor(scene, rig, 1.0, floor, 1.5f);
        report.check("postavlja se grupa modela, rig zadrzi root motion", placed &&
                     Loom::characterPlacementRoot(scene, rig) == model &&
                     std::fabs(scene.worldMatrix(model, 1.0)[3].y - floor.height) < 1e-5f &&
                     scene.get(rig)->translationKeys.size() == 1,
                     fmt("grupa y %.3f", double(scene.worldMatrix(model, 1.0)[3].y)));
        report.check("grupa solvea nije lik", Loom::characterPlacementRoot(scene, pointsId) == pointsId, "");
    }

    //Bez oblaka nema poda, i to se kaze
    {
        Warp::Stage empty;
        const Loom::PlateFloor none = Loom::findPlateFloor(empty, 1.0);
        report.check("bez oblaka nema poda", !none.valid && !none.problem.empty(), none.problem);
    }
    return report.result();
}
