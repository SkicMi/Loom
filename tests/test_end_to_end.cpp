// Lanac od kraja do kraja: nacrtani kadrovi -> pracenje -> rekonstrukcija -> usporedba s istinom.
//
// Sve dosad je opazanja dobivalo iz formule. Ovdje ih daju PRAVI PIKSELI: Loom nacrta niz kadrova
// iz poznatih poza, tracker nadje i prati uglove, a solver iz tih tragova vrati poze - ne znajuci
// nista o tome odakle su slike dosle.
//
// To je posljednja provjera prije nego prava snimka udje unutra, i jedina u kojoj su istovremeno
// istina poznata I pikseli stvarni. Na pravoj snimci istine vise nema, pa se ovdje mora vidjeti
// sve sto se ima vidjeti.
//
// STO SE MJERI: rekonstrukcija je tocna do polozaja scene u prostoru i do mjerila - to se iz slika
// ne moze doznati. Oboje se ponisti prije usporedbe, isto kao u S6.
//
// GRANICA KOJU JE OVAJ TEST NASAO, i zato je i ona ovdje: tocnost prati KORAK IZMEDJU KADROVA, ne
// ukupni luk. Lucas-Kanade prati samo pomak, a kad se izmedju dva kadra promijeni i perspektiva,
// prozor se izoblici i vrh sustavno otklizne. Izmjereno na istoj sceni:
//
//   kadrova / luk    korak      rotacija   polozaj    reprojekcija
//     12 / 60 st     5.5 st     2.50 st    0.138 m    0.806 px
//     24 / 60 st     2.6 st     0.61 st    0.083 m    0.625 px
//     12 / 20 st     1.8 st     0.13 st    0.017 m    0.182 px
//     24 / 20 st     0.9 st     0.33 st    0.021 m    0.213 px
//
// Za video to nije prepreka nego opis: 25 do 60 kadrova u sekundi znaci djelic stupnja po kadru.
// Zato glavni slucaj ovdje ima razmak kakav video daje, a grubi razmak stoji kao kontrola - da se
// vidi dokle alat vrijedi. Sljedeci korak kad zatreba vise: afini Lucas-Kanade, koji prati i
// izoblicenje prozora a ne samo pomak.
#include "TestHarness.h"

#include <Loom/Loom.h>
#include "Core/CameraIntrinsics.h"

#include <Engine/Reconstruct.h>
#include <Engine/Track.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace{

const uint32_t width = 480, height = 360;

double angleBetween(const glm::quat& a, const glm::quat& b){
    const glm::dquat first = glm::normalize(glm::dquat(a));
    const glm::dquat second = glm::normalize(glm::dquat(b));
    glm::dquat difference = glm::conjugate(first) * second;
    if(difference.w < 0.0) difference = -difference;
    const double vector = std::sqrt(difference.x * difference.x + difference.y * difference.y + difference.z * difference.z);
    return glm::degrees(2.0 * std::atan2(vector, difference.w));
}

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

//Sitan sum po cijeloj teksturi: pracenju trebaju uglovi, a glatka ploha ih nema
std::vector<uint8_t> noiseTexture(uint32_t size, uint32_t seed){
    std::mt19937 random(seed);
    std::vector<uint8_t> pixels(size_t(size) * size * 4);
    for(size_t i = 0; i < size_t(size) * size; ++i){
        const uint8_t value = uint8_t(60 + random() % 180);
        pixels[i * 4 + 0] = value;
        pixels[i * 4 + 1] = uint8_t(60 + random() % 180);
        pixels[i * 4 + 2] = uint8_t(60 + random() % 180);
        pixels[i * 4 + 3] = 255;
    }
    return pixels;
}

std::vector<uint8_t> toGray(const std::vector<uint8_t>& rgba){
    std::vector<uint8_t> gray(size_t(width) * height);
    for(size_t i = 0; i < gray.size(); ++i){
        gray[i] = uint8_t(0.299f * float(rgba[i * 4 + 0]) + 0.587f * float(rgba[i * 4 + 1]) + 0.114f * float(rgba[i * 4 + 2]));
    }
    return gray;
}

}

int main(){
    TestReport report("lanac od kraja do kraja");

    Loom::Scene scene(Loom::Preset::Offscreen);
    scene.setSize(width, height);
    scene.sun().setDirection({-0.4f, -1.0f, -0.3f});
    scene.environment().setAmbient({0.35f, 0.35f, 0.38f});

    const std::vector<uint8_t> texture = noiseTexture(128, 20260913);
    const Loom::TextureHandle noisy = scene.createTexture(texture.data(), 128, 128);

    //Scena: tlo i nekoliko kocaka na razlicitim dubinama. Kocke daju paralaksu, bez koje
    //rekonstrukcija nema iz cega odrediti dubinu
    auto drawScene = [&](){
        scene.drawPlane(noisy, Loom::Transform().scaled(24.0f));
        scene.drawCube(noisy, Loom::Transform().at(-2.0f, 0.8f, -1.0f).scaled(1.6f));
        scene.drawCube(noisy, Loom::Transform().at(1.8f, 0.5f, 1.2f).scaled(1.0f));
        scene.drawCube(noisy, Loom::Transform().at(0.2f, 1.4f, -3.0f).scaled(2.2f));
        scene.drawSphere(noisy, Loom::Transform().at(2.6f, 0.9f, -2.2f).scaled(1.2f));
        scene.drawCube(noisy, Loom::Transform().at(-3.2f, 0.6f, 2.4f).scaled(1.2f));
    };

    // -------------------------------------------------------------------------------
    // Kadrovi iz poznatih poza, pa pracenje
    // -------------------------------------------------------------------------------

    Engine::TrackConfig trackConfig;
    trackConfig.window = 6;
    trackConfig.minDistance = 12.0f;
    trackConfig.maxCorners = 500;
    trackConfig.minTracks = 200;
    Engine::Tracker tracker(trackConfig);

    std::vector<Engine::Pose> truth;
    const uint32_t frames = 24;

    for(uint32_t frame = 0; frame < frames; ++frame){
        const float t = float(frame) / float(frames - 1);
        const float angle = glm::radians(-10.0f + 20.0f * t);   //0.9 st po kadru, kao na videu

        scene.camera().setPosition({8.0f * std::sin(angle), 3.2f + 0.4f * t, 8.0f * std::cos(angle)});
        scene.camera().lookAt({0.0f, 0.7f, 0.0f});

        scene.startRendering();
            drawScene();
        scene.endRendering();

        truth.push_back(Engine::Pose{scene.camera().getPosition(), scene.camera().getOrientation()});

        const std::vector<uint8_t> gray = toGray(scene.readPixels());
        tracker.addFrame(Engine::GrayImage{gray.data(), width, height, width});
    }

    report.check("tragovi su nastali iz pravih piksela",
        tracker.frameCount() == frames && tracker.trackCount() > 200 && tracker.observations().size() > 1500,
        fmt("%u kadrova, %u tragova, %zu opazanja, %u jos zivo na kraju",
            tracker.frameCount(), tracker.trackCount(), tracker.observations().size(), tracker.activeTracks()));

    // -------------------------------------------------------------------------------
    // Rekonstrukcija iz tih tragova
    // -------------------------------------------------------------------------------

    const CameraIntrinsics loomIntrinsics = CameraIntrinsics::fromProjection(
        scene.camera().getProjection(width, height), width, height);

    Engine::Intrinsics intrinsics;
    intrinsics.fx = loomIntrinsics.fx;
    intrinsics.fy = std::fabs(loomIntrinsics.fy);
    intrinsics.cx = loomIntrinsics.cx;
    intrinsics.cy = loomIntrinsics.cy;
    intrinsics.width = width;
    intrinsics.height = height;

    Engine::ReconstructConfig reconstructConfig;
    reconstructConfig.huberPixels = 2.0;
    reconstructConfig.minPointsForPose = 10;

    const Engine::Reconstruction state = Engine::reconstruct(tracker.observations(), frames,
                                                             tracker.trackCount(), intrinsics, reconstructConfig);

    //Istina u sustavu prve kamere, pa mjerilo - vidi zaglavlje
    std::vector<Engine::Pose> expected(frames);
    for(uint32_t i = 0; i < frames; ++i){
        expected[i].orientation = glm::normalize(glm::conjugate(truth[0].orientation) * truth[i].orientation);
        expected[i].position = glm::conjugate(truth[0].orientation) * (truth[i].position - truth[0].position);
    }

    uint32_t farthest = 0;
    for(uint32_t i = 0; i < frames; ++i){
        if(state.posed[i] && glm::length(expected[i].position) > glm::length(expected[farthest].position)) farthest = i;
    }
    const double scale = glm::length(expected[farthest].position) > 0.0f
        ? double(glm::length(state.poses[farthest].position)) / double(glm::length(expected[farthest].position)) : 1.0;

    std::vector<double> rotations, positions;
    for(uint32_t i = 0; i < frames; ++i){
        if(!state.posed[i]) continue;
        rotations.push_back(angleBetween(state.poses[i].orientation, expected[i].orientation));
        positions.push_back(double(glm::length(state.poses[i].position / float(scale) - expected[i].position)));
    }

    report.check("sve kamere su rijesene",
        state.ok && state.posedCameras == frames,
        fmt("%u od %u kamera, %u tocaka", state.posedCameras, frames, state.solvedPoints));

    report.check("poze se poklapaju s onima iz kojih je crtano",
        medianOf(rotations) < 0.5 && medianOf(positions) < 0.05,
        fmt("rotacija medijan %.4f st (najgora %.4f), polozaj %.4f m, mjerilo %.4f",
            medianOf(rotations), rotations.empty() ? 0.0 : *std::max_element(rotations.begin(), rotations.end()),
            medianOf(positions), scale));

    report.check("reprojekcija je na razini pracenja",
        state.medianReprojection > 0.0 && state.medianReprojection < 0.5,
        fmt("%.3f px", state.medianReprojection));

    // -------------------------------------------------------------------------------
    // Kontrola: grubi razmak izmedju kadrova mora biti vidljivo losiji
    // -------------------------------------------------------------------------------
    //
    // Ovo je granica alata izrazena kao provjera. Ako bi jednog dana gruba snimka ispala jednako
    // dobra kao fina, to ne bi znacilo da je tracker postao bolji nego da fina vise ne valja

    {
        Engine::Tracker coarse(trackConfig);
        std::vector<Engine::Pose> coarseTruth;
        const uint32_t coarseFrames = 12;

        for(uint32_t frame = 0; frame < coarseFrames; ++frame){
            const float t = float(frame) / float(coarseFrames - 1);
            const float angle = glm::radians(-30.0f + 60.0f * t);   //5.5 st po kadru
            scene.camera().setPosition({8.0f * std::sin(angle), 3.2f + 0.4f * t, 8.0f * std::cos(angle)});
            scene.camera().lookAt({0.0f, 0.7f, 0.0f});

            scene.startRendering();
                drawScene();
            scene.endRendering();

            coarseTruth.push_back(Engine::Pose{scene.camera().getPosition(), scene.camera().getOrientation()});
            const std::vector<uint8_t> gray = toGray(scene.readPixels());
            coarse.addFrame(Engine::GrayImage{gray.data(), width, height, width});
        }

        const Engine::Reconstruction rough = Engine::reconstruct(coarse.observations(), coarseFrames,
                                                                  coarse.trackCount(), intrinsics, reconstructConfig);

        std::vector<Engine::Pose> coarseExpected(coarseFrames);
        for(uint32_t i = 0; i < coarseFrames; ++i){
            coarseExpected[i].orientation = glm::normalize(glm::conjugate(coarseTruth[0].orientation) * coarseTruth[i].orientation);
            coarseExpected[i].position = glm::conjugate(coarseTruth[0].orientation) * (coarseTruth[i].position - coarseTruth[0].position);
        }
        std::vector<double> coarseRotations;
        for(uint32_t i = 0; i < coarseFrames; ++i){
            if(!rough.posed[i]) continue;
            coarseRotations.push_back(angleBetween(rough.poses[i].orientation, coarseExpected[i].orientation));
        }

        report.check("grubi razmak izmedju kadrova se vidi",
            medianOf(coarseRotations) > 3.0 * medianOf(rotations),
            fmt("5.5 st po kadru daje %.4f st, a 0.9 st po kadru %.4f st",
                medianOf(coarseRotations), medianOf(rotations)));
    }

    // -------------------------------------------------------------------------------
    // Kontrola: nepomicna kamera nema iz cega odrediti dubinu
    // -------------------------------------------------------------------------------

    {
        Engine::Tracker still(trackConfig);
        scene.camera().setPosition({0.0f, 3.2f, 8.0f});
        scene.camera().lookAt({0.0f, 0.7f, 0.0f});

        for(uint32_t frame = 0; frame < 4; ++frame){
            scene.startRendering();
                drawScene();
            scene.endRendering();
            const std::vector<uint8_t> gray = toGray(scene.readPixels());
            still.addFrame(Engine::GrayImage{gray.data(), width, height, width});
        }

        const Engine::Reconstruction nothing = Engine::reconstruct(still.observations(), 4,
                                                                   still.trackCount(), intrinsics, reconstructConfig);

        report.check("nepomicna kamera ne daje rekonstrukciju",
            !nothing.ok || nothing.posedCameras < 4 || nothing.medianReprojection > 2.0,
            fmt("%u kamera, %u tocaka, reprojekcija %.2f px",
                nothing.posedCameras, nothing.solvedPoints, nothing.medianReprojection));
    }

    return report.result();
}
