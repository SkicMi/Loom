// S11: duga snimka - pracenje gusto u vremenu, rekonstrukcija rijetka.
//
// Dosad je svaki kadar koji se pratio ujedno i ulazio u rekonstrukciju, pa se za dugu snimku
// uzimao svaki n-ti. To je najgore od oboje: veliki korak za Lucas-Kanadea, koji prati samo pomak
// prozora i sustavno otklizne kad se izmedju dva kadra promijeni perspektiva (izmjereno u S9), i
// malo kadrova za rekonstrukciju.
//
// Ovdje se prati SVAKI kadar, a u rekonstrukciju ulazi podskup. Trag preskace kadrove kojih u
// podskupu nema i time povezuje daleke poglede, a nijedan veliki korak nije trebao preskociti.
//
// STO SE BRANI. Ista snimka, isti broj kadrova u rekonstrukciji, dva nacina da se do njih dodje:
//
//   gusto pa rijetko   prati se svih N, bira se podskup            <- ovo branimo
//   samo svaki n-ti    prati se i rekonstruira isti podskup        <- ovako je bilo
//
// Ako drugi nacin nije mjerljivo gori, kljucni kadrovi nisu nista rijesili i ovaj kod ne treba
// postojati. Zato je usporedba dio testa, a ne biljeska.
#include "TestHarness.h"

#include <Loom/Loom.h>
#include "Core/CameraIntrinsics.h"

#include <Engine/Keyframes.h>
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
    const double vector = std::sqrt(difference.x * difference.x + difference.y * difference.y +
                                    difference.z * difference.z);
    return glm::degrees(2.0 * std::atan2(vector, difference.w));
}

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

std::vector<uint8_t> noiseTexture(uint32_t size, uint32_t seed){
    std::mt19937 random(seed);
    std::vector<uint8_t> pixels(size_t(size) * size * 4);
    for(size_t i = 0; i < size_t(size) * size; ++i){
        pixels[i * 4 + 0] = uint8_t(60 + random() % 180);
        pixels[i * 4 + 1] = uint8_t(60 + random() % 180);
        pixels[i * 4 + 2] = uint8_t(60 + random() % 180);
        pixels[i * 4 + 3] = 255;
    }
    return pixels;
}

std::vector<uint8_t> toGray(const std::vector<uint8_t>& rgba){
    std::vector<uint8_t> gray(size_t(width) * height);
    for(size_t i = 0; i < gray.size(); ++i){
        gray[i] = uint8_t(0.299f * float(rgba[i * 4 + 0]) + 0.587f * float(rgba[i * 4 + 1]) +
                          0.114f * float(rgba[i * 4 + 2]));
    }
    return gray;
}

//Koliko rjesenje promasuje istinu, kad se maknu gauge i mjerilo - isti racun kao u S6 i S9
struct Miss{
    double rotation = 0.0;
    double position = 0.0;
    uint32_t posed = 0;
};

Miss compare(const Engine::Reconstruction& state, const std::vector<Engine::Pose>& truth){
    const size_t count = truth.size();
    std::vector<Engine::Pose> expected(count);
    for(size_t i = 0; i < count; ++i){
        expected[i].orientation = glm::normalize(glm::conjugate(truth[0].orientation) * truth[i].orientation);
        expected[i].position = glm::conjugate(truth[0].orientation) * (truth[i].position - truth[0].position);
    }

    size_t farthest = 0;
    for(size_t i = 0; i < count; ++i){
        if(state.posed[i] && glm::length(expected[i].position) > glm::length(expected[farthest].position)) farthest = i;
    }
    const double scale = glm::length(expected[farthest].position) > 0.0f
        ? double(glm::length(state.poses[farthest].position)) / double(glm::length(expected[farthest].position)) : 1.0;

    std::vector<double> rotations, positions;
    for(size_t i = 0; i < count; ++i){
        if(!state.posed[i]) continue;
        rotations.push_back(angleBetween(state.poses[i].orientation, expected[i].orientation));
        positions.push_back(double(glm::length(state.poses[i].position / float(scale) - expected[i].position)));
    }

    Miss miss;
    miss.rotation = medianOf(rotations);
    miss.position = medianOf(positions);
    miss.posed = state.posedCameras;
    return miss;
}

}

int main(){
    TestReport report("S11 duga snimka i kljucni kadrovi");

    Loom::Scene scene(Loom::Preset::Offscreen);
    scene.setSize(width, height);
    scene.sun().setDirection({-0.4f, -1.0f, -0.3f});
    scene.environment().setAmbient({0.35f, 0.35f, 0.38f});

    const std::vector<uint8_t> texture = noiseTexture(128, 20260913);
    const Loom::TextureHandle noisy = scene.createTexture(texture.data(), 128, 128);

    auto drawScene = [&](){
        scene.drawPlane(noisy, Loom::Transform().scaled(24.0f));
        scene.drawCube(noisy, Loom::Transform().at(-2.0f, 0.8f, -1.0f).scaled(1.6f));
        scene.drawCube(noisy, Loom::Transform().at(1.8f, 0.5f, 1.2f).scaled(1.0f));
        scene.drawCube(noisy, Loom::Transform().at(0.2f, 1.4f, -3.0f).scaled(2.2f));
        scene.drawSphere(noisy, Loom::Transform().at(2.6f, 0.9f, -2.2f).scaled(1.2f));
        scene.drawCube(noisy, Loom::Transform().at(-3.2f, 0.6f, 2.4f).scaled(1.2f));
    };

    //Duga snimka: luk od 60 st preko 180 kadrova je trecina stupnja po kadru - red velicine
    //kakav daje pravi video. Kamera se usput i podize, da gibanje ne bude cista kruznica
    const uint32_t frames = 180;
    const float arc = 60.0f;

    auto placeCamera = [&](uint32_t frame){
        const float t = float(frame) / float(frames - 1);
        const float angle = glm::radians(-0.5f * arc + arc * t);
        scene.camera().setPosition({8.0f * std::sin(angle), 3.0f + 1.2f * t, 8.0f * std::cos(angle)});
        scene.camera().lookAt({0.0f, 0.7f, 0.0f});
    };

    Engine::TrackConfig trackConfig;
    trackConfig.window = 6;
    trackConfig.minDistance = 12.0f;
    trackConfig.maxCorners = 500;
    trackConfig.minTracks = 250;

    // -------------------------------------------------------------------------------
    // Gusto pracenje svih kadrova
    // -------------------------------------------------------------------------------

    Engine::Tracker dense(trackConfig);
    std::vector<Engine::Pose> truth;

    for(uint32_t frame = 0; frame < frames; ++frame){
        placeCamera(frame);
        scene.startRendering();
            drawScene();
        scene.endRendering();
        truth.push_back(Engine::Pose{scene.camera().getPosition(), scene.camera().getOrientation()});

        const std::vector<uint8_t> gray = toGray(scene.readPixels());
        dense.addFrame(Engine::GrayImage{gray.data(), width, height, width});
    }

    const CameraIntrinsics loomIntrinsics = CameraIntrinsics::fromProjection(
        scene.camera().getProjection(width, height), width, height);
    Engine::Intrinsics intrinsics;
    intrinsics.fx = loomIntrinsics.fx;
    intrinsics.fy = std::fabs(loomIntrinsics.fy);
    intrinsics.cx = loomIntrinsics.cx;
    intrinsics.cy = loomIntrinsics.cy;
    intrinsics.width = width;
    intrinsics.height = height;

    //Tragovi preko 180 kadrova: da ih ima, moraju prezivjeti dulje nego do sljedeceg kljucnog
    std::vector<uint32_t> length(dense.trackCount(), 0);
    for(const Engine::Observation& observation : dense.observations()) ++length[observation.point];
    std::sort(length.begin(), length.end());

    report.check("tragovi prezive dugu snimku",
        dense.frameCount() == frames && length.back() > 20,
        fmt("%u kadrova, %u tragova, %zu opazanja, najdulji trag %u kadrova",
            dense.frameCount(), dense.trackCount(), dense.observations().size(), length.back()));

    // -------------------------------------------------------------------------------
    // Izbor kljucnih kadrova
    // -------------------------------------------------------------------------------

    const Engine::KeyframeSelection keys = Engine::chooseKeyframes(dense.observations(), frames, width);

    uint32_t widestGap = 0;
    for(size_t i = 1; i < keys.frames.size(); ++i){
        widestGap = std::max(widestGap, keys.frames[i] - keys.frames[i - 1]);
    }

    report.check("kljucnih kadrova je puno manje nego kadrova",
        keys.frames.size() >= 5 && keys.frames.size() < frames / 3,
        fmt("%zu kljucnih od %u kadrova, najveci razmak %u, medijan paralakse %.1f px",
            keys.frames.size(), frames, widestGap, keys.medianParallaxPixels));

    report.check("kljucni kadrovi pokrivaju cijelu snimku",
        keys.frames.front() == 0 && keys.frames.back() >= frames - 2,
        fmt("od kadra %u do kadra %u", keys.frames.front(), keys.frames.back()));

    std::vector<Engine::Pose> keyTruth;
    for(uint32_t frame : keys.frames) keyTruth.push_back(truth[frame]);

    Engine::ReconstructConfig reconstructConfig;
    reconstructConfig.huberPixels = 2.0;
    reconstructConfig.minPointsForPose = 10;

    const Engine::Reconstruction fromKeys = Engine::reconstruct(keys.observations,
        uint32_t(keys.frames.size()), dense.trackCount(), intrinsics, reconstructConfig);
    const Miss keyMiss = compare(fromKeys, keyTruth);

    report.check("rijesene su sve kljucne kamere",
        fromKeys.ok && keyMiss.posed == keys.frames.size(),
        fmt("%u od %zu kamera, %u tocaka, reprojekcija %.3f px",
            keyMiss.posed, keys.frames.size(), fromKeys.solvedPoints, fromKeys.medianReprojection));

    //Granice su IZMJERENE, i mjerenje pokazuje dvije cijene koje se placaju na dugoj snimci.
    //Ista scena, cetiri kombinacije:
    //
    //   luk 20 st, 40 kadrova    0.303 px   0.518 st   0.014 m
    //   luk 20 st, 180 kadrova   0.533 px   0.669 st   0.047 m   <- duljina kosta: drift pracenja
    //   luk 60 st, 40 kadrova    0.647 px   0.990 st   0.149 m   <- luk kosta: LK prati samo pomak
    //   luk 60 st, 180 kadrova   0.874 px   0.792 st   0.196 m   <- ovdje smo
    //
    //Duljina kosta jer se trag kroz 180 kadrova polako otklizne od onoga sto je pratio. Luk kosta
    //jer se prozor s perspektivom izoblici, a Lucas-Kanade trazi samo pomak - to je granica
    //zapisana jos u S9. Oboje ima lijek (sidrenje traga na kadar rodjenja, afini LK) i to je
    //sljedeci korak; ovdje se granica MJERI, da se poslije vidi je li je lijek pomaknuo
    report.check("poze se poklapaju s onima iz kojih je crtano",
        keyMiss.rotation < 1.2 && keyMiss.position < 0.30,
        fmt("rotacija %.4f st, polozaj %.4f m (medijan)", keyMiss.rotation, keyMiss.position));

    // -------------------------------------------------------------------------------
    // Isti kadrovi, ali praceni kao prije: samo oni, s velikim korakom medju njima
    // -------------------------------------------------------------------------------

    Engine::Tracker sparse(trackConfig);
    for(uint32_t frame : keys.frames){
        placeCamera(frame);
        scene.startRendering();
            drawScene();
        scene.endRendering();
        const std::vector<uint8_t> gray = toGray(scene.readPixels());
        sparse.addFrame(Engine::GrayImage{gray.data(), width, height, width});
    }

    const Engine::Reconstruction fromSparse = Engine::reconstruct(sparse.observations(),
        uint32_t(keys.frames.size()), sparse.trackCount(), intrinsics, reconstructConfig);
    const Miss sparseMiss = compare(fromSparse, keyTruth);

    std::printf("      gusto pa rijetko: %u kamera, %.4f st, %.4f m | samo svaki n-ti: %u kamera, %.4f st, %.4f m\n",
                keyMiss.posed, keyMiss.rotation, keyMiss.position,
                sparseMiss.posed, sparseMiss.rotation, sparseMiss.position);

    //Ovo je provjera koja brani postojanje kljucnih kadrova - ali tek uz drugi dio nize.
    //Uz gust izbor kljucnih kadrova (2.7 st medju njima) stari nacin je samo malo gori, jer LK
    //takav korak jos podnosi. Razlika postaje razlika u vrsti tek kad su kljucni kadrovi rijetki -
    //a bas to se dogadja na dugoj snimci.
    //
    //ZASTO SAMO 1.05 A NE VISE. Ovaj test kadrove CRTA, pa mjeri i rasterizator pod sobom.
    //Izmjereno: llvmpipe 1.1852/0.7508 = 1.58x, RTX 5070 0.9209/0.8287 = 1.11x. Prijasnjih 1.3x
    //je stajalo usred tog raspona, pa je prolaz ovisio o kartici a ne o racunu. Ovdje se zato
    //brani SMJER - da je stari nacin mjerljivo gori - dok tvrdnju o VELICINI nosi provjera s
    //rijetkim kljucnim kadrovima nize, gdje je razlika deseterostruka na oba rasterizatora
    report.check("pracenje samo kljucnih kadrova je gore i uz gust izbor",
        sparseMiss.rotation > 1.05 * keyMiss.rotation || sparseMiss.posed < keyMiss.posed,
        fmt("rotacija %.4f naspram %.4f st, %u naspram %u kamera",
            sparseMiss.rotation, keyMiss.rotation, sparseMiss.posed, keyMiss.posed));

    // -------------------------------------------------------------------------------
    // Isto jos jednom, ali s rijetkim kljucnim kadrovima - tamo gdje se stvarno odlucuje
    // -------------------------------------------------------------------------------

    {
        Engine::KeyframeConfig wide;
        wide.minParallaxFraction = 0.016;      //cetiri puta rjedje nego inace
        const Engine::KeyframeSelection few = Engine::chooseKeyframes(dense.observations(), frames, width, wide);

        std::vector<Engine::Pose> fewTruth;
        for(uint32_t frame : few.frames) fewTruth.push_back(truth[frame]);

        const Engine::Reconstruction denseFew = Engine::reconstruct(few.observations,
            uint32_t(few.frames.size()), dense.trackCount(), intrinsics, reconstructConfig);
        const Miss denseFewMiss = compare(denseFew, fewTruth);

        Engine::Tracker sparseFew(trackConfig);
        for(uint32_t frame : few.frames){
            placeCamera(frame);
            scene.startRendering();
                drawScene();
            scene.endRendering();
            const std::vector<uint8_t> gray = toGray(scene.readPixels());
            sparseFew.addFrame(Engine::GrayImage{gray.data(), width, height, width});
        }
        const Engine::Reconstruction sparseFewState = Engine::reconstruct(sparseFew.observations(),
            uint32_t(few.frames.size()), sparseFew.trackCount(), intrinsics, reconstructConfig);
        const Miss sparseFewMiss = compare(sparseFewState, fewTruth);

        std::printf("      rijetki kljucni kadrovi (%zu): gusto pa rijetko %u kamera %.4f st | samo njih %u kamera %.4f st\n",
                    few.frames.size(), denseFewMiss.posed, denseFewMiss.rotation,
                    sparseFewMiss.posed, sparseFewMiss.rotation);

        //Ovdje stari nacin mora pasti, i to ne za dlaku: korak medju kadrovima je prevelik da bi
        //Lucas-Kanade uopce nasao istu tocku
        report.check("uz rijetke kljucne kadrove stari nacin propada",
            sparseFewMiss.posed < denseFewMiss.posed || sparseFewMiss.rotation > 3.0 * denseFewMiss.rotation,
            fmt("%u naspram %u kamera, rotacija %.4f naspram %.4f st",
                sparseFewMiss.posed, denseFewMiss.posed, sparseFewMiss.rotation, denseFewMiss.rotation));
    }

    return report.result();
}
