#include "Engine/ColmapExport.h"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <vector>

namespace Engine{

bool writeColmapText(const std::string& directory,
                     const Reconstruction& reconstruction,
                     const Intrinsics& intrinsics,
                     const std::vector<Observation>& observations){
    std::ofstream cameras(directory + "/cameras.txt");
    std::ofstream images(directory + "/images.txt");
    std::ofstream points(directory + "/points3D.txt");
    if(!cameras || !images || !points){
        return false;
    }

    //Dvanaest znamenki, ne zadanih sest: sest daje gresku od nekoliko tisucinki piksela pri
    //citanju natrag, a ovo je podatkovni file iz kojeg netko drugi trenira
    cameras << std::setprecision(12);
    images << std::setprecision(12);
    points << std::setprecision(12);

    //Jedna kamera za sve kadrove: ista snimka, ista optika
    cameras << "# Camera list with one line of data per camera:\n";
    cameras << "#   CAMERA_ID, MODEL, WIDTH, HEIGHT, PARAMS[]\n";
    cameras << "1 PINHOLE " << intrinsics.width << " " << intrinsics.height << " "
            << intrinsics.fx << " " << intrinsics.fy << " " << intrinsics.cx << " " << intrinsics.cy << "\n";

    //Opazanja po kameri, jer images.txt trazi drugi redak s pikselima i indeksima tocaka
    std::vector<std::vector<const Observation*>> byCamera(reconstruction.poses.size());
    for(const Observation& observation : observations){
        if(observation.camera >= reconstruction.poses.size() || observation.point >= reconstruction.points.size()) continue;
        if(!reconstruction.posed[observation.camera] || !reconstruction.solved[observation.point]) continue;
        byCamera[observation.camera].push_back(&observation);
    }

    images << "# Image list with two lines of data per image:\n";
    images << "#   IMAGE_ID, QW, QX, QY, QZ, TX, TY, TZ, CAMERA_ID, NAME\n";
    images << "#   POINTS2D[] as (X, Y, POINT3D_ID)\n";

    for(size_t camera = 0; camera < reconstruction.poses.size(); ++camera){
        if(!reconstruction.posed[camera]) continue;

        //Vidi zaglavlje: R_colmap = M R', t_colmap = -R_colmap c
        const glm::mat3 ours = glm::mat3_cast(reconstruction.poses[camera].orientation);
        glm::mat3 colmap(1.0f);
        const float mirror[3] = {1.0f, -1.0f, -1.0f};
        for(int row = 0; row < 3; ++row){
            for(int column = 0; column < 3; ++column){
                //ours[column][row] je element (row, column); transponira se i zrcali redom
                colmap[column][row] = mirror[row] * ours[row][column];
            }
        }
        const glm::quat rotation = glm::normalize(glm::quat_cast(colmap));
        const glm::vec3 translation = -(colmap * reconstruction.poses[camera].position);

        char name[32];
        std::snprintf(name, sizeof(name), "frame_%04u.png", uint32_t(camera));

        images << (camera + 1) << " "
               << rotation.w << " " << rotation.x << " " << rotation.y << " " << rotation.z << " "
               << translation.x << " " << translation.y << " " << translation.z << " 1 " << name << "\n";

        bool first = true;
        for(const Observation* observation : byCamera[camera]){
            if(!first) images << " ";
            first = false;
            images << observation->pixel.x << " " << observation->pixel.y << " " << (observation->point + 1);
        }
        images << "\n";
    }

    points << "# 3D point list with one line of data per point:\n";
    points << "#   POINT3D_ID, X, Y, Z, R, G, B, ERROR, TRACK[] as (IMAGE_ID, POINT2D_IDX)\n";

    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> tracks(reconstruction.points.size());
    for(size_t camera = 0; camera < byCamera.size(); ++camera){
        for(size_t index = 0; index < byCamera[camera].size(); ++index){
            tracks[byCamera[camera][index]->point].push_back({uint32_t(camera + 1), uint32_t(index)});
        }
    }

    for(size_t point = 0; point < reconstruction.points.size(); ++point){
        if(!reconstruction.solved[point] || tracks[point].empty()) continue;
        const glm::vec3& position = reconstruction.points[point];

        points << (point + 1) << " " << position.x << " " << position.y << " " << position.z
               << " 200 200 200 0";
        for(const auto& entry : tracks[point]) points << " " << entry.first << " " << entry.second;
        points << "\n";
    }

    return bool(cameras) && bool(images) && bool(points);
}

}
