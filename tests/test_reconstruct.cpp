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

    // -------------------------------------------------------------------------------
    // S10: paralaksa odlucuje sto ulazi u rezultat
    //
    // Dronska snimka je pokazala promasaj koji nijedna dosadasnja mjera ne kaznjava: cetvrtina
    // tocaka odleti u beskonacnost, a reprojekcija ostane 0.191 px - jer daleka tocka lezi na
    // obje zrake i uredno se reprojicira ma gdje po njima bila.
    //
    // Ovdje se isto radi na sintetickoj sceni s UZAKIM lukom, jer se problem vidi samo tamo gdje
    // su kutovi mali. Mjeri se bez mjerila: udaljenost tocaka u dosezima putanje kamere, jer
    // mjerilo rekonstrukcije je slobodno pa se sirovi metri ne daju usporedjivati.
    //
    // Uz svaku provjeru ide i mutacija: gasenjem provjere (maxRelativeDepthError = 0) rep se MORA
    // vratiti, inace test ne mjeri provjeru nego nesto drugo
    // -------------------------------------------------------------------------------

    {
        //Uvjeti su IZMJERENI, ne odabrani po osjecaju. Luk od 0.05 rad ne stvara rep uopce (sve
        //tocke su na slicnoj dubini pa im je paralaksa 2.9 st, daleko iznad praga); rep se rodi
        //tek ispod 0.02 rad. Uzet je 0.01 rad uz tocke razvucene u dubinu, gdje je razlika
        //najcisca: 178 tocaka i rep 604 bez provjere, 55 tocaka i rep 0.75 s njom.
        //
        //IZMJERENA GRANICA, da se ne prodaje kao lijek za sve: uz luk 0.02 i dubinu 60 provjera
        //NE pomaze (rep 14441 s njom, 11380 bez nje). Tamo prezivi tocka koja ima siroku
        //paralaksu a lose je odredjena iz drugog razloga. Paralaksa je dakle nuzan uvjet, ne
        //dovoljan
        Engine::SyntheticConfig narrowConfig = config;
        narrowConfig.arc = 0.01f;
        narrowConfig.extent = glm::vec3(3.0f, 2.0f, 20.0f);
        narrowConfig.noisePixels = 0.5f;
        const Engine::SyntheticScene narrow = Engine::makeSyntheticScene(narrowConfig);

        //Rep se mjeri u dosezima putanje: sredina oblaka po medijanu, doseg po najdaljoj kameri
        auto tailOf = [](const Engine::Reconstruction& result){
            std::vector<float> axis[3];
            for(size_t i = 0; i < result.points.size(); ++i){
                if(!result.solved[i]) continue;
                for(int a = 0; a < 3; ++a) axis[a].push_back(result.points[i][a]);
            }
            glm::vec3 middle(0.0f);
            for(int a = 0; a < 3; ++a){
                if(axis[a].empty()) continue;
                std::sort(axis[a].begin(), axis[a].end());
                middle[a] = axis[a][axis[a].size() / 2];
            }

            float extent = 0.0f;
            for(size_t i = 0; i < result.poses.size(); ++i){
                if(result.posed[i]) extent = std::max(extent, glm::length(result.poses[i].position - middle));
            }

            std::vector<double> distance;
            for(size_t i = 0; i < result.points.size(); ++i){
                if(result.solved[i]) distance.push_back(double(glm::length(result.points[i] - middle)));
            }
            std::sort(distance.begin(), distance.end());
            if(distance.empty() || extent <= 0.0f) return 0.0;
            return distance[size_t(0.99 * double(distance.size() - 1))] / double(extent);
        };

        //MUTACIJA MORA UGASITI OBA PRAGA. Provjera paralakse ima dva izvora - izvedeni iz
        //zarista i apsolutni pod - pa gasenje samo jednog ostavi drugi da radi, i detektor
        //prestane detektirati a da to nigdje ne pise
        //JEDAN POCETNI PAR U OBA, i to je ovdje uvjet a ne postavka. Visestruki pokusaj bira izmedju
        //gotovih rjesenja po broju kamera i po bazi, a te dvije postavke mijenjaju bas ono sto se u
        //pokusaju mjeri - pa bi dva niza zavrsila na RAZLICITIM pocetnim parovima i usporedba vise
        //ne bi mjerila provjeru paralakse nego izbor para
        Engine::ReconstructConfig without;
        without.maxRelativeDepthError = 0.0;
        without.minParallaxDegrees = 0.0;
        without.initialPairTrials = 1;

        //A izvedeni prag se mjeri s ugasenim podom, inace se mjeri pod
        Engine::ReconstructConfig derivedOnly;
        derivedOnly.minParallaxDegrees = 0.0;
        derivedOnly.initialPairTrials = 1;

        const Engine::Reconstruction guarded = Engine::reconstruct(narrow.observations, narrow.poses.size(),
                                                                   narrow.points.size(), narrow.intrinsics,
                                                                   derivedOnly);
        const Engine::Reconstruction bare = Engine::reconstruct(narrow.observations, narrow.poses.size(),
                                                                narrow.points.size(), narrow.intrinsics, without);

        std::printf("      uzak luk: s provjerom %u tocaka rep %.2f, bez provjere %u tocaka rep %.2f\n",
                    guarded.solvedPoints, tailOf(guarded), bare.solvedPoints, tailOf(bare));

        report.check("prag se izvodi iz zarista, a ne zadaje",
            guarded.parallaxLimitDegrees > 0.2 && guarded.parallaxLimitDegrees < 0.5,
            fmt("f = %.0f px, sum 0.5 px, dopusteno 15%% dubine -> %.3f st",
                double(narrow.intrinsics.fx), guarded.parallaxLimitDegrees));

        //APSOLUTNI POD. Izvedeni prag je geometrijski tocan, ali pada s duljinom optike: isti
        //broj 0.15 daje 0.318 st na f = 600 i 0.036 st na f = 5285. Tocka koja se vidi pod tri
        //stotinke stupnja je za postavljanje sljedece kamere degenerirana ma kako joj dubina
        //ispala, i to je na pravoj snimci bila razlika izmedju 45 i svih 65 kamera.
        //
        //Uzima se ono sto je STROŽE, pa pod mora nadvladati manji izvedeni prag i ustupiti
        //vecem - obje strane se provjeravaju, jer max koji je zapravo min prolazi prvu
        Engine::ReconstructConfig highFloor;
        highFloor.minParallaxDegrees = 2.0;      //iznad izvedenih 0.318
        const Engine::Reconstruction floored = Engine::reconstruct(narrow.observations, narrow.poses.size(),
                                                                   narrow.points.size(), narrow.intrinsics,
                                                                   highFloor);

        Engine::ReconstructConfig lowFloor;
        lowFloor.minParallaxDegrees = 0.05;      //ispod izvedenih 0.318
        const Engine::Reconstruction unfloored = Engine::reconstruct(narrow.observations, narrow.poses.size(),
                                                                     narrow.points.size(), narrow.intrinsics,
                                                                     lowFloor);

        report.check("pod paralakse je strozi od oba",
            std::fabs(floored.parallaxLimitDegrees - 2.0) < 1e-9
            && std::fabs(unfloored.parallaxLimitDegrees - guarded.parallaxLimitDegrees) < 1e-9,
            fmt("pod 2.0 -> %.3f st, pod 0.05 -> %.3f st (izvedeno %.3f)",
                floored.parallaxLimitDegrees, unfloored.parallaxLimitDegrees,
                guarded.parallaxLimitDegrees));

        report.check("bez provjere rep odleti", tailOf(bare) > 50.0,
            fmt("p99 udaljenosti %.1f dosega putanje", tailOf(bare)));

        report.check("s provjerom repa nema", tailOf(guarded) < 3.0,
            fmt("p99 udaljenosti %.2f dosega putanje", tailOf(guarded)));

        //Provjera ne smije platiti kamerama: tocka slabe dubine i dalje dobro vodi rotaciju, pa
        //je prag tijekom gradnje blazi. Da nije, ostali bismo bez kamera - izmjereno na dronskoj
        //snimci, gdje puni prag tijekom gradnje rijesi 2 od 24
        report.check("kamere ostaju sve", guarded.posedCameras == bare.posedCameras,
            fmt("%u naspram %u kamera", guarded.posedCameras, bare.posedCameras));

        report.check("reprojekcija se ne pokvari",
            guarded.medianReprojection < 1.5 * bare.medianReprojection,
            fmt("%.3f px s provjerom naspram %.3f px bez nje",
                guarded.medianReprojection, bare.medianReprojection));
    }

    //-- pocetni par: javlja se, dade se zadati, i vise pokusaja ne kvari ---------------------
    //
    //Cijela rekonstrukcija visi o prvom paru. Izmjereno na pravoj snimci: na ISTOM grafu par 86-89
    //daje 4.69 st greske rotacije, a par 80-89 daje 119.94 st - a oba prolaze sve provjere koje
    //izbor para ima. Razlika se ne vidi dok se scena ne izgradi do kraja.
    //
    //Zato postoje tri stvari, i sve tri se ovdje brane:
    //
    //  javlja se     koji je par izabran i kakav je bio, inace se na pitanje "odakle je krenulo"
    //                ne da odgovoriti bez prekapanja po kodu
    //  dade se zadati  bez toga se ne moze izmjeriti je li kriv izbor para ili sve ostalo
    //  vise pokusaja  gradi se nekoliko kandidata do kraja i zadrzi najbolji. Na cistoj sintetici
    //                nema sto popraviti, pa se ovdje brani da NE POKVARI - isto pravilo koje je
    //                palo conflictFreeMerge
    {
        const Engine::Reconstruction once = Engine::reconstruct(scene.observations, scene.poses.size(),
                                                               scene.points.size(), scene.intrinsics);

        report.check("pocetni par se javlja",
            once.initialA != once.initialB
            && once.posed[once.initialA] && once.posed[once.initialB]
            && once.initialAngle > 0.0 && once.initialPoints >= 8,
            fmt("par %u-%u, kut %.2f st, %u tocaka", once.initialA, once.initialB,
                once.initialAngle, once.initialPoints));

        report.check("baza rjesenja se javlja",
            once.medianTriangulationAngle > 0.0,
            fmt("%.2f st", once.medianTriangulationAngle));

        //Zadani par: uzima se drugi od onoga koji je racun sam izabrao, pa se provjerava da je
        //stvarno posluzan
        Engine::ReconstructConfig forced;
        forced.forceInitialA = 0;
        forced.forceInitialB = uint32_t(scene.poses.size() / 2);
        if(forced.forceInitialB == 0) forced.forceInitialB = 1;

        const Engine::Reconstruction chosen = Engine::reconstruct(scene.observations, scene.poses.size(),
                                                                  scene.points.size(), scene.intrinsics, forced);

        const bool honoured = (chosen.initialA == forced.forceInitialA && chosen.initialB == forced.forceInitialB)
                           || (chosen.initialA == forced.forceInitialB && chosen.initialB == forced.forceInitialA);

        report.check("zadani pocetni par se postuje",
            chosen.ok && honoured,
            fmt("trazeno %u-%u, dobiveno %u-%u", forced.forceInitialA, forced.forceInitialB,
                chosen.initialA, chosen.initialB));

        Engine::ReconstructConfig many;
        many.initialPairTrials = 3;
        const Engine::Reconstruction best = Engine::reconstruct(scene.observations, scene.poses.size(),
                                                                scene.points.size(), scene.intrinsics, many);

        report.check("vise pokusaja ne kvari",
            best.ok && best.posedCameras >= once.posedCameras
            && (best.posedCameras > once.posedCameras
                || best.medianTriangulationAngle >= once.medianTriangulationAngle - 1e-9),
            fmt("%u kamera i baza %.3f st naspram %u i %.3f",
                best.posedCameras, best.medianTriangulationAngle,
                once.posedCameras, once.medianTriangulationAngle));

        const Comparison result = compare(best, scene);
        const Comparison plain = compare(once, scene);
        report.check("vise pokusaja i dalje pogadja istinu",
            result.medianRotation < 2.0 * std::max(0.01, plain.medianRotation)
            && result.medianPosition < 2.0 * std::max(0.001, plain.medianPosition),
            fmt("rotacija %.4f st i polozaj %.4f m, naspram %.4f i %.4f s jednim pokusajem",
                result.medianRotation, result.medianPosition,
                plain.medianRotation, plain.medianPosition));
    }

    return report.result();
}
