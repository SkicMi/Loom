// S0: sinteticka scena s poznatim odgovorom - temelj cijele faze S.
//
// Solver se ne moze mjeriti na pravoj snimci prije nego se zna da mjera radi. Reprojekcija koja
// je mala ne znaci da je kamera na pravom mjestu: solver moze pomaknuti i kameru i tocke tako da
// se greska smanji a scena bude kriva. Zato prvo scena u kojoj je tocan odgovor poznat.
//
// Sto se ovdje brani:
//
//   konvencija         Engineova projekcija mora dati ISTI piksel kao Loomova Camera +
//                      projectToPixels. Faza S vraca poze koje ce Loom crtati; razlika u
//                      konvenciji pokazala bi se tek kao scena naopako, mnogo kasnije
//   tocan odgovor      bez suma reprojekcija istinitih poza i tocaka mora biti nula do
//                      zaokruzivanja - inace mjera laze u svoju korist
//   mjera grize        pomak kamere za milimetar i za stotinku stupnja MORA se vidjeti. Mjera
//                      koja to prespava ne moze razlikovati dobar solver od lijenog
//   sum je onakav      zadana sigma mora se pojaviti u opazanjima: medijan duljine 2D Gaussovog
//     kakav je trazen  suma je sigma*sqrt(2 ln 2) = 1.177 sigma, i to se provjerava brojem
//   ista scena         isto sjeme daje iste bajtove, drugo sjeme drugu scenu
//   pokrivenost        svaka tocka mora biti vidjena iz barem dvije kamere, inace se ne da
//                      triangulirati, a svaka kamera mora vidjeti dovoljno tocaka
#include "TestHarness.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Core/Splat.h"

#include <Engine/SyntheticScene.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

int main(){
    TestReport report("S0 sinteticka scena");

    // -------------------------------------------------------------------------------
    // Konvencija: isti piksel kao Loomova kamera
    // -------------------------------------------------------------------------------

    {
        const uint32_t width = 640, height = 480;

        CameraConfig cameraConfig;
        cameraConfig.fovY = glm::radians(50.0f);
        cameraConfig.nearPlane = 0.1f;
        cameraConfig.farPlane = 100.0f;
        cameraConfig.position = glm::vec3(2.5f, 1.5f, 6.0f);
        cameraConfig.target = glm::vec3(0.3f, -0.2f, 0.0f);
        cameraConfig.up = glm::vec3(0.0f, 1.0f, 0.0f);
        Camera camera(cameraConfig);

        const glm::mat4 view = camera.getView();
        const CameraIntrinsics loomIntrinsics = CameraIntrinsics::fromProjection(
            camera.getProjection(width, height), width, height);

        //Ista poza, izrazena kako je Engine zapisuje
        Engine::Pose pose;
        pose.position = cameraConfig.position;
        pose.orientation = camera.getOrientation();

        //fy je u Loomu negativan jer Vulkan okrece Y; Engine to nosi kao minus u samoj projekciji
        Engine::Intrinsics intrinsics;
        intrinsics.fx = loomIntrinsics.fx;
        intrinsics.fy = std::fabs(loomIntrinsics.fy);
        intrinsics.cx = loomIntrinsics.cx;
        intrinsics.cy = loomIntrinsics.cy;
        intrinsics.width = width;
        intrinsics.height = height;

        double worst = 0.0;
        size_t compared = 0;
        for(float x = -2.0f; x <= 2.0f; x += 0.5f){
            for(float y = -1.5f; y <= 1.5f; y += 0.5f){
                for(float z = -2.0f; z <= 2.0f; z += 1.0f){
                    const glm::vec3 point(x, y, z);
                    glm::vec2 mine;
                    if(!Engine::project(pose, intrinsics, point, mine)) continue;

                    const glm::vec2 loom = SplatMath::projectToPixels(point, view, loomIntrinsics.fx,
                                                                      loomIntrinsics.fy, loomIntrinsics.cx,
                                                                      loomIntrinsics.cy);
                    worst = std::max(worst, double(glm::length(mine - loom)));
                    ++compared;
                }
            }
        }

        report.check("ista konvencija kao Loomova kamera", compared > 50 && worst < 1e-3,
            fmt("najveca razlika %.2e piksela kroz %zu tocaka", worst, compared));
    }

    // -------------------------------------------------------------------------------
    // Scena bez suma: tocan odgovor je tocno tocan
    // -------------------------------------------------------------------------------

    Engine::SyntheticConfig config;
    const Engine::SyntheticScene clean = Engine::makeSyntheticScene(config);

    report.check("scena je scena",
        clean.observations.size() > 3 * clean.points.size() && clean.poses.size() == config.cameraCount,
        fmt("%zu tocaka, %zu kamera, %zu opazanja", clean.points.size(), clean.poses.size(),
            clean.observations.size()));

    {
        double worst = 0.0;
        for(const Engine::Observation& observation : clean.observations){
            glm::vec2 pixel;
            Engine::project(clean.poses[observation.camera], clean.intrinsics, clean.points[observation.point], pixel);
            worst = std::max(worst, double(glm::length(pixel - observation.pixel)));
        }
        const double median = Engine::medianReprojection(clean, clean.poses, clean.points);

        report.check("bez suma je reprojekcija nula", worst < 1e-4 && median < 1e-4,
            fmt("najgore opazanje %.2e px, medijan %.2e px", worst, median));
    }

    // -------------------------------------------------------------------------------
    // Mjera mora gristi: milimetar i stotinka stupnja
    // -------------------------------------------------------------------------------

    {
        //PO KAMERI KOJA JE POMAKNUTA, i to je pouka koju je ovaj test dao u prvoj verziji:
        //mjereno preko svih osam kamera, pomak jedne dao je medijan 0.0000 px, jer sedam
        //neporemecenih drzi sredinu. Mjera nije bila kriva - gledao sam je na krivom mjestu
        std::vector<Engine::Pose> moved = clean.poses;
        moved[0].position.x += 0.001f;   //milimetar
        const double afterMillimetre = Engine::medianReprojection(clean, moved, clean.points, 0);
        const double elsewhere = Engine::medianReprojection(clean, moved, clean.points, 1);

        std::vector<Engine::Pose> turned = clean.poses;
        turned[0].orientation = glm::normalize(glm::angleAxis(glm::radians(0.01f), glm::vec3(0, 1, 0)) * turned[0].orientation);
        const double afterHundredth = Engine::medianReprojection(clean, turned, clean.points, 0);

        std::vector<glm::vec3> nudged = clean.points;
        for(glm::vec3& point : nudged) point.x += 0.001f;
        const double afterPoints = Engine::medianReprojection(clean, clean.poses, nudged);

        //Na 8 m i sa zaristem od 600 px, milimetar je oko 0.075 px a stotinka stupnja oko 0.1 px
        report.check("mjera grize na milimetar i na stotinku stupnja",
            afterMillimetre > 0.01 && afterHundredth > 0.01 && afterPoints > 0.01 && elsewhere == 0.0,
            fmt("pomaknuta kamera: milimetar %.4f px, stotinka stupnja %.4f px; nepomaknuta %.4f px; "
                "sve tocke za milimetar %.4f px",
                afterMillimetre, afterHundredth, elsewhere, afterPoints));
    }

    // -------------------------------------------------------------------------------
    // Sum je onakav kakav je trazen
    // -------------------------------------------------------------------------------

    {
        Engine::SyntheticConfig noisy = config;
        noisy.noisePixels = 0.5f;
        const Engine::SyntheticScene scene = Engine::makeSyntheticScene(noisy);

        const double median = Engine::medianReprojection(scene, scene.poses, scene.points);

        //Duljina 2D Gaussovog pomaka ima Rayleighovu razdiobu: medijan je sigma*sqrt(2 ln 2)
        const double expected = 0.5 * std::sqrt(2.0 * std::log(2.0));

        report.check("sum je onaj koji je trazen",
            median > 0.9 * expected && median < 1.1 * expected,
            fmt("medijan %.4f px, ocekivano %.4f (sigma 0.5, Rayleigh)", median, expected));

        report.check("sum ne dira tocan odgovor",
            scene.points == clean.points && scene.observations.size() == clean.observations.size(),
            fmt("%zu tocaka i %zu opazanja, isto kao bez suma", scene.points.size(), scene.observations.size()));
    }

    // -------------------------------------------------------------------------------
    // Ista scena za isto sjeme
    // -------------------------------------------------------------------------------

    {
        const Engine::SyntheticScene again = Engine::makeSyntheticScene(config);

        Engine::SyntheticConfig other = config;
        other.seed = config.seed + 1;
        const Engine::SyntheticScene different = Engine::makeSyntheticScene(other);

        const bool same = again.observations.size() == clean.observations.size() &&
                          std::memcmp(again.observations.data(), clean.observations.data(),
                                      clean.observations.size() * sizeof(Engine::Observation)) == 0;

        size_t movedPoints = 0;
        for(size_t i = 0; i < std::min(different.points.size(), clean.points.size()); ++i){
            if(different.points[i] != clean.points[i]) ++movedPoints;
        }

        report.check("isto sjeme daje istu scenu, drugo drugu",
            same && movedPoints > clean.points.size() / 2,
            fmt("isti bajtovi: %s; drugo sjeme pomaklo %zu od %zu tocaka",
                same ? "da" : "NE", movedPoints, clean.points.size()));
    }

    // -------------------------------------------------------------------------------
    // Pokrivenost: bez nje se nema sto triangulirati
    // -------------------------------------------------------------------------------

    {
        std::vector<uint32_t> perPoint(clean.points.size(), 0);
        std::vector<uint32_t> perCamera(clean.poses.size(), 0);
        bool insideImage = true;
        for(const Engine::Observation& observation : clean.observations){
            ++perPoint[observation.point];
            ++perCamera[observation.camera];
            if(observation.pixel.x < 0.0f || observation.pixel.y < 0.0f ||
               observation.pixel.x >= float(clean.intrinsics.width) ||
               observation.pixel.y >= float(clean.intrinsics.height)) insideImage = false;
        }

        const uint32_t leastSeen = *std::min_element(perPoint.begin(), perPoint.end());
        const uint32_t thinnestCamera = *std::min_element(perCamera.begin(), perCamera.end());

        report.check("svaka tocka je vidjena iz barem dvije kamere, svaka kamera vidi dovoljno",
            leastSeen >= 2 && thinnestCamera > clean.points.size() / 2 && insideImage,
            fmt("najmanje vidjena tocka %u kamera, najsiromasnija kamera %u tocaka, sve unutar slike: %s",
                leastSeen, thinnestCamera, insideImage ? "da" : "NE"));
    }

    return report.result();
}
