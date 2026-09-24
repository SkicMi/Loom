#include "Engine/SelfCalibration.h"

#include <chrono>
#include "Engine/Dense.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Engine{
namespace{

struct RaySample{
    double x;
    double y;
    double radiusSquared;
    double observedX;
    double observedY;
};

using Matrix3 = double[3][3];

struct ImagePair{
    std::vector<glm::vec2> first;
    std::vector<glm::vec2> second;
};

struct PairFundamental{
    Matrix3 fundamental{};
    double parallaxEvidence = 0.0;
};

double median(std::vector<double> values){
    if(values.empty()) return 0.0;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    return values[middle];
}

double rms(const std::vector<RaySample>& samples, double focal, double k1){
    if(samples.empty()) return 0.0;
    double sum = 0.0;
    for(const RaySample& sample : samples){
        const double radial = 1.0 + k1 * sample.radiusSquared;
        const double du = focal * sample.x * radial - sample.observedX;
        const double dv = focal * sample.y * radial - sample.observedY;
        sum += du * du + dv * dv;
    }
    return std::sqrt(sum / (2.0 * double(samples.size())));
}

std::vector<ImagePair> collectImagePairs(const std::vector<Observation>& observations,
                                         uint32_t cameraCount, uint32_t pointCount,
                                         uint32_t minimumSharedPoints){
    const float missing = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::vector<glm::vec2>> pixels(cameraCount,
        std::vector<glm::vec2>(pointCount, glm::vec2(missing)));
    for(const Observation& observation : observations){
        if(observation.camera < cameraCount && observation.point < pointCount)
            pixels[observation.camera][observation.point] = observation.pixel;
    }

    std::vector<ImagePair> pairs;
    for(uint32_t first = 0; first < cameraCount; ++first){
        for(uint32_t second = first + 1; second < cameraCount; ++second){
            ImagePair pair;
            for(uint32_t point = 0; point < pointCount; ++point){
                if(!std::isfinite(pixels[first][point].x) || !std::isfinite(pixels[second][point].x)) continue;
                pair.first.push_back(pixels[first][point]);
                pair.second.push_back(pixels[second][point]);
            }
            if(pair.first.size() >= minimumSharedPoints) pairs.push_back(std::move(pair));
        }
    }
    return pairs;
}

void multiply(const Matrix3& a, const Matrix3& b, Matrix3& out){
    Matrix3 product{};
    for(int row = 0; row < 3; ++row)
        for(int column = 0; column < 3; ++column)
            for(int k = 0; k < 3; ++k) product[row][column] += a[row][k] * b[k][column];
    std::copy(&product[0][0], &product[0][0] + 9, &out[0][0]);
}

void transpose(const Matrix3& input, Matrix3& output){
    for(int row = 0; row < 3; ++row)
        for(int column = 0; column < 3; ++column) output[row][column] = input[column][row];
}

bool estimateFundamental(const ImagePair& pair, const Intrinsics& camera, Matrix3& fundamental){
    if(pair.first.size() < 8 || pair.first.size() != pair.second.size()) return false;
    const double imageScale = double(std::max(camera.width, 1u));
    std::vector<glm::dvec2> first, second;
    first.reserve(pair.first.size());
    second.reserve(pair.second.size());
    glm::dvec2 meanFirst(0.0), meanSecond(0.0);
    for(size_t i = 0; i < pair.first.size(); ++i){
        first.emplace_back((double(pair.first[i].x) - camera.cx) / imageScale,
                           (double(pair.first[i].y) - camera.cy) / imageScale);
        second.emplace_back((double(pair.second[i].x) - camera.cx) / imageScale,
                            (double(pair.second[i].y) - camera.cy) / imageScale);
        meanFirst += first.back();
        meanSecond += second.back();
    }
    meanFirst /= double(first.size());
    meanSecond /= double(second.size());
    double distanceFirst = 0.0, distanceSecond = 0.0;
    for(size_t i = 0; i < first.size(); ++i){
        distanceFirst += glm::length(first[i] - meanFirst);
        distanceSecond += glm::length(second[i] - meanSecond);
    }
    distanceFirst /= double(first.size());
    distanceSecond /= double(second.size());
    if(distanceFirst < 1e-9 || distanceSecond < 1e-9) return false;
    const double scaleFirst = std::sqrt(2.0) / distanceFirst;
    const double scaleSecond = std::sqrt(2.0) / distanceSecond;

    std::vector<double> normal(81, 0.0);
    for(size_t i = 0; i < first.size(); ++i){
        const glm::dvec2 a = scaleFirst * (first[i] - meanFirst);
        const glm::dvec2 b = scaleSecond * (second[i] - meanSecond);
        const double row[9] = {b.x*a.x, b.x*a.y, b.x, b.y*a.x, b.y*a.y, b.y, a.x, a.y, 1.0};
        for(int r = 0; r < 9; ++r)
            for(int c = 0; c < 9; ++c) normal[size_t(r*9+c)] += row[r] * row[c];
    }
    std::vector<double> vector;
    if(!smallestEigenvector(normal, 9, vector)) return false;
    Matrix3 normalized{};
    for(int row = 0; row < 3; ++row)
        for(int column = 0; column < 3; ++column) normalized[row][column] = vector[size_t(row*3+column)];

    Matrix3 ntN{};
    for(int row = 0; row < 3; ++row)
        for(int column = 0; column < 3; ++column)
            for(int k = 0; k < 3; ++k) ntN[row][column] += normalized[k][row] * normalized[k][column];
    double values[3], vectors[3][3];
    symmetricEigen3(ntN, values, vectors);
    double fv[3]{};
    for(int row = 0; row < 3; ++row)
        for(int column = 0; column < 3; ++column) fv[row] += normalized[row][column] * vectors[column][2];
    for(int row = 0; row < 3; ++row)
        for(int column = 0; column < 3; ++column) normalized[row][column] -= fv[row] * vectors[column][2];

    Matrix3 firstTransform = {{scaleFirst, 0.0, -scaleFirst*meanFirst.x},
                              {0.0, scaleFirst, -scaleFirst*meanFirst.y}, {0.0, 0.0, 1.0}};
    Matrix3 secondTransform = {{scaleSecond, 0.0, -scaleSecond*meanSecond.x},
                               {0.0, scaleSecond, -scaleSecond*meanSecond.y}, {0.0, 0.0, 1.0}};
    Matrix3 secondTranspose{}, temporary{};
    transpose(secondTransform, secondTranspose);
    multiply(secondTranspose, normalized, temporary);
    multiply(temporary, firstTransform, fundamental);
    return true;
}

bool bougnouxFocalSquared(const Matrix3& fundamental, double& focalSquared){
    Matrix3 ft{}, fft{};
    transpose(fundamental, ft);
    multiply(fundamental, ft, fft);
    std::vector<double> packed(9);
    std::copy(&fft[0][0], &fft[0][0] + 9, packed.begin());
    std::vector<double> epipole;
    if(!smallestEigenvector(packed, 3, epipole)) return false;

    const double ix = fundamental[0][2];
    const double iy = fundamental[1][2];
    const double firstTerm = epipole[0] * iy - epipole[1] * ix;
    const double secondTerm = fundamental[2][2];

    const double ftpx = fundamental[2][0];
    const double ftpy = fundamental[2][1];
    const double fx = fundamental[0][0] * ftpx + fundamental[0][1] * ftpy;
    const double fy = fundamental[1][0] * ftpx + fundamental[1][1] * ftpy;
    const double denominator = epipole[0] * fy - epipole[1] * fx;
    if(std::fabs(denominator) < 1e-14) return false;
    focalSquared = -(firstTerm * secondTerm) / denominator;
    return std::isfinite(focalSquared) && focalSquared > 0.0;
}

double essentialConstraintError(const Matrix3& fundamental, double normalizedFocal){
    const double diagonal[3] = {normalizedFocal, normalizedFocal, 1.0};
    Matrix3 essential{};
    for(int row = 0; row < 3; ++row)
        for(int column = 0; column < 3; ++column)
            essential[row][column] = diagonal[row] * fundamental[row][column] * diagonal[column];
    Matrix3 transposeEssential{}, ete{};
    transpose(essential, transposeEssential);
    multiply(transposeEssential, essential, ete);
    double values[3], vectors[3][3];
    symmetricEigen3(ete, values, vectors);
    const double first = std::sqrt(std::max(values[0], 0.0));
    const double second = std::sqrt(std::max(values[1], 0.0));
    if(second <= 1e-14) return std::numeric_limits<double>::infinity();
    return std::fabs(first - second) / (first + second);
}

double homographyResidualPixels(const ImagePair& pair, const Intrinsics& camera){
    const double scale = double(std::max(camera.width, 1u));
    std::vector<double> normal(64, 0.0), rhs(8, 0.0);
    for(size_t i = 0; i < pair.first.size(); ++i){
        const double x = (double(pair.first[i].x) - camera.cx) / scale;
        const double y = (double(pair.first[i].y) - camera.cy) / scale;
        const double u = (double(pair.second[i].x) - camera.cx) / scale;
        const double v = (double(pair.second[i].y) - camera.cy) / scale;
        const double rows[2][8] = {{x,y,1,0,0,0,-u*x,-u*y},
                                   {0,0,0,x,y,1,-v*x,-v*y}};
        const double targets[2] = {u, v};
        for(int equation = 0; equation < 2; ++equation){
            for(int r = 0; r < 8; ++r){
                rhs[size_t(r)] += rows[equation][r] * targets[equation];
                for(int c = 0; c < 8; ++c)
                    normal[size_t(r*8+c)] += rows[equation][r] * rows[equation][c];
            }
        }
    }
    std::vector<double> h;
    if(!solveDense(normal, rhs, 8, h)) return 0.0;
    std::vector<double> errors;
    errors.reserve(pair.first.size());
    for(size_t i = 0; i < pair.first.size(); ++i){
        const double x = (double(pair.first[i].x) - camera.cx) / scale;
        const double y = (double(pair.first[i].y) - camera.cy) / scale;
        const double denominator = h[6]*x + h[7]*y + 1.0;
        if(std::fabs(denominator) < 1e-12) continue;
        const double u = (h[0]*x + h[1]*y + h[2]) / denominator;
        const double v = (h[3]*x + h[4]*y + h[5]) / denominator;
        const double observedU = (double(pair.second[i].x) - camera.cx) / scale;
        const double observedV = (double(pair.second[i].y) - camera.cy) / scale;
        errors.push_back(std::hypot(u - observedU, v - observedV) * scale);
    }
    return median(std::move(errors));
}

}

const char* viewGraphCalibrationStatusName(ViewGraphCalibrationStatus status){
    switch(status){
        case ViewGraphCalibrationStatus::Determined: return "odredjena";
        case ViewGraphCalibrationStatus::InsufficientData: return "premalo podataka";
        case ViewGraphCalibrationStatus::DegenerateGeometry: return "degenerirana geometrija";
        case ViewGraphCalibrationStatus::FlatObjective: return "ravna ciljna funkcija";
    }
    return "nepoznato";
}

ViewGraphCalibrationResult estimateViewGraphFocal(const std::vector<Observation>& observations,
                                                  uint32_t cameraCount,
                                                  uint32_t pointCount,
                                                  const Intrinsics& templateCamera,
                                                  const ViewGraphCalibrationConfig& config){
    ViewGraphCalibrationResult empty;
    if(cameraCount < 2 || pointCount < config.minimumSharedPoints){
        empty.status = ViewGraphCalibrationStatus::InsufficientData;
        return empty;
    }
    const double width = double(std::max(templateCamera.width, 1u));
    const double minimum = config.minimumFocalInImageWidths * width;
    const double maximum = config.maximumFocalInImageWidths * width;
    const std::vector<ImagePair> rawPairs = collectImagePairs(observations, cameraCount, pointCount,
                                                              config.minimumSharedPoints);
    if(rawPairs.size() < config.minimumPairs){
        empty.status = ViewGraphCalibrationStatus::InsufficientData;
        return empty;
    }
    uint32_t rawParallaxPairs = 0;
    for(const ImagePair& pair : rawPairs){
        if(homographyResidualPixels(pair, templateCamera) >= config.minimumHomographyResidualPixels)
            ++rawParallaxPairs;
    }
    if(rawParallaxPairs < config.minimumPairs){
        empty.status = ViewGraphCalibrationStatus::DegenerateGeometry;
        return empty;
    }

    auto evaluatePinhole = [&](const std::vector<Observation>& corrected){
        ViewGraphCalibrationResult result;
        const std::vector<ImagePair> pairs = collectImagePairs(corrected, cameraCount, pointCount,
                                                               config.minimumSharedPoints);
        if(pairs.size() < config.minimumPairs){
            result.status = ViewGraphCalibrationStatus::InsufficientData;
            return result;
        }
        std::vector<PairFundamental> pairModels;
        for(const ImagePair& pair : pairs){
            const double homographyResidual = homographyResidualPixels(pair, templateCamera);
            if(homographyResidual < config.minimumHomographyResidualPixels) continue;
            Matrix3 fundamental{};
            if(!estimateFundamental(pair, templateCamera, fundamental)) continue;
            double fundamentalNorm = 0.0;
            for(const auto& row : fundamental)
                for(double value : row) fundamentalNorm += value * value;
            fundamentalNorm = std::sqrt(fundamentalNorm);
            if(fundamentalNorm <= 0.0 ||
               std::fabs(fundamental[2][2]) < config.minimumBougnouxScale * fundamentalNorm) continue;
            double firstSquared = 0.0, secondSquared = 0.0;
            Matrix3 transposed{};
            transpose(fundamental, transposed);
            if(!bougnouxFocalSquared(fundamental, firstSquared) ||
               !bougnouxFocalSquared(transposed, secondSquared)) continue;
            const double first = std::sqrt(firstSquared) * width;
            const double second = std::sqrt(secondSquared) * width;
            if(first < minimum || first > maximum || second < minimum || second > maximum) continue;
            if(std::fabs(first - second) / std::max(first, second) > config.maximumPairFocalDisagreement) continue;
            PairFundamental model;
            std::copy(&fundamental[0][0], &fundamental[0][0] + 9, &model.fundamental[0][0]);
            model.parallaxEvidence = homographyResidual;
            pairModels.push_back(model);
        }
        if(pairModels.size() < config.minimumPairs){
            result.status = ViewGraphCalibrationStatus::DegenerateGeometry;
            result.usedPairs = uint32_t(pairModels.size());
            return result;
        }
        std::vector<double> evidence;
        for(const PairFundamental& model : pairModels) evidence.push_back(model.parallaxEvidence);
        const double evidenceLimit = median(std::move(evidence));
        std::vector<PairFundamental> strongModels;
        for(const PairFundamental& model : pairModels)
            if(model.parallaxEvidence >= evidenceLimit) strongModels.push_back(model);

        double bestError = std::numeric_limits<double>::infinity();
        for(uint32_t sample = 0; sample < 320; ++sample){
            const double t = double(sample) / 319.0;
            const double focal = minimum * std::pow(maximum / minimum, t);
            std::vector<double> errors;
            for(const PairFundamental& model : strongModels)
                errors.push_back(essentialConstraintError(model.fundamental, focal / width));
            const double error = median(std::move(errors));
            if(error < bestError){
                bestError = error;
                result.focalPixels = focal;
            }
        }
        result.medianErrorPixels = bestError * width;
        result.usedPairs = uint32_t(strongModels.size());
        result.status = std::isfinite(bestError) ? ViewGraphCalibrationStatus::Determined
                                                 : ViewGraphCalibrationStatus::FlatObjective;
        result.determined = std::isfinite(bestError);
        return result;
    };

    ViewGraphCalibrationResult best;
    best.medianErrorPixels = std::numeric_limits<double>::infinity();
    const uint32_t radialSamples = std::max(config.radialSamples, 1u);
    for(uint32_t radialSample = 0; radialSample < radialSamples; ++radialSample){
        const double t = radialSamples == 1 ? 0.5 : double(radialSample) / double(radialSamples - 1);
        const double imageRadial = config.maximumAbsoluteImageRadial * (2.0 * t - 1.0);
        std::vector<Observation> corrected = observations;
        for(Observation& observation : corrected){
            const double distortedX = (double(observation.pixel.x) - templateCamera.cx) / width;
            const double distortedY = (double(observation.pixel.y) - templateCamera.cy) / width;
            double x = distortedX, y = distortedY;
            for(int iteration = 0; iteration < 8; ++iteration){
                const double factor = 1.0 + imageRadial * (x*x + y*y);
                if(std::fabs(factor) < 0.2) break;
                x = distortedX / factor;
                y = distortedY / factor;
            }
            observation.pixel = glm::vec2(float(templateCamera.cx + width*x),
                                          float(templateCamera.cy + width*y));
        }
        ViewGraphCalibrationResult candidate = evaluatePinhole(corrected);
        if(!candidate.determined || candidate.medianErrorPixels >= best.medianErrorPixels) continue;
        candidate.k1 = imageRadial * std::pow(candidate.focalPixels / width, 2.0);
        best = candidate;
    }
    if(!best.determined){
        empty.status = ViewGraphCalibrationStatus::DegenerateGeometry;
        return empty;
    }
    return best;
}

const char* selfCalibrationStatusName(SelfCalibrationStatus status){
    switch(status){
        case SelfCalibrationStatus::Determined: return "odredjena";
        case SelfCalibrationStatus::InsufficientData: return "premalo podataka";
        case SelfCalibrationStatus::DegenerateGeometry: return "degenerirana geometrija";
        case SelfCalibrationStatus::IllConditioned: return "lose uvjetovano";
        case SelfCalibrationStatus::OutOfRange: return "izvan fizickih granica";
    }
    return "nepoznato";
}

SelfCalibrationResult refineSharedIntrinsics(const std::vector<Observation>& observations,
                                             const std::vector<Pose>& poses,
                                             const std::vector<glm::vec3>& points,
                                             const Intrinsics& initial,
                                             const SelfCalibrationConfig& config){
    SelfCalibrationResult result;
    result.intrinsics = initial;

    std::vector<RaySample> samples;
    std::vector<uint8_t> cameraUsed(poses.size(), 0);
    std::vector<double> depths;
    samples.reserve(observations.size());
    depths.reserve(observations.size());

    for(const Observation& observation : observations){
        if(observation.camera >= poses.size() || observation.point >= points.size()) continue;
        const Pose& pose = poses[observation.camera];
        const glm::vec3 cameraPoint = glm::conjugate(pose.orientation) *
                                      (points[observation.point] - pose.position);
        const double depth = -double(cameraPoint.z);
        if(!(depth > 1e-6) || !std::isfinite(depth)) continue;

        RaySample sample;
        sample.x = double(cameraPoint.x) / depth;
        sample.y = -double(cameraPoint.y) / depth;
        sample.radiusSquared = sample.x * sample.x + sample.y * sample.y;
        sample.observedX = double(observation.pixel.x) - double(initial.cx);
        sample.observedY = double(observation.pixel.y) - double(initial.cy);
        if(!std::isfinite(sample.observedX) || !std::isfinite(sample.observedY)) continue;
        samples.push_back(sample);
        depths.push_back(depth);
        cameraUsed[observation.camera] = 1;
    }

    result.usedObservations = uint32_t(samples.size());
    const uint32_t usedCameras = uint32_t(std::count(cameraUsed.begin(), cameraUsed.end(), uint8_t(1)));
    if(samples.size() < config.minimumObservations || usedCameras < config.minimumCameras){
        result.status = SelfCalibrationStatus::InsufficientData;
        return result;
    }

    double maximumBaseline = 0.0;
    for(size_t a = 0; a < poses.size(); ++a){
        if(!cameraUsed[a]) continue;
        for(size_t b = a + 1; b < poses.size(); ++b){
            if(cameraUsed[b]) maximumBaseline = std::max(maximumBaseline,
                double(glm::length(poses[a].position - poses[b].position)));
        }
    }
    const double sceneDepth = median(depths);
    result.baselineToDepth = sceneDepth > 0.0 ? maximumBaseline / sceneDepth : 0.0;
    if(result.baselineToDepth < config.minimumBaselineToDepth){
        result.status = SelfCalibrationStatus::DegenerateGeometry;
        return result;
    }

    const double initialFocal = 0.5 * (double(initial.fx) + double(initial.fy));
    result.startRms = rms(samples, initialFocal, initial.k1);
    double focal = initialFocal;
    double focalTimesK1 = initialFocal * double(initial.k1);

    const uint32_t iterations = std::max(1u, config.robustIterations);
    for(uint32_t iteration = 0; iteration < iterations; ++iteration){
        double aa = 0.0, ab = 0.0, bb = 0.0, ay = 0.0, by = 0.0;
        for(const RaySample& sample : samples){
            const double radialTermX = sample.x * sample.radiusSquared;
            const double radialTermY = sample.y * sample.radiusSquared;
            const double du = focal * sample.x + focalTimesK1 * radialTermX - sample.observedX;
            const double dv = focal * sample.y + focalTimesK1 * radialTermY - sample.observedY;
            const double error = std::sqrt(du * du + dv * dv);
            const double weight = config.huberPixels > 0.0 && error > config.huberPixels
                ? config.huberPixels / error : 1.0;

            aa += weight * (sample.x * sample.x + sample.y * sample.y);
            ab += weight * (sample.x * radialTermX + sample.y * radialTermY);
            bb += weight * (radialTermX * radialTermX + radialTermY * radialTermY);
            ay += weight * (sample.x * sample.observedX + sample.y * sample.observedY);
            by += weight * (radialTermX * sample.observedX + radialTermY * sample.observedY);
        }

        const double determinant = aa * bb - ab * ab;
        if(!(aa > 0.0) || !(bb > 0.0) || determinant <= 1e-10 * aa * bb){
            result.status = SelfCalibrationStatus::IllConditioned;
            return result;
        }
        focal = (ay * bb - by * ab) / determinant;
        focalTimesK1 = (by * aa - ay * ab) / determinant;
    }

    const double k1 = focal != 0.0 ? focalTimesK1 / focal : std::numeric_limits<double>::infinity();
    const double imageWidth = double(std::max(initial.width, 1u));
    if(!std::isfinite(focal) || !std::isfinite(k1) ||
       focal < config.minimumFocalInImageWidths * imageWidth ||
       focal > config.maximumFocalInImageWidths * imageWidth ||
       std::fabs(k1) > config.maximumAbsoluteK1){
        result.status = SelfCalibrationStatus::OutOfRange;
        return result;
    }

    result.intrinsics.fx = float(focal);
    result.intrinsics.fy = float(focal);
    result.intrinsics.k1 = float(k1);
    result.intrinsics.k2 = 0.0f;
    result.endRms = rms(samples, focal, k1);
    result.status = SelfCalibrationStatus::Determined;
    result.determined = true;
    return result;
}

JointSelfCalibrationResult selfCalibrateBundle(const std::vector<Observation>& rawObservations,
                                               const std::vector<Pose>& poses,
                                               const std::vector<glm::vec3>& points,
                                               const Intrinsics& initial,
                                               const JointSelfCalibrationConfig& config){
    JointSelfCalibrationResult result;
    result.intrinsics = initial;
    result.poses = poses;
    result.points = points;

    const ViewGraphCalibrationResult graph = estimateViewGraphFocal(
        rawObservations, uint32_t(poses.size()), uint32_t(points.size()), initial, config.viewGraph);
    if(!graph.determined){
        result.status = graph.status == ViewGraphCalibrationStatus::InsufficientData
            ? SelfCalibrationStatus::InsufficientData : SelfCalibrationStatus::DegenerateGeometry;
        return result;
    }
    result.graphFocalPixels = graph.focalPixels;
    result.intrinsics.fx = float(graph.focalPixels);
    result.intrinsics.fy = float(graph.focalPixels);
    result.intrinsics.k1 = float(graph.k1);
    result.intrinsics.k2 = 0.0f;

    const SelfCalibrationResult beginning = refineSharedIntrinsics(
        rawObservations, result.poses, result.points, result.intrinsics, config.intrinsics);
    result.startRms = beginning.startRms;

    for(uint32_t iteration = 0; iteration < config.maxIterations; ++iteration){
        std::vector<Observation> flatObservations = rawObservations;
        for(Observation& observation : flatObservations)
            observation.pixel = undistort(result.intrinsics, observation.pixel);
        Intrinsics flat = result.intrinsics;
        flat.k1 = 0.0f;
        flat.k2 = 0.0f;
        const BundleResult bundle = bundleAdjust(flatObservations, result.poses, result.points,
                                                  flat, config.bundle);
        if(!bundle.solved){
            result.status = SelfCalibrationStatus::IllConditioned;
            return result;
        }
        result.poses = bundle.poses;
        result.points = bundle.points;

        const SelfCalibrationResult calibration = refineSharedIntrinsics(
            rawObservations, result.poses, result.points, result.intrinsics, config.intrinsics);
        if(!calibration.determined){
            result.status = calibration.status;
            return result;
        }
        const double previousFocal = result.intrinsics.fx;
        const double fraction = std::clamp(config.updateFraction, 0.0, 1.0);
        result.intrinsics.fx = float(previousFocal + fraction *
            (double(calibration.intrinsics.fx) - previousFocal));
        result.intrinsics.fy = result.intrinsics.fx;
        result.intrinsics.k1 = float(double(result.intrinsics.k1) + fraction *
            (double(calibration.intrinsics.k1) - double(result.intrinsics.k1)));
        result.iterations = iteration + 1;
        if(std::fabs(double(result.intrinsics.fx) - previousFocal) /
           std::max(std::fabs(previousFocal), 1.0) < config.focalConvergence) break;
    }

    std::vector<Observation> finalFlat = rawObservations;
    for(Observation& observation : finalFlat)
        observation.pixel = undistort(result.intrinsics, observation.pixel);
    Intrinsics finalIntrinsics = result.intrinsics;
    finalIntrinsics.k1 = 0.0f;
    finalIntrinsics.k2 = 0.0f;
    const BundleResult finalBundle = bundleAdjust(finalFlat, result.poses, result.points,
                                                   finalIntrinsics, config.bundle);
    if(!finalBundle.solved){
        result.status = SelfCalibrationStatus::IllConditioned;
        return result;
    }
    result.poses = finalBundle.poses;
    result.points = finalBundle.points;
    const SelfCalibrationResult finalMeasurement = refineSharedIntrinsics(
        rawObservations, result.poses, result.points, result.intrinsics, config.intrinsics);
    result.endRms = finalMeasurement.startRms;
    result.status = SelfCalibrationStatus::Determined;
    result.determined = true;
    return result;
}

SelfCalibratedReconstruction reconstructSelfCalibrated(
    const std::vector<Observation>& rawObservations,
    uint32_t cameraCount,
    uint32_t pointCount,
    const Intrinsics& templateCamera,
    const ReconstructConfig& reconstructConfig,
    const JointSelfCalibrationConfig& calibrationConfig){
    SelfCalibratedReconstruction result;
    result.measuredIntrinsics = templateCamera;
    result.flatIntrinsics = templateCamera;
    using Clock = std::chrono::steady_clock;
    auto since = [](Clock::time_point from){ return std::chrono::duration<double>(Clock::now() - from).count(); };
    auto started = Clock::now();

    const ViewGraphCalibrationResult graph = estimateViewGraphFocal(
        rawObservations, cameraCount, pointCount, templateCamera, calibrationConfig.viewGraph);
    result.graphSeconds = since(started);
    result.graphStatus = graph.status;
    result.graphFocalPixels = graph.focalPixels;
    if(!graph.determined){
        result.status = graph.status == ViewGraphCalibrationStatus::InsufficientData
            ? SelfCalibrationStatus::InsufficientData : SelfCalibrationStatus::DegenerateGeometry;
        return result;
    }

    result.measuredIntrinsics.fx = result.measuredIntrinsics.fy = float(graph.focalPixels);
    result.measuredIntrinsics.k1 = float(graph.k1);
    result.measuredIntrinsics.k2 = 0.0f;
    result.flatIntrinsics = result.measuredIntrinsics;
    result.flatIntrinsics.k1 = result.flatIntrinsics.k2 = 0.0f;
    result.flatObservations = rawObservations;
    for(Observation& observation : result.flatObservations)
        observation.pixel = undistort(result.measuredIntrinsics, observation.pixel);

    started = Clock::now();
    result.reconstruction = reconstruct(result.flatObservations, cameraCount, pointCount,
                                        result.flatIntrinsics, reconstructConfig);
    result.reconstructSeconds = since(started);
    started = Clock::now();
    if(!result.reconstruction.ok){
        result.status = SelfCalibrationStatus::IllConditioned;
        return result;
    }

    //Kalibraciju ne smiju voditi promasaji koje je rekonstrukcija vec odbacila. Indeksi kamera i
    //tocaka ostaju izvorni, pa rezultat i dalje moze izravno natrag u Reconstruction.
    std::vector<Observation> usedRaw;
    usedRaw.reserve(result.reconstruction.usedObservations);
    for(size_t index = 0; index < rawObservations.size(); ++index){
        if(index < result.reconstruction.observationUsed.size() &&
           result.reconstruction.observationUsed[index]) usedRaw.push_back(rawObservations[index]);
    }
    const JointSelfCalibrationResult refined = selfCalibrateBundle(
        usedRaw, result.reconstruction.poses, result.reconstruction.points,
        result.measuredIntrinsics, calibrationConfig);
    result.bundleSeconds = since(started);
    result.status = refined.status;
    if(!refined.determined) return result;

    result.measuredIntrinsics = refined.intrinsics;
    result.flatIntrinsics = refined.intrinsics;
    result.flatIntrinsics.k1 = result.flatIntrinsics.k2 = 0.0f;
    result.flatObservations = rawObservations;
    for(Observation& observation : result.flatObservations)
        observation.pixel = undistort(result.measuredIntrinsics, observation.pixel);
    result.reconstruction.poses = refined.poses;
    result.reconstruction.points = refined.points;

    //Broj kamera/tocaka i maske se zajednickim bundleom ne mijenjaju, ali SVE prijavljene mjere
    //moraju opisivati nove poze, tocke i konacnu kalibraciju - ukljucujuci izdvojena opazanja.
    refreshReconstructionDiagnostics(result.flatObservations, result.flatIntrinsics,
                                     reconstructConfig, result.reconstruction);
    result.determined = true;
    return result;
}

}
