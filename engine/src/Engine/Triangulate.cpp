#include "Engine/Triangulate.h"

#include <cmath>

namespace Engine{

glm::vec3 rayDirection(const Pose& pose, const Intrinsics& intrinsics, const glm::vec2& pixel){
    //Obrnuto od project(): tamo je u = cx + fx * x/dubina, v = cy - fy * y/dubina, a kamera
    //gleda niz -Z. Dubina se iz jednog piksela ne zna, pa ostaje smjer
    const glm::vec3 inCamera((pixel.x - intrinsics.cx) / intrinsics.fx,
                             -(pixel.y - intrinsics.cy) / intrinsics.fy,
                             -1.0f);

    return glm::normalize(pose.orientation * inCamera);
}

bool triangulate(const std::vector<Pose>& poses,
                 const Intrinsics& intrinsics,
                 const std::vector<View>& views,
                 glm::vec3& point){
    if(views.size() < 2){
        return false;   //jedno vidjenje je zraka, ne tocka
    }

    //Tocka najbliza svim zrakama: zbroj kvadrata udaljenosti do pravaca je kvadratna funkcija, pa
    //je njezin minimum rjesenje sustava A x = b. Za zraku (ishodiste o, smjer d) udaljenost mjeri
    //projekcija na ravninu okomitu na d, a to je matrica (I - d d')
    glm::mat3 A(0.0f);
    glm::vec3 b(0.0f);

    for(const View& view : views){
        if(view.camera >= poses.size()){
            return false;
        }
        const Pose& pose = poses[view.camera];
        const glm::vec3 d = rayDirection(pose, intrinsics, view.pixel);

        glm::mat3 P(1.0f);
        for(int row = 0; row < 3; ++row){
            for(int column = 0; column < 3; ++column){
                P[column][row] -= d[row] * d[column];
            }
        }

        A += P;
        b += P * pose.position;
    }

    //Gotovo paralelne zrake: determinanta pada prema nuli i rjesenje odleti. Prag je na skali
    //same matrice, jer A raste s brojem vidjenja - apsolutni broj bi znacio nesto drugo za dvije
    //kamere nego za osam
    const float scale = float(views.size());
    if(std::fabs(glm::determinant(A)) < 1e-6f * scale * scale * scale){
        return false;
    }

    point = glm::inverse(A) * b;
    return true;
}

std::vector<glm::vec3> triangulateAll(const std::vector<Observation>& observations,
                                      const std::vector<Pose>& poses,
                                      const Intrinsics& intrinsics,
                                      size_t pointCount,
                                      std::vector<uint8_t>& solved){
    std::vector<std::vector<View>> perPoint(pointCount);
    for(const Observation& observation : observations){
        if(observation.point < pointCount){
            perPoint[observation.point].push_back(View{observation.camera, observation.pixel});
        }
    }

    std::vector<glm::vec3> points(pointCount, glm::vec3(0.0f));
    solved.assign(pointCount, 0);

    for(size_t i = 0; i < pointCount; ++i){
        solved[i] = triangulate(poses, intrinsics, perPoint[i], points[i]) ? 1u : 0u;
    }
    return points;
}

}
