#include "Engine/Reconstruct.h"
#include "Engine/Triangulate.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <unordered_map>

namespace Engine{
namespace{

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

//Reprojekcija po opazanjima koja su usla u rjesenje. keep, ako nije prazan, izuzima ona koja je
//ciscenje odbacilo - vidi Reconstruction::medianReprojection
double medianOver(const std::vector<Observation>& observations, const Reconstruction& state,
                  const Intrinsics& intrinsics, const std::vector<uint8_t>& keep = {}){
    std::vector<double> errors;
    for(size_t index = 0; index < observations.size(); ++index){
        if(!keep.empty() && !keep[index]) continue;
        const Observation& observation = observations[index];
        if(!state.posed[observation.camera] || !state.solved[observation.point]) continue;
        glm::vec2 pixel;
        if(!project(state.poses[observation.camera], intrinsics, state.points[observation.point], pixel)){
            errors.push_back(double(intrinsics.width));
            continue;
        }
        errors.push_back(double(glm::length(pixel - observation.pixel)));
    }
    return medianOf(errors);
}

}

void refreshReconstructionDiagnostics(const std::vector<Observation>& observations,
                                      const Intrinsics& intrinsics,
                                      const ReconstructConfig& config,
                                      Reconstruction& state){
    state.medianReprojection = medianOver(observations, state, intrinsics,
                                          state.observationUsed);
    state.heldOutObservations = 0;
    state.heldOutReprojection = 0.0;

    std::vector<std::vector<const Observation*>> byPoint(state.points.size());
    for(const Observation& observation : observations){
        if(observation.camera >= state.poses.size() || observation.point >= state.points.size())
            continue;
        byPoint[observation.point].push_back(&observation);
    }

    if(config.holdOutEvery > 1){
        std::vector<uint32_t> left(byPoint.size(), 0);
        for(size_t point = 0; point < byPoint.size(); ++point)
            left[point] = uint32_t(byPoint[point].size());

        std::vector<double> errors;
        for(size_t index = 0; index < observations.size(); index += config.holdOutEvery){
            const Observation& observation = observations[index];
            if(observation.camera >= state.poses.size() || observation.point >= state.points.size())
                continue;
            if(left[observation.point] <= 3) continue;
            --left[observation.point];
            if(!state.posed[observation.camera] || !state.solved[observation.point]) continue;

            glm::vec2 pixel;
            if(!project(state.poses[observation.camera], intrinsics,
                        state.points[observation.point], pixel)) continue;
            errors.push_back(double(glm::length(pixel - observation.pixel)));
        }
        state.heldOutObservations = uint32_t(errors.size());
        state.heldOutReprojection = medianOf(errors);
    }

    std::vector<double> angles;
    angles.reserve(state.points.size());
    for(size_t point = 0; point < state.points.size(); ++point){
        if(!state.solved[point]) continue;
        std::vector<View> views;
        for(const Observation* observation : byPoint[point]){
            const size_t index = size_t(observation - observations.data());
            if(index >= state.observationUsed.size() || !state.observationUsed[index]) continue;
            if(state.posed[observation->camera])
                views.push_back(View{observation->camera, observation->pixel});
        }
        if(views.size() < 2) continue;
        angles.push_back(parallaxDegrees(state.poses, intrinsics, views));
    }
    state.medianTriangulationAngle = medianOf(angles);
}

Reconstruction reconstructImpl(const std::vector<Observation>& observations,
                               size_t cameraCount,
                               size_t pointCount,
                               const Intrinsics& intrinsics,
                               const ReconstructConfig& config,
                               bool selectInitialPairOnly = false){
    using Clock = std::chrono::steady_clock;
    auto elapsed = [](Clock::time_point start){
        return std::chrono::duration<double>(Clock::now() - start).count();
    };
    //=================================================================================
    // VISE POCETNIH PAROVA, svaki do kraja - vidi ReconstructConfig::initialPairTrials.
    //
    // Izvodi se rekurzivno: svaki pokusaj je obicna rekonstrukcija sa ZADANIM parom i jednim
    // pokusajem, pa se ovdje samo bira izmedju gotovih rjesenja. Time se ne udvaja nijedan korak
    // gradnje i nijedan uvjet ne moze se razici izmedju dva puta
    //=================================================================================
    //=================================================================================
    // SAV U LANCU - vidi ReconstructConfig::seamFactor.
    //
    // Izvodi se, kao i visestruki pocetni par, preko obicnog poziva: rjesenje se izgradi kako bi se
    // i inace izgradilo, pa se u njemu potrazi sav; ako ga ima, gradi se jos jednom uz zadano
    // odbacivanje repa. Zadrzava se bolje od dvoga
    //=================================================================================
    if(config.seamFactor > 0.0 && config.keepTo == 0){
        ReconstructConfig plain = config;
        plain.seamFactor = 0.0;

        Reconstruction first = reconstruct(observations, cameraCount, pointCount, intrinsics, plain);
        if(!first.ok) return first;

        //NAS VLASTITI korak, bez ikakve istine izvana
        auto turnBetween = [&](const Reconstruction& state, size_t a, size_t b){
            const glm::dmat3 one = glm::dmat3(glm::mat3_cast(state.poses[a].orientation));
            const glm::dmat3 two = glm::dmat3(glm::mat3_cast(state.poses[b].orientation));
            const glm::dmat3 difference = glm::transpose(one) * two;
            const double cosine = std::max(-1.0, std::min(1.0,
                (difference[0][0] + difference[1][1] + difference[2][2] - 1.0) * 0.5));
            return glm::degrees(std::acos(cosine));
        };

        std::vector<double> steps;
        std::vector<size_t> stepAt;
        for(size_t camera = 0; camera + 1 < cameraCount; ++camera){
            if(!first.posed[camera] || !first.posed[camera + 1]) continue;
            steps.push_back(turnBetween(first, camera, camera + 1));
            stepAt.push_back(camera + 1);
        }

        std::vector<size_t> seams;
        if(steps.size() >= 5){
            std::vector<double> sorted = steps;
            std::sort(sorted.begin(), sorted.end());
            const double middle = sorted[sorted.size() / 2];
            const double limit = config.seamFactor * middle;

            for(size_t i = 0; i < steps.size(); ++i){
                if(steps[i] <= limit) continue;
                seams.push_back(stepAt[i]);
            }
        }

        first.seamsFound = uint32_t(seams.size());
        if(!seams.empty()) first.seamAt = uint32_t(seams.front());
        if(seams.empty()) return first;

        //DIO KOJI SADRZI POCETNI PAR - vidi ReconstructConfig::keepFrom. Savovi dijele niz na
        //odsjecke; zadrzava se onaj u kojem je sjeme, jer je jedino on gradjen od necega poznatog
        size_t keepFrom = 0, keepTo = cameraCount;
        for(size_t seam : seams){
            if(seam <= size_t(first.initialA) && seam <= size_t(first.initialB)) keepFrom = seam;
        }
        for(size_t index = seams.size(); index-- > 0;){
            const size_t seam = seams[index];
            if(seam > size_t(first.initialA) && seam > size_t(first.initialB)) keepTo = seam;
        }
        if(keepTo <= keepFrom) return first;

        ReconstructConfig cut = config;
        cut.seamFactor = 0.0;
        cut.keepFrom = uint32_t(keepFrom);
        cut.keepTo = uint32_t(keepTo);

        //ISTI POCETNI PAR, jedan pokusaj. Ne mijenja se sjeme nego sto se oko njega gradi, pa bi
        //ponovno biranje para samo trostruko produljilo racun i unijelo drugu promjenu u isti pokus
        cut.forceInitialA = first.initialA;
        cut.forceInitialB = first.initialB;
        cut.initialPairTrials = 1;

        const Reconstruction again = reconstruct(observations, cameraCount, pointCount, intrinsics, cut);

        //MJERA JE BROJ PREOSTALIH SAVOVA, pa tek onda kamere i baza. Sav je upravo ono sto se
        //popravlja, a kamera i baza ne bi ga razlikovale - rjesenje s dva sava ima jednako kamera i
        //cesto sirу bazu od onoga bez njih
        auto seamPositions = [&](const Reconstruction& state){
            std::vector<size_t> found;
            std::vector<double> turns;
            std::vector<size_t> at;
            for(size_t camera = 0; camera + 1 < cameraCount; ++camera){
                if(!state.posed[camera] || !state.posed[camera + 1]) continue;
                turns.push_back(turnBetween(state, camera, camera + 1));
                at.push_back(camera + 1);
            }
            if(turns.size() < 5) return found;

            std::vector<double> sorted = turns;
            std::sort(sorted.begin(), sorted.end());
            const double limit = config.seamFactor * sorted[sorted.size() / 2];
            for(size_t i = 0; i < turns.size(); ++i) if(turns[i] > limit) found.push_back(at[i]);
            return found;
        };

        auto seamsIn = [&](const Reconstruction& state){
            std::vector<double> turns;
            for(size_t camera = 0; camera + 1 < cameraCount; ++camera){
                if(!state.posed[camera] || !state.posed[camera + 1]) continue;
                turns.push_back(turnBetween(state, camera, camera + 1));
            }
            if(turns.size() < 5) return size_t(0);
            std::vector<double> sorted = turns;
            std::sort(sorted.begin(), sorted.end());
            const double limit = config.seamFactor * sorted[sorted.size() / 2];
            size_t count = 0;
            for(double turn : turns) if(turn > limit) ++count;
            return count;
        };

        Reconstruction best = first;
        const size_t before = seams.size();
        const size_t after = again.ok ? seamsIn(again) : before + 1;

        if(again.ok && (after < before
                     || (after == before && again.posedCameras > first.posedCameras))){
            best = again;
            best.seamRepaired = 1;
        }
        best.seamAt = uint32_t(seams.front());
        best.seamsFound = uint32_t(before);

        //=============================================================================
        // NAJVECI ZDRAVI ODSJECAK - vidi ReconstructConfig::keepLargestHealthySegment.
        //
        // Ako sav i nakon popravka ostane, rjesenje se proteze preko loma: dva dijela koja se
        // medjusobno ne slazu, a svaki je u sebi uredan. Umjesto takvog rjesenja zadrzi se
        // najdulji niz kadrova bez sava unutar sebe.
        //
        // Odsjecak se ne odreze na gotovom rjesenju nego se gradi IZNOVA samo iz njega: tocke
        // koje su nastale pod utjecajem pokvarenog dijela tako ispadnu same, a sve brojke -
        // reprojekcija, baza, izdvojena opazanja - izracunaju se nad onim sto se stvarno
        // isporucuje
        //=============================================================================
        if(!config.keepLargestHealthySegment) return best;

        const std::vector<size_t> remaining = seamPositions(best);
        if(remaining.empty()) return best;

        //Granice odsjecaka: pocetak, svaki sav, kraj
        std::vector<size_t> edges;
        edges.push_back(0);
        for(size_t seam : remaining) edges.push_back(seam);
        edges.push_back(cameraCount);

        size_t bestFrom = 0, bestTo = cameraCount, bestCount = 0;
        for(size_t i = 0; i + 1 < edges.size(); ++i){
            size_t count = 0;
            for(size_t camera = edges[i]; camera < edges[i + 1]; ++camera){
                if(best.posed[camera]) ++count;
            }
            if(count > bestCount){ bestCount = count; bestFrom = edges[i]; bestTo = edges[i + 1]; }
        }

        //Odsjecak koji ni sam nema dovoljno kamera nije rjesenje nego ostatak
        if(bestCount < 4 || bestCount == best.posedCameras) return best;

        ReconstructConfig trim = config;
        trim.seamFactor = 0.0;
        trim.keepLargestHealthySegment = false;
        trim.keepFrom = uint32_t(bestFrom);
        trim.keepTo = uint32_t(bestTo);
        trim.reAddAfterTrim = false;
        trim.forceInitialA = best.initialA;
        trim.forceInitialB = best.initialB;
        trim.initialPairTrials = 1;

        //Sjeme mora biti U odsjecku; inace se bira kao i inace
        if(best.initialA < bestFrom || best.initialA >= bestTo ||
           best.initialB < bestFrom || best.initialB >= bestTo){
            trim.forceInitialA = 0;
            trim.forceInitialB = 0;
            trim.initialPairTrials = config.initialPairTrials;
        }

        Reconstruction healthy = reconstruct(observations, cameraCount, pointCount, intrinsics, trim);
        if(!healthy.ok || seamPositions(healthy).size() >= remaining.size()) return best;

        healthy.seamAt = best.seamAt;
        healthy.seamsFound = best.seamsFound;
        healthy.seamRepaired = best.seamRepaired;
        healthy.healthyFrom = uint32_t(bestFrom);
        healthy.healthyTo = uint32_t(bestTo);
        healthy.camerasDroppedBySeam = best.posedCameras - healthy.posedCameras;
        return healthy;
    }

    if(config.initialPairTrials > 1 && config.forceInitialA == config.forceInitialB){
        ReconstructConfig once = config;
        once.initialPairTrials = 1;

        Reconstruction best;
        std::vector<std::pair<uint32_t, uint32_t>> seen;
        std::vector<Reconstruction::Trial> trials;

        auto keepIfBetter = [&](Reconstruction attempt){
            trials.push_back(Reconstruction::Trial{attempt.initialA, attempt.initialB, attempt.posedCameras,
                                                   attempt.solvedPoints, attempt.medianTriangulationAngle,
                                                   attempt.medianReprojection});
            //BROJ KAMERA PRVO, PA BAZA. Rjesenje s manje kamera nije bolje ma kako siroku bazu
            //imalo - ono naprosto nije rijesilo snimku
            const bool better = !best.ok
                || attempt.posedCameras > best.posedCameras
                || (attempt.posedCameras == best.posedCameras
                    && attempt.medianTriangulationAngle > best.medianTriangulationAngle);
            if(better) best = std::move(attempt);
        };

        if(config.parallelInitialPairTrials){
            //Izbor sljedeceg para ovisi samo o prethodno IZABRANIM parovima, ne o ostatku njihove
            //rekonstrukcije. Zato se prvo jeftino ponovi samo pocetna faza i dobije tocno isti niz
            //parova kao u sekvencijalnom putu. Tek tada se puna, medusobno neovisna rjesenja grade
            //istodobno. Redoslijed get() i izbora pobjednika ostaje stari, pa su i tie-breakovi isti.
            std::vector<std::pair<uint32_t, uint32_t>> selected;
            for(uint32_t trial = 0; trial < config.initialPairTrials; ++trial){
                ReconstructConfig probe = once;
                probe.skipInitialPairs = seen;
                const Reconstruction choice = reconstructImpl(
                    observations, cameraCount, pointCount, intrinsics, probe, true);
                if(!choice.ok) break;

                const std::pair<uint32_t, uint32_t> pair{choice.initialA, choice.initialB};
                if(std::find(seen.begin(), seen.end(), pair) != seen.end()) break;
                seen.push_back(pair);
                selected.push_back(pair);
            }

            std::vector<std::future<Reconstruction>> attempts;
            attempts.reserve(selected.size());
            for(const auto& pair : selected){
                ReconstructConfig forced = once;
                forced.forceInitialA = pair.first;
                forced.forceInitialB = pair.second;
                forced.skipInitialPairs.clear();
                attempts.push_back(std::async(std::launch::async,
                    [&, forced]{
                        return reconstruct(observations, cameraCount, pointCount,
                                           intrinsics, forced);
                    }));
            }

            for(auto& future : attempts){
                Reconstruction attempt = future.get();
                if(!attempt.ok) break;
                keepIfBetter(std::move(attempt));
            }
        }else{
            for(uint32_t trial = 0; trial < config.initialPairTrials; ++trial){
                //Sljedeci po redu izbor: isti racun kao inace, ali bez parova koji su vec probani
                ReconstructConfig probe = once;
                probe.skipInitialPairs = seen;

                //POKUSAJI SE NE RAZMICU PO SNIMCI, i to je izmjereno - vidi initialPairSpread

                Reconstruction attempt = reconstruct(observations, cameraCount, pointCount,
                                                     intrinsics, probe);
                if(!attempt.ok) break;

                const std::pair<uint32_t, uint32_t> pair{attempt.initialA, attempt.initialB};
                if(std::find(seen.begin(), seen.end(), pair) != seen.end()) break;
                seen.push_back(pair);

                keepIfBetter(std::move(attempt));
            }
        }

        if(best.ok){
            best.trials = std::move(trials);
            return best;
        }
    }

    Reconstruction state;
    state.poses.assign(cameraCount, Pose{});
    state.posed.assign(cameraCount, 0);
    //Prag se izvodi iz zarista i suma - vidi ReconstructConfig. Tijekom gradnje je TROSTRUKO
    //blazi: ondje postoji samo da sustav ne bude singularan, jer slaba tocka i dalje dobro vodi
    //rotaciju sljedece kamere. Izmjereno: uz puni prag tijekom gradnje dronska snimka rijesi 2 od
    //24 kamere, uz blazi svih 24. Sto se VJERUJE odlucuje se na kraju, nad konacnim pozama
    const double derivedParallax = config.maxRelativeDepthError > 0.0
        ? glm::degrees(config.assumedPixelNoise / (double(intrinsics.fx) * config.maxRelativeDepthError))
        : 0.0;
    //Ono sto je strože od izvedenog praga i apsolutnog poda - vidi minParallaxDegrees
    const double finalParallax = std::max(derivedParallax, config.minParallaxDegrees);
    const double workingParallax = finalParallax / 3.0;
    state.parallaxLimitDegrees = finalParallax;

    state.points.assign(pointCount, glm::vec3(0.0f));
    state.solved.assign(pointCount, 0);

    if(cameraCount < 2 || pointCount < 8 || observations.empty()){
        return state;
    }

    //OPAZANJA KOJA JOS SUDJELUJU. Ciscenje ide PO OPAZANJU a ne po tocki: jedno krivo poklapanje
    //u dugom tragu inace odnese cijelu tocku, a nju vidi jos deset kamera koje su u pravu
    std::vector<uint8_t> usable(observations.size(), 1);
    auto indexOf = [&](const Observation* observation){
        return size_t(observation - observations.data());
    };

    //Tko sto vidi. Dvije strane istog popisa, jer se jedna cita po kameri a druga po tocki
    std::vector<std::vector<const Observation*>> byCamera(cameraCount);
    std::vector<std::vector<const Observation*>> byPoint(pointCount);
    for(const Observation& observation : observations){
        if(observation.camera >= cameraCount || observation.point >= pointCount) continue;
        byCamera[observation.camera].push_back(&observation);
        byPoint[observation.point].push_back(&observation);
    }

    //IZDVOJENA OPAZANJA - vidi ReconstructConfig::holdOutEvery. Vade se prije ikakvog racuna, pa
    //ne ulaze ni u triangulaciju, ni u bundle, ni u ciscenje
    if(config.holdOutEvery > 1){
        std::vector<uint32_t> left(pointCount, 0);
        for(size_t point = 0; point < pointCount; ++point) left[point] = uint32_t(byPoint[point].size());

        for(size_t index = 0; index < observations.size(); index += config.holdOutEvery){
            const Observation& one = observations[index];
            if(one.camera >= cameraCount || one.point >= pointCount) continue;

            //Tocka mora i bez njega ostati vidjena iz barem tri kadra - inace se mijenja ulaz
            if(left[one.point] <= 3) continue;
            --left[one.point];

            usable[index] = 0;
        }
    }

    // ---------------------------------------------------------------------------------
    // Pocetni par: onaj koji NAJVISE TRIANGULIRA, a ne onaj koji slucajno sadrzi kameru 0
    // ---------------------------------------------------------------------------------
    //
    // Prva verzija je par trazila iskljucivo oko kamere 0: najudaljeniji kadar koji s njom jos
    // dijeli dovoljno tocaka. To pretpostavlja da je kadar 0 dobar kadar, a on je samo prvi.
    //
    // Izmjereno na pravim podacima (COLMAP-ov model iste snimke, 65 kamera): kamera 0 ima 141
    // opazanje, a prosjek je 3564 - dvadeset pet puta slabija od prosjeka, jer je rubni kadar.
    // Par se onda birao izmedju nje i kadra s kojim dijeli 56 tocaka, dvoprizorna poza je ispala
    // besmislena, i SVIH 56 tocaka je zavrsilo iza kamere. Nula tocaka, dvije kamere, kraj.
    //
    // Sada se gledaju SVI parovi, a mjera nije koliko tocaka dijele nego koliko ih se iz njih dade
    // stvarno triangulirati - ispred obje kamere i s dovoljno paralakse. To je jedina mjera koja
    // ne zavarava: par moze dijeliti tisucu tocaka a nemati bazu, i tada ne vrijedi nista.

    const auto initialPairStarted = Clock::now();
    std::unordered_map<uint64_t, uint32_t> shared;
    {
        std::vector<uint32_t> seeing;
        for(size_t point = 0; point < pointCount; ++point){
            seeing.clear();
            for(const Observation* observation : byPoint[point]) seeing.push_back(observation->camera);
            std::sort(seeing.begin(), seeing.end());
            seeing.erase(std::unique(seeing.begin(), seeing.end()), seeing.end());

            for(size_t i = 0; i < seeing.size(); ++i){
                for(size_t j = i + 1; j < seeing.size(); ++j){
                    ++shared[uint64_t(seeing[i]) * uint64_t(cameraCount) + uint64_t(seeing[j])];
                }
            }
        }
    }
    if(shared.empty()) return state;

    struct Candidate{
        uint32_t a = 0, b = 0;
        uint32_t common = 0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(shared.size());
    for(const auto& entry : shared){
        if(entry.second < 8) continue;          //ispod ovoga dvoprizorna poza nema sto rjesavati
        candidates.push_back(Candidate{uint32_t(entry.first / uint64_t(cameraCount)),
                                       uint32_t(entry.first % uint64_t(cameraCount)),
                                       entry.second});
    }
    if(candidates.empty()) return state;

    //Najprometniji parovi prvi, ali se ne vjeruje broju nego se provjerava racunom
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& x, const Candidate& y){return x.common > y.common;});

    auto sharedPixels = [&](uint32_t a, uint32_t b,
                            std::vector<glm::vec2>& pixelsA, std::vector<glm::vec2>& pixelsB,
                            std::vector<uint32_t>& which){
        pixelsA.clear(); pixelsB.clear(); which.clear();
        std::vector<const Observation*> inA(pointCount, nullptr);
        for(const Observation* observation : byCamera[a]) inA[observation->point] = observation;
        for(const Observation* observation : byCamera[b]){
            if(!inA[observation->point]) continue;
            pixelsA.push_back(inA[observation->point]->pixel);
            pixelsB.push_back(observation->pixel);
            which.push_back(observation->point);
        }
    };

    struct Tried{
        uint32_t a = 0, b = 0;
        uint32_t usable = 0;
        double medianAngle = 0.0;     //kut pod kojim se zrake sijeku, u stupnjevima
        TwoViewResult pair;
    };
    std::vector<Tried> tried;

    std::vector<glm::vec2> pixelsA, pixelsB;
    std::vector<uint32_t> sharedPoints;

    //Provjerava se ogranicen broj kandidata: RANSAC po paru nije besplatan, a par koji dijeli malo
    //tocaka ionako ne moze pobijediti par koji dijeli mnogo
    const size_t toCheck = std::min<size_t>(candidates.size(), config.initialPairCandidates);
    for(size_t index = 0; index < toCheck; ++index){
        const Candidate& candidate = candidates[index];
        sharedPixels(candidate.a, candidate.b, pixelsA, pixelsB, sharedPoints);
        if(pixelsA.size() < 8) continue;

        const TwoViewResult attempt = relativePoseRobust(pixelsA, pixelsB, intrinsics, config.ransac);
        if(!attempt.solved) continue;

        //Koliko se iz ovog para stvarno dade triangulirati - ispred obje kamere i s paralaksom
        std::vector<Pose> twoPoses(cameraCount);
        std::vector<uint8_t> twoPosed(cameraCount, 0);
        twoPoses[candidate.a] = Pose{};
        twoPoses[candidate.b] = attempt.pose;
        twoPosed[candidate.a] = 1;
        twoPosed[candidate.b] = 1;

        uint32_t usable = 0;
        std::vector<double> angles;
        for(size_t i = 0; i < sharedPoints.size(); ++i){
            const std::vector<View> views{View{candidate.a, pixelsA[i]}, View{candidate.b, pixelsB[i]}};
            glm::vec3 position;
            if(!triangulate(twoPoses, intrinsics, views, position, workingParallax)) continue;

            glm::vec2 pixel;
            if(!project(twoPoses[candidate.a], intrinsics, position, pixel)) continue;
            if(!project(twoPoses[candidate.b], intrinsics, position, pixel)) continue;
            ++usable;

            //Kut pod kojim se dvije zrake sijeku. To je prava mjera baze: dva kadra mogu dijeliti
            //tisucu tocaka a gledati ih gotovo iz istog mjesta, i tada dubina ne postoji
            const glm::vec3 toA = glm::normalize(position - twoPoses[candidate.a].position);
            const glm::vec3 toB = glm::normalize(position - twoPoses[candidate.b].position);
            angles.push_back(glm::degrees(std::acos(double(glm::clamp(glm::dot(toA, toB), -1.0f, 1.0f)))));
        }

        if(usable < 8 || angles.empty()) continue;
        std::nth_element(angles.begin(), angles.begin() + long(angles.size() / 2), angles.end());
        tried.push_back(Tried{candidate.a, candidate.b, usable, angles[angles.size() / 2], attempt});
    }

    if(tried.empty()) return state;

    //KUT JE UVJET, BROJ TOCAKA JE IZBOR - ne obrnuto. Prvo sam filtrirao po broju tocaka pa birao
    //po kutu, i to je promasilo: par sa 309 tocaka i bazom od 1.71 stupnja izbacio je sve ostale iz
    //igre, a par s 80 tocaka i 9.58 stupnjeva - koji je bio pravi - nije ni dosao na red. Dubina iz
    //uske baze ne valja koliko god tocaka bilo, pa uska baza ne smije biti kandidat nego otpasti.
    //
    //Prag se IZVODI, ne zadaje. workingParallax je kut pri kojem jedna tocka jos drzi dopustenu
    //gresku dubine (S10). Pocetni par nosi cijelu rekonstrukciju - iz njega nastaju sve prve tocke
    //na koje se zatim oslanja svaka sljedeca kamera - pa se od njega trazi red velicine vise
    const double demandedAngle = 10.0 * workingParallax;

    const Tried* chosen = nullptr;

    //Zadani par, ako ga je pozivatelj trazio i ako je uopce prosao provjeru racunom
    if(config.forceInitialA != config.forceInitialB){
        for(const Tried& one : tried){
            const bool same = (one.a == config.forceInitialA && one.b == config.forceInitialB)
                           || (one.a == config.forceInitialB && one.b == config.forceInitialA);
            if(same){ chosen = &one; break; }
        }
    }

    auto alreadyTried = [&](const Tried& one){
        for(const auto& pair : config.skipInitialPairs){
            if(pair.first == one.a && pair.second == one.b) return true;
            if(pair.first == one.b && pair.second == one.a) return true;

            //I preblizu vec probanome je isto sto i probano - vidi initialPairSpread
            if(config.initialPairSpread > 0){
                const double here = 0.5 * (double(one.a) + double(one.b));
                const double there = 0.5 * (double(pair.first) + double(pair.second));
                if(std::fabs(here - there) < double(config.initialPairSpread)) return true;
            }
        }
        return false;
    };

    for(const Tried& one : tried){
        if(chosen && config.forceInitialA != config.forceInitialB) break;
        if(alreadyTried(one)) continue;
        if(one.medianAngle < demandedAngle) continue;
        if(!chosen || one.usable > chosen->usable) chosen = &one;
    }

    //Kad nijedan par nema toliku bazu, uzima se najsiri koji postoji - bolje uska baza nego nikakva
    if(!chosen){
        for(const Tried& one : tried){
            if(alreadyTried(one)) continue;
            if(!chosen || one.medianAngle > chosen->medianAngle) chosen = &one;
        }
    }
    if(!chosen) return state;

    const uint32_t first = chosen->a;
    const uint32_t partner = chosen->b;
    state.initialA = first;
    state.initialB = partner;
    state.initialAngle = chosen->medianAngle;
    state.initialPoints = chosen->usable;
    const TwoViewResult pair = chosen->pair;
    sharedPixels(first, partner, pixelsA, pixelsB, sharedPoints);

    state.poses[first] = Pose{};            //ishodiste i jedinicna orijentacija: gauge
    state.poses[partner] = pair.pose;       //pomak je jedinicni - mjerilo ostaje slobodno
    state.posed[first] = 1;
    state.posed[partner] = 1;
    state.posedCameras = 2;
    state.timing.initialPairSeconds += elapsed(initialPairStarted);
    if(selectInitialPairOnly){
        state.ok = true;
        return state;
    }

    // ---------------------------------------------------------------------------------
    // Triangulacija svega sto vide dvije rijesene kamere, pa nova kamera, pa opet
    // ---------------------------------------------------------------------------------

    //redoSolved: PONOVNA TRIANGULACIJA vec rijesenih tocaka.
    //
    //Bez nje tocka zauvijek ostaje ondje gdje su je stavile prve dvije kamere koje su je vidjele -
    //cesto par s uskom bazom, jer se pocetne kamere biraju po broju tocaka a ne po tome sto ta
    //tocka treba. Kad je poslije vidi jos deset kamera, taj se podatak nikad ne iskoristi, a
    //bundle tocku moze samo lokalno pomaknuti. Tisuce takvih tocaka onda drze poze u lososu
    //minimumu iz kojeg bundle ne izlazi
    auto triangulateVisible = [&](bool redoSolved){
        const auto phaseStarted = Clock::now();
        ++state.timing.triangulationCalls;
        for(size_t point = 0; point < pointCount; ++point){
            if(state.solved[point] && !redoSolved) continue;

            std::vector<View> views;
            for(const Observation* observation : byPoint[point]){
                if(!usable[indexOf(observation)]) continue;
                if(state.posed[observation->camera]) views.push_back(View{observation->camera, observation->pixel});
            }
            if(views.size() < 2) continue;

            glm::vec3 position;
            if(!triangulate(state.poses, intrinsics, views, position, workingParallax)) continue;

            //Tocka iza neke od kamera koje je vide nije rjesenje nego smetnja
            bool inFront = true;
            for(const View& view : views){
                glm::vec2 pixel;
                if(!project(state.poses[view.camera], intrinsics, position, pixel)) inFront = false;
            }
            if(!inFront) continue;

            state.points[point] = position;
            if(!state.solved[point]){
                state.solved[point] = 1;
                ++state.solvedPoints;
            }
        }
        state.timing.triangulationSeconds += elapsed(phaseStarted);
    };

    //Koja je kamera zadnja usla. Lokalni prozor se racuna oko nje, jer je ona jedina zbog koje se
    //bundle uopce ponovno zove
    size_t lastAddedCamera = 0;

    //LOKALNI PROZOR SE SMIJE SAMO U RASTU. Refine i zavrsni prolazi moraju biti GLOBALNI, inace
    //nigdje u lancu ne bi ostalo mjesto na kojem se nakupljeni drift ispravlja - a bas se zato
    //refine i zove periodicki. Lokalni bundle svaki korak, globalni povremeno: to je podjela koja
    //cijeli postupak drzi i brzim i ispravnim
    auto runBundle = [&](bool allowLocal = false){
        const auto phaseStarted = Clock::now();
        ++state.timing.bundleCalls;

        //=====================================================================================
        // LOKALNI PROZOR - vidi ReconstructConfig::localBundleWindow.
        //
        // Daleke kamere se ne micu, ali njihova opazanja OSTAJU u problemu: one drze tocke koje
        // vide, pa se tocka ne moze odsetati samo zato sto ju prozor vuce. Bez toga bi lokalni
        // bundle tiho razbio ono sto je vec bilo dobro
        //=====================================================================================
        std::vector<uint8_t> fixedCameras;
        std::vector<uint8_t> pointInWindow;
        const bool local = allowLocal && config.localBundleWindow > 0 &&
                           state.posedCameras > config.localBundleWindow;
        if(local){
            const size_t half = size_t(config.localBundleWindow) / 2;
            const size_t from = lastAddedCamera > half ? lastAddedCamera - half : 0;
            const size_t to = std::min(cameraCount, lastAddedCamera + half + 1);

            fixedCameras.assign(cameraCount, uint8_t(1));
            for(size_t camera = from; camera < to; ++camera) fixedCameras[camera] = 0;

            //Tocke koje prozor uopce vidi. Ostale se ovaj put ne diraju - njih nitko nije pomaknuo
            pointInWindow.assign(pointCount, uint8_t(0));
            for(size_t index = 0; index < observations.size(); ++index){
                if(!usable[index]) continue;
                const Observation& observation = observations[index];
                if(observation.camera >= from && observation.camera < to &&
                   state.posed[observation.camera] && state.solved[observation.point]){
                    pointInWindow[observation.point] = 1;
                }
            }
        }

        std::vector<Observation> kept;
        kept.reserve(observations.size());
        for(size_t index = 0; index < observations.size(); ++index){
            if(!usable[index]) continue;
            const Observation& observation = observations[index];
            if(!state.posed[observation.camera] || !state.solved[observation.point]) continue;
            if(local && !pointInWindow[observation.point]) continue;
            kept.push_back(observation);
        }
        if(kept.empty()){
            state.timing.bundleSeconds += elapsed(phaseStarted);
            return;
        }

        BundleConfig bundleConfig;
        bundleConfig.huberPixels = config.huberPixels;
        bundleConfig.maxIterations = config.bundleIterations;
        bundleConfig.fixedCameras = std::move(fixedCameras);

        const BundleResult result = bundleAdjust(kept, state.poses, state.points, intrinsics, bundleConfig);
        state.timing.bundleCostSeconds += result.timing.costSeconds;
        state.timing.bundleLinearizeSeconds += result.timing.linearizeSeconds;
        state.timing.bundleSchurSeconds += result.timing.schurSeconds;
        state.timing.bundleDenseSolveSeconds += result.timing.denseSolveSeconds;
        state.timing.bundleBackSubstituteSeconds += result.timing.backSubstituteSeconds;
        if(result.solved){
            state.poses = result.poses;
            state.points = result.points;
        }
        state.timing.bundleSeconds += elapsed(phaseStarted);
    };

    //=================================================================================
    // CISCENJE PO OPAZANJU, pa ponovna triangulacija, pa bundle - i to u krug.
    //
    // Zasto bas ovim redom i zasto uopce: izmjereno je da nas bundle NIJE kriv. Pusten na
    // COLMAP-ovo gotovo rjesenje iste snimke drzi ga na mjestu i jos ga neznatno popravi
    // (0.7461 -> 0.7380 px). Isti taj bundle na nasoj rekonstrukciji sjedne na 3.34 px i
    // ondje ostane koliko god iteracija dobio (5 iteracija 3.330, 15 iteracija 3.341).
    //
    // Dakle rjesenje nije lose zato sto bundle ne zna sici, nego zato sto ga inkrementalni
    // put dovede u DRUGI, losiji minimum iz kojeg lokalni korak ne izlazi. Iz njega se izlazi
    // samo mijenjanjem onoga sto bundle dobije: izbaciti opazanja koja lazu, i tocke posloziti
    // iznova iz svih kamera koje ih sada vide, a ne iz one dvije koje su ih prve vidjele.
    //
    // PRAG SE STEZE SAM. Fiksni prag od cetiri piksela nad rjesenjem koje je na tri i pol bi u
    // prvom krugu odbacio pola scene. Zato je granica visekratnik TRENUTNOG medijana, sve dok
    // ne padne na filterPixels - pa je ciscenje u pocetku blago i pooostrava se kako rjesenje
    // postaje bolje
    //=================================================================================

    auto reprojectionOf = [&](const Observation& observation, double& error){
        glm::vec2 pixel;
        if(!project(state.poses[observation.camera], intrinsics, state.points[observation.point], pixel)){
            error = std::numeric_limits<double>::infinity();
            return false;
        }
        error = glm::length(pixel - observation.pixel);
        return true;
    };

    auto filterObservations = [&](double maxPixels){
        const auto phaseStarted = Clock::now();
        size_t dropped = 0;
        if(maxPixels <= 0.0){
            state.timing.filteringSeconds += elapsed(phaseStarted);
            return dropped;
        }

        for(size_t index = 0; index < observations.size(); ++index){
            if(!usable[index]) continue;
            const Observation& observation = observations[index];
            if(!state.posed[observation.camera] || !state.solved[observation.point]) continue;

            double error = 0.0;
            if(!reprojectionOf(observation, error) || error > maxPixels){
                usable[index] = 0;
                ++dropped;
            }
        }

        //Tocka koju vide manje od dvije kamere vise nije triangulirana nego pogodjena
        for(size_t point = 0; point < pointCount; ++point){
            if(!state.solved[point]) continue;
            size_t left = 0;
            for(const Observation* observation : byPoint[point]){
                if(usable[indexOf(observation)] && state.posed[observation->camera]) ++left;
            }
            if(left < 2){
                state.solved[point] = 0;
                --state.solvedPoints;
            }
        }
        state.timing.filteringSeconds += elapsed(phaseStarted);
        return dropped;
    };

    auto currentMedian = [&](){
        const auto phaseStarted = Clock::now();
        std::vector<double> errors;
        errors.reserve(observations.size());
        for(size_t index = 0; index < observations.size(); ++index){
            if(!usable[index]) continue;
            const Observation& observation = observations[index];
            if(!state.posed[observation.camera] || !state.solved[observation.point]) continue;
            double error = 0.0;
            if(reprojectionOf(observation, error)) errors.push_back(error);
        }
        const double result = medianOf(errors);
        state.timing.filteringSeconds += elapsed(phaseStarted);
        return result;
    };

    //USPUT LAGANO, NA KRAJU TEMELJITO. Puni ciklus usred gradnje bio bi na 634 kamere oko
    //dvadeset devet ciscenja po pet krugova - dakle sto cetrdeset globalnih bundleova, sto je
    //neupotrebljivo. Usput je dovoljan jedan krug: posao mu je drzati drift na uzdi, ne polirati
    auto refine = [&](uint32_t rounds){
        for(uint32_t round = 0; round < rounds; ++round){
            const double median = currentMedian();
            const double limit = std::max(config.filterPixels, config.filterMedians * median);

            const size_t dropped = filterObservations(limit);
            triangulateVisible(true);
            runBundle();

            //Krug koji nije nista izbacio nema sto izbaciti ni u sljedecem: prag ovisi o
            //medijanu, a on se bez izbacivanja i ponovne triangulacije jedva mice
            if(dropped == 0) break;
        }
    };

    triangulateVisible(false);
    runBundle();

    //Redom dodaje kameru koja vidi najvise vec rijesenih tocaka.
    //
    //KAMERA KOJA PADNE PRESKACE SE, ne zaustavlja lanac. Prije je jedan neuspjeh bio break, uz
    //obrazlozenje "bolje manje nego krivo" - ali to brka dvije razlicite tvrdnje: OVA se kamera ne
    //da rijesiti nije isto sto i NIJEDNA se ne da rijesiti. Kako se uvijek uzima kamera koja vidi
    //najvise rijesenih tocaka, dovoljno je da bas ta padne pa se ostale nikad ni ne pokusaju.
    //Izmjereno na pravoj snimci: 3 rijesene kamere od 318, jer je cetvrta pala.
    //
    //Oprez i dalje stoji tamo gdje pripada: kamera koja promasi vise od acceptPixels se ne uzima.
    //Samo se zbog nje ne odbacuje ostatak snimke
    //Piramida vidljivosti - vidi ReconstructConfig::visibilityScore. Jedno polje za sve kamere,
    //jer se ocjenjuje jedna po jedna
    constexpr int pyramidLevels = 6;
    std::array<size_t, pyramidLevels> levelOffset{};
    size_t pyramidCells = 0;
    for(int level = 0; level < pyramidLevels; ++level){
        levelOffset[size_t(level)] = pyramidCells;
        const size_t dim = size_t(1) << (level + 1);
        pyramidCells += dim * dim;
    }
    std::vector<uint8_t> occupied(pyramidCells, 0);

    //Ocjena kamere i broj rijesenih tocaka koje vidi. Broj i dalje treba, jer je on uvjet
    //(minPointsForPose), a ocjena je izbor
    auto scoreCamera = [&](size_t camera, size_t& seen){
        seen = 0;
        if(!config.visibilityScore){
            for(const Observation* observation : byCamera[camera]){
                if(state.solved[observation->point] && usable[indexOf(observation)]) ++seen;
            }
            return seen;
        }

        std::fill(occupied.begin(), occupied.end(), uint8_t(0));
        size_t score = 0;

        for(const Observation* observation : byCamera[camera]){
            if(!state.solved[observation->point] || !usable[indexOf(observation)]) continue;
            ++seen;

            for(int level = 0; level < pyramidLevels; ++level){
                const size_t dim = size_t(1) << (level + 1);
                const long cx = long(double(observation->pixel.x) / double(intrinsics.width) * double(dim));
                const long cy = long(double(observation->pixel.y) / double(intrinsics.height) * double(dim));
                if(cx < 0 || cy < 0 || cx >= long(dim) || cy >= long(dim)) continue;

                uint8_t& cell = occupied[levelOffset[size_t(level)] + size_t(cy) * dim + size_t(cx)];
                if(cell) continue;
                cell = 1;
                score += dim * dim;   //tezina razine je broj celija na njoj
            }
        }
        return score;
    };

    auto addCameras = [&](){
    std::vector<uint8_t> refused(cameraCount, 0);
    bool secondChanceSpent = false;

    //Sljedeci broj kamera pri kojem se cisti usred gradnje - vidi refineGrowth
    size_t nextRefine = config.refineGrowth > 1.0
        ? size_t(double(state.posedCameras) * config.refineGrowth) + 1
        : size_t(-1);
    size_t nextBundle = config.incrementalBundleGrowth > 1.0
        ? size_t(double(state.posedCameras) * config.incrementalBundleGrowth) + 1
        : state.posedCameras + 1;

    for(size_t attempt = 0; attempt < 8 * cameraCount + 64; ++attempt){
        size_t best = cameraCount;
        size_t bestScore = 0;
        size_t bestCount = 0;
        for(size_t camera = 0; camera < cameraCount; ++camera){
            if(state.posed[camera] || refused[camera]) continue;

            size_t seen = 0;
            const size_t score = scoreCamera(camera, seen);
            if(seen < config.minPointsForPose) continue;

            if(score > bestScore){
                bestScore = score;
                bestCount = seen;
                best = camera;
            }
        }
        if(best == cameraCount || bestCount < config.minPointsForPose){
            //Nema vise kandidata. Jednom se odbijenima da druga prilika, jer je u medjuvremenu
            //nastalo tocaka kojih tada nije bilo; ako ni tada nitko ne prodje, stalo je
            if(secondChanceSpent) break;
            secondChanceSpent = true;
            std::fill(refused.begin(), refused.end(), 0);
            continue;
        }

        //Pocetna poza: najblizi vec rijeseni kadar. Vidi zaglavlje - P3P jos nemamo
        size_t nearest = 0;
        size_t nearestDistance = cameraCount + 1;
        for(size_t camera = 0; camera < cameraCount; ++camera){
            if(!state.posed[camera]) continue;
            const size_t distance = camera > best ? camera - best : best - camera;
            if(distance < nearestDistance){
                nearestDistance = distance;
                nearest = camera;
            }
        }

        std::vector<PointObservation> seen;
        for(const Observation* observation : byCamera[best]){
            if(state.solved[observation->point]) seen.push_back(PointObservation{observation->point, observation->pixel});
        }

        Pose placed;
        bool placedOk = false;
        double placedMedian = 0.0;
        const auto poseStarted = Clock::now();
        ++state.timing.poseCalls;

        if(config.poseRansac){
            PoseRansacConfig poseConfig;
            poseConfig.solve.huberPixels = config.huberPixels;
            poseConfig.maxError = config.poseMaxError;
            poseConfig.minInliers = config.minPointsForPose;
            poseConfig.minInlierRatio = config.poseMinInlierRatio;

            const PoseRansacResult pose = solvePoseRansac(state.points, seen, intrinsics,
                                                          state.poses[nearest], poseConfig);
            placed = pose.pose;
            placedOk = pose.solved;

            //MEDIJAN PO SKUPU KOJI SE SLAZE, ne preko svega. Preko svega bi ovdje mjerio koliko je
            //tocaka lose triangulirano, a pitanje je je li POZA dobra
            placedMedian = pose.inlierMedian;
        }else{
            PoseSolveConfig poseConfig;
            poseConfig.huberPixels = config.huberPixels;
            const PoseSolveResult pose = solvePose(state.points, seen, intrinsics,
                                                   state.poses[nearest], poseConfig);
            placed = pose.pose;
            placedOk = pose.solved;
            placedMedian = pose.endMedian;
        }
        state.timing.poseSeconds += elapsed(poseStarted);

        if(!placedOk || placedMedian > config.acceptPixels){
            refused[best] = 1;
            continue;   //ova kamera ne ide; ostale se i dalje pokusavaju
        }

        state.poses[best] = placed;
        state.posed[best] = 1;
        ++state.posedCameras;
        lastAddedCamera = size_t(best);

        //Javljanje napretka - vidi ReconstructConfig::onProgress
        if(config.onProgress && config.progressEvery > 0 &&
           state.posedCameras % config.progressEvery == 0){
            config.onProgress(state);
        }

        triangulateVisible(false);
        const bool refinementDue = state.posedCameras >= nextRefine;
        const bool bundleDue = config.incrementalBundleGrowth <= 1.0 ||
                               state.posedCameras >= nextBundle;

        //Uz zadanu vrijednost 1.0 ostaje tocno stari redoslijed, ukljucujuci bundle neposredno
        //prije refinea. Rjedja kadenca ga na granici ne duplira jer refine vec zavrsava bundleom.
        if(bundleDue && (!refinementDue || config.incrementalBundleGrowth <= 1.0)) runBundle(true);

        if(refinementDue){
            refine(1);
            nextRefine = size_t(double(state.posedCameras) * config.refineGrowth) + 1;
        }
        if(bundleDue){
            nextBundle = size_t(double(state.posedCameras) * config.incrementalBundleGrowth) + 1;
        }

        //Uspjeh znaci da je nastalo novih tocaka, pa odbijene vrijedi jos jednom pokusati - ali
        //tek kad se iscrpe kandidati, ne odmah. Brisanje nakon svakog uspjeha bi na tristo kamera
        //dalo kvadratno mnogo pokusaja
        secondChanceSpent = false;
    }
    };

    //KAMERE, PA CISCENJE, PA OPET KAMERE. Kamera koja je pala jer joj je PnP promasio vise od
    //acceptPixels nije nuzno losa kamera - mozda su bile lose tocke koje je vidjela. Nakon
    //ciscenja i ponovne triangulacije te su tocke drugdje, pa ista kamera dobiva drugi racun
    addCameras();

    //REP SE ODBACUJE I GRADI IZNOVA - vidi ReconstructConfig::seamFactor. Kamere od sava nadalje
    //su registrirane dok su tocke ispred jos bile lose; sada su tocke bolje, pa dobivaju drugu
    //priliku. Tocke koje vise nitko ne vidi ispadaju same, jer se triangulira samo iz rijesenih
    if(config.keepTo > 0 && config.keepTo <= cameraCount){
        for(size_t camera = 0; camera < cameraCount; ++camera){
            if(!state.posed[camera]) continue;
            if(camera >= config.keepFrom && camera < config.keepTo) continue;
            state.posed[camera] = 0;
            --state.posedCameras;
        }
        std::fill(state.solved.begin(), state.solved.end(), uint8_t(0));
        state.solvedPoints = 0;
        std::fill(usable.begin(), usable.end(), uint8_t(1));

        triangulateVisible(false);
        runBundle();
        refine(config.refineRounds);

        //Popravak sava gradi rep iznova - to mu je svrha. Odrezivanje na zdravi odsjecak ga NE
        //gradi, jer bi vratilo ono sto je upravo odrezano
        if(config.reAddAfterTrim) addCameras();

        state.healthyFrom = config.keepFrom;
        state.healthyTo = config.keepTo;
    }

    for(uint32_t sweep = 0; sweep < 2 && config.reAddAfterTrim && config.refineRounds > 0; ++sweep){
        const uint32_t before = state.posedCameras;
        refine(config.refineRounds);
        addCameras();
        if(state.posedCameras == before) break;
    }
    refine(config.refineRounds);

    //=================================================================================
    // DRUGO MISLJENJE ZA KAMERU KOJA SE ZAGLAVILA - vidi ReconstructConfig::rescueFactor
    //=================================================================================

    if((config.rescueFactor > 0.0 || config.stepOutlierFactor > 0.0) && state.posedCameras > 2){
        const double overall = currentMedian();
        bool anyChanged = false;

        //NAGLI SKOK U NIZU - vidi ReconstructConfig::stepOutlierFactor. Racuna se prije nego se
        //ijedna poza dira, jer se svaka promjena vidi u susjednim koracima
        std::vector<uint8_t> jumped(cameraCount, 0);
        if(config.stepOutlierFactor > 0.0){
            auto turnBetween = [&](size_t a, size_t b){
                const glm::dmat3 first = glm::dmat3(glm::mat3_cast(state.poses[a].orientation));
                const glm::dmat3 second = glm::dmat3(glm::mat3_cast(state.poses[b].orientation));
                const glm::dmat3 difference = glm::transpose(first) * second;
                const double cosine = std::max(-1.0, std::min(1.0,
                    (difference[0][0] + difference[1][1] + difference[2][2] - 1.0) * 0.5));
                return glm::degrees(std::acos(cosine));
            };

            std::vector<double> steps;
            for(size_t camera = 0; camera + 1 < cameraCount; ++camera){
                if(!state.posed[camera] || !state.posed[camera + 1]) continue;
                steps.push_back(turnBetween(camera, camera + 1));
            }

            if(steps.size() >= 5){
                const double middle = medianOf(steps);
                const double limit = config.stepOutlierFactor * middle;

                //PUT KROZ KAMERU NASPRAM PUTA PREKO NJE.
                //
                //Prvo sam trazio kameru kojoj su OBA susjedna koraka velika, i to ne radi: zaokret
                //krive kamere se s jedne strane zbraja s gibanjem a s druge oduzima. Izmjereno na
                //sintetici, kamera zaokrenuta 25 st uz korak od 11.46: susjedni koraci ispadnu
                //36.32 i 13.90 - jedan golem, drugi posve obican.
                //
                //Ono sto je stvarno svojstvo krive kamere jest da je put KROZ nju dulji nego put
                //PREKO nje. Za ispravnu kameru ta su dva gotovo jednaka, jer se zaokreti zbrajaju
                //oko iste osi. Za zaokrenutu je razlika dvostruki zaokret
                for(size_t camera = 1; camera + 1 < cameraCount; ++camera){
                    if(!state.posed[camera] || !state.posed[camera - 1] || !state.posed[camera + 1]) continue;

                    const double through = turnBetween(camera - 1, camera) + turnBetween(camera, camera + 1);
                    const double across = turnBetween(camera - 1, camera + 1);
                    if(through - across > limit) jumped[camera] = 1;
                }
            }
        }

        {
            for(size_t camera = 0; camera < cameraCount; ++camera){
                if(!state.posed[camera]) continue;

                //Vlastita reprojekcija ove kamere, po opazanjima koja jos sudjeluju
                std::vector<double> mine;
                for(const Observation* observation : byCamera[camera]){
                    if(!usable[indexOf(observation)] || !state.solved[observation->point]) continue;
                    double error = 0.0;
                    if(reprojectionOf(*observation, error)) mine.push_back(error);
                }
                if(mine.size() < config.minPointsForPose) continue;

                const double own = medianOf(mine);
                const bool byError = config.rescueFactor > 0.0 && overall > 0.0
                                  && own > config.rescueFactor * overall;
                if(!byError && !jumped[camera]) continue;

                //POLAZI SE OD SUSJEDA, NE OD SEBE. Vlastita poza je upravo ono iz cega treba
                //izaci; susjedna kamera je najbolja druga pretpostavka koju imamo
                size_t nearest = cameraCount;
                size_t nearestDistance = cameraCount + 1;
                for(size_t other = 0; other < cameraCount; ++other){
                    if(other == camera || !state.posed[other]) continue;
                    const size_t distance = other > camera ? other - camera : camera - other;
                    if(distance < nearestDistance){ nearestDistance = distance; nearest = other; }
                }
                if(nearest == cameraCount) continue;

                std::vector<PointObservation> seen;
                for(const Observation* observation : byCamera[camera]){
                    if(!usable[indexOf(observation)] || !state.solved[observation->point]) continue;
                    seen.push_back(PointObservation{observation->point, observation->pixel});
                }

                PoseRansacConfig poseConfig;
                poseConfig.solve.huberPixels = config.huberPixels;
                poseConfig.maxError = config.poseMaxError;
                poseConfig.minInliers = config.minPointsForPose;
                poseConfig.minInlierRatio = config.poseMinInlierRatio;

                const PoseRansacResult again = solvePoseRansac(state.points, seen, intrinsics,
                                                               state.poses[nearest], poseConfig);
                if(!again.solved) continue;

                //KAMERA KOJA JE SKOCILA PRIMA I JEDNAKO DOBRU POZU. Njezina stara poza nije losa po
                //reprojekciji - u tome i jest kvar - pa bi uvjet "mora biti bolja" odbio popravak.
                //Trazi se samo da ne bude bitno gora; sto je od dvije poza tocna, odlucuje niz
                const double allowed = jumped[camera] ? 1.5 * own : own;
                if(again.inlierMedian >= allowed) continue;

                state.poses[camera] = again.pose;
                ++state.rescuedCameras;
                anyChanged = true;
            }
        }

        //Tek kad su sve provjerene: tocke iznova iz novih poza, pa bundle
        if(anyChanged){
            triangulateVisible(true);
            runBundle();
            refine(config.refineRounds);
        }
    }

    // ---------------------------------------------------------------------------------
    // Zadnja rijec o tockama: paralaksa nad KONACNIM pozama
    //
    // Provjera pri triangulaciji nije dovoljna, i to je mjerenje pokazalo: bundle poslije pomakne
    // poze, a na desetinkama stupnja i mali pomak mijenja kut medju zrakama za pola. Na dronskoj
    // snimci su uz prag od 0.2 st u rezultatu ostajale tocke cija je konacna paralaksa 0.142 st.
    //
    // Tocka koja ovdje padne nije izgubljena nego POSTENO oznacena: njezin smjer znamo, dubinu ne.
    // Reprojekcija je takvu nikad ne bi prijavila - daleka tocka lezi na obje zrake i uredno se
    // reprojicira ma gdje po njima bila
    // ---------------------------------------------------------------------------------

    const auto diagnosticsStarted = Clock::now();
    if(finalParallax > 0.0){
        for(size_t point = 0; point < pointCount; ++point){
            if(!state.solved[point]) continue;

            std::vector<View> views;
            for(const Observation* observation : byPoint[point]){
                if(state.posed[observation->camera]) views.push_back(View{observation->camera, observation->pixel});
            }
            if(parallaxDegrees(state.poses, intrinsics, views) >= finalParallax) continue;

            state.solved[point] = 0;
            --state.solvedPoints;
        }
    }

    state.usedObservations = 0;
    state.filteredObservations = 0;
    state.observationUsed.assign(observations.size(), 0);
    for(size_t index = 0; index < observations.size(); ++index){
        const Observation& observation = observations[index];
        if(!state.posed[observation.camera] || !state.solved[observation.point]) continue;
        if(usable[index]){ ++state.usedObservations; state.observationUsed[index] = 1; }
        else              ++state.filteredObservations;
    }

    //Sve tri mjere dolaze iz istog puta koji se moze pozvati i nakon vanjskog bundlea. Time se
    //samokalibrirani kandidat ne mora ponovno graditi samo da bi dobio svjezu dijagnostiku.
    refreshReconstructionDiagnostics(observations, intrinsics, config, state);

    state.ok = state.posedCameras >= 2 && state.solvedPoints > 0;
    // ---------------------------------------------------------------------------------
    // Ishodiste je PRVA RIJESENA KAMERA, uvijek
    // ---------------------------------------------------------------------------------
    //
    // Mjerilo i polozaj rekonstrukcije su slobodni - to je gauge - pa netko mora odabrati gdje je
    // ishodiste. Prije je to bila kamera 0, jer je par uvijek kretao od nje. Sada par bira racun,
    // pa ishodiste vise nije unaprijed poznato.
    //
    // ZASTO SE TO NE SMIJE PUSTITI. Pozivatelj usporedjuje poze s istinom tako da obje svede na
    // prvu kameru; ako rekonstrukcija odjednom stoji u okviru neke druge, razlika izadje kao
    // rotacija od 57.29 stupnjeva - sto je tocno jedan radijan, i tocno ono sto se dogodilo kad
    // sam izbor para promijenio a ovo zaboravio. Rjesenje je bilo ispravno cijelo vrijeme; krivo
    // je bilo samo mjesto s kojeg se gleda.
    //
    // Pretvorba ne mijenja nijednu medjusobnu udaljenost ni kut, pa ni jednu reprojekciju
    {
        size_t gauge = cameraCount;
        for(size_t camera = 0; camera < cameraCount; ++camera){
            if(state.posed[camera]){ gauge = camera; break; }
        }

        if(gauge < cameraCount){
            const Pose origin = state.poses[gauge];
            const glm::quat inverse = glm::conjugate(glm::normalize(origin.orientation));

            for(size_t camera = 0; camera < cameraCount; ++camera){
                if(!state.posed[camera]) continue;
                state.poses[camera].position = inverse * (state.poses[camera].position - origin.position);
                state.poses[camera].orientation = glm::normalize(inverse * state.poses[camera].orientation);
            }
            for(size_t point = 0; point < pointCount; ++point){
                if(!state.solved[point]) continue;
                state.points[point] = inverse * (state.points[point] - origin.position);
            }
        }
    }

    state.timing.diagnosticsSeconds += elapsed(diagnosticsStarted);
    return state;
}

Reconstruction reconstruct(const std::vector<Observation>& observations,
                           size_t cameraCount,
                           size_t pointCount,
                           const Intrinsics& intrinsics,
                           const ReconstructConfig& config){
    const auto started = std::chrono::steady_clock::now();
    Reconstruction result = reconstructImpl(observations, cameraCount, pointCount, intrinsics,
                                            config);
    result.timing.totalSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

}
