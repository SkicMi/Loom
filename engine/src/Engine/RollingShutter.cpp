#include "Engine/RollingShutter.h"
#include "Engine/ColmapExport.h"
#include "Engine/ColmapImport.h"
#include "Engine/Reconstruct.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <algorithm>
#include <cmath>
#include <map>

namespace Engine{
namespace{

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::nth_element(values.begin(), values.begin() + long(values.size() / 2), values.end());
    return values[values.size() / 2];
}

//Medijan ostatka izdvojenih opazanja u pozi njihova retka
double heldOutMedian(const std::vector<Observation>& held, const BundleResult& result,
                     const BundleConfig& config, const Intrinsics& k){
    std::vector<double> lengths;
    lengths.reserve(held.size());
    for(const Observation& o : held){
        if(o.camera >= result.poses.size() || o.point >= result.points.size()) continue;
        const Pose pose = rollingShutterPose(result.poses[o.camera], config, o.camera, o.pixel.y, k.cy);
        const glm::vec3 inCamera = glm::conjugate(pose.orientation) * (result.points[o.point] - pose.position);
        if(inCamera.z >= 0.0f) continue;
        const glm::vec2 pixel(k.cx + k.fx * inCamera.x / -inCamera.z, k.cy - k.fy * inCamera.y / -inCamera.z);
        lengths.push_back(double(glm::length(pixel - o.pixel)));
    }
    return medianOf(lengths);
}

}

RollingShutterResult estimateRollingShutter(const std::vector<Observation>& observations,
                                            const std::vector<Pose>& poses,
                                            const std::vector<glm::vec3>& points,
                                            const Intrinsics& intrinsics,
                                            const std::vector<glm::vec3>& linear,
                                            const std::vector<glm::vec3>& angular,
                                            const RollingShutterConfig& config){
    RollingShutterResult out;
    if(observations.empty() || poses.empty() || intrinsics.height == 0) return out;

    std::vector<Observation> used, held;
    for(size_t i = 0; i < observations.size(); ++i){
        (config.holdEvery > 1 && i % config.holdEvery == config.holdEvery / 2 ? held : used).push_back(observations[i]);
    }

    auto configFor = [&](double readout){
        BundleConfig bundle;
        bundle.huberPixels = config.huberPixels;
        bundle.maxIterations = config.iterations;
        bundle.rowTime = readout / double(intrinsics.height);
        bundle.linearVelocity = linear;
        bundle.angularVelocity = angular;
        return bundle;
    };
    std::map<double, double> score;
    auto measure = [&](double readout){
        const double key = std::round(readout * 1000.0) / 1000.0;
        auto found = score.find(key);
        if(found != score.end()) return found->second;
        const BundleConfig bundle = configFor(key);
        const BundleResult result = bundleAdjust(used, poses, points, intrinsics, bundle);
        ++out.bundles;
        return score[key] = heldOutMedian(held, result, bundle, intrinsics);
    };

    out.heldOutGlobal = measure(0.0);
    double best = 0.0, bestScore = out.heldOutGlobal;
    for(double readout = config.coarseFrom; readout <= config.coarseTo + 1e-9; readout += config.coarseStep){
        const double s = measure(readout);
        if(s < bestScore){ bestScore = s; best = readout; }
    }
    //Fino: pola grubog koraka na svaku stranu
    const double around = best;
    for(double readout = around - 0.5 * config.coarseStep; readout <= around + 0.5 * config.coarseStep + 1e-9; readout += config.fineStep){
        const double s = measure(readout);
        if(s < bestScore){ bestScore = s; best = readout; }
    }
    out.heldOutBest = bestScore;

    if(out.heldOutGlobal <= 0.0 || bestScore > out.heldOutGlobal * (1.0 - config.minimumGain) || best == 0.0) return out;

    out.used = true;
    out.readout = std::round(best * 1000.0) / 1000.0;
    const BundleConfig bundle = configFor(out.readout);
    const BundleResult final = bundleAdjust(observations, poses, points, intrinsics, bundle);
    ++out.bundles;
    out.points = final.points;
    out.centre = final.poses;
    out.top.resize(final.poses.size());
    out.bottom.resize(final.poses.size());
    for(size_t c = 0; c < final.poses.size(); ++c){
        out.top[c] = rollingShutterPose(final.poses[c], bundle, c, 0.0f, intrinsics.cy);
        out.bottom[c] = rollingShutterPose(final.poses[c], bundle, c, float(intrinsics.height), intrinsics.cy);
    }
    return out;
}

namespace{

//Kamera po kadru iz kamera.usda: "N: ( (xx), (yy), (zz), (tx, ty, tz, 1) )", matrica po retcima
std::map<int, Pose> readUsdCameras(const std::string& path){
    std::map<int, Pose> out;
    std::ifstream file(path);
    std::string line;
    bool inside = false;
    while(std::getline(file, line)){
        if(line.find("xformOp:transform.timeSamples") != std::string::npos){ inside = true; continue; }
        if(!inside) continue;
        if(line.find('}') != std::string::npos && line.find(':') == std::string::npos) break;
        int frame = 0;
        double m[16];
        if(std::sscanf(line.c_str(), " %d: ( (%lf, %lf, %lf, %lf), (%lf, %lf, %lf, %lf), (%lf, %lf, %lf, %lf), (%lf, %lf, %lf, %lf) )",
                       &frame, &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &m[6], &m[7], &m[8], &m[9], &m[10], &m[11],
                       &m[12], &m[13], &m[14], &m[15]) != 17) continue;
        glm::mat3 rotation;
        for(int row = 0; row < 3; ++row) for(int column = 0; column < 3; ++column) rotation[row][column] = float(m[row * 4 + column]);
        Pose pose;
        pose.orientation = glm::normalize(glm::quat_cast(rotation));
        pose.position = glm::vec3(float(m[12]), float(m[13]), float(m[14]));
        out[frame] = pose;
    }
    return out;
}

}

bool rollingShutterForResult(const std::string& directory, RollingShutterResult& result, std::string& report,
                             const RollingShutterConfig& config){
    namespace fs = std::filesystem;
    result = RollingShutterResult{};
    std::error_code ignored;
    fs::remove_all(fs::path(directory) / "rs_top", ignored);
    fs::remove_all(fs::path(directory) / "rs_bottom", ignored);
    fs::remove(fs::path(directory) / "rolling_shutter.txt", ignored);

    ColmapModel model;
    if(!readColmapText(directory, model)){ report = "cannot read the COLMAP model"; return false; }
    const std::map<int, Pose> dense = readUsdCameras(directory + "/kamera.usda");
    if(dense.size() < 3){ report = "kamera.usda has no per-frame camera"; return false; }

    //Kljucni kadar = kadar iz kamera.usda s istim polozajem (ondje je kamera za svaki kadar)
    const std::vector<Pose>& poses = model.reconstruction.poses;
    std::vector<glm::vec3> linear(poses.size(), glm::vec3(0.0f)), angular(poses.size(), glm::vec3(0.0f));
    double worst = 0.0;
    for(size_t c = 0; c < poses.size(); ++c){
        double best = 1e30;
        int frame = 0;
        for(const auto& [f, pose] : dense){
            const double d = double(glm::length(pose.position - poses[c].position));
            if(d < best){ best = d; frame = f; }
        }
        worst = std::max(worst, best);
        auto before = dense.find(frame - 1), after = dense.find(frame + 1);
        if(before == dense.end() || after == dense.end()) continue;
        std::vector<glm::vec3> l, a;
        rollingShutterVelocities({before->second, poses[c], after->second},
                                 {double(frame - 1), double(frame), double(frame + 1)}, l, a);
        linear[c] = l[1];
        angular[c] = a[1];
    }

    result = estimateRollingShutter(model.observations, poses, model.reconstruction.points, model.intrinsics,
                                    linear, angular, config);
    char text[256];
    std::snprintf(text, sizeof(text), "readout %.2f frames, held-out %.3f -> %.3f px (%u bundles)%s",
                  result.readout, result.heldOutGlobal, result.heldOutBest, result.bundles,
                  result.used ? "" : " - not enough gain, not used");
    report = text;
    if(!result.used) return true;

    std::vector<glm::u8vec3> colours;
    {
        std::ifstream file(directory + "/points3D.txt");
        std::string line;
        while(std::getline(file, line)){
            if(line.empty() || line[0] == '#') continue;
            long long id; double x, y, z; int r, g, b;
            if(std::sscanf(line.c_str(), "%lld %lf %lf %lf %d %d %d", &id, &x, &y, &z, &r, &g, &b) == 7) colours.push_back(glm::u8vec3(r, g, b));
        }
    }
    if(colours.size() != result.points.size()) colours.clear();
    bool written = true;
    for(const auto& [name, rows] : {std::make_pair("rs_top", &result.top), std::make_pair("rs_bottom", &result.bottom)}){
        Reconstruction reconstruction = model.reconstruction;
        reconstruction.poses = *rows;
        reconstruction.points = result.points;
        const std::string out = (fs::path(directory) / name).string();
        fs::create_directories(out, ignored);
        written = written && writeColmapText(out, reconstruction, model.intrinsics, model.observations, model.imageNames, colours);
    }
    std::ofstream note(fs::path(directory) / "rolling_shutter.txt");
    note << "readout_frames " << result.readout << "\nheld_out_global_px " << result.heldOutGlobal
         << "\nheld_out_rolling_px " << result.heldOutBest << "\n";
    if(!written) report += "; writing rs_top/rs_bottom FAILED";
    return written;
}

}
