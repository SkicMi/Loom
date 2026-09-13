#include "Engine/SolvePose.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Engine{
namespace{

using Matrix6 = std::array<std::array<double, 6>, 6>;
using Vector6 = std::array<double, 6>;

//Gaussova eliminacija s biranjem stozera. Sest nepoznanica ne trazi biblioteku, a trazi da se
//singularan sustav PREPOZNA - inace se vrati korak od beskonacno i poza odleti
bool solve6(Matrix6 A, Vector6 b, Vector6& x){
    for(int column = 0; column < 6; ++column){
        int pivot = column;
        for(int row = column + 1; row < 6; ++row){
            if(std::fabs(A[row][column]) > std::fabs(A[pivot][column])) pivot = row;
        }
        if(std::fabs(A[pivot][column]) < 1e-12){
            return false;
        }
        std::swap(A[column], A[pivot]);
        std::swap(b[column], b[pivot]);

        for(int row = column + 1; row < 6; ++row){
            const double factor = A[row][column] / A[column][column];
            if(factor == 0.0) continue;
            for(int k = column; k < 6; ++k) A[row][k] -= factor * A[column][k];
            b[row] -= factor * b[column];
        }
    }

    for(int row = 5; row >= 0; --row){
        double sum = b[row];
        for(int k = row + 1; k < 6; ++k) sum -= A[row][k] * x[k];
        x[row] = sum / A[row][row];
    }
    return true;
}

//Medijan duljine reziduala, u pikselima - ista mjera kojom se mjeri i cijela faza S
double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

//Tezina jednog opazanja pod Huberom: 1 dok je promasaj ispod praga, pa opada kao koriejen omjera.
//Mnozi se i rezidual i jakobijan, sto je standardni nacin da se Huber dobije iz obicnih najmanjih
//kvadrata (iterativno preuzimanje tezina)
double huberWeight(double length, double delta){
    if(delta <= 0.0 || length <= delta) return 1.0;
    return std::sqrt(delta / length);
}

//Kazna jednog opazanja: kvadratna ispod praga, linearna iznad njega
double huberCost(double length, double delta){
    if(delta <= 0.0 || length <= delta) return length * length;
    return 2.0 * delta * length - delta * delta;
}

//Zbroj kazni i medijan promasaja, za danu pozu
double costOf(const Pose& pose, const Intrinsics& intrinsics,
              const std::vector<glm::vec3>& points,
              const std::vector<PointObservation>& observations,
              double delta,
              double& median){
    double cost = 0.0;
    std::vector<double> lengths;
    lengths.reserve(observations.size());

    for(const PointObservation& observation : observations){
        if(observation.point >= points.size()) continue;
        double residual[2];
        double jacobian[2][6];
        if(!poseJacobian(pose, intrinsics, points[observation.point], observation.pixel, residual, jacobian)){
            //Tocka iza kamere: kaznjava se, ali ne rusi racun
            cost += double(intrinsics.width) * double(intrinsics.width);
            lengths.push_back(double(intrinsics.width));
            continue;
        }
        const double length = std::sqrt(residual[0] * residual[0] + residual[1] * residual[1]);
        cost += huberCost(length, delta);
        lengths.push_back(length);
    }
    median = medianOf(lengths);
    return cost;
}

}

bool poseJacobian(const Pose& pose,
                  const Intrinsics& intrinsics,
                  const glm::vec3& point,
                  const glm::vec2& observed,
                  double residual[2],
                  double jacobian[2][6]){
    const glm::vec3 inCamera = glm::conjugate(pose.orientation) * (point - pose.position);
    const double depth = -double(inCamera.z);
    if(depth <= 0.0){
        return false;
    }

    const double fx = intrinsics.fx, fy = intrinsics.fy;
    const double x = inCamera.x, y = inCamera.y;

    residual[0] = double(intrinsics.cx) + fx * x / depth - double(observed.x);
    residual[1] = double(intrinsics.cy) - fy * y / depth - double(observed.y);

    //Piksel po tocki u kameri (2x3). Dubina je -z, pa derivacija po z ide preko 1/depth^2
    const double duv[2][3] = {
        { fx / depth,        0.0,        fx * x / (depth * depth) },
        { 0.0,        -fy / depth,      -fy * y / (depth * depth) }
    };

    //Tocka u kameri po pozi (3x6). Pomak kamere za dt pomakne tocku za -dt; mala rotacija dtheta
    //oko kamere pomakne je za dtheta x p, a to je matrica [p]x
    const double px = x, py = y, pz = double(inCamera.z);
    //Rotacijski dio je +[p]x, i predznak je ovdje izveden a ne pogodjen: uz p = R'(x - t) i korak
    //t <- t + R dt, R <- R exp([dtheta]x), prvi red daje p' = p - dt - dtheta x p, a
    //-dtheta x p = +p x dtheta = [p]x dtheta. Prva verzija je imala -[p]x: numericka derivacija
    //je to odmah pokazala kao relativnu razliku od tocno 2.00 (suprotan predznak, ne kriva
    //velicina), a solver je stajao na mjestu jer je svaki korak isao u krivu stranu
    const double dp[3][6] = {
        { -1.0,  0.0,  0.0,   0.0,  -pz,   py },
        {  0.0, -1.0,  0.0,   pz,   0.0,  -px },
        {  0.0,  0.0, -1.0,  -py,   px,   0.0 }
    };

    for(int row = 0; row < 2; ++row){
        for(int column = 0; column < 6; ++column){
            jacobian[row][column] = duv[row][0] * dp[0][column]
                                  + duv[row][1] * dp[1][column]
                                  + duv[row][2] * dp[2][column];
        }
    }
    return true;
}

PoseSolveResult solvePose(const std::vector<glm::vec3>& points,
                          const std::vector<PointObservation>& observations,
                          const Intrinsics& intrinsics,
                          const Pose& initial,
                          const PoseSolveConfig& config){
    PoseSolveResult result;
    result.pose = initial;

    //Tri tocke daju sest jednadzbi za sest nepoznanica - ispod toga sustav nema sto odrediti
    if(observations.size() < 3){
        return result;
    }

    double median = 0.0;
    double cost = costOf(result.pose, intrinsics, points, observations, config.huberPixels, median);
    result.startMedian = median;
    result.endMedian = median;
    result.solved = true;

    double lambda = config.lambda;

    for(uint32_t iteration = 0; iteration < config.maxIterations; ++iteration){
        Matrix6 H{};
        Vector6 g{};
        uint32_t used = 0;

        for(const PointObservation& observation : observations){
            if(observation.point >= points.size()) continue;
            double residual[2];
            double jacobian[2][6];
            if(!poseJacobian(result.pose, intrinsics, points[observation.point], observation.pixel, residual, jacobian)){
                continue;
            }
            ++used;
            const double weight = huberWeight(std::sqrt(residual[0] * residual[0] + residual[1] * residual[1]),
                                              config.huberPixels);
            for(int row = 0; row < 2; ++row){
                const double weighted = weight * residual[row];
                for(int i = 0; i < 6; ++i){
                    const double ji = weight * jacobian[row][i];
                    g[size_t(i)] += ji * weighted;
                    for(int j = 0; j < 6; ++j){
                        H[size_t(i)][size_t(j)] += ji * weight * jacobian[row][j];
                    }
                }
            }
        }

        if(used < 3){
            break;
        }

        //Prigusenje ide na dijagonalu, razmjerno njoj samoj: tako je isto za metre i za radijane
        Matrix6 damped = H;
        for(int i = 0; i < 6; ++i) damped[size_t(i)][size_t(i)] += lambda * H[size_t(i)][size_t(i)] + 1e-12;

        Vector6 step{};
        Vector6 negative{};
        for(int i = 0; i < 6; ++i) negative[size_t(i)] = -g[size_t(i)];
        if(!solve6(damped, negative, step)){
            break;
        }

        double length = 0.0;
        for(int i = 0; i < 6; ++i) length += step[size_t(i)] * step[size_t(i)];
        if(std::sqrt(length) < config.minStep){
            break;
        }

        //Pomak je u kamerinom sustavu, pa se u svijet vraca kroz orijentaciju; rotacija se
        //nadovezuje s desne strane, jer je i ona izrazena u kameri
        //Viticaste zagrade, ne obicne: float(step[0]) se dade procitati i kao deklaracija
        //parametra imena step, pa je prevodilac ovo prvi put shvatio kao deklaraciju funkcije
        const glm::vec3 deltaTranslation{static_cast<float>(step[0]), static_cast<float>(step[1]), static_cast<float>(step[2])};
        const glm::vec3 deltaRotation{static_cast<float>(step[3]), static_cast<float>(step[4]), static_cast<float>(step[5])};

        Pose candidate = result.pose;
        candidate.position += result.pose.orientation * deltaTranslation;
        candidate.orientation = glm::normalize(result.pose.orientation *
            glm::quat(1.0f, 0.5f * deltaRotation.x, 0.5f * deltaRotation.y, 0.5f * deltaRotation.z));

        double candidateMedian = 0.0;
        const double candidateCost = costOf(candidate, intrinsics, points, observations, config.huberPixels, candidateMedian);

        if(candidateCost < cost){
            result.pose = candidate;
            cost = candidateCost;
            result.endMedian = candidateMedian;
            lambda = std::max(lambda / 3.0, 1e-12);
        }else{
            //Korak je promasio: vise prigusenja znaci kraci i oprezniji sljedeci
            lambda *= 10.0;
            if(lambda > 1e12) break;
        }
        result.iterations = iteration + 1;
    }
    return result;
}

}
