// RollingShutterProbe - koliko rolling shutter kvari rjesenje prave snimke, i koliko traje citanje.
//
//   ./RollingShutterProbe ~/Desktop/loomTestClips/C0257_loom            <- od -1.2 do 1.2 kadra
//   ./RollingShutterProbe mapa_loom 0.0 0.4 0.8                           <- samo zadane vrijednosti
//
// Ne solva iznova. Uzme gotovo rjesenje (COLMAP tekst) i kameru po kadru iz kamera.usda, pa za
// svako zadano vrijeme citanja pusti zavrsni bundle s modelom rolling shuttera (Bundle.h). Mjeri:
//
//   ostatak       medijan reprojekcije na opazanjima iz kojih se racuna
//   izdvojeni     medijan na svakom osmom opazanju, koje bundle NE vidi, i omjer prema gornjem.
//                 Model koji samo prenauci smanji prvi broj, a ovaj ne
//   polje         staticni i promjenjivi dio polja ostataka (ResidualField.h). Rolling shutter je
//                 promjenjiv - ovisi o gibanju - pa bi pravo vrijeme citanja trebalo srusiti bas njega
//
// Vrijeme citanja je u KADROVIMA: 1.0 znaci da citanje traje cijeli razmak izmedju kadrova, a
// negativno da senzor cita odozdo prema gore.
#include <Engine/Bundle.h>
#include <Engine/ColmapExport.h>
#include <Engine/ColmapImport.h>
#include <Engine/Dense.h>
#include <Engine/Reconstruct.h>
#include <Engine/ResidualField.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace{

using namespace Engine;

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

bool residualOf(const Pose& pose, const Intrinsics& k, const glm::vec3& point, const glm::vec2& observed, glm::vec2& r){
    glm::vec2 pixel;
    const glm::vec3 inCamera = glm::conjugate(pose.orientation) * (point - pose.position);
    if(inCamera.z >= 0.0f) return false;
    pixel.x = k.cx + k.fx * inCamera.x / -inCamera.z;
    pixel.y = k.cy - k.fy * inCamera.y / -inCamera.z;
    r = pixel - observed;
    return true;
}

double median(std::vector<double> v){
    if(v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

}

int main(int argc, char** argv){
    if(argc < 2){
        std::printf("Usage: RollingShutterProbe <result folder> [readout in frames ...]\n");
        return 1;
    }
    const std::string directory = argv[1];
    ColmapModel model;
    if(!readColmapText(directory, model)){ std::printf("Cannot read the COLMAP model in %s\n", directory.c_str()); return 1; }
    const Intrinsics k = model.intrinsics;
    std::vector<Pose> poses = model.reconstruction.poses;
    const std::vector<glm::vec3> points = model.reconstruction.points;
    std::printf("Model: %zu cameras, %zu points, %zu observations, %ux%u, f %.1f\n", poses.size(), points.size(),
                model.observations.size(), k.width, k.height, double(k.fx));

    //Koji kadar snimke je koja kamera: najblizi polozaj u kamera.usda (ondje je kamera za SVAKI kadar)
    const std::map<int, Pose> dense = readUsdCameras(directory + "/kamera.usda");
    if(dense.size() < 3){ std::printf("kamera.usda has no per-frame camera\n"); return 1; }
    std::vector<double> times(poses.size(), 0.0);
    double worstMatch = 0.0, worstAngle = 0.0;
    for(size_t c = 0; c < poses.size(); ++c){
        double best = 1e30;
        for(const auto& [frame, pose] : dense){
            const double d = glm::length(pose.position - poses[c].position);
            if(d < best){ best = d; times[c] = double(frame); }
        }
        const Pose& matched = dense.at(int(times[c]));
        worstMatch = std::max(worstMatch, best);
        worstAngle = std::max(worstAngle, double(glm::degrees(glm::angle(glm::normalize(glm::conjugate(matched.orientation) * poses[c].orientation)))));
    }
    std::printf("Keyframes matched to %zu video frames: worst position %.2e, worst angle %.4f deg\n", dense.size(), worstMatch, worstAngle);

    //Brzine iz SUSJEDNIH VIDEO KADROVA (razmak 1), ne iz susjednih kljucnih - ondje je 10 kadrova
    //izmedju, a trzaj ruke je brzi od toga
    std::vector<glm::vec3> linear(poses.size()), angular(poses.size());
    for(size_t c = 0; c < poses.size(); ++c){
        const int f = int(times[c]);
        std::vector<Pose> around;
        std::vector<double> at;
        for(int d = -1; d <= 1; ++d){
            auto found = dense.find(f + d);
            if(found != dense.end()){ around.push_back(found->second); at.push_back(double(f + d)); }
        }
        std::vector<glm::vec3> l, a;
        rollingShutterVelocities(around, at, l, a);
        //Srednji clan je kamera sama; njezina poza u kamera.usda je ista kao ova (vidi gore)
        const size_t middle = around.size() == 3 ? 1 : 0;
        linear[c] = l.empty() ? glm::vec3(0.0f) : l[middle];
        angular[c] = a.empty() ? glm::vec3(0.0f) : a[middle];
    }
    std::vector<double> speeds;
    for(const glm::vec3& w : angular) speeds.push_back(glm::degrees(double(glm::length(w))));
    std::printf("Angular speed per frame: median %.3f deg, max %.3f deg\n", median(speeds),
                *std::max_element(speeds.begin(), speeds.end()));

    //Svako osmo opazanje ide u provjeru, bundle ga ne vidi
    std::vector<Observation> used, held;
    for(size_t i = 0; i < model.observations.size(); ++i) (i % 8 == 3 ? held : used).push_back(model.observations[i]);

    //Izbori mjerenja iz okoline: ROLLING_ITERATIONS, ROLLING_VELOCITY=keyframes (brzine iz
    //susjednih kljucnih kadrova umjesto video kadrova), ROLLING_SMOOTH=N (prosjek brzine preko +-N kadrova)
    const uint32_t iterations = std::getenv("ROLLING_ITERATIONS") ? uint32_t(std::atoi(std::getenv("ROLLING_ITERATIONS"))) : 20u;
    if(std::getenv("ROLLING_VELOCITY") && std::string(std::getenv("ROLLING_VELOCITY")) == "keyframes"){
        rollingShutterVelocities(poses, times, linear, angular);
        std::printf("Velocities from neighbouring KEYFRAMES\n");
    }else if(std::getenv("ROLLING_SMOOTH")){
        const int reach = std::atoi(std::getenv("ROLLING_SMOOTH"));
        for(size_t c = 0; c < poses.size(); ++c){
            const int f = int(times[c]);
            auto lo = dense.find(f - reach), hi = dense.find(f + reach);
            if(lo == dense.end() || hi == dense.end()) continue;
            std::vector<Pose> around{lo->second, dense.at(f), hi->second};
            std::vector<double> at{double(f - reach), double(f), double(f + reach)};
            std::vector<glm::vec3> l, a;
            rollingShutterVelocities(around, at, l, a);
            linear[c] = l[1]; angular[c] = a[1];
        }
        std::printf("Velocities over +-%d video frames\n", reach);
    }

    std::vector<double> readouts;
    for(int i = 2; i < argc; ++i) readouts.push_back(std::atof(argv[i]));
    if(readouts.empty()) for(int i = -12; i <= 12; i += 2) readouts.push_back(0.1 * i);

    //ROLLING_WRITE=mapa: zavrsni bundle na SVIM opazanjima uz prvo zadano vrijeme citanja, pa se
    //model zapise za trening - sredina kadra u mapi, gornji i donji redak u mapa/rs_top i
    //mapa/rs_bottom (isti COLMAP oblik; trener iz njih uzme pocetak i kraj citanja)
    if(const char* target = std::getenv("ROLLING_WRITE")){
        const double readout = readouts.front();
        BundleConfig config;
        config.huberPixels = 2.0;
        config.maxIterations = iterations;
        config.rowTime = readout / double(k.height);
        config.linearVelocity = linear;
        config.angularVelocity = angular;
        const BundleResult result = bundleAdjust(model.observations, poses, points, k, config);
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
        if(colours.size() != points.size()) colours.clear();
        auto write = [&](const std::string& out, float row){
            Reconstruction rec = model.reconstruction;
            rec.points = result.points;
            for(size_t c = 0; c < rec.poses.size(); ++c) rec.poses[c] = rollingShutterPose(result.poses[c], config, c, row, k.cy);
            std::filesystem::create_directories(out);
            return writeColmapText(out, rec, k, model.observations, model.imageNames, colours);
        };
        const std::string out = target;
        const bool ok = write(out, k.cy) && write(out + "/rs_top", 0.0f) && write(out + "/rs_bottom", float(k.height));
        std::error_code error;
        std::filesystem::create_directory_symlink(std::filesystem::absolute(directory + "/images"), out + "/images", error);
        std::printf("Wrote %s (readout %.2f frames, residual %.3f -> %.3f px): %s\n", out.c_str(), readout,
                    result.startMedian, result.endMedian, ok ? "ok" : "FAILED");
        return ok ? 0 : 1;
    }

    std::printf("\n readout  residual  held-out  ratio   field static  field changing  seconds\n");
    for(double readout : readouts){
        const auto started = std::chrono::steady_clock::now();
        BundleConfig config;
        config.huberPixels = 2.0;
        config.maxIterations = iterations;
        config.rowTime = readout / double(k.height);
        config.linearVelocity = linear;
        config.angularVelocity = angular;
        const BundleResult result = bundleAdjust(used, poses, points, k, config);

        //Ostaci u modelu, i "virtualna" opazanja za polje: polje racuna s pozom kadra, pa mu se
        //preda opazanje pomaknuto tako da pozom kadra dobije bas ostatak modela
        std::vector<double> usedLengths, heldLengths;
        std::vector<Observation> virtualObservations;
        for(const Observation& o : used){
            const Pose rowPose = rollingShutterPose(result.poses[o.camera], config, o.camera, o.pixel.y, k.cy);
            glm::vec2 r, frameR;
            if(!residualOf(rowPose, k, result.points[o.point], o.pixel, r)) continue;
            usedLengths.push_back(glm::length(r));
            if(!residualOf(result.poses[o.camera], k, result.points[o.point], o.pixel, frameR)) continue;
            Observation v = o;
            v.pixel = o.pixel + (frameR - r);
            virtualObservations.push_back(v);
        }
        for(const Observation& o : held){
            const Pose rowPose = rollingShutterPose(result.poses[o.camera], config, o.camera, o.pixel.y, k.cy);
            glm::vec2 r;
            if(residualOf(rowPose, k, result.points[o.point], o.pixel, r)) heldLengths.push_back(glm::length(r));
        }
        const ResidualFieldResult field = analyzeResidualField(virtualObservations, result.poses, result.points, k);

        //ROLLING_EXPLAIN: sto objasnjava ostatak po kadru. Za svaki kadar se ostaci (bez Huberovih
        //promasaja, do 8 px) prilagode najmanjim kvadratima redom na sve sire modele, i mjeri se
        //koliko varijance svaki objasni. Koordinate su normirane (x - cx)/f, (y - cy)/f
        if(std::getenv("ROLLING_EXPLAIN")){
            struct Sample{ double x, y, rx, ry; };
            std::vector<std::vector<Sample>> perFrame(result.poses.size());
            for(const Observation& o : used){
                const Pose rowPose = rollingShutterPose(result.poses[o.camera], config, o.camera, o.pixel.y, k.cy);
                glm::vec2 r;
                if(!residualOf(rowPose, k, result.points[o.point], o.pixel, r) || glm::length(r) > 8.0f) continue;
                perFrame[o.camera].push_back({(o.pixel.x - k.cx) / k.fx, (o.pixel.y - k.cy) / k.fy, r.x, r.y});
            }
            //Baze: svaka je par funkcija (za rx, ry) koordinata
            struct Model{ const char* name; std::vector<int> terms; };
            //Clanovi: 0 pomak x, 1 pomak y, 2 mjerilo (x,y), 3 zakret (-y,x), 4 smik (y,0), 5 razvlacenje (0,y),
            //6 radijalno r^2 (x,y), 7 afino ostatak (x,0), 8 (0,x)
            auto term = [](int t, double x, double y, double& a, double& b){
                const double r2 = x * x + y * y;
                switch(t){
                    case 0: a = 1; b = 0; break;          case 1: a = 0; b = 1; break;
                    case 2: a = x; b = y; break;          case 3: a = -y; b = x; break;
                    case 4: a = y; b = 0; break;          case 5: a = 0; b = y; break;
                    case 6: a = x * r2; b = y * r2; break; case 7: a = x; b = 0; break;
                    default: a = 0; b = x; break;
                }
            };
            const std::vector<Model> models = {
                {"shift only", {0, 1}},
                {"+ focal (scale)", {0, 1, 2}},
                {"+ roll", {0, 1, 2, 3}},
                {"+ rolling shutter shear/stretch", {0, 1, 2, 3, 4, 5}},
                {"+ radial r^2", {0, 1, 2, 3, 4, 5, 6}},
                {"full affine + radial", {0, 1, 2, 3, 4, 5, 6, 7, 8}},
            };
            std::vector<double> total(models.size(), 0.0);
            double variance = 0.0;
            std::vector<std::pair<double, double>> speedVsRms;
            for(size_t c = 0; c < perFrame.size(); ++c){
                const auto& samples = perFrame[c];
                if(samples.size() < 50) continue;
                double frameVariance = 0.0;
                for(const Sample& s : samples) frameVariance += s.rx * s.rx + s.ry * s.ry;
                variance += frameVariance;
                speedVsRms.push_back({glm::degrees(double(glm::length(angular[c]))), std::sqrt(frameVariance / double(samples.size()))});
                for(size_t m = 0; m < models.size(); ++m){
                    const auto& terms = models[m].terms;
                    const int n = int(terms.size());
                    std::vector<double> A(size_t(n * n), 0.0), g(size_t(n), 0.0);
                    std::vector<double> ax(static_cast<size_t>(n), 0.0), by(static_cast<size_t>(n), 0.0);
                    for(const Sample& s : samples){
                        for(int i = 0; i < n; ++i) term(terms[size_t(i)], s.x, s.y, ax[size_t(i)], by[size_t(i)]);
                        for(int i = 0; i < n; ++i){
                            g[size_t(i)] += ax[size_t(i)] * s.rx + by[size_t(i)] * s.ry;
                            for(int j = 0; j < n; ++j) A[size_t(i * n + j)] += ax[size_t(i)] * ax[size_t(j)] + by[size_t(i)] * by[size_t(j)];
                        }
                    }
                    for(int i = 0; i < n; ++i) A[size_t(i * n + i)] += 1e-12;
                    std::vector<double> solution;
                    if(!solveDense(A, g, n, solution)) continue;
                    double left = 0.0;
                    for(const Sample& s : samples){
                        double px = 0.0, py = 0.0;
                        for(int i = 0; i < n; ++i){
                            double a, b; term(terms[size_t(i)], s.x, s.y, a, b);
                            px += solution[size_t(i)] * a; py += solution[size_t(i)] * b;
                        }
                        left += (s.rx - px) * (s.rx - px) + (s.ry - py) * (s.ry - py);
                    }
                    total[m] += left;
                }
            }
            std::printf("   residual variance explained per frame (inliers < 8 px):\n");
            for(size_t m = 0; m < models.size(); ++m){
                std::printf("     %-34s left %.1f %%  (rms %.3f px)\n", models[m].name, 100.0 * total[m] / variance,
                            std::sqrt(total[m] / std::max(1.0, variance) * variance / double(used.size())));
            }
            //Veza s brzinom: korelacija rms kadra i kutne brzine
            double mx = 0, my = 0;
            for(auto& p : speedVsRms){ mx += p.first; my += p.second; }
            mx /= double(speedVsRms.size()); my /= double(speedVsRms.size());
            double sxy = 0, sxx = 0, syy = 0;
            for(auto& p : speedVsRms){ sxy += (p.first - mx) * (p.second - my); sxx += (p.first - mx) * (p.first - mx); syy += (p.second - my) * (p.second - my); }
            std::printf("     frame rms vs angular speed: correlation %.2f\n", sxy / std::sqrt(sxx * syy + 1e-30));
            std::sort(speedVsRms.begin(), speedVsRms.end(), [](auto& a, auto& b){ return a.second > b.second; });
            std::printf("     worst frames (speed deg/frame, rms px):");
            for(size_t i = 0; i < std::min<size_t>(6, speedVsRms.size()); ++i) std::printf(" (%.2f, %.2f)", speedVsRms[i].first, speedVsRms[i].second);
            std::printf("\n");
        }
        const double usedMedian = median(usedLengths), heldMedian = median(heldLengths);
        std::printf(" %6.2f   %7.4f   %7.4f   %5.3f   %9.4f     %9.4f     %6.1f\n", readout, usedMedian, heldMedian,
                    usedMedian > 0.0 ? heldMedian / usedMedian : 0.0, field.staticRms, field.temporalRms,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        std::fflush(stdout);
    }
    return 0;
}
