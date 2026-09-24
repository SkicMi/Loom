// Rolling shutter u bundleu (BundleConfig::rowTime).
//
// ZASTO SE OVO TESTIRA. Model se na pravoj snimci ne moze provjeriti - tamo se ne zna ni koliko
// je citanje senzora trajalo ni kako se kamera gibala. Ovdje se zna sve: kamera ide bocno i
// zakrece se kao iz ruke, a svako opazanje nastaje u pozi SVOG RETKA. Tada:
//
//   GLOBALNI         bundle bez modela ne objasni opazanja kad gibanje TRZA (iz ruke). Stalno
//                    gibanje se gotovo cijelo upije u iskrivljenu geometriju - ostatak tada
//                    izgleda dobro, a poze su krive - pa se mjeri i greska polozaja prema istini
//   MODEL            s pravim rowTime i brzinama iz SUSJEDNIH KADROVA (kako ce raditi na snimci,
//                    ne iz istine) ostatak pada na razinu suma, a poze na istinu
//   TRAZENJE         najmanja cijena po rowTime pada na pravu vrijednost, pa se rowTime moze
//                    izmjeriti iz same snimke
//   BEZ GIBANJA      kamera koja stoji: rowTime ne mijenja nista (model ne izmislja pomak)
#include "TestHarness.h"

#include <Engine/Bundle.h>

#include <cmath>

namespace{

using namespace Engine;

struct Scene{
    std::vector<Pose> poses;
    std::vector<double> times;
    std::vector<glm::vec3> points;
    std::vector<Observation> observations;
    Intrinsics intrinsics;
};

//Kamera se krece bocno i zakrece oko svoje osi gore, uz sporo njihanje kao iz ruke (period
//stotinjak kadrova - brze njihanje razlika susjednih kadrova ne moze ni vidjeti), 1920x1080,
//kadar svakih 'spacing' kadrova snimke. Opazanje: redak v u kojem tocka padne kad se poza uzme u trenutku tog retka
Scene makeScene(double rowTime, float sideways, float yawRate, float noise, uint32_t seed, float wobble = 1.0f){
    Scene scene;
    scene.intrinsics.fx = scene.intrinsics.fy = 1500.0f;
    scene.intrinsics.cx = 960.0f; scene.intrinsics.cy = 540.0f;
    scene.intrinsics.width = 1920; scene.intrinsics.height = 1080;
    const int cameras = 40, spacing = 2;
    for(int i = 0; i < cameras; ++i){
        const float t = float(i * spacing);
        Pose pose;
        pose.position = glm::vec3(sideways * t, wobble * 0.02f * std::sin(0.03f * t), 0.0f);
        pose.orientation = glm::angleAxis(yawRate * t + wobble * 0.03f * std::sin(0.1f * t), glm::vec3(0.0f, 1.0f, 0.0f)) *
                           glm::angleAxis(wobble * 0.05f * std::sin(0.02f * t), glm::vec3(1.0f, 0.0f, 0.0f));
        scene.poses.push_back(pose);
        scene.times.push_back(double(t));
    }
    uint32_t state = seed;
    auto random = [&](){ state ^= state << 13; state ^= state >> 17; state ^= state << 5; return float(state % 100001) / 50000.0f - 1.0f; };
    for(int p = 0; p < 900; ++p) scene.points.push_back(glm::vec3(random() * 9.0f + 3.0f, random() * 3.0f, -8.0f - 5.0f * (random() + 1.0f)));

    //Istinske brzine: analiticki, iz gibanja gore (bocno + zakret), ne iz susjeda
    BundleConfig truth;
    truth.rowTime = rowTime;
    for(int i = 0; i < cameras; ++i){
        const float t = float(i * spacing);
        truth.linearVelocity.push_back(glm::vec3(sideways, wobble * 0.02f * 0.03f * std::cos(0.03f * t), 0.0f));
        //Kutna brzina u osima kamere za R = Zakret(t) * Nagib(t): zakret oko svjetskog Y prebacen
        //u osi kamere, plus brzina nagiba oko lokalnog X
        const glm::quat q = scene.poses[size_t(i)].orientation;
        truth.angularVelocity.push_back(glm::conjugate(q) * glm::vec3(0.0f, yawRate + wobble * 0.03f * 0.1f * std::cos(0.1f * t), 0.0f) +
                                        glm::vec3(wobble * 0.05f * 0.02f * std::cos(0.02f * t), 0.0f, 0.0f));
    }
    for(size_t c = 0; c < scene.poses.size(); ++c){
        for(size_t p = 0; p < scene.points.size(); ++p){
            glm::vec2 pixel;
            if(!project(scene.poses[c], scene.intrinsics, scene.points[p], pixel)) continue;
            bool inside = true;
            for(int k = 0; k < 6 && inside; ++k){
                const Pose atRow = rollingShutterPose(scene.poses[c], truth, c, pixel.y, scene.intrinsics.cy);
                inside = project(atRow, scene.intrinsics, scene.points[p], pixel);
            }
            if(!inside) continue;
            scene.observations.push_back(Observation{uint32_t(c), uint32_t(p), pixel + glm::vec2(random(), random()) * noise});
        }
    }
    return scene;
}

//Bundle od istine uz zadani rowTime i brzine iz SUSJEDNIH kadrova; vraca medijan ostatka.
//positionError: medijan udaljenosti rijesenih kamera od istine
double solveWith(const Scene& scene, double rowTime, BundleResult* out = nullptr, double* positionError = nullptr){
    BundleConfig config;
    config.maxIterations = 30;
    config.rowTime = rowTime;
    rollingShutterVelocities(scene.poses, scene.times, config.linearVelocity, config.angularVelocity);
    BundleResult result = bundleAdjust(scene.observations, scene.poses, scene.points, scene.intrinsics, config);
    if(out) *out = result;
    if(positionError){
        std::vector<double> errors;
        for(size_t i = 0; i < scene.poses.size(); ++i) errors.push_back(glm::length(result.poses[i].position - scene.poses[i].position));
        std::nth_element(errors.begin(), errors.begin() + long(errors.size() / 2), errors.end());
        *positionError = errors[errors.size() / 2];
    }
    return result.endMedian;
}

}

int main(){
    TestReport report("S7 rolling shutter");

    //Citanje senzora 80 posto razmaka kadrova, preko 1080 redaka; kamera ide bocno i zakrece
    const double rowTime = 0.8 / 1080.0;
    const Scene scene = makeScene(rowTime, 0.02f, 0.01f, 0.0f, 7u);

    double globalPosition = 0.0, modelPosition = 0.0;
    const double global = solveWith(scene, 0.0, nullptr, &globalPosition);
    const double modelled = solveWith(scene, rowTime, nullptr, &modelPosition);
    report.check("uz trzaj iz ruke globalni zatvarac ostavi ostatak i krive poze, model ne (brzine iz susjednih kadrova)",
        global > 5.0 * modelled && modelled < 0.02 && modelPosition < 0.2 * globalPosition && scene.observations.size() > 5000,
        fmt("medijan bez modela %.3f px, s modelom %.4f px; kamere od istine %.4f / %.4f, %zu opazanja",
            global, modelled, globalPosition, modelPosition, scene.observations.size()));

    //Trazenje: najmanji medijan po rowTime mora pasti na pravu vrijednost
    double best = 1e9, bestFraction = -1.0;
    for(double fraction = 0.0; fraction <= 1.2001; fraction += 0.1){
        const double median = solveWith(scene, fraction / 1080.0);
        if(median < best){ best = median; bestFraction = fraction; }
    }
    report.check("najmanji ostatak je na pravom vremenu citanja", std::fabs(bestFraction - 0.8) < 0.05,
        fmt("najbolje %.1f okvira citanja (istina 0.8), medijan %.4f px", bestFraction, best));

    //Obrnut smjer citanja (odozdo) daje negativan rowTime: mora se isto naci, ne zamijeniti
    const Scene upward = makeScene(-rowTime, 0.02f, 0.01f, 0.0f, 9u);
    const double wrongSign = solveWith(upward, rowTime), rightSign = solveWith(upward, -rowTime);
    report.check("smjer citanja se razlikuje (odozdo = negativan rowTime)", rightSign < 0.05 && wrongSign > 3.0 * rightSign,
        fmt("krivi smjer %.3f px, pravi %.4f px", wrongSign, rightSign));

    //Uz sum od 0.5 px model i dalje spusti ostatak do razine suma
    const Scene noisy = makeScene(rowTime, 0.02f, 0.01f, 0.5f, 11u);
    const double noisyGlobal = solveWith(noisy, 0.0), noisyModel = solveWith(noisy, rowTime);
    double noisyBest = 1e9, noisyFraction = -1.0;
    for(double fraction = 0.0; fraction <= 1.2001; fraction += 0.2){
        const double median = solveWith(noisy, fraction / 1080.0);
        if(median < noisyBest){ noisyBest = median; noisyFraction = fraction; }
    }
    report.check("uz sum 0.5 px model i dalje spusti ostatak, i trazenje nadje vrijeme citanja",
        noisyModel < noisyGlobal && std::fabs(noisyFraction - 0.8) < 0.25,
        fmt("bez modela %.3f px, s modelom %.3f px, najbolje %.1f okvira citanja", noisyGlobal, noisyModel, noisyFraction));

    //Kamera koja stoji: rowTime nema na sto djelovati
    const Scene still = makeScene(0.0, 0.0f, 0.0f, 0.0f, 13u, 0.0f);
    const double stillGlobal = solveWith(still, 0.0), stillModel = solveWith(still, rowTime);
    report.check("kamera koja stoji: rowTime ne mijenja nista", std::fabs(stillGlobal - stillModel) < 1e-4 && stillModel < 1e-3,
        fmt("bez %.6f, s %.6f px", stillGlobal, stillModel));

    return report.result();
}
