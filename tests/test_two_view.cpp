// S4: inicijalizacija iz dva pogleda - relativna poza bez ijedne poznate poze.
//
// S1, S2 i S3 pretpostavljaju da poze vec otprilike znamo. Na pravoj snimci ih nemamo, i odavde
// mora krenuti: dva kadra, ista tocka vidjena u oba, i nista vise.
//
// Sto se brani:
//
//   rotacija i smjer   protiv prave relativne poze iz sinteticke scene. DULJINA POMAKA SE NE
//                      provjerava jer se iz dvije slike ne da doznati - provjerava se da je
//                      jedinicna, i to je sve sto smije tvrditi
//   ispred kamere      algebra daje cetiri rjesenja i tri od njih gledaju scenu s krive strane.
//                      Izabrano mora imati SVE tocke ispred obje kamere
//   sa sumom           pola piksela suma smije pomaknuti kut, ali malo - i to se mjeri, ne tvrdi
//   krivi parovi       kad se opazanja druge kamere posloze krivim redom, rjesenje MORA biti
//                      vidljivo krivo. Inace test ne bi razlikovao racun od slucajnosti
//   premalo tocaka     sedam tocaka ne odredjuje devet clanova, i to mora reci
#include "TestHarness.h"

#include <Engine/SyntheticScene.h>
#include <Engine/TwoView.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace{

//KUT SE RACUNA PREKO atan2, NE acos, i u doubleu. Prva verzija je uzimala acos od float skalarnog
//produkta - a blizu jedinice se taj produkt zaokruzi na tocno 1.0f, pa svaki kut manji od ~0.04
//stupnja ispadne kao 0.00. Test bi tako bio slijep bas u podrucju koje ga zanima: i bez suma i sa
//sumom od pola piksela ispisivao je nulu, sto je odmah bilo sumnjivo jer sum ne moze biti bez
//ikakvog ucinka
double angleBetween(const glm::quat& a, const glm::quat& b){
    const glm::dquat first = glm::normalize(glm::dquat(a));
    const glm::dquat second = glm::normalize(glm::dquat(b));
    glm::dquat difference = glm::conjugate(first) * second;
    if(difference.w < 0.0) difference = -difference;   //isti zakret, drugi predznak kvaterniona
    const double vector = std::sqrt(difference.x * difference.x + difference.y * difference.y + difference.z * difference.z);
    return glm::degrees(2.0 * std::atan2(vector, difference.w));
}

double angleBetween(const glm::vec3& a, const glm::vec3& b){
    const glm::dvec3 first = glm::normalize(glm::dvec3(a));
    const glm::dvec3 second = glm::normalize(glm::dvec3(b));
    return glm::degrees(std::atan2(glm::length(glm::cross(first, second)), glm::dot(first, second)));
}

//Parovi opazanja: ista tocka vidjena iz obje kamere
void commonPoints(const Engine::SyntheticScene& scene, uint32_t cameraA, uint32_t cameraB,
                  std::vector<glm::vec2>& pixelsA, std::vector<glm::vec2>& pixelsB){
    std::vector<const Engine::Observation*> inA(scene.points.size(), nullptr);
    for(const Engine::Observation& observation : scene.observations){
        if(observation.camera == cameraA) inA[observation.point] = &observation;
    }
    for(const Engine::Observation& observation : scene.observations){
        if(observation.camera != cameraB) continue;
        if(!inA[observation.point]) continue;
        pixelsA.push_back(inA[observation.point]->pixel);
        pixelsB.push_back(observation.pixel);
    }
}

}

int main(){
    TestReport report("S4 dva pogleda");

    Engine::SyntheticConfig config;
    const Engine::SyntheticScene clean = Engine::makeSyntheticScene(config);

    const uint32_t cameraA = 0, cameraB = 4;
    const Engine::Pose& poseA = clean.poses[cameraA];
    const Engine::Pose& poseB = clean.poses[cameraB];

    //Prava relativna poza: kamera B izrazena u sustavu kamere A
    const glm::quat truthRotation = glm::normalize(glm::conjugate(poseA.orientation) * poseB.orientation);
    const glm::vec3 truthDirection = glm::normalize(glm::conjugate(poseA.orientation) * (poseB.position - poseA.position));

    std::vector<glm::vec2> pixelsA, pixelsB;
    commonPoints(clean, cameraA, cameraB, pixelsA, pixelsB);

    {
        const Engine::TwoViewResult result = Engine::relativePose(pixelsA, pixelsB, clean.intrinsics);

        const double rotationError = result.solved ? angleBetween(result.pose.orientation, truthRotation) : 999.0;
        const double directionError = result.solved ? angleBetween(result.pose.position, truthDirection) : 999.0;
        const double length = result.solved ? double(glm::length(result.pose.position)) : 0.0;

        report.check("bez suma: rotacija i smjer su tocni",
            result.solved && rotationError < 0.01 && directionError < 0.01,
            fmt("rotacija %.2e stupnjeva, smjer %.2e stupnjeva, iz %u parova", rotationError, directionError, result.used));

        report.check("pomak je jedinicni, jer mjerila nema",
            std::fabs(length - 1.0) < 1e-5,
            fmt("|pomak| = %.6f", length));

        report.check("izabrano rjesenje ima sve tocke ispred obje kamere",
            result.inFront == result.used,
            fmt("%u od %u tocaka ispred obje kamere", result.inFront, result.used));
    }

    // -------------------------------------------------------------------------------
    // Sa sumom
    // -------------------------------------------------------------------------------

    {
        Engine::SyntheticConfig noisyConfig = config;
        noisyConfig.noisePixels = 0.5f;
        const Engine::SyntheticScene noisy = Engine::makeSyntheticScene(noisyConfig);

        std::vector<glm::vec2> noisyA, noisyB;
        commonPoints(noisy, cameraA, cameraB, noisyA, noisyB);

        const Engine::TwoViewResult result = Engine::relativePose(noisyA, noisyB, noisy.intrinsics);
        const double rotationError = result.solved ? angleBetween(result.pose.orientation, truthRotation) : 999.0;
        const double directionError = result.solved ? angleBetween(result.pose.position, truthDirection) : 999.0;

        //IZMJERENO: rotacija 0.0533, smjer 0.0449 stupnjeva uz sum od pola piksela. Prag je
        //cetiri-pet puta iznad toga, a bez suma je greska 5e-06 stupnjeva - dakle sum je jedini
        //uzrok, i to se vidi
        report.check("sa sumom ostaje blizu",
            result.solved && rotationError < 0.25 && directionError < 0.25,
            fmt("rotacija %.4f stupnjeva, smjer %.4f stupnjeva", rotationError, directionError));
    }

    // -------------------------------------------------------------------------------
    // Kontrola: krivi parovi moraju dati vidljivo krivo
    // -------------------------------------------------------------------------------

    {
        std::vector<glm::vec2> shuffled = pixelsB;
        std::rotate(shuffled.begin(), shuffled.begin() + 1, shuffled.end());

        const Engine::TwoViewResult result = Engine::relativePose(pixelsA, shuffled, clean.intrinsics);
        const double rotationError = result.solved ? angleBetween(result.pose.orientation, truthRotation) : 999.0;

        report.check("krivo posloženi parovi daju vidljivo krivo",
            !result.solved || rotationError > 5.0,
            fmt("%s, rotacija %.2f stupnjeva", result.solved ? "rijesio" : "odbio", rotationError));
    }

    // -------------------------------------------------------------------------------
    // Premalo tocaka
    // -------------------------------------------------------------------------------

    {
        const std::vector<glm::vec2> sevenA(pixelsA.begin(), pixelsA.begin() + 7);
        const std::vector<glm::vec2> sevenB(pixelsB.begin(), pixelsB.begin() + 7);
        const Engine::TwoViewResult result = Engine::relativePose(sevenA, sevenB, clean.intrinsics);

        report.check("sedam tocaka nije dosta", !result.solved,
            "devet clanova esencijalne matrice trazi barem osam parova");
    }

    return report.result();
}
