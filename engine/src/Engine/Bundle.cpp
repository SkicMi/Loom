#include "Engine/Bundle.h"
#include "Engine/Bands.h"
#include "Engine/Dense.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace Engine{
namespace{

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    //Treba samo srednji element, ne cijeli poredak. std::sort je na 808 tisuca opazanja radio
    //O(n log n) poslije svakog kandidata; nth_element vraca isti gornji medijan u O(n), a niz je
    //ionako lokalna kopija koja se odmah odbaci.
    std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    return values[values.size() / 2];
}

//Iste dvije funkcije kao u SolvePose: tezina i kazna pod Huberom
double huberWeight(double length, double delta){
    if(delta <= 0.0 || length <= delta) return 1.0;
    return std::sqrt(delta / length);
}

double huberCost(double length, double delta){
    if(delta <= 0.0 || length <= delta) return length * length;
    return 2.0 * delta * length - delta * delta;
}

bool reprojectionResidual(const Pose& pose,
                          const Intrinsics& intrinsics,
                          const glm::vec3& point,
                          const glm::vec2& observed,
                          double residual[2]){
    const glm::vec3 inCamera = glm::conjugate(pose.orientation) * (point - pose.position);
    const double depth = -double(inCamera.z);
    if(depth <= 0.0) return false;
    residual[0] = double(intrinsics.cx) + double(intrinsics.fx) * double(inCamera.x) / depth
                - double(observed.x);
    residual[1] = double(intrinsics.cy) - double(intrinsics.fy) * double(inCamera.y) / depth
                - double(observed.y);
    return true;
}

double costOf(const std::vector<Pose>& poses, const std::vector<glm::vec3>& points,
              const Intrinsics& intrinsics, const std::vector<Observation>& observations,
              double delta,
              double& median,
              const BundleConfig& config){
    const bool rolling = config.rowTime != 0.0;
    double cost = 0.0;
    std::vector<double> lengths;
    lengths.reserve(observations.size());

    for(const Observation& observation : observations){
        if(observation.camera >= poses.size() || observation.point >= points.size()) continue;
        double residual[2];
        const Pose pose = rolling ? rollingShutterPose(poses[observation.camera], config, observation.camera,
                                                       observation.pixel.y, intrinsics.cy)
                                  : poses[observation.camera];
        if(!reprojectionResidual(pose, intrinsics,
                                 points[observation.point], observation.pixel, residual)){
            cost += double(intrinsics.width) * double(intrinsics.width);
            lengths.push_back(double(intrinsics.width));
            continue;
        }
        const double length = std::sqrt(residual[0] * residual[0] + residual[1] * residual[1]);
        const double scale = observation.point < config.precisePointsFrom ? config.coarseWeight : 1.0;
        cost += huberCost(scale * length, delta);
        lengths.push_back(length);
    }
    median = medianOf(lengths);
    return cost;
}

//Jedan blok E za par (kamera, tocka): 6x3
struct CameraBlock{
    int camera = 0;
    std::array<double, 18> e{};
};

//Oba javna Jacobiana namjerno ostaju zasebna i nepromijenjena, jer ih koriste S2 i testovi.
//Bundleu trebaju OBA za isto opazanje. Pozivati pointJacobian pa poseJacobian znaci dvaput
//rotirati istu tocku u kameru, dvaput projicirati i dvaput graditi isti duv. Ovdje se zajednicki
//dio racuna jednom, a oba izlaza koriste tocno iste formule i red operacija kao javne funkcije.
bool bundleJacobians(const Pose& pose,
                     const Intrinsics& intrinsics,
                     const glm::vec3& point,
                     const glm::vec2& observed,
                     double residual[2],
                     double pointPart[2][3],
                     double cameraPart[2][6]){
    const glm::vec3 inCamera = glm::conjugate(pose.orientation) * (point - pose.position);
    const double depth = -double(inCamera.z);
    if(depth <= 0.0) return false;

    const double fx = intrinsics.fx, fy = intrinsics.fy;
    const double x = inCamera.x, y = inCamera.y;
    residual[0] = double(intrinsics.cx) + fx * x / depth - double(observed.x);
    residual[1] = double(intrinsics.cy) - fy * y / depth - double(observed.y);

    const double duv[2][3] = {
        { fx / depth,        0.0,        fx * x / (depth * depth) },
        { 0.0,        -fy / depth,      -fy * y / (depth * depth) }
    };

    const glm::mat3 rotation = glm::mat3_cast(glm::conjugate(pose.orientation));
    for(int row = 0; row < 2; ++row){
        for(int column = 0; column < 3; ++column){
            pointPart[row][column] = duv[row][0] * double(rotation[column][0])
                                   + duv[row][1] * double(rotation[column][1])
                                   + duv[row][2] * double(rotation[column][2]);
        }
    }

    const double px = x, py = y, pz = double(inCamera.z);
    const double dp[3][6] = {
        { -1.0,  0.0,  0.0,   0.0,  -pz,   py },
        {  0.0, -1.0,  0.0,   pz,   0.0,  -px },
        {  0.0,  0.0, -1.0,  -py,   px,   0.0 }
    };
    for(int row = 0; row < 2; ++row){
        for(int column = 0; column < 6; ++column){
            cameraPart[row][column] = duv[row][0] * dp[0][column]
                                    + duv[row][1] * dp[1][column]
                                    + duv[row][2] * dp[2][column];
        }
    }
    return true;
}

}

Pose rollingShutterPose(const Pose& pose, const BundleConfig& config, size_t camera, float row, float centreRow){
    if(config.rowTime == 0.0) return pose;
    const float s = float(double(row - centreRow) * config.rowTime);
    Pose out = pose;
    if(camera < config.linearVelocity.size()) out.position += s * config.linearVelocity[camera];
    if(camera < config.angularVelocity.size()){
        const glm::vec3 turn = s * config.angularVelocity[camera];
        const float angle = glm::length(turn);
        if(angle > 1e-12f) out.orientation = glm::normalize(pose.orientation * glm::angleAxis(angle, turn / angle));
    }
    return out;
}

void rollingShutterVelocities(const std::vector<Pose>& poses, const std::vector<double>& times,
                              std::vector<glm::vec3>& linear, std::vector<glm::vec3>& angular){
    const size_t n = poses.size();
    linear.assign(n, glm::vec3(0.0f));
    angular.assign(n, glm::vec3(0.0f));
    if(n < 2 || times.size() != n) return;
    //Kut rotacije od a do b u osima a: log(a^-1 b)
    auto turn = [](const glm::quat& a, const glm::quat& b){
        glm::quat d = glm::normalize(glm::conjugate(a) * b);
        if(d.w < 0.0f) d = -d;
        const float sine = glm::length(glm::vec3(d.x, d.y, d.z));
        if(sine < 1e-9f) return glm::vec3(0.0f);
        return glm::vec3(d.x, d.y, d.z) * (2.0f * std::atan2(sine, d.w) / sine);
    };
    for(size_t i = 0; i < n; ++i){
        const size_t a = i == 0 ? 0 : i - 1;
        const size_t b = i + 1 == n ? n - 1 : i + 1;
        const double span = times[b] - times[a];
        if(span <= 0.0) continue;
        linear[i] = (poses[b].position - poses[a].position) / float(span);
        //Kutna brzina u osima kamere i: zbroj dvaju polukoraka, svaki u osima svoje pocetne poze,
        //prebacen u osi kamere i (za male kutove razlika je drugog reda)
        const glm::vec3 total = turn(poses[i].orientation, poses[b].orientation) - turn(poses[i].orientation, poses[a].orientation);
        angular[i] = total / float(span);
    }
}

bool pointJacobian(const Pose& pose,
                   const Intrinsics& intrinsics,
                   const glm::vec3& point,
                   const glm::vec2& observed,
                   double residual[2],
                   double jacobian[2][3]){
    const glm::vec3 inCamera = glm::conjugate(pose.orientation) * (point - pose.position);
    const double depth = -double(inCamera.z);
    if(depth <= 0.0){
        return false;
    }

    const double fx = intrinsics.fx, fy = intrinsics.fy;
    const double x = inCamera.x, y = inCamera.y;

    residual[0] = double(intrinsics.cx) + fx * x / depth - double(observed.x);
    residual[1] = double(intrinsics.cy) - fy * y / depth - double(observed.y);

    //Piksel po tocki u kameri (2x3), isti kao u poseJacobian
    const double duv[2][3] = {
        { fx / depth,        0.0,        fx * x / (depth * depth) },
        { 0.0,        -fy / depth,      -fy * y / (depth * depth) }
    };

    //Tocka u kameri po tocki u SVIJETU je rotacija pogleda: p = R'(x - t), pa je dp/dx = R'
    const glm::mat3 rotation = glm::mat3_cast(glm::conjugate(pose.orientation));
    for(int row = 0; row < 2; ++row){
        for(int column = 0; column < 3; ++column){
            jacobian[row][column] = duv[row][0] * double(rotation[column][0])
                                  + duv[row][1] * double(rotation[column][1])
                                  + duv[row][2] * double(rotation[column][2]);
        }
    }
    return true;
}

BundleResult bundleAdjust(const std::vector<Observation>& observations,
                          const std::vector<Pose>& poses,
                          const std::vector<glm::vec3>& points,
                          const Intrinsics& intrinsics,
                          const BundleConfig& config){
    using Clock = std::chrono::steady_clock;
    const auto totalStarted = Clock::now();
    auto elapsed = [](Clock::time_point started){
        return std::chrono::duration<double>(Clock::now() - started).count();
    };

    BundleResult result;
    result.poses = poses;
    result.points = points;

    if(observations.empty() || poses.empty() || points.empty()){
        result.timing.totalSeconds = elapsed(totalStarted);
        return result;
    }

    const size_t cameraCount = poses.size();
    const size_t pointCount = points.size();

    //Gauge: prva kamera se ne mice, pa rjesenje ne moze kliziti kroz prostor
    std::vector<int> freeIndex(cameraCount, -1);
    int freeCameras = 0;
    for(size_t camera = 0; camera < cameraCount; ++camera){
        if(config.fixFirstCamera && camera == 0) continue;
        if(camera < config.fixedCameras.size() && config.fixedCameras[camera]) continue;
        freeIndex[camera] = freeCameras++;
    }
    const int n = 6 * freeCameras;

    //E je prije bio vector<vector<CameraBlock>>: svaka tocka je u svakoj iteraciji zasebno
    //alocirala svoj sitni niz blokova. Na pravom grafu to je stotine tisuca malloc/free poziva po
    //bundle iteraciji. Kapacitet po tocki ovisi samo o ulaznim opazanjima, pa se jednom izracuna
    //prefix-sum i svi blokovi drze u jednom ravnom polju. blockCount kaze koliko ih je u ovoj
    //iteraciji stvarno proslo projekciju. Punjenje ide redom opazanja, a Schur redom tocaka i
    //blokova kao prije, pa se red zbrajanja ne mijenja ni za jedan double.
    std::vector<size_t> blockOffset(pointCount + 1, 0);
    for(const Observation& observation : observations){
        if(observation.camera >= cameraCount || observation.point >= pointCount) continue;
        if(freeIndex[observation.camera] < 0) continue;
        ++blockOffset[observation.point + 1];
    }
    for(size_t point = 0; point < pointCount; ++point){
        blockOffset[point + 1] += blockOffset[point];
    }
    std::vector<CameraBlock> E(blockOffset.back());
    std::vector<size_t> blockCount(pointCount, 0);

    double median = 0.0;
    const auto initialCostStarted = Clock::now();
    double cost = costOf(result.poses, result.points, intrinsics, observations, config.huberPixels, median, config);
    result.timing.costSeconds += elapsed(initialCostStarted);
    result.startMedian = median;
    result.endMedian = median;
    result.solved = true;

    double lambda = config.lambda;

    //=====================================================================================
    // JAKOBIJAN SE RACUNA JEDNOM PO OPAZANJU, PA SE PISE U DVA NEOVISNA PROSTORA U DVA PROLAZA.
    //
    // Prijasnja petlja je u JEDNOM prolazu radila troje: racunala jakobijan (skupo - projekcija,
    // trig), pisala u gPoint/C (indeksirano PO TOCKI) i pisala u gCamera/B/E (indeksirano PO
    // KAMERI). To se ne da razdijeliti po dretvama na jedan nacin kao Schur, jer bi svaka podjela
    // - po tocki ili po kameri - ostavila DRUGU polovicu pisanja da se sudara izmedju dretvi.
    //
    // Zato su tri prolaza, svaki siguran za sebe:
    //
    //   1. jakobijan          po OPAZANJU, svako u svoj red predmemorije - nema sudara, nema dijeljenog izlaza
    //   2. gCamera/B          po KAMERI, kao Schur - svaka dretva cita SVA opazanja i preskace tudju kameru
    //   3. gPoint/C, E        po TOCKI, isto - svaka dretva cita sva opazanja i preskace tudju tocku
    //
    // Isti obrazac koji je vec dokazan na Schuru: cijena je da svaka dretva prodje sve, dobitak je
    // da nijedna ne ceka drugu. Poredak zbrajanja unutar svake izlazne celije ostaje TOCNO onaj iz
    // izvornog jednog prolaza - opazanja se u prolazima 2 i 3 obilaze u IZVORNOM redoslijedu, samo
    // se tudja preskacu - pa je rezultat bit po bit isti kao prije
    //=====================================================================================
    struct Linearized{
        double residual[2];
        double pointPart[2][3];
        double cameraPart[2][6];
        double weightSquared = 0.0;
        uint8_t valid = 0;
    };
    std::vector<Linearized> cache(observations.size());

    for(uint32_t iteration = 0; iteration < config.maxIterations; ++iteration){
        const auto linearizeStarted = Clock::now();
        std::vector<double> B(size_t(freeCameras) * 36, 0.0);
        std::vector<double> gCamera(size_t(freeCameras) * 6, 0.0);
        std::vector<double> C(pointCount * 9, 0.0);
        std::vector<double> gPoint(pointCount * 3, 0.0);
        std::fill(blockCount.begin(), blockCount.end(), size_t(0));

        //PROLAZ 1: jakobijan po opazanju. Svaka dretva pise samo u svoj red - nema izlaza koji
        //dijele
        inBands(0, int(observations.size()), [&](uint32_t, int firstItem, int lastItem){
            for(int index = firstItem; index < lastItem; ++index){
                const Observation& observation = observations[size_t(index)];
                Linearized& slot = cache[size_t(index)];
                slot.valid = 0;
                if(observation.camera >= cameraCount || observation.point >= pointCount) continue;

                //Pod rolling shutterom jakobijan se racuna u pozi RETKA. Korak se i dalje primjenjuje
                //na pozu kadra; razlika u osima je reda s*|omega|*|korak|, dakle drugog reda
                const Pose pose = rollingShutterPose(result.poses[observation.camera], config, observation.camera,
                                                     observation.pixel.y, intrinsics.cy);
                const glm::vec3& point = result.points[observation.point];
                if(!bundleJacobians(pose, intrinsics, point, observation.pixel,
                                    slot.residual, slot.pointPart, slot.cameraPart)) continue;

                //Tezina pod Huberom mnozi i rezidual i jakobijan, pa ulazi u sve tri strane odjednom
                //Tezina po izvoru (vidi BundleConfig::coarseWeight) - kao izbijeljeni ostatak
                const double scale = observation.point < config.precisePointsFrom ? config.coarseWeight : 1.0;
                const double weight = scale * huberWeight(
                    scale * std::sqrt(slot.residual[0] * slot.residual[0] + slot.residual[1] * slot.residual[1]),
                    config.huberPixels);
                slot.weightSquared = weight * weight;
                slot.valid = 1;
            }
        });

        uint32_t used = 0;
        for(const Linearized& slot : cache) used += slot.valid;
        if(used < 4){
            break;
        }

        //PROLAZ 2: gCamera/B, po kameri - isti obrazac kao Schur nize
        inBands(0, freeCameras, [&](uint32_t, int firstCamera, int lastCamera){
            for(size_t index = 0; index < observations.size(); ++index){
                const Linearized& slot = cache[index];
                if(!slot.valid) continue;
                const Observation& observation = observations[index];

                const int free = freeIndex[observation.camera];
                if(free < firstCamera || free >= lastCamera) continue;   //ukljucuje fiksnu (-1)

                const size_t camera = size_t(free);
                for(int i = 0; i < 6; ++i){
                    for(int row = 0; row < 2; ++row) gCamera[camera * 6 + size_t(i)] += slot.weightSquared * slot.cameraPart[row][i] * slot.residual[row];
                    for(int j = 0; j < 6; ++j){
                        for(int row = 0; row < 2; ++row) B[camera * 36 + size_t(i * 6 + j)] += slot.weightSquared * slot.cameraPart[row][i] * slot.cameraPart[row][j];
                    }
                }
            }
        });

        //PROLAZ 3: gPoint/C i E, po tocki - isto. Fiksna kamera i dalje puni gPoint/C (tocka se
        //triangulira i iz nje), ali ne dobiva E blok - kao u izvorniku
        inBands(0, int(pointCount), [&](uint32_t, int firstPoint, int lastPoint){
            for(size_t index = 0; index < observations.size(); ++index){
                const Linearized& slot = cache[index];
                if(!slot.valid) continue;
                const Observation& observation = observations[index];
                if(int(observation.point) < firstPoint || int(observation.point) >= lastPoint) continue;

                for(int i = 0; i < 3; ++i){
                    for(int row = 0; row < 2; ++row) gPoint[observation.point * 3 + size_t(i)] += slot.weightSquared * slot.pointPart[row][i] * slot.residual[row];
                    for(int j = 0; j < 3; ++j){
                        for(int row = 0; row < 2; ++row) C[observation.point * 9 + size_t(i * 3 + j)] += slot.weightSquared * slot.pointPart[row][i] * slot.pointPart[row][j];
                    }
                }

                if(freeIndex[observation.camera] < 0) continue;   //fiksna kamera nema E blok

                const size_t camera = size_t(freeIndex[observation.camera]);
                CameraBlock block;
                block.camera = int(camera);
                for(int i = 0; i < 6; ++i){
                    for(int j = 0; j < 3; ++j){
                        for(int row = 0; row < 2; ++row) block.e[size_t(i * 3 + j)] += slot.weightSquared * slot.cameraPart[row][i] * slot.pointPart[row][j];
                    }
                }
                E[blockOffset[observation.point] + blockCount[observation.point]++] = block;
            }
        }, 1);

        //Prigusenje na dijagonalu oba bloka, razmjerno njoj samoj
        for(int c = 0; c < freeCameras; ++c){
            for(int i = 0; i < 6; ++i) B[size_t(c * 36 + i * 6 + i)] += lambda * B[size_t(c * 36 + i * 6 + i)] + 1e-12;
        }
        for(size_t p = 0; p < pointCount; ++p){
            for(int i = 0; i < 3; ++i) C[p * 9 + size_t(i * 3 + i)] += lambda * C[p * 9 + size_t(i * 3 + i)] + 1e-12;
        }
        result.timing.linearizeSeconds += elapsed(linearizeStarted);

        //SCHUR: svaka tocka se izbaci iz sustava, i ostane samo sustav po kamerama
        const auto schurStarted = Clock::now();
        std::vector<double> S(size_t(n) * size_t(n), 0.0);
        std::vector<double> rhs(size_t(n), 0.0);
        for(int c = 0; c < freeCameras; ++c){
            for(int i = 0; i < 6; ++i){
                rhs[size_t(c * 6 + i)] = -gCamera[size_t(c * 6 + i)];
                for(int j = 0; j < 6; ++j) S[size_t((c * 6 + i) * n + c * 6 + j)] = B[size_t(c * 36 + i * 6 + j)];
            }
        }

        //=================================================================================
        // SCHUR PO DRETVAMA, BIT PO BIT ISTO.
        //
        // Zbrajanje u pokretnom zarezu nije asocijativno, pa se posao NE smije podijeliti po
        // tockama: svaka tocka dodaje u iste celije S-a i rhs-a, a podijeljen zbroj daje drugi
        // broj. Zlatni hash bi pao, i to s pravom.
        //
        // Zato se ne dijeli ULAZ nego IZLAZ. Redak S-a i clan rhs-a odredjeni su PRVOM kamerom
        // para; kad dretva dobije svoj skup kamera, u njezine celije ne pise nitko drugi, a
        // doprinosi u njih i dalje stizu redom tocaka i redom blokova - dakle tocno onim
        // redoslijedom kojim su stizali sekvencijalno.
        //
        // Cijena je da svaka dretva prodje sve tocke i preskoci tudje blokove, ali to je usporedba
        // cijelog broja naspram 36 mnozenja koja preskace
        //=================================================================================

        std::vector<std::array<double, 9>> inverseC(pointCount);
        std::vector<uint8_t> pointUsable(pointCount, 0);

        //Inverz je po tocki i nista ne dijeli, pa ide ravno po dretvama
        inBands(0, int(pointCount), [&](uint32_t, int firstItem, int lastItem){
            for(int index = firstItem; index < lastItem; ++index){
                const size_t p = size_t(index);
                double m[3][3], inverse[3][3];
                for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) m[i][j] = C[p * 9 + size_t(i * 3 + j)];
                if(!invert3(m, inverse)) continue;

                pointUsable[p] = 1;
                for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) inverseC[p][size_t(i * 3 + j)] = inverse[i][j];
            }
        });

        inBands(0, freeCameras, [&](uint32_t, int firstCamera, int lastCamera){
            for(size_t p = 0; p < pointCount; ++p){
                if(!pointUsable[p]) continue;

                const double* inverse = inverseC[p].data();
                const size_t begin = blockOffset[p];
                const size_t end = begin + blockCount[p];

                for(size_t firstIndex = begin; firstIndex < end; ++firstIndex){
                    const CameraBlock& first = E[firstIndex];
                    if(first.camera < firstCamera || first.camera >= lastCamera) continue;

                    //W = E * C^-1 (6x3)
                    double W[6][3] = {};
                    for(int i = 0; i < 6; ++i){
                        for(int j = 0; j < 3; ++j){
                            double sum = 0.0;
                            for(int k = 0; k < 3; ++k) sum += first.e[size_t(i * 3 + k)] * inverse[size_t(k * 3 + j)];
                            W[i][j] = sum;
                        }
                    }
                    for(int i = 0; i < 6; ++i){
                        double sum = 0.0;
                        for(int k = 0; k < 3; ++k) sum += W[i][k] * gPoint[p * 3 + size_t(k)];
                        rhs[size_t(first.camera * 6 + i)] += sum;
                    }
                    for(size_t secondIndex = begin; secondIndex < end; ++secondIndex){
                        const CameraBlock& second = E[secondIndex];
                        for(int i = 0; i < 6; ++i){
                            for(int j = 0; j < 6; ++j){
                                double sum = 0.0;
                                for(int k = 0; k < 3; ++k) sum += W[i][k] * second.e[size_t(j * 3 + k)];
                                S[size_t((first.camera * 6 + i) * n + second.camera * 6 + j)] -= sum;
                            }
                        }
                    }
                }
            }
        }, 1);
        result.timing.schurSeconds += elapsed(schurStarted);

        const auto denseStarted = Clock::now();
        std::vector<double> cameraStep(size_t(n), 0.0);
        //=====================================================================================
        // TRAKASTO RJESAVANJE KAD SE ISPLATI - vidi Engine/Dense.h, solveBanded.
        //
        // S je n x n i rjesava se gusto, O(n^3). Ali gotovo je prazna: izmjereno na pravoj snimci
        // (kameni zid, 229 kamera) samo 15.8 posto parova kamera uopce dijeli neku tocku, i nijedan
        // par udaljeniji od 39 kamera ne dijeli nijednu. Ovdje se stvarna polusirina IZMJERI iz
        // blokova, pa se posao izvan nje preskoci.
        //
        // Aritmetika ostaje ista jer se preskacu iskljucivo clanovi koji su tocno nula; zlatni hash
        // bundlea to brani. Kad vrpca nije uska, ide se gustim putem kao i prije
        //=====================================================================================
        int cameraSpread = 0;
        for(size_t p = 0; p < pointCount; ++p){
            if(!pointUsable[p]) continue;
            const size_t begin = blockOffset[p], end = begin + blockCount[p];
            if(end <= begin) continue;
            int lowest = E[begin].camera, highest = E[begin].camera;
            for(size_t index = begin + 1; index < end; ++index){
                lowest = std::min(lowest, E[index].camera);
                highest = std::max(highest, E[index].camera);
            }
            cameraSpread = std::max(cameraSpread, highest - lowest);
        }
        const int halfWidth = 6 * (cameraSpread + 1) - 1;

        //Ispod trecine je granica opreza, ne mjerenja: sira vrpca uz ispunu od pivotiranja vise
        //nista ne stedi, a gusti put je vec provjeren
        const bool banded = n > 0 && halfWidth * 3 < n;
        if(banded && !solveBanded(S, rhs, n, halfWidth, cameraStep)){
            result.timing.denseSolveSeconds += elapsed(denseStarted);
            break;
        }
        if(!banded && n > 0 && !solveDense(S, rhs, n, cameraStep)){
            result.timing.denseSolveSeconds += elapsed(denseStarted);
            break;   //singularan sustav: dalje bi bilo nagadjanje
        }
        result.timing.denseSolveSeconds += elapsed(denseStarted);

        //Tocke se vrate uvrstavanjem: dp = C^-1 (-g - E' dc)
        const auto backSubstituteStarted = Clock::now();
        std::vector<glm::vec3> pointStep(pointCount, glm::vec3(0.0f));
        double stepLength = 0.0;
        for(double value : cameraStep) stepLength += value * value;

        for(size_t p = 0; p < pointCount; ++p){
            if(!pointUsable[p]) continue;
            double right[3] = { -gPoint[p * 3 + 0], -gPoint[p * 3 + 1], -gPoint[p * 3 + 2] };
            const size_t begin = blockOffset[p];
            const size_t end = begin + blockCount[p];
            for(size_t blockIndex = begin; blockIndex < end; ++blockIndex){
                const CameraBlock& block = E[blockIndex];
                for(int j = 0; j < 3; ++j){
                    double sum = 0.0;
                    for(int i = 0; i < 6; ++i) sum += block.e[size_t(i * 3 + j)] * cameraStep[size_t(block.camera * 6 + i)];
                    right[j] -= sum;
                }
            }
            double delta[3] = {0.0, 0.0, 0.0};
            for(int i = 0; i < 3; ++i){
                for(int k = 0; k < 3; ++k) delta[i] += inverseC[p][size_t(i * 3 + k)] * right[k];
            }
            pointStep[p] = glm::vec3(float(delta[0]), float(delta[1]), float(delta[2]));
            stepLength += delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
        }

        if(std::sqrt(stepLength) < config.minStep){
            result.timing.backSubstituteSeconds += elapsed(backSubstituteStarted);
            break;
        }

        BundleResult candidate;
        candidate.poses = result.poses;
        candidate.points = result.points;

        for(size_t camera = 0; camera < cameraCount; ++camera){
            const int index = freeIndex[camera];
            if(index < 0) continue;
            const glm::vec3 translation{static_cast<float>(cameraStep[size_t(index * 6 + 0)]),
                                        static_cast<float>(cameraStep[size_t(index * 6 + 1)]),
                                        static_cast<float>(cameraStep[size_t(index * 6 + 2)])};
            const glm::vec3 rotation{static_cast<float>(cameraStep[size_t(index * 6 + 3)]),
                                     static_cast<float>(cameraStep[size_t(index * 6 + 4)]),
                                     static_cast<float>(cameraStep[size_t(index * 6 + 5)])};

            candidate.poses[camera].position += result.poses[camera].orientation * translation;
            candidate.poses[camera].orientation = glm::normalize(result.poses[camera].orientation *
                glm::quat(1.0f, 0.5f * rotation.x, 0.5f * rotation.y, 0.5f * rotation.z));
        }
        for(size_t p = 0; p < pointCount; ++p) candidate.points[p] += pointStep[p];
        result.timing.backSubstituteSeconds += elapsed(backSubstituteStarted);

        double candidateMedian = 0.0;
        const auto candidateCostStarted = Clock::now();
        const double candidateCost = costOf(candidate.poses, candidate.points, intrinsics, observations, config.huberPixels, candidateMedian, config);
        result.timing.costSeconds += elapsed(candidateCostStarted);

        if(candidateCost < cost){
            result.poses = candidate.poses;
            result.points = candidate.points;
            cost = candidateCost;
            result.endMedian = candidateMedian;
            lambda = std::max(lambda / 3.0, 1e-12);
        }else{
            lambda *= 10.0;
            if(lambda > 1e12) break;
        }
        result.iterations = iteration + 1;
    }
    result.timing.totalSeconds = elapsed(totalStarted);
    return result;
}

}
