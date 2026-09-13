// S6: cijeli lanac - iz samih opazanja do poza i tocaka, bez ijedne poznate poze.
//
// Ovo je prvi test koji solveru ne daje NISTA osim onoga sto bi imao i na pravoj snimci: piksele i
// podatak koja opazanja pripadaju istoj tocki. Poze, dubine i mjerilo su ono sto mora izaci van.
//
// S CIME SE USPOREDJUJE: rekonstrukcija je tocna do dvije stvari koje se iz slika NE MOGU doznati -
// gdje je cijela scena u prostoru (gauge) i koliko je velika (mjerilo). Prva se ponistava tako da
// se istina izrazi u sustavu prve kamere, druga tako da se izmjeri i podijeli. Sve ostalo mora se
// poklopiti, i to je ono sto se ovdje mjeri.
#include "TestHarness.h"

#include <Engine/Reconstruct.h>
#include <Engine/SyntheticScene.h>

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

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

struct Comparison{
    double medianRotation = 0.0;
    double worstRotation = 0.0;
    double medianPosition = 0.0;
    double medianPoint = 0.0;
    double scale = 0.0;
};

//Istina izrazena u sustavu PRVE kamere, pa podijeljena mjerilom. Tek tada su dvije rekonstrukcije
//iste stvari usporedive
Comparison compare(const Engine::Reconstruction& state, const Engine::SyntheticScene& truth){
    Comparison out;
    const Engine::Pose& origin = truth.poses[0];

    std::vector<Engine::Pose> expected(truth.poses.size());
    for(size_t i = 0; i < truth.poses.size(); ++i){
        expected[i].orientation = glm::normalize(glm::conjugate(origin.orientation) * truth.poses[i].orientation);
        expected[i].position = glm::conjugate(origin.orientation) * (truth.poses[i].position - origin.position);
    }

    //Mjerilo iz najudaljenije rijesene kamere: sto dulja baza, to manje sum ulazi u sam omjer
    size_t farthest = 0;
    for(size_t i = 0; i < state.poses.size(); ++i){
        if(state.posed[i] && glm::length(expected[i].position) > glm::length(expected[farthest].position)) farthest = i;
    }
    const double truthLength = double(glm::length(expected[farthest].position));
    out.scale = truthLength > 0.0 ? double(glm::length(state.poses[farthest].position)) / truthLength : 1.0;
    if(out.scale <= 0.0) out.scale = 1.0;

    std::vector<double> rotations, positions, points;
    for(size_t i = 0; i < state.poses.size(); ++i){
        if(!state.posed[i]) continue;
        rotations.push_back(angleBetween(state.poses[i].orientation, expected[i].orientation));
        const glm::vec3 scaled = state.poses[i].position / float(out.scale);
        positions.push_back(double(glm::length(scaled - expected[i].position)));
    }
    for(size_t i = 0; i < state.points.size(); ++i){
        if(!state.solved[i]) continue;
        const glm::vec3 expectedPoint = glm::conjugate(origin.orientation) * (truth.points[i] - origin.position);
        const glm::vec3 scaled = state.points[i] / float(out.scale);
        points.push_back(double(glm::length(scaled - expectedPoint)));
    }

    out.medianRotation = medianOf(rotations);
    out.worstRotation = rotations.empty() ? 0.0 : *std::max_element(rotations.begin(), rotations.end());
    out.medianPosition = medianOf(positions);
    out.medianPoint = medianOf(points);
    return out;
}

}

int main(){
    TestReport report("S6 cijeli lanac");

    Engine::SyntheticConfig config;
    config.noisePixels = 0.5f;
    const Engine::SyntheticScene scene = Engine::makeSyntheticScene(config);

    // -------------------------------------------------------------------------------
    // Sa samim opazanjima, bez ijedne poze
    // -------------------------------------------------------------------------------

    {
        const Engine::Reconstruction state = Engine::reconstruct(scene.observations, scene.poses.size(),
                                                                 scene.points.size(), scene.intrinsics);
        const Comparison result = compare(state, scene);

        report.check("sve kamere i vecina tocaka su rijesene",
            state.ok && state.posedCameras == scene.poses.size() && state.solvedPoints > scene.points.size() * 9 / 10,
            fmt("%u od %zu kamera, %u od %zu tocaka", state.posedCameras, scene.poses.size(),
                state.solvedPoints, scene.points.size()));

        //PRAGOVI SU STEGNUTI NA ONO STO BUNDLE DAJE. S bundleom: rotacija 0.0211 st, polozaj
        //0.0033 m, tocke 0.0056 m, reprojekcija 0.530 px. Bez njega: 0.0400 st, 0.0052 m,
        //0.0101 m, 0.645 px - i s prijasnjim labavim pragovima je to prolazilo, pa se nije vidjelo
        //da bundle uopce nesto radi
        report.check("poze se poklapaju s istinom",
            result.medianRotation < 0.03 && result.worstRotation < 0.1 && result.medianPosition < 0.0045,
            fmt("rotacija medijan %.4f st, najgora %.4f st; polozaj %.4f m; mjerilo %.4f",
                result.medianRotation, result.worstRotation, result.medianPosition, result.scale));

        report.check("tocke se poklapaju s istinom",
            result.medianPoint < 0.008,
            fmt("medijan %.4f m", result.medianPoint));

        report.check("reprojekcija je na razini suma",
            state.medianReprojection > 0.2 && state.medianReprojection < 0.60,
            fmt("%.3f px", state.medianReprojection));
    }

    // -------------------------------------------------------------------------------
    // Isto, ali s promasenim poklapanjima
    // -------------------------------------------------------------------------------

    {
        Engine::SyntheticScene dirty = scene;
        std::mt19937 random(909);
        size_t spoiled = 0;
        for(Engine::Observation& observation : dirty.observations){
            if(double(random() % 1000000) / 1000000.0 >= 0.10) continue;
            observation.pixel = glm::vec2(float(random() % dirty.intrinsics.width), float(random() % dirty.intrinsics.height));
            ++spoiled;
        }

        const Engine::Reconstruction state = Engine::reconstruct(dirty.observations, dirty.poses.size(),
                                                                 dirty.points.size(), dirty.intrinsics);
        const Comparison result = compare(state, scene);

        report.check("s promasajima lanac i dalje stoji",
            state.ok && state.posedCameras == scene.poses.size() &&
            result.medianRotation < 0.3 && result.medianPosition < 0.05,
            fmt("%zu promasenih opazanja; %u kamera, rotacija %.4f st, polozaj %.4f m, tocke %.4f m",
                spoiled, state.posedCameras, result.medianRotation, result.medianPosition, result.medianPoint));
    }

    // -------------------------------------------------------------------------------
    // Uski luk: sto trpi a sto ne
    // -------------------------------------------------------------------------------
    //
    // Kad su svi kadrovi blizu jedan drugome, zrake se sijeku pod sitnim kutom i DUBINA je lose
    // odredjena. Kamere to ne osjete - drzi ih mnostvo tocaka. Izmjereno na istoj sceni:
    // tocke 0.079 m na luku od 6 stupnjeva naspram 0.0056 m na 80, uz jednaku reprojekciju.
    // To je geometrija, ne slabost solvera, i test to trazi kao razliku a ne kao gresku

    {
        Engine::SyntheticConfig narrowConfig = config;
        narrowConfig.arc = 0.1f;   //oko sest stupnjeva ukupno
        const Engine::SyntheticScene narrow = Engine::makeSyntheticScene(narrowConfig);

        const Engine::Reconstruction state = Engine::reconstruct(narrow.observations, narrow.poses.size(),
                                                                 narrow.points.size(), narrow.intrinsics);
        const Comparison result = compare(state, narrow);

        report.check("uski luk: kamere se i dalje rijese, a tocke su bitno losije",
            state.posedCameras == narrow.poses.size() &&
            result.medianRotation < 0.1 &&
            state.medianReprojection < 0.60 &&
            result.medianPoint > 5.0 * 0.0056,
            fmt("rotacija %.4f st, tocke %.4f m (siroki luk 0.0056 m), reprojekcija %.3f px",
                result.medianRotation, result.medianPoint, state.medianReprojection));
    }

    // -------------------------------------------------------------------------------
    // Kontrola: od besmislenih opazanja ne smije nastati rekonstrukcija
    // -------------------------------------------------------------------------------

    {
        Engine::SyntheticScene noise = scene;
        std::mt19937 random(31337);
        for(Engine::Observation& observation : noise.observations){
            observation.pixel = glm::vec2(float(random() % noise.intrinsics.width), float(random() % noise.intrinsics.height));
        }

        const Engine::Reconstruction state = Engine::reconstruct(noise.observations, noise.poses.size(),
                                                                 noise.points.size(), noise.intrinsics);

        report.check("od samog suma ne nastaje rekonstrukcija",
            !state.ok || state.posedCameras < scene.poses.size() || state.medianReprojection > 5.0,
            fmt("%u kamera, %u tocaka, reprojekcija %.2f px", state.posedCameras, state.solvedPoints, state.medianReprojection));
    }

    return report.result();
}
