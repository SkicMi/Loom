#include "Engine/SyntheticScene.h"

#include <glm/gtx/quaternion.hpp>   //quatLookAt, ista funkcija koju koristi i Loomova Camera

#include <algorithm>
#include <cmath>
#include <random>

namespace Engine{

namespace{

//JEDINA PROJEKCIJA U OVOM FILEU, i to namjerno: prva verzija ju je imala dvaput, jednom ovdje i
//jednom u medianReprojection. Mutacija koja je okrenula okomitu os u jednom primjerku obarala je
//i provjere koje s njom nemaju veze, jer su se dvije kopije raziile. Test je to uhvatio, ali
//druga kopija nije smjela ni postojati.
//
//Granice slike ovdje NISU: project() ih dodaje jer bira sto se uopce opaza, a medianReprojection
//ih namjerno nema - tocka koju dana poza gurne izvan kadra je greska koju mjera mora vidjeti
bool projectUnbounded(const Pose& pose, const Intrinsics& intrinsics, const glm::vec3& point, glm::vec2& pixel){
    //Iz svijeta u kameru: prvo pomak, pa obrnuta rotacija. Za jedinicni kvaternion je konjugat
    //inverz, i jeftiniji - isto kako to racuna Loomova Camera
    const glm::vec3 inCamera = glm::conjugate(pose.orientation) * (point - pose.position);

    //Kamera gleda niz -Z, pa je dubina pozitivna kad je tocka ispred nje
    const float depth = -inCamera.z;
    if(depth <= 0.0f){
        return false;
    }

    //Normalizirane koordinate: gdje bi zraka pala na ravnini udaljenoj jedan, prije zarista
    float nx = inCamera.x / depth;
    float ny = -inCamera.y / depth;                                 //v raste prema dolje

    //Zakrivljenje se primjenjuje OVDJE, na normaliziranim koordinatama - ne na pikselima. Da se
    //racuna u pikselima, isti k bi znacio drugu distorziju za svaku razlucivost
    if(intrinsics.k1 != 0.0f || intrinsics.k2 != 0.0f){
        const float squared = nx * nx + ny * ny;
        const float factor = 1.0f + squared * (intrinsics.k1 + squared * intrinsics.k2);
        nx *= factor;
        ny *= factor;
    }

    pixel.x = intrinsics.cx + intrinsics.fx * nx;
    pixel.y = intrinsics.cy + intrinsics.fy * ny;
    return true;
}

}

glm::vec2 undistort(const Intrinsics& intrinsics, const glm::vec2& pixel){
    if(intrinsics.k1 == 0.0f && intrinsics.k2 == 0.0f) return pixel;
    if(intrinsics.fx == 0.0f || intrinsics.fy == 0.0f) return pixel;

    //U normalizirane, pa natrag - isti put kojim project ide, samo unatrag
    const float mx = (pixel.x - intrinsics.cx) / intrinsics.fx;
    const float my = (pixel.y - intrinsics.cy) / intrinsics.fy;

    float nx = mx, ny = my;
    for(int step = 0; step < 5; ++step){
        const float squared = nx * nx + ny * ny;
        const float factor = 1.0f + squared * (intrinsics.k1 + squared * intrinsics.k2);
        if(factor <= 0.0f) break;
        nx = mx / factor;
        ny = my / factor;
    }

    return glm::vec2(intrinsics.cx + intrinsics.fx * nx, intrinsics.cy + intrinsics.fy * ny);
}

std::vector<uint8_t> undistortRgba(const uint8_t* pixels,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t stride,
                                   const Intrinsics& intrinsics){
    if(!pixels || width == 0 || height == 0 || stride < width ||
       intrinsics.fx == 0.0f || intrinsics.fy == 0.0f) return {};

    std::vector<uint8_t> output(size_t(width) * height * 4, 0);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const float nx = (float(x) - intrinsics.cx) / intrinsics.fx;
            const float ny = (float(y) - intrinsics.cy) / intrinsics.fy;
            const float radiusSquared = nx * nx + ny * ny;
            const float factor = 1.0f + radiusSquared *
                (intrinsics.k1 + radiusSquared * intrinsics.k2);
            const float sourceX = intrinsics.cx + intrinsics.fx * nx * factor;
            const float sourceY = intrinsics.cy + intrinsics.fy * ny * factor;
            if(sourceX < 0.0f || sourceY < 0.0f ||
               sourceX > float(width - 1) || sourceY > float(height - 1)) continue;

            const uint32_t x0 = uint32_t(sourceX), y0 = uint32_t(sourceY);
            const uint32_t x1 = std::min(x0 + 1, width - 1);
            const uint32_t y1 = std::min(y0 + 1, height - 1);
            const float tx = sourceX - float(x0), ty = sourceY - float(y0);
            uint8_t* destination = output.data() + (size_t(y) * width + x) * 4;
            for(uint32_t channel = 0; channel < 4; ++channel){
                const float top = (1.0f - tx) * pixels[(size_t(y0) * stride + x0) * 4 + channel] +
                                  tx * pixels[(size_t(y0) * stride + x1) * 4 + channel];
                const float bottom = (1.0f - tx) * pixels[(size_t(y1) * stride + x0) * 4 + channel] +
                                     tx * pixels[(size_t(y1) * stride + x1) * 4 + channel];
                destination[channel] = uint8_t(std::clamp((1.0f - ty) * top + ty * bottom,
                                                          0.0f, 255.0f) + 0.5f);
            }
        }
    }
    return output;
}

bool project(const Pose& pose, const Intrinsics& intrinsics, const glm::vec3& point, glm::vec2& pixel){
    if(!projectUnbounded(pose, intrinsics, point, pixel)){
        return false;
    }
    return pixel.x >= 0.0f && pixel.y >= 0.0f &&
           pixel.x < float(intrinsics.width) && pixel.y < float(intrinsics.height);
}

SyntheticScene makeSyntheticScene(const SyntheticConfig& config){
    SyntheticScene scene;
    scene.intrinsics = config.intrinsics;

    std::mt19937 random(config.seed);
    auto uniform = [&](float low, float high){
        return low + (high - low) * float(random() % 1000000) / 1000000.0f;
    };

    scene.points.reserve(config.pointCount);
    for(uint32_t i = 0; i < config.pointCount; ++i){
        scene.points.push_back(glm::vec3(uniform(-config.extent.x, config.extent.x),
                                         uniform(-config.extent.y, config.extent.y),
                                         uniform(-config.extent.z, config.extent.z)));
    }

    scene.poses.reserve(config.cameraCount);
    for(uint32_t i = 0; i < config.cameraCount; ++i){
        //Jedna kamera znaci jedan kut; vise njih se rasporede po luku
        const float t = config.cameraCount > 1 ? float(i) / float(config.cameraCount - 1) : 0.5f;
        const float angle = config.arc * (t - 0.5f);

        Pose pose;
        pose.position = glm::vec3(config.radius * std::sin(angle),
                                  config.height,
                                  config.radius * std::cos(angle));

        //Gleda u ishodiste. Kvaternion, ne Eulerovi kutovi - isti razlog kao u Loomovoj kameri
        const glm::vec3 forward = glm::normalize(-pose.position);
        pose.orientation = glm::quatLookAt(forward, glm::vec3(0.0f, 1.0f, 0.0f));
        scene.poses.push_back(pose);
    }

    //Sum se dodaje TEK OPAZANJU, nikad tockama ni pozama: one ostaju tocan odgovor
    std::normal_distribution<float> noise(0.0f, config.noisePixels);

    for(uint32_t camera = 0; camera < scene.poses.size(); ++camera){
        for(uint32_t point = 0; point < scene.points.size(); ++point){
            glm::vec2 pixel;
            if(!project(scene.poses[camera], scene.intrinsics, scene.points[point], pixel)){
                continue;
            }
            if(config.noisePixels > 0.0f){
                pixel.x += noise(random);
                pixel.y += noise(random);
            }
            scene.observations.push_back(Observation{camera, point, pixel});
        }
    }
    return scene;
}

double medianReprojection(const SyntheticScene& scene,
                          const std::vector<Pose>& poses,
                          const std::vector<glm::vec3>& points,
                          uint32_t camera){
    std::vector<double> errors;
    errors.reserve(scene.observations.size());

    for(const Observation& observation : scene.observations){
        if(observation.camera >= poses.size() || observation.point >= points.size()){
            continue;
        }
        if(camera != allCameras && observation.camera != camera){
            continue;
        }
        //Ista projekcija koju koristi i project(), samo bez granica slike: tocka koju dana poza
        //gurne izvan kadra je greska koju mjera mora vidjeti, a ne preskociti
        glm::vec2 predicted;
        if(!projectUnbounded(poses[observation.camera], scene.intrinsics, points[observation.point], predicted)){
            errors.push_back(double(scene.intrinsics.width));   //iza kamere: koliko god daleko
            continue;
        }
        errors.push_back(double(glm::length(predicted - observation.pixel)));
    }

    if(errors.empty()){
        return 0.0;
    }
    std::sort(errors.begin(), errors.end());
    return errors[errors.size() / 2];
}

}
