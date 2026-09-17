// APSOLUTNA GRESKA SOLVERA, na snimci koju sami napravimo.
//
//   ./TruthBench [putanja] [kadrova] [sum] [sirina]
//
// ZASTO OVO POSTOJI. Sve dosad je mjereno na jedan od dva nacina, i oba su neupotrebljiva za
// pitanje "koliko je alat tocan":
//
//   protiv COLMAP-a   trazi da se COLMAP pusti na svaku snimku, traje satima, a on NIJE istina
//                     nego drugo rjesenje - i danas se pokazalo da usporedba s njim zna lagati
//                     (poravnata rotacija na ravnoj putanji, referenca na krivoj strani sava)
//   PSNR splata       dvadeset minuta po varijanti, i mjeri cijeli lanac umjesto solvera
//
// Ovdje Loom sam nacrta scenu i vodi kameru POZNATIM putem. Solver dobije samo piksele. Greska je
// time apsolutna, a ne "naspram necega" - i snimaka se dade napraviti koliko treba.
//
// STO OVO NE MJERI, i to treba znati: nacrtani kadar nema sum senzora, mutnoću od gibanja, rolling
// shutter ni kompresiju. Sum se dade dodati argumentom; ostalo ne. Zato ovo mjeri GEOMETRIJU i
// POKLAPANJE, a ne cijeli lanac - za to i dalje treba prava snimka.
//
// PUTANJE, i svaka pita nesto drugo:
//
//   luk        kamera obilazi scenu - najlaksi slucaj, siroka baza, ovo mora raditi uvijek
//   prolaz     kamera ide RAVNO kroz scenu; baza je uska i dubina slabo odredjena
//   zaokret    kamera se vrti u mjestu; paralakse NEMA i rekonstrukcija ne smije uspjeti.
//              Ovo je negativna kontrola: alat koji ovdje javi uspjeh laze
//   drhtaj     luk uz drhtanje ruke - isti put, ali nemirno
#include <Loom/Loom.h>

#include "Core/CameraIntrinsics.h"

#include <Engine/MatchGraph.h>
#include <Engine/Reconstruct.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace{

//Sum koji se ponavlja: isti argument mora dati isti rezultat, inace se dvije izmjere ne daju
//usporediti. Xorshift, jer standardni generator nije zajamceno isti izmedju prevodilaca
struct Noise{
    uint32_t state = 20260917u;
    float next(){
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return float(state & 0xffffffu) / float(0xffffff) - 0.5f;
    }
};

std::vector<uint8_t> speckleTexture(uint32_t size, uint32_t seed){
    std::vector<uint8_t> pixels(size_t(size) * size * 4);
    uint32_t state = seed;
    for(size_t i = 0; i < size_t(size) * size; ++i){
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const uint8_t value = uint8_t(60 + (state & 0x7f));
        pixels[i * 4 + 0] = value;
        pixels[i * 4 + 1] = uint8_t(60 + ((state >> 8) & 0x7f));
        pixels[i * 4 + 2] = uint8_t(60 + ((state >> 16) & 0x7f));
        pixels[i * 4 + 3] = 255;
    }
    return pixels;
}

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double angleBetween(const glm::dmat3& a, const glm::dmat3& b){
    const glm::dmat3 difference = glm::transpose(a) * b;
    const double cosine = std::max(-1.0, std::min(1.0,
        (difference[0][0] + difference[1][1] + difference[2][2] - 1.0) * 0.5));
    return glm::degrees(std::acos(cosine));
}

//Greska protiv poznate istine. Mjerilo i ishodiste su slobodni, pa se polozaji poravnavaju
//slicnoscu; ROTACIJA se NE poravnava nego se gleda rasap zaokreta oko najsredisnjeg - poravnanje
//po polozajima na ravnoj putanji ne odredjuje zaokret oko osi putanje i tada laze
struct Error{
    uint32_t cameras = 0;
    double position = 0.0;      //postotak opsega putanje
    double rotation = 0.0;      //stupnjevi, bez poravnanja
    double stepRotation = 0.0;  //iz kadra u kadar
    double stepDirection = 0.0;

    //OBLIK PUTANJE, omjeri rasapa polozaja. Kad je putanja pravac ili ravnina, poravnanje po
    //polozajima je slabo odredjeno i greska polozaja koju ono daje nije mjera nego artefakt - to
    //se danas izmjerilo na pravoj snimci i ovdje se mora VIDJETI, a ne tiho ulaziti u broj
    double flatness = 0.0, straightness = 0.0;
    bool degenerate = false;
};

Error compare(const Engine::Reconstruction& ours, const std::vector<Engine::Pose>& truth){
    Error out;
    std::vector<size_t> both;
    for(size_t i = 0; i < truth.size() && i < ours.poses.size(); ++i){
        if(ours.posed[i]) both.push_back(i);
    }
    out.cameras = uint32_t(both.size());
    if(both.size() < 4) return out;

    std::vector<glm::dvec3> mine, theirs;
    for(size_t i : both){
        mine.push_back(glm::dvec3(ours.poses[i].position));
        theirs.push_back(glm::dvec3(truth[i].position));
    }

    glm::dvec3 centreMine(0.0), centreTheirs(0.0);
    for(size_t i = 0; i < mine.size(); ++i){ centreMine += mine[i]; centreTheirs += theirs[i]; }
    centreMine /= double(mine.size()); centreTheirs /= double(mine.size());

    glm::dmat3 covariance(0.0);
    double spread = 0.0;
    for(size_t i = 0; i < mine.size(); ++i){
        const glm::dvec3 a = mine[i] - centreMine, b = theirs[i] - centreTheirs;
        for(int r = 0; r < 3; ++r) for(int c = 0; c < 3; ++c) covariance[c][r] += b[r] * a[c];
        spread += glm::dot(a, a);
    }
    covariance /= double(mine.size()); spread /= double(mine.size());

    glm::dmat3 rotation = covariance;
    for(int step = 0; step < 60; ++step) rotation = 0.5 * (rotation + glm::transpose(glm::inverse(rotation)));
    if(glm::determinant(rotation) < 0.0) rotation[2] = -rotation[2];

    double trace = 0.0;
    for(int r = 0; r < 3; ++r) for(int c = 0; c < 3; ++c) trace += rotation[c][r] * covariance[c][r];
    const double scale = spread > 0.0 ? trace / spread : 1.0;

    double extent = 0.0;
    for(size_t i = 0; i < theirs.size(); ++i){
        for(size_t j = i + 1; j < theirs.size(); ++j){
            extent = std::max(extent, glm::length(theirs[i] - theirs[j]));
        }
    }

    std::vector<double> misses;
    for(size_t i = 0; i < mine.size(); ++i){
        misses.push_back(glm::length(centreTheirs + scale * (rotation * (mine[i] - centreMine)) - theirs[i]));
    }
    out.position = extent > 0.0 ? 100.0 * medianOf(misses) / extent : 0.0;
    if(!std::isfinite(out.position)) out.position = -1.0;

    //Oblik putanje preko svojstvenih brojeva rasapa - potencijskom metodom na deflaciji, dovoljno
    //za omjer
    {
        glm::dmat3 work(0.0);
        for(const glm::dvec3& one : theirs){
            const glm::dvec3 d = one - centreTheirs;
            for(int r = 0; r < 3; ++r) for(int c = 0; c < 3; ++c) work[c][r] += d[r] * d[c];
        }
        work /= double(theirs.size());

        std::vector<double> values;
        for(int k = 0; k < 3; ++k){
            glm::dvec3 v(1.0, 0.7, 0.3);
            double lambda = 0.0;
            for(int step = 0; step < 200; ++step){
                const glm::dvec3 next = work * v;
                const double length = glm::length(next);
                if(length < 1e-18) break;
                v = next / length;
                lambda = glm::dot(v, work * v);
            }
            values.push_back(std::fabs(lambda));
            for(int r = 0; r < 3; ++r) for(int c = 0; c < 3; ++c) work[c][r] -= lambda * v[r] * v[c];
        }
        std::sort(values.rbegin(), values.rend());
        out.straightness = values[0] > 0.0 ? std::sqrt(values[1] / values[0]) : 0.0;
        out.flatness = values[0] > 0.0 ? std::sqrt(values[2] / values[0]) : 0.0;
        out.degenerate = out.straightness < 0.05 || out.position < 0.0;
    }

    //Zaokret koji preslikava njegov okvir u nas je za ispravno rjesenje isti za sve kamere; mjeri
    //se rasap oko najsredisnjeg od njih
    std::vector<glm::dmat3> offsets;
    for(size_t i : both){
        offsets.push_back(glm::dmat3(glm::mat3_cast(ours.poses[i].orientation))
                        * glm::transpose(glm::dmat3(glm::mat3_cast(truth[i].orientation))));
    }
    size_t centre = 0;
    double bestSum = -1.0;
    for(size_t i = 0; i < offsets.size(); ++i){
        double sum = 0.0;
        for(size_t j = 0; j < offsets.size(); ++j) sum += angleBetween(offsets[i], offsets[j]);
        if(bestSum < 0.0 || sum < bestSum){ bestSum = sum; centre = i; }
    }
    std::vector<double> turns;
    for(size_t i = 0; i < offsets.size(); ++i){
        if(i != centre) turns.push_back(angleBetween(offsets[centre], offsets[i]));
    }
    out.rotation = medianOf(turns);

    //Iz kadra u kadar - ovo ne ovisi ni o kakvom poravnanju
    std::vector<double> stepTurns, stepDirections;
    for(size_t k = 1; k < both.size(); ++k){
        if(both[k] != both[k - 1] + 1) continue;
        const size_t a = both[k - 1], b = both[k];

        const glm::dmat3 mineA = glm::dmat3(glm::mat3_cast(ours.poses[a].orientation));
        const glm::dmat3 mineB = glm::dmat3(glm::mat3_cast(ours.poses[b].orientation));
        const glm::dmat3 trueA = glm::dmat3(glm::mat3_cast(truth[a].orientation));
        const glm::dmat3 trueB = glm::dmat3(glm::mat3_cast(truth[b].orientation));
        stepTurns.push_back(angleBetween(glm::transpose(mineA) * mineB, glm::transpose(trueA) * trueB));

        const glm::dvec3 mineMove = glm::transpose(mineA) * (glm::dvec3(ours.poses[b].position) - glm::dvec3(ours.poses[a].position));
        const glm::dvec3 trueMove = glm::transpose(trueA) * (glm::dvec3(truth[b].position) - glm::dvec3(truth[a].position));
        if(glm::length(mineMove) > 1e-12 && glm::length(trueMove) > 1e-12){
            const double dot = glm::dot(glm::normalize(mineMove), glm::normalize(trueMove));
            stepDirections.push_back(glm::degrees(std::acos(std::max(-1.0, std::min(1.0, dot)))));
        }
    }
    out.stepRotation = medianOf(stepTurns);
    out.stepDirection = medianOf(stepDirections);
    return out;
}

}

int main(int argc, char** argv){
    const std::string which = argc > 1 ? std::string(argv[1]) : std::string("luk");
    const uint32_t frames = argc > 2 ? uint32_t(std::atoi(argv[2])) : 40u;
    const float noiseLevel = argc > 3 ? float(std::atof(argv[3])) : 0.0f;
    const uint32_t width = argc > 4 ? uint32_t(std::atoi(argv[4])) : 1280u;

    //=====================================================================================
    // LAZNA STABILIZACIJA, u pikselima.
    //
    // Elektronicka stabilizacija na mobitelu ne pomice kameru nego SLIKU, i to ne jednoliko nego
    // mrezasto - redak po redak, da uhvati i rolling shutter. Time kadar vise NE ODGOVARA nijednoj
    // pozi krute kamere, pa nikakvo ugadjanje pragova ne pomaze.
    //
    // Ovdje se to oponasa izoblicenjem koje se mijenja po kadru i NIJE jednoliko preko slike -
    // jednolik pomak bi se dao upiti u pozu i ne bi mjerio nista.
    //
    // Postoji da se prag upozorenja IZMJERI umjesto pogodi: treba znati kako izgleda udio parova
    // koji prodju geometriju kad model ne vrijedi
    //=====================================================================================
    const float warp = argc > 5 ? float(std::atof(argv[5])) : 0.0f;
    const uint32_t height = width * 9 / 16;

    Loom::Scene scene(Loom::Preset::Offscreen);
    scene.setSize(width, height);
    scene.setClearColor({0.02f, 0.025f, 0.03f, 1.0f});
    scene.sun().setDirection({-0.4f, -1.0f, -0.3f});
    scene.environment().setAmbient({0.25f, 0.26f, 0.30f});

    //Tri razlicite teksture, jer scena s jednom istom teksturom svugdje daje poklapanja koja se ne
    //razlikuju - a to je lakse nego sto ijedna prava snimka jest
    const std::vector<uint8_t> first = speckleTexture(256, 20260917u);
    const std::vector<uint8_t> second = speckleTexture(256, 913u);
    const std::vector<uint8_t> third = speckleTexture(256, 55127u);
    const Loom::TextureHandle floorTexture = scene.createTexture(first.data(), 256, 256);
    const Loom::TextureHandle wallTexture = scene.createTexture(second.data(), 256, 256);
    const Loom::TextureHandle propTexture = scene.createTexture(third.data(), 256, 256);

    auto drawWorld = [&](){
        scene.drawPlane(floorTexture, Loom::Transform().scaled(30.0f));
        //Zidovi: bez njih kamera koja ide ravno vidi samo pod i scena je prazna
        scene.drawCube(wallTexture, Loom::Transform().at(0.0f, 3.0f, -12.0f).scaled(glm::vec3(24.0f, 6.0f, 0.4f)));
        scene.drawCube(wallTexture, Loom::Transform().at(-12.0f, 3.0f, 0.0f).scaled(glm::vec3(0.4f, 6.0f, 24.0f)));
        scene.drawCube(wallTexture, Loom::Transform().at(12.0f, 3.0f, 0.0f).scaled(glm::vec3(0.4f, 6.0f, 24.0f)));

        scene.drawCube(propTexture, Loom::Transform().at(-2.0f, 0.8f, -1.0f).scaled(1.6f));
        scene.drawCube(propTexture, Loom::Transform().at(1.8f, 0.5f, 1.2f).scaled(1.0f));
        scene.drawCube(propTexture, Loom::Transform().at(0.2f, 1.4f, -3.0f).scaled(2.2f));
        scene.drawSphere(propTexture, Loom::Transform().at(2.6f, 0.9f, -2.2f).scaled(1.2f));
        scene.drawCube(propTexture, Loom::Transform().at(-3.2f, 0.6f, 2.4f).scaled(1.2f));
        scene.drawSphere(propTexture, Loom::Transform().at(-5.0f, 1.2f, -4.0f).scaled(1.6f));
        scene.drawCube(propTexture, Loom::Transform().at(4.5f, 1.0f, 3.0f).scaled(1.4f));
    };

    Noise noise;
    std::vector<std::vector<uint8_t>> store(frames);
    std::vector<Engine::GrayImage> images(frames);
    std::vector<Engine::Pose> truth;

    for(uint32_t frame = 0; frame < frames; ++frame){
        const float t = frames > 1 ? float(frame) / float(frames - 1) : 0.0f;
        scene.setFrame(frame, 30.0f);

        if(which == "luk" || which == "drhtaj"){
            const float arc = glm::radians(-30.0f + 60.0f * t);
            glm::vec3 at(9.0f * std::sin(arc), 3.0f + 0.5f * t, 9.0f * std::cos(arc));
            if(which == "drhtaj") at += glm::vec3(0.12f * noise.next(), 0.12f * noise.next(), 0.12f * noise.next());
            scene.camera().setPosition(at);
            scene.camera().lookAt({0.0f, 0.9f, 0.0f});
        }else if(which == "prolaz"){
            //Ravno kroz scenu: pogled naprijed, pa je baza uska i dubina slabo odredjena
            scene.camera().setPosition({-1.0f + 0.5f * t, 1.7f, 9.0f - 14.0f * t});
            scene.camera().lookAt({-1.0f + 0.5f * t, 1.5f, -12.0f});
        }else{
            //Zaokret u mjestu: paralakse nema. NEGATIVNA KONTROLA - ovdje se ne smije uspjeti
            const float turn = glm::radians(-40.0f + 80.0f * t);
            scene.camera().setPosition({0.0f, 1.7f, 6.0f});
            scene.camera().lookAt({6.0f * std::sin(turn), 1.5f, 6.0f - 6.0f * std::cos(turn)});
        }

        scene.startRendering();
            drawWorld();
        scene.endRendering();

        truth.push_back(Engine::Pose{scene.camera().getPosition(), scene.camera().getOrientation()});

        const std::vector<uint8_t> pixels = scene.readPixels();

        //Siva slika, pa tek onda izoblicenje - inace bi se uzorkovalo u boji bez potrebe
        std::vector<float> gray(size_t(width) * height, 0.0f);
        for(size_t i = 0; i < gray.size(); ++i){
            gray[i] = 0.299f * float(pixels[i * 4 + 0]) + 0.587f * float(pixels[i * 4 + 1])
                    + 0.114f * float(pixels[i * 4 + 2]);
        }

        store[frame].assign(size_t(width) * height, 0);
        const float phase = 0.7f * float(frame);
        for(uint32_t y = 0; y < height; ++y){
            for(uint32_t x = 0; x < width; ++x){
                float value;
                if(warp > 0.0f){
                    //Pomak koji se mijenja PREKO SLIKE i po kadru. Jednolik bi se upio u pozu
                    const float dx = warp * std::sin(6.2831853f * float(y) / float(height) + phase);
                    const float dy = warp * std::cos(6.2831853f * float(x) / float(width) + phase * 1.3f);
                    const float sx = std::max(0.0f, std::min(float(width) - 1.001f, float(x) + dx));
                    const float sy = std::max(0.0f, std::min(float(height) - 1.001f, float(y) + dy));

                    const uint32_t x0 = uint32_t(sx), y0 = uint32_t(sy);
                    const float fx = sx - float(x0), fy = sy - float(y0);
                    const float a = gray[size_t(y0) * width + x0];
                    const float b = gray[size_t(y0) * width + x0 + 1];
                    const float c = gray[size_t(y0 + 1) * width + x0];
                    const float d = gray[size_t(y0 + 1) * width + x0 + 1];
                    value = (a * (1.0f - fx) + b * fx) * (1.0f - fy) + (c * (1.0f - fx) + d * fx) * fy;
                }else{
                    value = gray[size_t(y) * width + x];
                }
                if(noiseLevel > 0.0f) value += noiseLevel * 255.0f * noise.next();
                store[frame][size_t(y) * width + x] = uint8_t(std::max(0.0f, std::min(255.0f, value)));
            }
        }
        images[frame] = Engine::GrayImage{store[frame].data(), width, height, width};
    }

    const CameraIntrinsics fromLoom = CameraIntrinsics::fromProjection(
        scene.camera().getProjection(width, height), width, height);

    Engine::Intrinsics intrinsics;
    intrinsics.fx = fromLoom.fx;
    intrinsics.fy = std::fabs(fromLoom.fy);
    intrinsics.cx = fromLoom.cx;
    intrinsics.cy = fromLoom.cy;
    intrinsics.width = width;
    intrinsics.height = height;

    std::printf("%s: %u kadrova %ux%u, sum %.3f, izoblicenje %.1f px, f = %.1f px\n",
                which.c_str(), frames, width, height, double(noiseLevel), double(warp),
                double(intrinsics.fx));

    //ISTI LANAC KOJI IMA I VideoSolve: uglovi za pokrivenost, prostor mjerila za tocnost, spojeno
    Engine::MatchGraphConfig cornerConfig;
    cornerConfig.detect.maxCorners = 20000;
    cornerConfig.detect.minDistance = 12.0f;
    cornerConfig.describe.ratio = 0.9f;
    cornerConfig.describe.maxDistance = 96;

    Engine::MatchGraphConfig fineConfig = cornerConfig;
    fineConfig.useScaleSpace = true;
    fineConfig.scaleSpace.minDistance = 4.0f;

    const Engine::MatchGraphResult corners = Engine::buildMatchGraph(images, intrinsics, cornerConfig);
    const Engine::MatchGraphResult fine = Engine::buildMatchGraph(images, intrinsics, fineConfig);
    const Engine::MatchGraphResult graph = fine.pointCount > 0
        ? Engine::mergeGraphs(corners, fine) : corners;

    //UDIO PAROVA KOJI PRODJU GEOMETRIJU. Bogato poklapanje uz slab prolaz znaci da deskriptori
    //nalaze isto mjesto, a nijedna poza to ne objasni - dakle da kadar ne odgovara krutoj kameri
    const double acceptance = corners.comparedFrames > 0
        ? double(corners.acceptedFrames) / double(corners.comparedFrames) : 0.0;
    std::printf("  geometrija: %u od %u parova proslo (%.0f %%), medijan %.0f parova po paru\n",
                corners.acceptedFrames, corners.comparedFrames, 100.0 * acceptance,
                corners.medianMatchesPerPair);

    std::printf("  graf: uglovi %u tocaka / %zu opazanja, mjerilo %u / %zu, spojeno %u / %zu\n",
                corners.pointCount, corners.observations.size(),
                fine.pointCount, fine.observations.size(),
                graph.pointCount, graph.observations.size());

    Engine::ReconstructConfig config;
    config.huberPixels = 2.0;
    config.acceptPixels = std::max(6.0, 2.0 * double(graph.localizationPixels));
    config.minPointsForPose = 20;

    //Svako deseto opazanje se izdvaja i sluzi kao provjera - ovdje se moze provjeriti i sama ta
    //mjera, jer istinu znamo. Ako izdvojena reprojekcija prati stvarnu gresku, mjera vrijedi i
    //ondje gdje istine nema
    config.holdOutEvery = 10;

    const Engine::Reconstruction solved = Engine::reconstruct(graph.observations, frames,
                                                              graph.pointCount, intrinsics, config);
    const Error error = compare(solved, truth);

    std::printf("  rijeseno %u od %u kamera, %u tocaka, reprojekcija %.3f px, baza %.2f st\n",
                solved.posedCameras, frames, solved.solvedPoints,
                solved.medianReprojection, solved.medianTriangulationAngle);
    std::printf("  PROVJERA BEZ ISTINE: izdvojenih %u, reprojekcija na njima %.3f px "
                "(na koristenima %.3f, omjer %.2f)\n",
                solved.heldOutObservations, solved.heldOutReprojection, solved.medianReprojection,
                solved.medianReprojection > 0.0 ? solved.heldOutReprojection / solved.medianReprojection : 0.0);
    std::printf("  GRESKA: polozaj %.3f %%, rotacija %.4f st, po koraku %.4f st, smjer %.3f st\n",
                error.position, error.rotation, error.stepRotation, error.stepDirection);
    std::printf("  oblik putanje: drugi/prvi %.3f, treci/prvi %.3f%s\n",
                error.straightness, error.flatness,
                error.degenerate ? "  - PRAVAC ili TOCKA, greska polozaja ovdje nije mjera" : "");

    if(which == "zaokret"){
        std::printf("  (negativna kontrola: paralakse nema, pa je svaki uspjeh ovdje sumnjiv)\n");
    }
    return 0;
}
