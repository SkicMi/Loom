// Camera solve, nacrtan: prave kamere i tocke protiv onih koje je solver nasao.
//
// Sve dosad je mjereno brojkama. Ovo je ista stvar za oko: sinteticka scena se rijesi SAMO iz
// opazanja (nijedna poza se ne daje), pa se rjesenje nacrta preko istine. Ako se poklapa, vidi se;
// ako je poza promasila, vidi se jos lakse.
//
//   ./SolveViewer                 <- prozor, scena se polako okrece
//   ./SolveViewer 40 solve.png    <- 40 kadrova pa snimka i kraj
//   ./SolveViewer 40 solve.png 50 <- i razlika uvecana 50 puta
//
// DVIJE ODLUKE O CRTANJU, obje su prikaz a ne rezultat:
//
//   prave kamere su podignute za 0.6 m   inace ih rijesene ne bi skrivale nego bi rijesene
//                                        nestale u njima: poklapaju se na tri milimetra, pa veci
//                                        objekt proguta manji i slika izgleda tocno i kad nije
//   razlika se smije uvecati             greska od 3 mm na sceni od 8 m je manja od piksela.
//                                        Faktor 50 je cini vidljivom, i tek tada slika razlikuje
//                                        dobar solve od losega
//
// PORAVNANJE PRIJE CRTANJA. Rekonstrukcija je tocna do dvije stvari koje se iz slika ne mogu
// doznati: gdje je scena u prostoru i koliko je velika. Prva kamera solvera je u ishodistu, a
// pomak pocetnog para je jedinicni - pa se rjesenje prvo vrati u sustav prave prve kamere i
// pomnozi mjerilom. To NIJE popravljanje rezultata nego uklanjanje onoga sto nikad nije ni bilo
// odredjeno; sve ostalo mora se poklopiti samo.
//
// Stepenica 1: jedini Loomov include je <Loom/Loom.h>, bez ijednog Vulkan tipa.
#include <Loom/Loom.h>

#include <Engine/Reconstruct.h>
#include <Engine/SyntheticScene.h>

#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace{

//Jednobojna tekstura, jer stepenica 1 boju daje teksturom. Cetiri piksela su dovoljna
Loom::TextureHandle colour(Loom::Scene& scene, uint8_t r, uint8_t g, uint8_t b){
    const uint8_t pixels[16] = {r,g,b,255, r,g,b,255, r,g,b,255, r,g,b,255};
    return scene.createTexture(pixels, 2, 2);
}

glm::mat4 poseMatrix(const Engine::Pose& pose, float size){
    return glm::translate(glm::mat4(1.0f), pose.position) * glm::mat4_cast(pose.orientation) *
           glm::scale(glm::mat4(1.0f), glm::vec3(size));
}

}

int main(int argc, char** argv){
    const uint32_t framesThenShot = argc > 1 ? uint32_t(std::atoi(argv[1])) : 0;
    const std::string shotName = argc > 2 ? std::string(argv[2]) : std::string("solve.png");
    const float exaggeration = argc > 3 ? float(std::atof(argv[3])) : 1.0f;

    // -------------------------------------------------------------------------------
    // Scena, pa rjesenje iz samih opazanja
    // -------------------------------------------------------------------------------

    Engine::SyntheticConfig config;
    config.noisePixels = 0.5f;
    const Engine::SyntheticScene truth = Engine::makeSyntheticScene(config);

    printf("Rjesavam %zu kamera i %zu tocaka iz %zu opazanja, bez ijedne zadane poze...\n",
           truth.poses.size(), truth.points.size(), truth.observations.size());

    const Engine::Reconstruction state = Engine::reconstruct(truth.observations, truth.poses.size(),
                                                             truth.points.size(), truth.intrinsics);

    printf("  rijeseno %u kamera i %u tocaka, reprojekcija %.3f px\n",
           state.posedCameras, state.solvedPoints, state.medianReprojection);

    //Mjerilo i sustav prve kamere - vidi zaglavlje
    const Engine::Pose& origin = truth.poses[0];
    size_t farthest = 1;
    for(size_t i = 1; i < truth.poses.size(); ++i){
        if(!state.posed[i]) continue;
        if(glm::length(truth.poses[i].position - origin.position) >
           glm::length(truth.poses[farthest].position - origin.position)) farthest = i;
    }
    const float truthBaseline = glm::length(truth.poses[farthest].position - origin.position);
    const float solvedBaseline = glm::length(state.poses[farthest].position);
    const float scale = solvedBaseline > 0.0f ? truthBaseline / solvedBaseline : 1.0f;

    auto intoWorld = [&](const glm::vec3& point){
        return origin.position + origin.orientation * (point * scale);
    };
    auto poseIntoWorld = [&](const Engine::Pose& pose){
        Engine::Pose out;
        out.position = intoWorld(pose.position);
        out.orientation = glm::normalize(origin.orientation * pose.orientation);
        return out;
    };

    // -------------------------------------------------------------------------------
    // I sad isto to, za oko
    // -------------------------------------------------------------------------------

    Loom::Scene scene(framesThenShot > 0 ? Loom::Preset::Offscreen : Loom::Preset::Lit3D);
    scene.setTitle("Loom - camera solve");
    scene.setSize(1280, 720);
    scene.setClearColor({0.05f, 0.06f, 0.08f, 1.0f});

    const Loom::TextureHandle truthColour = colour(scene, 90, 220, 120);    //zeleno: istina
    const Loom::TextureHandle solvedColour = colour(scene, 250, 160, 60);   //narancasto: rjeseno
    const Loom::TextureHandle pointColour = colour(scene, 120, 160, 230);   //plavo: tocke solvera
    const Loom::TextureHandle truthPointColour = colour(scene, 150, 150, 150);

    scene.sun().setDirection({-0.4f, -1.0f, -0.3f});
    scene.environment().setAmbient({0.18f, 0.19f, 0.22f});

    //Kamera gleda scenu iz daljine; sve je oko ishodista jer je i sinteticka scena takva
    float angle = 0.6f;
    uint32_t drawn = 0;

    //Vidi zaglavlje: podizanje je prikaz, ne rezultat
    const glm::vec3 lift(0.0f, 0.6f, 0.0f);
    printf("  prave kamere i tocke su podignute za %.1f m; razlika je uvecana %.0fx\n",
           double(lift.y), double(exaggeration));

    while(scene.isRunning()){
        if(framesThenShot == 0) angle = 0.6f + 0.25f * scene.time();

        scene.camera().setPosition({14.0f * std::sin(angle), 7.0f, 14.0f * std::cos(angle)});
        scene.camera().lookAt({0.0f, 0.0f, 0.0f});

        scene.startRendering();

            //Tocke: prave sive i podignute, rijesene plave na svom mjestu
            for(size_t i = 0; i < truth.points.size(); i += 3){
                scene.drawCube(truthPointColour, Loom::Transform().at(truth.points[i] + lift).scaled(0.04f));
                if(!state.solved[i]) continue;
                const glm::vec3 solvedPoint = truth.points[i] + (intoWorld(state.points[i]) - truth.points[i]) * exaggeration;
                scene.drawCube(pointColour, Loom::Transform().at(solvedPoint).scaled(0.04f));
            }

            //Kamere: piramida pokazuje kamo gleda
            for(size_t i = 0; i < truth.poses.size(); ++i){
                scene.drawPyramid(truthColour, poseMatrix(Engine::Pose{truth.poses[i].position + lift, truth.poses[i].orientation}, 0.3f));
                if(!state.posed[i]) continue;

                const Engine::Pose solved = poseIntoWorld(state.poses[i]);
                Engine::Pose shown;
                shown.position = truth.poses[i].position + (solved.position - truth.poses[i].position) * exaggeration;
                shown.orientation = glm::normalize(glm::slerp(truth.poses[i].orientation, solved.orientation, exaggeration));
                scene.drawPyramid(solvedColour, poseMatrix(shown, 0.3f));
            }

        scene.endRendering();

        if(framesThenShot > 0 && ++drawn >= framesThenShot){
            Loom::Sequence sequence;
            sequence.setDirectory(".");
            sequence.setPrefix(shotName.substr(0, shotName.find_last_of('.')));
            const std::string written = sequence.write(scene);
            printf("Snimljeno %s\n", written.c_str());
            break;
        }
    }
    return 0;
}
