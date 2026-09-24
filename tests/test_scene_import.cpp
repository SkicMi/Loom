// Rezultat solvea ulazi u scenu: kamera kroz vrijeme, tocke, uspravnost kao transformacija grupe.
//
// ZASTO SE OVO TESTIRA. Ovo je most izmedju solvera i editora. Ako promasi, sve u editoru
// izgleda uredno - kamera se krece, tocke stoje - samo kocka stavljena na pod ne stoji na podu
// snimke. Tri stvari mogu promasiti bez ijednog znaka:
//
//   1. USD MATRICA JE PO RETCIMA. Procitana po stupcima, kamera stoji u ishodistu i vrti se oko
//      njega - krug kroz Engineov izvoz to hvata
//   2. VRIJEME. Izvoz pise kadar snimke k kao timeCode 1 + k * korak; ako uvoz broji kljuceve
//      redom, sve se giba korak puta prebrzo
//   3. USPRAVNOST. Grupa mora zakrenuti i kameru i tocke ISTOM transformacijom, inace kamera
//      gleda pokraj scene
#include "TestHarness.h"

#include "../src/LoomScene.h"

#include "Engine/ColmapExport.h"
#include "Engine/UsdExport.h"

#include <filesystem>

namespace{

struct Scene{
    Engine::Reconstruction reconstruction;
    std::vector<Engine::Observation> observations;
    std::vector<std::string> names;
};

//Kamere na luku oko kvadra tocaka, vodoravne - pa sve nagnuto 25 st, kao kad je prva kamera
//bila nagnuta
Scene tiltedScene(){
    Scene scene;
    const glm::quat tilt = glm::angleAxis(glm::radians(25.0f), glm::normalize(glm::vec3(1.0f, 0.2f, 0.3f)));
    for(int i = 0; i < 1500; ++i){
        const float a = float(i) * 0.7919f;
        scene.reconstruction.points.push_back(tilt * glm::vec3(std::sin(a) * 2.0f, std::cos(a * 1.7f) * 1.2f,
                                                               std::sin(a * 0.3f) * 2.0f));
        scene.reconstruction.solved.push_back(1);
    }
    for(int i = 0; i < 20; ++i){
        const float a = -0.6f + float(i) * 0.06f;
        const glm::vec3 position(std::sin(a) * 6.0f, 0.5f, std::cos(a) * 6.0f);
        Engine::Pose pose;
        pose.position = tilt * position;
        pose.orientation = glm::normalize(tilt * glm::quatLookAt(glm::normalize(-position), glm::vec3(0, 1, 0)));
        scene.reconstruction.poses.push_back(pose);
        scene.reconstruction.posed.push_back(1);
        scene.names.push_back(fmt("frame_%04d.png", i));
    }
    for(uint32_t point = 0; point < scene.reconstruction.points.size(); ++point){
        for(uint32_t camera = point % 20; camera < 20; camera += 6){
            Engine::Observation observation;
            observation.camera = camera;
            observation.point = point;
            scene.observations.push_back(observation);
        }
    }
    scene.reconstruction.posedCameras = 20;
    scene.reconstruction.solvedPoints = uint32_t(scene.reconstruction.points.size());
    return scene;
}

}

int main(){
    TestReport report("W2 rezultat solvea u sceni");
    namespace fs = std::filesystem;

    const fs::path directory = fs::temp_directory_path() / "loom_uvoz_test" / "snimka_loom";
    fs::remove_all(directory.parent_path());
    fs::create_directories(directory);

    const Scene scene = tiltedScene();
    Engine::Intrinsics intrinsics;
    intrinsics.fx = intrinsics.fy = 1500.0f;
    intrinsics.cx = 960.0f; intrinsics.cy = 540.0f;
    intrinsics.width = 1920; intrinsics.height = 1080;
    Engine::writeColmapText(directory.string(), scene.reconstruction, intrinsics, scene.observations, scene.names);

    //Svaki treci kadar snimke, kao VideoSolve kad pune slicice nisu uspjele
    Engine::UsdExportConfig usdConfig;
    usdConfig.firstFrame = 1;
    usdConfig.frameStep = 3;
    usdConfig.framesPerSecond = 50.0;
    Engine::writeUsdScene((directory / "kamera.usda").string(), scene.reconstruction, intrinsics, 1920, 1080, {}, usdConfig);

    //-- 1. USD krug: Engine pise, Warp cita ---------------------------------------------------
    {
        Warp::UsdCamera usd;
        const bool read = Warp::readUsdCamera((directory / "kamera.usda").string(), usd);
        float worst = 0.0f, transposed = 0.0f;
        for(size_t i = 0; read && i < usd.times.size() && i < 20; ++i){
            const Engine::Pose& pose = scene.reconstruction.poses[i];
            worst = std::max(worst, glm::length(glm::vec3(usd.transforms[i][3]) - pose.position));
            const glm::mat3 axes = glm::mat3(usd.transforms[i]);
            worst = std::max(worst, glm::length(axes * glm::vec3(0, 0, -1) - pose.orientation * glm::vec3(0, 0, -1)));
            //Po stupcima umjesto po retcima: polozaj bi bio donji redak, a on je (0, 0, 0, 1)
            const glm::mat4 wrong = glm::transpose(usd.transforms[i]);
            transposed = std::max(transposed, glm::length(glm::vec3(wrong[3]) - pose.position));
        }
        report.check("kamera.usda se cita natrag u iste poze",
            read && usd.times.size() == 20 && worst < 1e-4f && transposed > 1.0f,
            fmt("%zu uzoraka, najveca razlika %.2e; procitano po stupcima promasilo bi %.2f",
                usd.times.size(), worst, transposed));
        report.check("vrijeme je vrijeme snimke", read && usd.times[1] == 4.0 && usd.times.back() == 58.0 &&
                                                  usd.framesPerSecond == 50.0,
            fmt("uzorci na %.0f, %.0f ... %.0f, %.0f fps", usd.times[0], usd.times[1], usd.times.back(), usd.framesPerSecond));
    }

    //-- 2. uvoz: stablo, kljucevi, objektiv ---------------------------------------------------
    Warp::Stage stage;
    const Loom::ImportReport imported = Loom::importResult(stage, directory, "/snimke/snimka.mp4");
    {
        const Warp::Entity* camera = stage.get(imported.camera);
        report.check("grana je /snimka s kamerom i tockama",
            imported.problem.empty() && stage.path(imported.camera) == "/snimka/Camera" &&
            stage.path(imported.points) == "/snimka/Points" && imported.splat == Warp::None,
            stage.path(imported.camera) + ", " + stage.path(imported.points));
        report.check("kamera nosi objektiv i snimku",
            camera && camera->camera && camera->camera->focalPixels == 1500.0f && camera->camera->width == 1920 &&
            camera->camera->plate == "/snimke/snimka.mp4",
            fmt("zarisna %.0f px, %u px siroko", camera ? camera->camera->focalPixels : 0.0f,
                camera ? camera->camera->width : 0u));
        report.check("kljuc na svakom uzorku, timeline preuzme raspon",
            imported.fromUsd && imported.cameraKeys == 20 && stage.startFrame == 1.0 && stage.endFrame == 58.0 &&
            stage.framesPerSecond == 50.0,
            fmt("%zu kljuceva, kadrovi %.0f-%.0f", imported.cameraKeys, stage.startFrame, stage.endFrame));
    }

    //-- 3. uspravnost: grupa zakrene kameru i tocke jednako -------------------------------------
    {
        float worstRight = 0.0f;
        for(int i = 0; i < 20; ++i){
            const glm::mat4 world = stage.worldMatrix(imported.camera, 1.0 + 3.0 * i);
            worstRight = std::max(worstRight, std::fabs(glm::normalize(glm::vec3(world[0])).y));
        }
        report.check("kamere u sceni imaju vodoravan horizont",
            imported.upright && worstRight < 2e-3f && std::fabs(imported.tiltDegrees - 24.55f) < 0.1f,
            fmt("najveci nagib desne osi %.2e, grupa ispravila %.2f st", worstRight, imported.tiltDegrees));

        //Kamera i tocke moraju ostati jedna prema drugoj kako su bile: tocka projicirana kroz
        //kameru u sceni = ista tocka kroz kameru iz solvea
        const glm::mat4 group = stage.worldMatrix(imported.group, 1.0);
        const glm::mat4 camera = stage.worldMatrix(imported.camera, 1.0);
        const glm::vec3 point = scene.reconstruction.points[7];
        const glm::vec3 inScene = glm::vec3(glm::inverse(camera) * group * glm::vec4(point, 1.0f));
        const glm::vec3 inSolve = glm::conjugate(scene.reconstruction.poses[0].orientation) *
                                  (point - scene.reconstruction.poses[0].position);
        report.check("tocka kroz kameru ostaje ista", glm::length(inScene - inSolve) < 1e-4f,
            fmt("razlika %.2e", glm::length(inScene - inSolve)));
    }

    //-- 3b. pod na nuli: donjih 5 % tocaka ispod y = 0 ---------------------------------------
    {
        const glm::mat4 group = stage.worldMatrix(imported.group, 1.0);
        size_t below = 0;
        for(const glm::vec3& p : scene.reconstruction.points){
            if((group * glm::vec4(p, 1.0f)).y < 0.0f) ++below;
        }
        const float share = float(below) / float(scene.reconstruction.points.size());
        report.check("pod scene je na y = 0", share > 0.03f && share < 0.07f,
            fmt("%.1f %% tocaka ispod nule", 100.0f * share));
    }

    //-- 4. izmedju kljuceva kamera je izmedju polozaja ------------------------------------------
    {
        const glm::vec3 a(stage.worldMatrix(imported.camera, 4.0)[3]);
        const glm::vec3 b(stage.worldMatrix(imported.camera, 7.0)[3]);
        const glm::vec3 middle(stage.worldMatrix(imported.camera, 5.5)[3]);
        report.check("kadar izmedju kljuceva je izmedju polozaja", glm::length(middle - 0.5f * (a + b)) < 1e-4f,
            fmt("odstupanje %.2e", glm::length(middle - 0.5f * (a + b))));
    }

    //-- 5. bez kamera.usda: kljucni kadrovi redom -----------------------------------------------
    {
        fs::remove(directory / "kamera.usda");
        Warp::Stage old;
        const Loom::ImportReport fallback = Loom::importResult(old, directory);
        report.check("stara mapa bez usda: kljucevi redom od 1",
            !fallback.fromUsd && fallback.cameraKeys == 20 && old.endFrame == 20.0 && fallback.upright,
            fmt("%zu kljuceva, do kadra %.0f", fallback.cameraKeys, old.endFrame));

        Warp::Stage empty;
        const Loom::ImportReport nothing = Loom::importResult(empty, directory.parent_path() / "nema");
        report.check("mapa bez rezultata ne stvara nista", !nothing.problem.empty() && empty.size() == 0,
            nothing.problem);
    }

    fs::remove_all(directory.parent_path());
    return report.result();
}
