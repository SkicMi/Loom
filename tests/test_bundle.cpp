// S3: bundle adjustment - poze i tocke se popravljaju ZAJEDNO, preko Schurovog komplementa.
//
// PnP (S2) popravlja pozu uz nepomicne tocke, triangulacija (S1) tocke uz nepomicne poze. Svaka
// vjeruje onome sto joj je dano, pa se greska seli s jedne strane na drugu. Ovdje su nepoznanice
// oboje: 6 po kameri i 3 po tocki, za ovu scenu 42 + 900.
//
// Sto se brani:
//
//   jakobijan po tocki   protiv numericke derivacije, prema skali retka - isto pravilo koje je u
//                        S2 otkrilo obrnut predznak
//   zajedno je bolje     BA mora dati tocke bitno blize istini nego triangulacija s istim (krivim)
//                        pozama. To je razlog zasto BA uopce postoji, pa je i mjereno
//   bez suma             sve se mora vratiti na istinu do zaokruzivanja
//   sa sumom             reprojekcija padne na razinu suma, a poze i tocke ostanu blizu
//   sidro miruje         fiksna kamera se NE SMIJE pomaknuti ni za bit; bez nje cijelo rjesenje
//                        smije kliziti kroz prostor i usporedba s istinom gubi smisao
//   bez iteracija        kontrola da posao rade koraci, a ne sama pocetna pretpostavka
#include "TestHarness.h"

#include <Engine/Bundle.h>
#include <Engine/SyntheticScene.h>
#include <Engine/Triangulate.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace{

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double medianPointError(const std::vector<glm::vec3>& got, const std::vector<glm::vec3>& truth){
    std::vector<double> errors;
    for(size_t i = 0; i < truth.size() && i < got.size(); ++i) errors.push_back(double(glm::length(got[i] - truth[i])));
    return medianOf(errors);
}

double medianPoseError(const std::vector<Engine::Pose>& got, const std::vector<Engine::Pose>& truth){
    std::vector<double> errors;
    for(size_t i = 0; i < truth.size() && i < got.size(); ++i) errors.push_back(double(glm::length(got[i].position - truth[i].position)));
    return medianOf(errors);
}

//MJERILO JE SLOBODNO. Uz fiksnu kameru 0 ostaje jedan neodredjen stupanj slobode: skaliraj sve
//udaljenosti oko nje i nijedno opazanje se ne promijeni. Reprojekcija te dvije scene ne moze
//razlikovati, pa se s istinom usporedjuje OBLIK - mjerilo se izmjeri iz baze prema drugoj kameri
//i ponisti. U pravom radu mjerilo dolazi izvana (poznata baza ili senzor), ne iz slika
double scaleOf(const std::vector<Engine::Pose>& got, const std::vector<Engine::Pose>& truth){
    const double mine = double(glm::length(got[1].position - got[0].position));
    const double real = double(glm::length(truth[1].position - truth[0].position));
    return real > 0.0 ? mine / real : 1.0;
}

glm::vec3 unscale(const glm::vec3& value, const glm::vec3& anchor, double scale){
    return anchor + (value - anchor) / float(scale);
}

//Poremecaj koji je uvijek isti: kamere 1.. u stranu i malo zakrenute, tocke razasute oko istine.
//Kamera 0 se NE dira - ona je sidro, i da je pomaknuta pa fiksirana, cijelo bi rjesenje legitimno
//sjelo u njezin krivi sustav
void disturb(std::vector<Engine::Pose>& poses, std::vector<glm::vec3>& points){
    for(size_t i = 1; i < poses.size(); ++i){
        const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        poses[i].position += poses[i].orientation * glm::vec3(0.05f * sign, -0.03f, 0.04f * sign);
        poses[i].orientation = glm::normalize(poses[i].orientation *
            glm::angleAxis(glm::radians(1.0f * sign), glm::normalize(glm::vec3(0.3f, 1.0f, -0.2f))));
    }
    for(size_t i = 0; i < points.size(); ++i){
        const float a = float((i * 37) % 19) / 19.0f - 0.5f;
        const float b = float((i * 53) % 23) / 23.0f - 0.5f;
        const float c = float((i * 71) % 29) / 29.0f - 0.5f;
        points[i] += 0.06f * glm::vec3(a, b, c);
    }
}

}

int main(){
    TestReport report("S3 bundle adjustment");

    Engine::SyntheticConfig config;
    const Engine::SyntheticScene clean = Engine::makeSyntheticScene(config);

    // -------------------------------------------------------------------------------
    // Jakobijan po tocki protiv numericke derivacije
    // -------------------------------------------------------------------------------

    {
        const double h = 1e-3;
        double worst = 0.0;
        size_t compared = 0;

        for(size_t i = 0; i < clean.observations.size(); i += 91){
            const Engine::Observation& observation = clean.observations[i];
            const Engine::Pose& pose = clean.poses[observation.camera];
            //Tocka namjerno pomaknuta s istine, da reziduali ne budu nula
            const glm::vec3 point = clean.points[observation.point] + glm::vec3(0.04f, -0.03f, 0.05f);

            double residual[2], jacobian[2][3];
            if(!Engine::pointJacobian(pose, clean.intrinsics, point, observation.pixel, residual, jacobian)) continue;

            double rowScale[2] = {1.0, 1.0};
            for(int row = 0; row < 2; ++row){
                for(int column = 0; column < 3; ++column) rowScale[row] = std::max(rowScale[row], std::fabs(jacobian[row][column]));
            }

            for(int parameter = 0; parameter < 3; ++parameter){
                glm::vec3 step(0.0f);
                step[parameter] = float(h);

                double plus[2], minus[2], ignored[2][3];
                if(!Engine::pointJacobian(pose, clean.intrinsics, point + step, observation.pixel, plus, ignored)) continue;
                if(!Engine::pointJacobian(pose, clean.intrinsics, point - step, observation.pixel, minus, ignored)) continue;

                for(int row = 0; row < 2; ++row){
                    const double numeric = (plus[row] - minus[row]) / (2.0 * h);
                    worst = std::max(worst, std::fabs(numeric - jacobian[row][parameter]) / rowScale[row]);
                    ++compared;
                }
            }
        }

        report.check("jakobijan po tocki se slaze s numerickom derivacijom", compared > 30 && worst < 1e-3,
            fmt("najveca relativna razlika %.2e kroz %zu clanova", worst, compared));
    }

    // -------------------------------------------------------------------------------
    // Bez suma: sve se vraca na istinu
    // -------------------------------------------------------------------------------

    {
        std::vector<Engine::Pose> poses = clean.poses;
        std::vector<glm::vec3> points = clean.points;
        disturb(poses, points);

        const double poseBefore = medianPoseError(poses, clean.poses);
        const double pointBefore = medianPointError(points, clean.points);

        const Engine::BundleResult result = Engine::bundleAdjust(clean.observations, poses, points, clean.intrinsics);

        //Mjerilo se izmjeri i ponisti prije usporedbe - vidi scaleOf
        const double scale = scaleOf(result.poses, clean.poses);
        std::vector<Engine::Pose> aligned = result.poses;
        std::vector<glm::vec3> alignedPoints = result.points;
        for(Engine::Pose& pose : aligned) pose.position = unscale(pose.position, result.poses[0].position, scale);
        for(glm::vec3& point : alignedPoints) point = unscale(point, result.poses[0].position, scale);

        const double poseAfter = medianPoseError(aligned, clean.poses);
        const double pointAfter = medianPointError(alignedPoints, clean.points);

        report.check("bez suma se oblik vraca na istinu",
            result.solved && result.endMedian < 1e-2 && poseAfter < 1e-4 && pointAfter < 1e-4,
            fmt("poze %.4f -> %.2e m, tocke %.4f -> %.2e m, mjerilo %.6f, reprojekcija %.2f -> %.2e px kroz %u koraka",
                poseBefore, poseAfter, pointBefore, pointAfter, scale, result.startMedian, result.endMedian, result.iterations));

        //Sidro: bit po bit, jer fiksna kamera nema nepoznanica
        report.check("sidro miruje",
            result.poses[0].position == clean.poses[0].position && result.poses[0].orientation == clean.poses[0].orientation,
            "prva kamera je ostala tocno gdje je bila");
    }

    // -------------------------------------------------------------------------------
    // Zajedno je bolje nego svaki korak za sebe
    // -------------------------------------------------------------------------------

    {
        std::vector<Engine::Pose> poses = clean.poses;
        std::vector<glm::vec3> points = clean.points;
        disturb(poses, points);

        //Triangulacija s tim istim (krivim) pozama - najbolje sto S1 sam moze
        std::vector<uint8_t> solved;
        const std::vector<glm::vec3> triangulated = Engine::triangulateAll(clean.observations, poses,
                                                                           clean.intrinsics, clean.points.size(), solved);
        const double fromTriangulation = medianPointError(triangulated, clean.points);

        const Engine::BundleResult result = Engine::bundleAdjust(clean.observations, poses, points, clean.intrinsics);
        const double scale = scaleOf(result.poses, clean.poses);
        std::vector<glm::vec3> alignedPoints = result.points;
        for(glm::vec3& point : alignedPoints) point = unscale(point, result.poses[0].position, scale);
        const double fromBundle = medianPointError(alignedPoints, clean.points);

        report.check("zajedno je bolje nego samo triangulacija",
            fromBundle < 0.1 * fromTriangulation,
            fmt("tocke %.4f m s krivim pozama, %.2e m kad se poze popravljaju zajedno", fromTriangulation, fromBundle));
    }

    // -------------------------------------------------------------------------------
    // Sa sumom
    // -------------------------------------------------------------------------------

    {
        Engine::SyntheticConfig noisyConfig = config;
        noisyConfig.noisePixels = 0.5f;
        const Engine::SyntheticScene noisy = Engine::makeSyntheticScene(noisyConfig);

        std::vector<Engine::Pose> poses = noisy.poses;
        std::vector<glm::vec3> points = noisy.points;
        disturb(poses, points);

        const Engine::BundleResult result = Engine::bundleAdjust(noisy.observations, poses, points, noisy.intrinsics);

        const double scale = scaleOf(result.poses, noisy.poses);
        std::vector<Engine::Pose> aligned = result.poses;
        std::vector<glm::vec3> alignedPoints = result.points;
        for(Engine::Pose& pose : aligned) pose.position = unscale(pose.position, result.poses[0].position, scale);
        for(glm::vec3& point : alignedPoints) point = unscale(point, result.poses[0].position, scale);

        const double poseAfter = medianPoseError(aligned, noisy.poses);
        const double pointAfter = medianPointError(alignedPoints, noisy.points);

        //Reprojekcija ne smije pasti ispod suma: ispod njega nema sto biti, a solver koji to tvrdi
        //zapravo je savio scenu oko suma
        report.check("sa sumom: reprojekcija na razini suma, poze i tocke blizu",
            result.solved && result.endMedian > 0.2 && result.endMedian < 1.0 && poseAfter < 0.01 && pointAfter < 0.05,
            fmt("poze %.4f m, tocke %.4f m, reprojekcija %.2f -> %.3f px", poseAfter, pointAfter, result.startMedian, result.endMedian));
    }

    // -------------------------------------------------------------------------------
    // Brzina: uvrstavanje tocaka mora znati za pomak kamera
    // -------------------------------------------------------------------------------
    //
    // I OVU PROVJERU JE TRAZILA MUTACIJA. Kad se u uvrstavanju izbaci clan E' dc - dakle kad se
    // tocke poprave bez veze s time kako su se pomaknule kamere - solver i dalje stigne do istog
    // odgovora, jer prigusenje prihvaca samo korake koji smanje gresku. Razlikuje se BRZINA:
    //
    //   koraka    tocno          bez veze s kamerama
    //     1     2.78e-02 px            2.48 px
    //     2     1.17e-04               0.46
    //     3     2.40e-05               0.27
    //     6     2.40e-05               0.044
    //
    //Prag 1e-3 je cetrdesetak puta iznad tocnog i dvjesto puta ispod mutiranog

    {
        std::vector<Engine::Pose> poses = clean.poses;
        std::vector<glm::vec3> points = clean.points;
        disturb(poses, points);

        Engine::BundleConfig three;
        three.maxIterations = 3;
        const Engine::BundleResult result = Engine::bundleAdjust(clean.observations, poses, points, clean.intrinsics, three);

        report.check("tri koraka su dosta", result.endMedian < 1e-3,
            fmt("reprojekcija %.2f -> %.2e px u tri koraka", result.startMedian, result.endMedian));
    }

    // -------------------------------------------------------------------------------
    // Kontrola: bez iteracija se nista ne mijenja
    // -------------------------------------------------------------------------------

    {
        std::vector<Engine::Pose> poses = clean.poses;
        std::vector<glm::vec3> points = clean.points;
        disturb(poses, points);

        Engine::BundleConfig none;
        none.maxIterations = 0;
        const Engine::BundleResult result = Engine::bundleAdjust(clean.observations, poses, points, clean.intrinsics, none);

        report.check("bez iteracija nema popravka",
            result.endMedian == result.startMedian && result.points == points && result.startMedian > 1.0,
            fmt("reprojekcija ostala %.2f px", result.endMedian));
    }

    return report.result();
}
