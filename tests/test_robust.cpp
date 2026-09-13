// S5: otpornost na promasena poklapanja.
//
// Sve dosad pretpostavlja da je svako opazanje ispravno, samo malo pomaknuto sumom. Na pravoj
// snimci to ne stoji: poklapanja izmedju kadrova se rade po izgledu, a izgled vara - ponavljajuci
// uzorak, odsjaj, pomicni objekt. Takav par ne promasuje za piksel nego za pola slike.
//
// NAJMANJI KVADRATI TO NE MOGU PREZIVJETI, i to je matematicka cinjenica a ne nedostatak
// implementacije: kazna raste s kvadratom promasaja, pa jedan par koji promasuje 300 px vuce
// jednako kao devet tisuca ispravnih koji promasuju 1 px. Zato dvije obrane:
//
//   RANSAC      za relativnu pozu: procjena iz osam nasumicnih parova, pa se prebroje oni koji
//               se s njom slazu. Dovoljno pokusaja i barem jedan uzorak bude cist
//   Huber       za PnP i bundle: iznad praga kazna raste linearno umjesto kvadratno, pa promaseni
//               par prestaje vuci cim se prepozna kao promasen
//
// Svaka obrana ovdje ima i svoj par bez nje - jer "s Huberom je dobro" nista ne znaci dok se ne
// vidi koliko je lose bez njega.
#include "TestHarness.h"

#include <Engine/Bundle.h>
#include <Engine/SolvePose.h>
#include <Engine/SyntheticScene.h>
#include <Engine/TwoView.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace{

double angleBetween(const glm::quat& a, const glm::quat& b){
    const glm::dquat first = glm::normalize(glm::dquat(a));
    const glm::dquat second = glm::normalize(glm::dquat(b));
    glm::dquat difference = glm::conjugate(first) * second;
    if(difference.w < 0.0) difference = -difference;
    const double vector = std::sqrt(difference.x * difference.x + difference.y * difference.y + difference.z * difference.z);
    return glm::degrees(2.0 * std::atan2(vector, difference.w));
}

double angleBetween(const glm::vec3& a, const glm::vec3& b){
    const glm::dvec3 first = glm::normalize(glm::dvec3(a));
    const glm::dvec3 second = glm::normalize(glm::dvec3(b));
    return glm::degrees(std::atan2(glm::length(glm::cross(first, second)), glm::dot(first, second)));
}

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

//Svako peto-sesto opazanje odleti nekamo drugdje na sliku. Uvijek isto, jer sjeme je fiksno
std::vector<uint8_t> spoil(Engine::SyntheticScene& scene, double fraction, uint32_t seed){
    std::mt19937 random(seed);
    std::vector<uint8_t> spoiled(scene.observations.size(), 0);
    for(size_t i = 0; i < scene.observations.size(); ++i){
        if(double(random() % 1000000) / 1000000.0 >= fraction) continue;
        scene.observations[i].pixel = glm::vec2(float(random() % scene.intrinsics.width),
                                                float(random() % scene.intrinsics.height));
        spoiled[i] = 1;
    }
    return spoiled;
}

}

int main(){
    TestReport report("S5 otpornost");

    Engine::SyntheticConfig config;
    config.noisePixels = 0.5f;
    const Engine::SyntheticScene clean = Engine::makeSyntheticScene(config);

    Engine::SyntheticScene dirty = clean;
    const std::vector<uint8_t> spoiled = spoil(dirty, 0.15, 4242);
    const size_t spoiledCount = size_t(std::count(spoiled.begin(), spoiled.end(), uint8_t(1)));

    report.check("scena ima promasena poklapanja",
        spoiledCount > dirty.observations.size() / 10,
        fmt("%zu od %zu opazanja je promaseno", spoiledCount, dirty.observations.size()));

    // -------------------------------------------------------------------------------
    // Dva pogleda: RANSAC protiv obicnog racuna
    // -------------------------------------------------------------------------------

    const uint32_t cameraA = 0, cameraB = 4;
    const glm::quat truthRotation = glm::normalize(glm::conjugate(clean.poses[cameraA].orientation) * clean.poses[cameraB].orientation);
    const glm::vec3 truthDirection = glm::normalize(glm::conjugate(clean.poses[cameraA].orientation) *
                                                    (clean.poses[cameraB].position - clean.poses[cameraA].position));

    {
        std::vector<glm::vec2> pixelsA, pixelsB;
        std::vector<uint8_t> pairSpoiled;
        std::vector<const Engine::Observation*> inA(dirty.points.size(), nullptr);
        std::vector<uint8_t> spoiledInA(dirty.points.size(), 0);

        for(size_t i = 0; i < dirty.observations.size(); ++i){
            if(dirty.observations[i].camera != cameraA) continue;
            inA[dirty.observations[i].point] = &dirty.observations[i];
            spoiledInA[dirty.observations[i].point] = spoiled[i];
        }
        for(size_t i = 0; i < dirty.observations.size(); ++i){
            const Engine::Observation& observation = dirty.observations[i];
            if(observation.camera != cameraB || !inA[observation.point]) continue;
            pixelsA.push_back(inA[observation.point]->pixel);
            pixelsB.push_back(observation.pixel);
            pairSpoiled.push_back(spoiled[i] || spoiledInA[observation.point]);
        }

        const Engine::TwoViewResult plain = Engine::relativePose(pixelsA, pixelsB, dirty.intrinsics);
        const Engine::TwoViewResult robust = Engine::relativePoseRobust(pixelsA, pixelsB, dirty.intrinsics);

        const double plainRotation = plain.solved ? angleBetween(plain.pose.orientation, truthRotation) : 999.0;
        const double robustRotation = robust.solved ? angleBetween(robust.pose.orientation, truthRotation) : 999.0;
        const double robustDirection = robust.solved ? angleBetween(robust.pose.position, truthDirection) : 999.0;

        report.check("bez RANSAC-a promasaji ruse relativnu pozu", plainRotation > 1.0,
            fmt("rotacija %.2f stupnjeva bez zastite", plainRotation));

        report.check("s RANSAC-om se poza vraca", robust.solved && robustRotation < 0.5 && robustDirection < 0.5,
            fmt("rotacija %.3f stupnjeva, smjer %.3f stupnjeva (bez zastite %.2f)",
                robustRotation, robustDirection, plainRotation));

        //Maska: koliko je pravih promasaja prepoznato, i koliko je ispravnih odbaceno
        size_t caught = 0, lost = 0;
        for(size_t i = 0; i < pairSpoiled.size() && i < robust.inliers.size(); ++i){
            if(pairSpoiled[i] && !robust.inliers[i]) ++caught;
            if(!pairSpoiled[i] && !robust.inliers[i]) ++lost;
        }
        const size_t trulySpoiled = size_t(std::count(pairSpoiled.begin(), pairSpoiled.end(), uint8_t(1)));

        report.check("maska prepoznaje bas promasene parove",
            caught > trulySpoiled * 9 / 10 && lost < pairSpoiled.size() / 20,
            fmt("prepoznato %zu od %zu promasenih, odbaceno %zu ispravnih od %zu",
                caught, trulySpoiled, lost, pairSpoiled.size() - trulySpoiled));

        //Isto sjeme, isti odgovor: RANSAC bira nasumicno, pa bi inace dva pokretanja davala dvije
        //rekonstrukcije i nijedno mjerenje se ne bi dalo ponoviti
        const Engine::TwoViewResult again = Engine::relativePoseRobust(pixelsA, pixelsB, dirty.intrinsics);
        report.check("isto sjeme daje isti odgovor",
            again.inlierCount == robust.inlierCount &&
            again.pose.position == robust.pose.position && again.pose.orientation == robust.pose.orientation,
            fmt("%u naspram %u parova koji se slazu", again.inlierCount, robust.inlierCount));
    }

    // -------------------------------------------------------------------------------
    // PnP: Huber protiv cistih najmanjih kvadrata
    // -------------------------------------------------------------------------------

    {
        const uint32_t camera = 3;
        std::vector<Engine::PointObservation> seen;
        for(const Engine::Observation& observation : dirty.observations){
            if(observation.camera == camera) seen.push_back(Engine::PointObservation{observation.point, observation.pixel});
        }

        const Engine::Pose truth = clean.poses[camera];
        Engine::Pose start = truth;
        start.position += truth.orientation * glm::vec3(0.1f, -0.05f, 0.08f);

        const Engine::PoseSolveResult plain = Engine::solvePose(clean.points, seen, dirty.intrinsics, start);

        Engine::PoseSolveConfig robustConfig;
        robustConfig.huberPixels = 2.0;
        const Engine::PoseSolveResult robust = Engine::solvePose(clean.points, seen, dirty.intrinsics, start, robustConfig);

        const double plainError = double(glm::length(plain.pose.position - truth.position));
        const double robustError = double(glm::length(robust.pose.position - truth.position));

        report.check("Huber popravlja pozu koju promasaji izvuku",
            robustError < 0.2 * plainError && robustError < 0.02,
            fmt("%.4f m bez Hubera, %.4f m s njim", plainError, robustError));
    }

    // -------------------------------------------------------------------------------
    // Bundle: isto, ali kad se popravljaju i poze i tocke
    // -------------------------------------------------------------------------------

    {
        std::vector<Engine::Pose> poses = clean.poses;
        std::vector<glm::vec3> points = clean.points;
        for(size_t i = 1; i < poses.size(); ++i){
            const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
            poses[i].position += poses[i].orientation * glm::vec3(0.03f * sign, -0.02f, 0.02f * sign);
        }

        const Engine::BundleResult plain = Engine::bundleAdjust(dirty.observations, poses, points, dirty.intrinsics);

        Engine::BundleConfig robustConfig;
        robustConfig.huberPixels = 2.0;
        const Engine::BundleResult robust = Engine::bundleAdjust(dirty.observations, poses, points, dirty.intrinsics, robustConfig);

        //MJERILO SE PONISTAVA PRIJE USPOREDBE, isto kao u S3. Prva verzija ovog testa mjerila je
        //sirove udaljenosti i zakljucila da je s Huberom GORE (5.49 m naspram 0.75 m) - a zapravo
        //je oblik bio bolji trideset puta, samo je mjerilo odlutalo. Uz jednu fiksnu kameru ono
        //nije odredjeno, a kad se promasaji priguse, slobodni smjer luta jos lakse: izmjereno
        //0.72x bez Hubera, 1.65x s njim, pa i 10x na pragu od pet piksela
        auto shapeError = [&](const Engine::BundleResult& result){
            const double scale = double(glm::length(result.poses[1].position - result.poses[0].position)) /
                                 double(glm::length(clean.poses[1].position - clean.poses[0].position));
            std::vector<double> errors;
            for(size_t i = 0; i < clean.points.size(); ++i){
                const glm::vec3 corrected = result.poses[0].position + (result.points[i] - result.poses[0].position) / float(scale);
                errors.push_back(double(glm::length(corrected - clean.points[i])));
            }
            return medianOf(errors);
        };

        const double plainMedian = shapeError(plain);
        const double robustMedian = shapeError(robust);

        report.check("Huber cuva oblik u bundleu",
            robustMedian < 0.05 && robustMedian < 0.1 * plainMedian,
            fmt("tocke %.4f m bez Hubera, %.4f m s njim (mjerilo se u usporedbi ponistava)",
                plainMedian, robustMedian));

        //Bez Hubera rjesenje savije scenu prema promasajima, pa reprojekcija NARASTE u odnosu na
        //pocetnu. To je izravan dokaz da najmanji kvadrati ovdje ne pomazu nego stete
        report.check("bez Hubera reprojekcija naraste",
            plain.endMedian > plain.startMedian && robust.endMedian < robust.startMedian,
            fmt("bez Hubera %.2f -> %.2f px, s Huberom %.2f -> %.2f px",
                plain.startMedian, plain.endMedian, robust.startMedian, robust.endMedian));
    }

    return report.result();
}
