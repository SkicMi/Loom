#include "TestHarness.h"

#include <Engine/SelfCalibration.h>
#include <Engine/SyntheticScene.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace{

double relativeError(double got, double truth){
    return std::fabs(got - truth) / truth;
}

void disturbGeometry(std::vector<Engine::Pose>& poses, std::vector<glm::vec3>& points){
    for(size_t i = 1; i < poses.size(); ++i){
        const float sign = i % 2 == 0 ? 1.0f : -1.0f;
        poses[i].position += poses[i].orientation * glm::vec3(0.025f * sign, -0.015f, 0.02f);
        poses[i].orientation = glm::normalize(poses[i].orientation *
            glm::angleAxis(glm::radians(0.4f * sign), glm::normalize(glm::vec3(0.2f, 1.0f, -0.1f))));
    }
    for(size_t i = 0; i < points.size(); ++i){
        const float x = float((i * 37) % 17) / 17.0f - 0.5f;
        const float y = float((i * 53) % 19) / 19.0f - 0.5f;
        const float z = float((i * 71) % 23) / 23.0f - 0.5f;
        points[i] += 0.025f * glm::vec3(x, y, z);
    }
}

}

int main(){
    TestReport report("generic shared camera self-calibration");

    Engine::SyntheticConfig config;
    config.pointCount = 500;
    config.cameraCount = 10;
    config.noisePixels = 0.35f;
    config.intrinsics.fx = 920.0f;
    config.intrinsics.fy = 920.0f;
    config.intrinsics.cx = 640.0f;
    config.intrinsics.cy = 360.0f;
    config.intrinsics.width = 1280;
    config.intrinsics.height = 720;
    const Engine::SyntheticScene pinhole = Engine::makeSyntheticScene(config);

    {
        std::vector<Engine::Pose> calibrationPoses = pinhole.poses;
        for(size_t i = 0; i < calibrationPoses.size(); ++i){
            const float offset = glm::radians(-2.25f + 0.5f * float(i));
            calibrationPoses[i].orientation = glm::normalize(calibrationPoses[i].orientation *
                glm::angleAxis(offset, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        std::vector<Engine::Observation> calibrationObservations;
        for(uint32_t camera = 0; camera < calibrationPoses.size(); ++camera){
            for(uint32_t point = 0; point < pinhole.points.size(); ++point){
                glm::vec2 pixel;
                if(!Engine::project(calibrationPoses[camera], pinhole.intrinsics, pinhole.points[point], pixel)) continue;
                const float noiseX = 0.2f * (float((point * 17 + camera * 13) % 11) / 5.0f - 1.0f);
                const float noiseY = 0.2f * (float((point * 23 + camera * 7) % 13) / 6.0f - 1.0f);
                calibrationObservations.push_back({camera, point, pixel + glm::vec2(noiseX, noiseY)});
            }
        }
        Engine::ViewGraphCalibrationConfig graphConfig;
        const Engine::ViewGraphCalibrationResult result = Engine::estimateViewGraphFocal(
            calibrationObservations, uint32_t(calibrationPoses.size()), uint32_t(pinhole.points.size()),
            pinhole.intrinsics, graphConfig);

        report.check("view graph nalazi zariste samo iz 2D veza",
            result.determined && relativeError(result.focalPixels, pinhole.intrinsics.fx) < 0.025 &&
                std::fabs(result.k1) < 0.01 && result.usedPairs >= 5,
            fmt("f %.2f/%.2f px, k1 %.4f, %u parova, medijan %.3f px",
                result.focalPixels, pinhole.intrinsics.fx, result.k1, result.usedPairs, result.medianErrorPixels));
    }

    {
        const Engine::ViewGraphCalibrationResult result = Engine::estimateViewGraphFocal(
            pinhole.observations, uint32_t(pinhole.poses.size()), uint32_t(pinhole.points.size()),
            pinhole.intrinsics);
        report.check("kriticni look-at luk ne izmisli zariste",
            !result.determined && result.status == Engine::ViewGraphCalibrationStatus::DegenerateGeometry,
            fmt("status %s, %u upotrebljivih parova", Engine::viewGraphCalibrationStatusName(result.status),
                result.usedPairs));
    }

    {
        Engine::Intrinsics unknown = pinhole.intrinsics;
        unknown.fx = unknown.fy = 0.5f * float(unknown.width);
        const Engine::SelfCalibratedReconstruction result = Engine::reconstructSelfCalibrated(
            pinhole.observations, uint32_t(pinhole.poses.size()), uint32_t(pinhole.points.size()),
            unknown);
        report.check("cijeli put ne izvozi broj iz kriticnog luka",
            !result.determined &&
                result.graphStatus == Engine::ViewGraphCalibrationStatus::DegenerateGeometry,
            fmt("status %s, view-graph %s", Engine::selfCalibrationStatusName(result.status),
                Engine::viewGraphCalibrationStatusName(result.graphStatus)));
    }

    {
        std::vector<Engine::Pose> rotationPoses = pinhole.poses;
        for(Engine::Pose& pose : rotationPoses) pose.position = rotationPoses.front().position;
        std::vector<Engine::Observation> rotationObservations;
        for(uint32_t camera = 0; camera < rotationPoses.size(); ++camera){
            for(uint32_t point = 0; point < pinhole.points.size(); ++point){
                glm::vec2 pixel;
                if(Engine::project(rotationPoses[camera], pinhole.intrinsics, pinhole.points[point], pixel))
                    rotationObservations.push_back({camera, point, pixel});
            }
        }
        const Engine::ViewGraphCalibrationResult result = Engine::estimateViewGraphFocal(
            rotationObservations, uint32_t(rotationPoses.size()), uint32_t(pinhole.points.size()),
            pinhole.intrinsics);

        report.check("cista rotacija ne izmisli zariste",
            !result.determined && result.status == Engine::ViewGraphCalibrationStatus::DegenerateGeometry,
            fmt("status %s, %u upotrebljivih parova", Engine::viewGraphCalibrationStatusName(result.status),
                result.usedPairs));
    }

    {
        Engine::SyntheticConfig jointConfig = config;
        jointConfig.noisePixels = 0.2f;
        jointConfig.intrinsics.k1 = -0.045f;
        Engine::SyntheticScene scene = Engine::makeSyntheticScene(jointConfig);
        for(size_t i = 0; i < scene.poses.size(); ++i){
            const float offset = glm::radians(-2.25f + 0.5f * float(i));
            scene.poses[i].orientation = glm::normalize(scene.poses[i].orientation *
                glm::angleAxis(offset, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        scene.observations.clear();
        for(uint32_t camera = 0; camera < scene.poses.size(); ++camera){
            for(uint32_t point = 0; point < scene.points.size(); ++point){
                glm::vec2 pixel;
                if(Engine::project(scene.poses[camera], scene.intrinsics, scene.points[point], pixel))
                    scene.observations.push_back({camera, point, pixel});
            }
        }
        std::vector<Engine::Pose> poses = scene.poses;
        std::vector<glm::vec3> points = scene.points;
        disturbGeometry(poses, points);
        Engine::Intrinsics initial = scene.intrinsics;
        initial.fx *= 0.65f;
        initial.fy *= 0.65f;
        initial.k1 = 0.0f;

        const Engine::JointSelfCalibrationResult result = Engine::selfCalibrateBundle(
            scene.observations, poses, points, initial);
        report.check("view graph vodi zajednicki bundle do f i k1",
            result.determined && relativeError(result.intrinsics.fx, scene.intrinsics.fx) < 0.025 &&
                std::fabs(result.intrinsics.k1 - scene.intrinsics.k1) < 0.012 && result.endRms < 0.8,
            fmt("view-graph %.2f, f %.2f/%.2f px, k1 %.5f/%.5f, RMS %.3f -> %.3f px kroz %u koraka",
                result.graphFocalPixels, result.intrinsics.fx, scene.intrinsics.fx, result.intrinsics.k1, scene.intrinsics.k1,
                result.startRms, result.endRms, result.iterations));
    }

    {
        //Ovo je granica koju VideoSolve stvarno treba: nema poznatih poza ni tocaka. Ulaze samo
        //sirova, distorzirana 2D opazanja, a izlaz mora biti rekonstrukcija nad ravnim pikselima i
        //odvojeno izmjerena fizicka leca. Test namjerno ne poziva pojedine korake rucno, jer bi
        //tada mogao proci i dok ih VideoSolve spaja krivim redom.
        Engine::SyntheticConfig pipelineConfig = config;
        pipelineConfig.noisePixels = 0.15f;
        pipelineConfig.intrinsics.k1 = -0.045f;
        Engine::SyntheticScene scene = Engine::makeSyntheticScene(pipelineConfig);
        for(size_t i = 0; i < scene.poses.size(); ++i){
            const float offset = glm::radians(-2.25f + 0.5f * float(i));
            scene.poses[i].orientation = glm::normalize(scene.poses[i].orientation *
                glm::angleAxis(offset, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        scene.observations.clear();
        for(uint32_t camera = 0; camera < scene.poses.size(); ++camera){
            for(uint32_t point = 0; point < scene.points.size(); ++point){
                glm::vec2 pixel;
                if(Engine::project(scene.poses[camera], scene.intrinsics, scene.points[point], pixel))
                    scene.observations.push_back({camera, point, pixel});
            }
        }

        Engine::Intrinsics unknown = scene.intrinsics;
        unknown.fx = unknown.fy = 0.5f * float(unknown.width);
        unknown.k1 = 0.0f;
        Engine::ReconstructConfig reconstructConfig;
        reconstructConfig.acceptPixels = 4.0;
        reconstructConfig.minPointsForPose = 20;
        reconstructConfig.initialPairTrials = 2;
        reconstructConfig.holdOutEvery = 10;
        const Engine::SelfCalibratedReconstruction result = Engine::reconstructSelfCalibrated(
            scene.observations, uint32_t(scene.poses.size()), uint32_t(scene.points.size()),
            unknown, reconstructConfig);

        report.check("stvarni rekonstrukcijski put odredi fizicku lecu",
            result.determined && result.reconstruction.ok &&
                result.reconstruction.posedCameras == scene.poses.size() &&
                relativeError(result.measuredIntrinsics.fx, scene.intrinsics.fx) < 0.03 &&
                std::fabs(result.measuredIntrinsics.k1 - scene.intrinsics.k1) < 0.015 &&
                result.flatIntrinsics.k1 == 0.0f &&
                result.flatObservations.size() == scene.observations.size(),
            fmt("status %s, %u/%zu kamera, f %.2f/%.2f px, k1 %.5f/%.5f",
                Engine::selfCalibrationStatusName(result.status),
                result.reconstruction.posedCameras, scene.poses.size(),
                result.measuredIntrinsics.fx, scene.intrinsics.fx,
                result.measuredIntrinsics.k1, scene.intrinsics.k1));

        //Ne vjerujemo samo helperu koji je upravo dodan: test neovisno ponovno odabire svako
        //deseto opazanje tocno po pravilima rekonstrukcije i iz konacnih poza/tocaka racuna
        //ocekivani medijan. To hvata upravo regresiju zbog koje je stara held-out mjera ostajala
        //iz geometrije prije joint bundlea.
        std::vector<uint32_t> left(scene.points.size(), 0);
        for(const Engine::Observation& observation : result.flatObservations)
            if(observation.point < left.size()) ++left[observation.point];
        std::vector<double> heldOutErrors;
        for(size_t index = 0; index < result.flatObservations.size();
            index += reconstructConfig.holdOutEvery){
            const Engine::Observation& observation = result.flatObservations[index];
            if(observation.camera >= result.reconstruction.poses.size() ||
               observation.point >= result.reconstruction.points.size() ||
               left[observation.point] <= 3) continue;
            --left[observation.point];
            if(!result.reconstruction.posed[observation.camera] ||
               !result.reconstruction.solved[observation.point]) continue;
            glm::vec2 projected;
            if(Engine::project(result.reconstruction.poses[observation.camera],
                               result.flatIntrinsics,
                               result.reconstruction.points[observation.point], projected))
                heldOutErrors.push_back(double(glm::length(projected - observation.pixel)));
        }
        std::sort(heldOutErrors.begin(), heldOutErrors.end());
        const double heldOutMedian = heldOutErrors.empty()
            ? 0.0 : heldOutErrors[heldOutErrors.size() / 2];
        report.check("joint bundle osvjezi neovisnu held-out provjeru",
            result.determined && !heldOutErrors.empty() &&
                result.reconstruction.heldOutObservations == heldOutErrors.size() &&
                result.reconstruction.heldOutReprojection == heldOutMedian,
            fmt("%u/%zu opazanja, medijan %.9f/%.9f px",
                result.reconstruction.heldOutObservations, heldOutErrors.size(),
                result.reconstruction.heldOutReprojection, heldOutMedian));
    }

    {
        //Izlazna slika mora proci isti model kao opazanja. Crveni kanal kodira x, zeleni y, pa
        //vrijednost izlaznog piksela izravno govori s kojeg je mjesta sirove slike uzet.
        constexpr uint32_t width = 64, height = 48;
        std::vector<uint8_t> source(size_t(width) * height * 4, 255);
        for(uint32_t y = 0; y < height; ++y){
            for(uint32_t x = 0; x < width; ++x){
                uint8_t* pixel = source.data() + (size_t(y) * width + x) * 4;
                pixel[0] = uint8_t(x * 3);
                pixel[1] = uint8_t(y * 4);
                pixel[2] = 17;
            }
        }
        Engine::Intrinsics lens;
        lens.width = width; lens.height = height;
        lens.cx = 0.5f * float(width); lens.cy = 0.5f * float(height);
        lens.fx = lens.fy = 50.0f; lens.k1 = -0.12f;
        const std::vector<uint8_t> flat = Engine::undistortRgba(source.data(), width, height, width, lens);

        const uint32_t x = 8, y = 12;
        const float nx = (float(x) - lens.cx) / lens.fx;
        const float ny = (float(y) - lens.cy) / lens.fy;
        const float factor = 1.0f + lens.k1 * (nx * nx + ny * ny);
        const float sourceX = lens.cx + lens.fx * nx * factor;
        const float sourceY = lens.cy + lens.fy * ny * factor;
        const uint8_t* got = flat.data() + (size_t(y) * width + x) * 4;
        report.check("slika i opazanja koriste isti model distorzije",
            flat.size() == source.size() && std::fabs(float(got[0]) / 3.0f - sourceX) < 0.6f &&
                std::fabs(float(got[1]) / 4.0f - sourceY) < 0.6f && got[2] == 17 && got[3] == 255,
            fmt("izlaz (%u,%u) cita (%.2f,%.2f), boja daje (%.2f,%.2f)",
                x, y, sourceX, sourceY, float(got[0]) / 3.0f, float(got[1]) / 4.0f));

        lens.k1 = 0.0f;
        const std::vector<uint8_t> unchanged =
            Engine::undistortRgba(source.data(), width, height, width, lens);
        report.check("ravna leca ostavlja RGBA sliku bit-identicnom", unchanged == source,
            fmt("%zu ulaznih i %zu izlaznih bajtova", source.size(), unchanged.size()));

        report.check("nevaljani stride se odbija",
            Engine::undistortRgba(source.data(), width, height, width - 1, lens).empty(),
            "stride manji od sirine nema puni red piksela");
    }

    Engine::SelfCalibrationConfig calibrationConfig;
    calibrationConfig.minimumBaselineToDepth = 0.01;

    for(const double initialScale : {0.60, 1.60}){
        Engine::Intrinsics initial = pinhole.intrinsics;
        initial.fx = float(initial.fx * initialScale);
        initial.fy = float(initial.fy * initialScale);

        const Engine::SelfCalibrationResult result = Engine::refineSharedIntrinsics(
            pinhole.observations, pinhole.poses, pinhole.points, initial, calibrationConfig);

        report.check(initialScale < 1.0 ? "zariste se oporavi odozdo" : "zariste se oporavi odozgo",
            result.determined && relativeError(result.intrinsics.fx, pinhole.intrinsics.fx) < 0.01 &&
                relativeError(result.intrinsics.fy, pinhole.intrinsics.fy) < 0.01 && result.endRms < result.startRms,
            fmt("f %.2f -> %.2f px (istina %.2f), RMS %.3f -> %.3f px, %u opazanja",
                initial.fx, result.intrinsics.fx, pinhole.intrinsics.fx,
                result.startRms, result.endRms, result.usedObservations));
    }

    {
        Engine::SyntheticConfig distortedConfig = config;
        distortedConfig.intrinsics.k1 = -0.085f;
        const Engine::SyntheticScene distorted = Engine::makeSyntheticScene(distortedConfig);
        Engine::Intrinsics initial = distorted.intrinsics;
        initial.fx *= 1.35f;
        initial.fy *= 1.35f;
        initial.k1 = 0.0f;

        const Engine::SelfCalibrationResult result = Engine::refineSharedIntrinsics(
            distorted.observations, distorted.poses, distorted.points, initial, calibrationConfig);

        report.check("zariste i radijalna distorzija oporave se zajedno",
            result.determined && relativeError(result.intrinsics.fx, distorted.intrinsics.fx) < 0.015 &&
                std::fabs(result.intrinsics.k1 - distorted.intrinsics.k1) < 0.008 && result.endRms < 0.7,
            fmt("f %.2f/%.2f px, k1 %.5f/%.5f, RMS %.3f -> %.3f px",
                result.intrinsics.fx, distorted.intrinsics.fx, result.intrinsics.k1,
                distorted.intrinsics.k1, result.startRms, result.endRms));
    }

    {
        Engine::Intrinsics initial = pinhole.intrinsics;
        initial.fx *= 0.8f;
        initial.fy *= 0.8f;
        initial.k1 = 0.04f;
        const Engine::SelfCalibrationResult result = Engine::refineSharedIntrinsics(
            pinhole.observations, pinhole.poses, pinhole.points, initial, calibrationConfig);

        report.check("ravna leca ne dobije laznu distorziju",
            result.determined && std::fabs(result.intrinsics.k1) < 0.006,
            fmt("k1 %.6f, f %.2f px, RMS %.3f px", result.intrinsics.k1,
                result.intrinsics.fx, result.endRms));
    }

    {
        std::vector<Engine::Pose> noBaseline = pinhole.poses;
        for(Engine::Pose& pose : noBaseline) pose.position = noBaseline.front().position;

        const Engine::SelfCalibrationResult result = Engine::refineSharedIntrinsics(
            pinhole.observations, noBaseline, pinhole.points, pinhole.intrinsics, calibrationConfig);

        report.check("bez translacijske baze rezultat je neodredjen",
            !result.determined && result.status == Engine::SelfCalibrationStatus::DegenerateGeometry,
            fmt("status %s, baza/dubina %.3e", Engine::selfCalibrationStatusName(result.status),
                result.baselineToDepth));
    }

    {
        const std::vector<Engine::Observation> tooFew(pinhole.observations.begin(),
                                                       pinhole.observations.begin() + 3);
        const Engine::SelfCalibrationResult result = Engine::refineSharedIntrinsics(
            tooFew, pinhole.poses, pinhole.points, pinhole.intrinsics, calibrationConfig);

        report.check("premalo podataka se ne proglasi kalibracijom",
            !result.determined && result.status == Engine::SelfCalibrationStatus::InsufficientData,
            fmt("status %s, %u valjanih opazanja", Engine::selfCalibrationStatusName(result.status),
                result.usedObservations));
    }

    report.checkNoValidationMessages();
    return report.result();
}
