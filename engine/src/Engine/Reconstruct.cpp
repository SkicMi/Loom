#include "Engine/Reconstruct.h"
#include "Engine/Triangulate.h"

#include <algorithm>
#include <cmath>

namespace Engine{
namespace{

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

//Reprojekcija po opazanjima koja su usla u rekonstrukciju - dakle samo rijesene kamere i tocke
double medianOver(const std::vector<Observation>& observations, const Reconstruction& state,
                  const Intrinsics& intrinsics){
    std::vector<double> errors;
    for(const Observation& observation : observations){
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

Reconstruction reconstruct(const std::vector<Observation>& observations,
                           size_t cameraCount,
                           size_t pointCount,
                           const Intrinsics& intrinsics,
                           const ReconstructConfig& config){
    Reconstruction state;
    state.poses.assign(cameraCount, Pose{});
    state.posed.assign(cameraCount, 0);
    state.points.assign(pointCount, glm::vec3(0.0f));
    state.solved.assign(pointCount, 0);

    if(cameraCount < 2 || pointCount < 8 || observations.empty()){
        return state;
    }

    //Tko sto vidi. Dvije strane istog popisa, jer se jedna cita po kameri a druga po tocki
    std::vector<std::vector<const Observation*>> byCamera(cameraCount);
    std::vector<std::vector<const Observation*>> byPoint(pointCount);
    for(const Observation& observation : observations){
        if(observation.camera >= cameraCount || observation.point >= pointCount) continue;
        byCamera[observation.camera].push_back(&observation);
        byPoint[observation.point].push_back(&observation);
    }

    // ---------------------------------------------------------------------------------
    // Pocetni par: najudaljeniji kadar koji jos dijeli dovoljno tocaka s prvim
    // ---------------------------------------------------------------------------------

    std::vector<uint8_t> seenByFirst(pointCount, 0);
    for(const Observation* observation : byCamera[0]) seenByFirst[observation->point] = 1;

    size_t partner = 0;
    size_t partnerCommon = 0;
    for(size_t camera = cameraCount - 1; camera > 0; --camera){
        size_t common = 0;
        for(const Observation* observation : byCamera[camera]) if(seenByFirst[observation->point]) ++common;
        if(common >= std::max<size_t>(30, byCamera[0].size() / 4)){
            partner = camera;
            partnerCommon = common;
            break;
        }
        if(common > partnerCommon){
            partner = camera;
            partnerCommon = common;
        }
    }
    if(partner == 0 || partnerCommon < 8){
        return state;
    }

    std::vector<glm::vec2> pixelsA, pixelsB;
    std::vector<uint32_t> sharedPoints;
    {
        std::vector<const Observation*> inFirst(pointCount, nullptr);
        for(const Observation* observation : byCamera[0]) inFirst[observation->point] = observation;
        for(const Observation* observation : byCamera[partner]){
            if(!inFirst[observation->point]) continue;
            pixelsA.push_back(inFirst[observation->point]->pixel);
            pixelsB.push_back(observation->pixel);
            sharedPoints.push_back(observation->point);
        }
    }

    const TwoViewResult pair = relativePoseRobust(pixelsA, pixelsB, intrinsics, config.ransac);
    if(!pair.solved){
        return state;
    }

    state.poses[0] = Pose{};                //ishodiste i jedinicna orijentacija: gauge
    state.poses[partner] = pair.pose;       //pomak je jedinicni - mjerilo ostaje slobodno
    state.posed[0] = 1;
    state.posed[partner] = 1;
    state.posedCameras = 2;

    // ---------------------------------------------------------------------------------
    // Triangulacija svega sto vide dvije rijesene kamere, pa nova kamera, pa opet
    // ---------------------------------------------------------------------------------

    auto triangulateVisible = [&](){
        for(size_t point = 0; point < pointCount; ++point){
            if(state.solved[point]) continue;
            std::vector<View> views;
            for(const Observation* observation : byPoint[point]){
                if(state.posed[observation->camera]) views.push_back(View{observation->camera, observation->pixel});
            }
            if(views.size() < 2) continue;

            glm::vec3 position;
            if(!triangulate(state.poses, intrinsics, views, position)) continue;

            //Tocka iza neke od kamera koje je vide nije rjesenje nego smetnja
            bool inFront = true;
            for(const View& view : views){
                glm::vec2 pixel;
                if(!project(state.poses[view.camera], intrinsics, position, pixel)) inFront = false;
            }
            if(!inFront) continue;

            state.points[point] = position;
            state.solved[point] = 1;
            ++state.solvedPoints;
        }
    };

    auto runBundle = [&](){
        std::vector<Observation> kept;
        kept.reserve(observations.size());
        for(const Observation& observation : observations){
            if(state.posed[observation.camera] && state.solved[observation.point]) kept.push_back(observation);
        }
        if(kept.empty()) return;

        BundleConfig bundleConfig;
        bundleConfig.huberPixels = config.huberPixels;
        bundleConfig.maxIterations = config.bundleIterations;

        const BundleResult result = bundleAdjust(kept, state.poses, state.points, intrinsics, bundleConfig);
        if(!result.solved) return;
        state.poses = result.poses;
        state.points = result.points;
    };

    triangulateVisible();
    runBundle();

    //Redom dodaje kameru koja vidi najvise vec rijesenih tocaka
    for(size_t added = 0; added + 2 <= cameraCount; ++added){
        size_t best = cameraCount;
        size_t bestCount = 0;
        for(size_t camera = 0; camera < cameraCount; ++camera){
            if(state.posed[camera]) continue;
            size_t count = 0;
            for(const Observation* observation : byCamera[camera]) if(state.solved[observation->point]) ++count;
            if(count > bestCount){
                bestCount = count;
                best = camera;
            }
        }
        if(best == cameraCount || bestCount < config.minPointsForPose) break;

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

        PoseSolveConfig poseConfig;
        poseConfig.huberPixels = config.huberPixels;
        const PoseSolveResult pose = solvePose(state.points, seen, intrinsics, state.poses[nearest], poseConfig);

        if(!pose.solved || pose.endMedian > config.acceptPixels){
            break;   //kamera koja se ne da rijesiti zaustavlja lanac; bolje manje nego krivo
        }

        state.poses[best] = pose.pose;
        state.posed[best] = 1;
        ++state.posedCameras;

        triangulateVisible();
        runBundle();
    }

    state.medianReprojection = medianOver(observations, state, intrinsics);
    state.ok = state.posedCameras >= 2 && state.solvedPoints > 0;
    return state;
}

}
