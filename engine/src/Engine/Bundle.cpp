#include "Engine/Bundle.h"
#include "Engine/Dense.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Engine{
namespace{

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
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

double costOf(const std::vector<Pose>& poses, const std::vector<glm::vec3>& points,
              const Intrinsics& intrinsics, const std::vector<Observation>& observations,
              double delta,
              double& median){
    double cost = 0.0;
    std::vector<double> lengths;
    lengths.reserve(observations.size());

    for(const Observation& observation : observations){
        if(observation.camera >= poses.size() || observation.point >= points.size()) continue;
        double residual[2], jacobian[2][3];
        if(!pointJacobian(poses[observation.camera], intrinsics, points[observation.point], observation.pixel, residual, jacobian)){
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

//Jedan blok E za par (kamera, tocka): 6x3
struct CameraBlock{
    int camera = 0;
    std::array<double, 18> e{};
};

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
    BundleResult result;
    result.poses = poses;
    result.points = points;

    if(observations.empty() || poses.empty() || points.empty()){
        return result;
    }

    const size_t cameraCount = poses.size();
    const size_t pointCount = points.size();

    //Gauge: prva kamera se ne mice, pa rjesenje ne moze kliziti kroz prostor
    std::vector<int> freeIndex(cameraCount, -1);
    int freeCameras = 0;
    for(size_t camera = 0; camera < cameraCount; ++camera){
        if(config.fixFirstCamera && camera == 0) continue;
        freeIndex[camera] = freeCameras++;
    }
    const int n = 6 * freeCameras;

    double median = 0.0;
    double cost = costOf(result.poses, result.points, intrinsics, observations, config.huberPixels, median);
    result.startMedian = median;
    result.endMedian = median;
    result.solved = true;

    double lambda = config.lambda;

    for(uint32_t iteration = 0; iteration < config.maxIterations; ++iteration){
        std::vector<double> B(size_t(freeCameras) * 36, 0.0);
        std::vector<double> gCamera(size_t(freeCameras) * 6, 0.0);
        std::vector<double> C(pointCount * 9, 0.0);
        std::vector<double> gPoint(pointCount * 3, 0.0);
        std::vector<std::vector<CameraBlock>> E(pointCount);

        uint32_t used = 0;
        for(const Observation& observation : observations){
            if(observation.camera >= cameraCount || observation.point >= pointCount) continue;

            const Pose& pose = result.poses[observation.camera];
            const glm::vec3& point = result.points[observation.point];

            double residual[2], pointPart[2][3];
            if(!pointJacobian(pose, intrinsics, point, observation.pixel, residual, pointPart)) continue;
            ++used;

            //Tezina pod Huberom mnozi i rezidual i jakobijan, pa ulazi u sve tri strane odjednom
            const double weight = huberWeight(std::sqrt(residual[0] * residual[0] + residual[1] * residual[1]),
                                              config.huberPixels);
            const double weightSquared = weight * weight;

            for(int i = 0; i < 3; ++i){
                for(int row = 0; row < 2; ++row) gPoint[observation.point * 3 + size_t(i)] += weightSquared * pointPart[row][i] * residual[row];
                for(int j = 0; j < 3; ++j){
                    for(int row = 0; row < 2; ++row) C[observation.point * 9 + size_t(i * 3 + j)] += weightSquared * pointPart[row][i] * pointPart[row][j];
                }
            }

            //Fiksna kamera nema nepoznanica, ali njezina opazanja i dalje drze tocke
            if(freeIndex[observation.camera] < 0) continue;

            double cameraResidual[2], cameraPart[2][6];
            if(!poseJacobian(pose, intrinsics, point, observation.pixel, cameraResidual, cameraPart)) continue;

            const size_t camera = size_t(freeIndex[observation.camera]);
            for(int i = 0; i < 6; ++i){
                for(int row = 0; row < 2; ++row) gCamera[camera * 6 + size_t(i)] += weightSquared * cameraPart[row][i] * residual[row];
                for(int j = 0; j < 6; ++j){
                    for(int row = 0; row < 2; ++row) B[camera * 36 + size_t(i * 6 + j)] += weightSquared * cameraPart[row][i] * cameraPart[row][j];
                }
            }

            CameraBlock block;
            block.camera = int(camera);
            for(int i = 0; i < 6; ++i){
                for(int j = 0; j < 3; ++j){
                    for(int row = 0; row < 2; ++row) block.e[size_t(i * 3 + j)] += weightSquared * cameraPart[row][i] * pointPart[row][j];
                }
            }
            E[observation.point].push_back(block);
        }

        if(used < 4){
            break;
        }

        //Prigusenje na dijagonalu oba bloka, razmjerno njoj samoj
        for(int c = 0; c < freeCameras; ++c){
            for(int i = 0; i < 6; ++i) B[size_t(c * 36 + i * 6 + i)] += lambda * B[size_t(c * 36 + i * 6 + i)] + 1e-12;
        }
        for(size_t p = 0; p < pointCount; ++p){
            for(int i = 0; i < 3; ++i) C[p * 9 + size_t(i * 3 + i)] += lambda * C[p * 9 + size_t(i * 3 + i)] + 1e-12;
        }

        //SCHUR: svaka tocka se izbaci iz sustava, i ostane samo sustav po kamerama
        std::vector<double> S(size_t(n) * size_t(n), 0.0);
        std::vector<double> rhs(size_t(n), 0.0);
        for(int c = 0; c < freeCameras; ++c){
            for(int i = 0; i < 6; ++i){
                rhs[size_t(c * 6 + i)] = -gCamera[size_t(c * 6 + i)];
                for(int j = 0; j < 6; ++j) S[size_t((c * 6 + i) * n + c * 6 + j)] = B[size_t(c * 36 + i * 6 + j)];
            }
        }

        std::vector<std::array<double, 9>> inverseC(pointCount);
        std::vector<uint8_t> pointUsable(pointCount, 0);

        for(size_t p = 0; p < pointCount; ++p){
            double m[3][3], inverse[3][3];
            for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) m[i][j] = C[p * 9 + size_t(i * 3 + j)];
            if(!invert3(m, inverse)) continue;

            pointUsable[p] = 1;
            for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) inverseC[p][size_t(i * 3 + j)] = inverse[i][j];

            for(const CameraBlock& first : E[p]){
                //W = E * C^-1 (6x3)
                double W[6][3] = {};
                for(int i = 0; i < 6; ++i){
                    for(int j = 0; j < 3; ++j){
                        double sum = 0.0;
                        for(int k = 0; k < 3; ++k) sum += first.e[size_t(i * 3 + k)] * inverse[k][j];
                        W[i][j] = sum;
                    }
                }
                for(int i = 0; i < 6; ++i){
                    double sum = 0.0;
                    for(int k = 0; k < 3; ++k) sum += W[i][k] * gPoint[p * 3 + size_t(k)];
                    rhs[size_t(first.camera * 6 + i)] += sum;
                }
                for(const CameraBlock& second : E[p]){
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

        std::vector<double> cameraStep(size_t(n), 0.0);
        if(n > 0 && !solveDense(S, rhs, n, cameraStep)){
            break;   //singularan sustav: dalje bi bilo nagadjanje
        }

        //Tocke se vrate uvrstavanjem: dp = C^-1 (-g - E' dc)
        std::vector<glm::vec3> pointStep(pointCount, glm::vec3(0.0f));
        double stepLength = 0.0;
        for(double value : cameraStep) stepLength += value * value;

        for(size_t p = 0; p < pointCount; ++p){
            if(!pointUsable[p]) continue;
            double right[3] = { -gPoint[p * 3 + 0], -gPoint[p * 3 + 1], -gPoint[p * 3 + 2] };
            for(const CameraBlock& block : E[p]){
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

        double candidateMedian = 0.0;
        const double candidateCost = costOf(candidate.poses, candidate.points, intrinsics, observations, config.huberPixels, candidateMedian);

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
    return result;
}

}
