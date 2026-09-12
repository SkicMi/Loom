// S2: PnP - iz poznatih tocaka i opazanja natrag do poze kamere.
//
// Obrnuto od triangulacije (S1), i zajedno s njom zatvara krug poze -> tocke -> poze. Referenca je
// i dalje sinteticka scena (S0), gdje se tocan odgovor zna, pa se mjeri UDALJENOST OD PRAVE POZE u
// metrima i stupnjevima - a ne samo koliko je solver zadovoljan sam sa sobom.
//
// Sto se brani:
//
//   jakobijan          protiv numericke derivacije, clan po clan. Kriv predznak u jednom stupcu ne
//                      rusi konvergenciju iz bliskog starta - solver samo ide sporije i zavrsi
//                      drugdje. Nijedna provjera "poza je blizu prave" to ne bi uhvatila
//   blizak start       10 cm i 3 stupnja: mora se vratiti na pravu pozu do zaokruzivanja
//   dalek start        1 m i 15 stupnjeva: mora i dalje stici, inace solver vrijedi samo kad mu
//                      odgovor vec dodje gotovo tocan
//   sa sumom           0.5 px suma: poza ostaje blizu, a reprojekcija padne na razinu suma
//   bez iteracija      s maxIterations 0 nista se ne smije popraviti. To je kontrola da posao
//                      rade iteracije, a ne sama pocetna pretpostavka
//   premalo tocaka     dvije tocke ne odredjuju sest nepoznanica, i to mora reci
//
// KORAK NUMERICKE DERIVACIJE JE 1e-3, ne manji: poze su u floatu, pa presitan korak mjeri
// zaokruzivanje umjesto derivacije (8 m u floatu ima korak 8e-7).
#include "TestHarness.h"

#include <Engine/SolvePose.h>
#include <Engine/SyntheticScene.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace{

//Ista parametrizacija koju tvrdi jakobijan: pomak i rotacija u KAMERINOM sustavu
Engine::Pose perturb(const Engine::Pose& pose, const glm::vec3& translation, const glm::vec3& rotation){
    Engine::Pose out = pose;
    out.position += pose.orientation * translation;

    const float angle = glm::length(rotation);
    if(angle > 0.0f){
        out.orientation = glm::normalize(pose.orientation * glm::angleAxis(angle, rotation / angle));
    }
    return out;
}

double angleBetween(const glm::quat& a, const glm::quat& b){
    const float dot = std::fabs(glm::dot(glm::normalize(a), glm::normalize(b)));
    return glm::degrees(2.0 * std::acos(double(std::min(1.0f, dot))));
}

std::vector<Engine::PointObservation> observationsOf(const Engine::SyntheticScene& scene, uint32_t camera){
    std::vector<Engine::PointObservation> out;
    for(const Engine::Observation& observation : scene.observations){
        if(observation.camera == camera) out.push_back(Engine::PointObservation{observation.point, observation.pixel});
    }
    return out;
}

}

int main(){
    TestReport report("S2 PnP");

    Engine::SyntheticConfig config;
    const Engine::SyntheticScene clean = Engine::makeSyntheticScene(config);

    const uint32_t camera = 3;
    const Engine::Pose truth = clean.poses[camera];
    const std::vector<Engine::PointObservation> seen = observationsOf(clean, camera);

    // -------------------------------------------------------------------------------
    // Jakobijan protiv numericke derivacije
    // -------------------------------------------------------------------------------

    {
        const double h = 1e-3;
        double worst = 0.0;
        size_t compared = 0;

        //Poza namjerno NIJE tocna: u tocnoj su reziduali nula, pa bi se greska u predznaku
        //lakse sakrila
        const Engine::Pose probe = perturb(truth, glm::vec3(0.05f, -0.03f, 0.08f), glm::vec3(0.01f, 0.02f, -0.015f));

        for(size_t i = 0; i < seen.size(); i += 37){
            const glm::vec3& point = clean.points[seen[i].point];
            const glm::vec2& pixel = seen[i].pixel;

            double residual[2], jacobian[2][6];
            if(!Engine::poseJacobian(probe, clean.intrinsics, point, pixel, residual, jacobian)) continue;

            //SKALA RETKA, ne jedinica. Numericka derivacija ima svoj pod: poze su u floatu, pa se
            //rezidual izmedju dva koraka razlikuje za oko 3e-5 px zaokruzivanja, sto podijeljeno s
            //2h daje sum od ~0.015 u jedinicama px/m. Clan kojemu je prava vrijednost nula tada
            //izgleda kao promasaj od 0.015, a to nije greska nego pod mjerenja. Mjeri se zato
            //prema najvecem clanu istog retka - isto pravilo koje je vec jednom ispravilo prag u
            //test_splat_prepare
            double rowScale[2] = {1.0, 1.0};
            for(int row = 0; row < 2; ++row){
                for(int column = 0; column < 6; ++column){
                    rowScale[row] = std::max(rowScale[row], std::fabs(jacobian[row][column]));
                }
            }

            for(int parameter = 0; parameter < 6; ++parameter){
                glm::vec3 translation(0.0f), rotation(0.0f);
                if(parameter < 3) translation[parameter] = float(h);
                else rotation[parameter - 3] = float(h);

                double plus[2], minus[2], ignored[2][6];
                const Engine::Pose forward = perturb(probe, translation, rotation);
                const Engine::Pose backward = perturb(probe, -translation, -rotation);
                if(!Engine::poseJacobian(forward, clean.intrinsics, point, pixel, plus, ignored)) continue;
                if(!Engine::poseJacobian(backward, clean.intrinsics, point, pixel, minus, ignored)) continue;

                for(int row = 0; row < 2; ++row){
                    const double numeric = (plus[row] - minus[row]) / (2.0 * h);
                    const double analytic = jacobian[row][parameter];
                    worst = std::max(worst, std::fabs(numeric - analytic) / rowScale[row]);
                    ++compared;
                }
            }
        }

        //IZMJERENO 4.36e-05 uz skalu retka; prag je dvadesetak puta iznad toga. Obrnut predznak
        //daje 2.0, ispusten clan oko 1.0 - oboje daleko iznad
        report.check("jakobijan se slaze s numerickom derivacijom", compared > 50 && worst < 1e-3,
            fmt("najveca relativna razlika %.2e kroz %zu clanova", worst, compared));
    }

    // -------------------------------------------------------------------------------
    // Blizak i dalek start
    // -------------------------------------------------------------------------------

    auto solveFrom = [&](const Engine::Pose& start, const std::vector<Engine::PointObservation>& observations,
                         const std::vector<glm::vec3>& points){
        return Engine::solvePose(points, observations, clean.intrinsics, start);
    };

    {
        const Engine::Pose near = perturb(truth, glm::vec3(0.1f, -0.05f, 0.08f), glm::vec3(glm::radians(3.0f), 0.0f, 0.0f));
        const Engine::PoseSolveResult result = solveFrom(near, seen, clean.points);

        const double distance = double(glm::length(result.pose.position - truth.position));
        const double angle = angleBetween(result.pose.orientation, truth.orientation);

        report.check("blizak start: vraca se na pravu pozu",
            result.solved && distance < 1e-3 && angle < 1e-2 && result.endMedian < 1e-2,
            fmt("%.2e m, %.2e stupnjeva, reprojekcija %.1f -> %.2e px kroz %u iteracija",
                distance, angle, result.startMedian, result.endMedian, result.iterations));
    }

    {
        const Engine::Pose far = perturb(truth, glm::vec3(1.0f, 0.4f, -0.7f), glm::vec3(glm::radians(15.0f), glm::radians(-8.0f), 0.0f));
        const Engine::PoseSolveResult result = solveFrom(far, seen, clean.points);

        const double distance = double(glm::length(result.pose.position - truth.position));
        const double angle = angleBetween(result.pose.orientation, truth.orientation);

        report.check("dalek start: i dalje stize",
            result.solved && distance < 1e-3 && angle < 1e-2 && result.endMedian < 1e-2,
            fmt("%.2e m, %.2e stupnjeva, reprojekcija %.1f -> %.2e px kroz %u iteracija",
                distance, angle, result.startMedian, result.endMedian, result.iterations));
    }

    // -------------------------------------------------------------------------------
    // Brzina: s ispravnim jakobijanom tri koraka su dosta
    // -------------------------------------------------------------------------------
    //
    // OVO JE PROVJERA KOJU JE TRAZILA MUTACIJA. Pomak primijenjen u svjetskom umjesto u kamerinom
    // sustavu ne razlikuje se u konacnoj pozi - prigusenje prihvaca samo korake koji smanje
    // gresku, pa solver doluta do istog odgovora. Razlikuje se u BRZINI, jer Gauss-Newton s
    // tocnim jakobijanom konvergira kvadratno. Izmjereno iz starta 10 cm i 3 stupnja:
    //
    //   koraka    tocno        pomak u svjetskom sustavu
    //     1       2.1e-01 px         1.07 px
    //     3       1.35e-05           3.83e-02
    //     5       1.50e-05           1.37e-03
    //
    //Prag 1e-3 je sedamdeset puta iznad tocnog i tridesetak puta ispod mutiranog

    {
        Engine::PoseSolveConfig three;
        three.maxIterations = 3;
        const Engine::Pose near = perturb(truth, glm::vec3(0.1f, -0.05f, 0.08f), glm::vec3(glm::radians(3.0f), 0.0f, 0.0f));
        const Engine::PoseSolveResult result = Engine::solvePose(clean.points, seen, clean.intrinsics, near, three);

        report.check("tri koraka su dosta", result.endMedian < 1e-3,
            fmt("reprojekcija %.1f -> %.2e px u tri koraka", result.startMedian, result.endMedian));
    }

    // -------------------------------------------------------------------------------
    // Kontrola: bez iteracija se nista ne smije popraviti
    // -------------------------------------------------------------------------------

    {
        Engine::PoseSolveConfig none;
        none.maxIterations = 0;
        const Engine::Pose near = perturb(truth, glm::vec3(0.1f, -0.05f, 0.08f), glm::vec3(glm::radians(3.0f), 0.0f, 0.0f));
        const Engine::PoseSolveResult result = Engine::solvePose(clean.points, seen, clean.intrinsics, near, none);

        report.check("bez iteracija nema popravka",
            result.endMedian == result.startMedian && result.startMedian > 1.0,
            fmt("reprojekcija ostala %.2f px", result.endMedian));
    }

    // -------------------------------------------------------------------------------
    // Sa sumom
    // -------------------------------------------------------------------------------

    {
        Engine::SyntheticConfig noisyConfig = config;
        noisyConfig.noisePixels = 0.5f;
        const Engine::SyntheticScene noisy = Engine::makeSyntheticScene(noisyConfig);
        const std::vector<Engine::PointObservation> noisySeen = observationsOf(noisy, camera);

        const Engine::Pose start = perturb(truth, glm::vec3(0.1f, -0.05f, 0.08f), glm::vec3(glm::radians(3.0f), 0.0f, 0.0f));
        const Engine::PoseSolveResult result = Engine::solvePose(noisy.points, noisySeen, noisy.intrinsics, start);

        const double distance = double(glm::length(result.pose.position - truth.position));
        const double angle = angleBetween(result.pose.orientation, truth.orientation);

        //Sum od pola piksela na 8 m i zaristu 600 px: poza ostaje unutar milimetara, a reprojekcija
        //padne na razinu samog suma - ne ispod nje, jer ispod nje nema sto biti
        report.check("sa sumom poza ostaje blizu, reprojekcija na razini suma",
            result.solved && distance < 0.02 && angle < 0.2 && result.endMedian > 0.2 && result.endMedian < 1.0,
            fmt("%.4f m, %.4f stupnjeva, reprojekcija %.2f -> %.3f px", distance, angle, result.startMedian, result.endMedian));
    }

    // -------------------------------------------------------------------------------
    // Premalo tocaka
    // -------------------------------------------------------------------------------

    {
        const std::vector<Engine::PointObservation> two(seen.begin(), seen.begin() + 2);
        const Engine::PoseSolveResult result = Engine::solvePose(clean.points, two, clean.intrinsics, truth);

        report.check("dvije tocke ne odredjuju pozu", !result.solved,
            "sest nepoznanica trazi barem tri tocke, i to mora reci");
    }

    return report.result();
}
