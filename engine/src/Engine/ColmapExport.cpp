#include "Engine/ColmapExport.h"

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace Engine{

std::vector<glm::u8vec3> pointColours(const Reconstruction& reconstruction,
                                      const std::vector<Observation>& observations,
                                      const std::vector<ColourImage>& images,
                                      uint32_t shrink){
    std::vector<glm::u8vec3> out(reconstruction.points.size(), glm::u8vec3(200, 200, 200));
    if(images.empty() || shrink == 0) return out;

    //Uzorci po tocki. Ogranicen broj po tocki: dulji trag ne daje bolju boju, a 946 tisuca
    //opazanja puta tri bajta je posao koji se ne mora raditi
    constexpr size_t mostSamples = 9;
    std::vector<std::vector<glm::u8vec3>> samples(reconstruction.points.size());

    for(const Observation& one : observations){
        if(one.point >= samples.size() || one.camera >= images.size()) continue;
        if(samples[one.point].size() >= mostSamples) continue;

        const ColourImage& image = images[one.camera];
        if(!image.pixels) continue;

        const int x = int(one.pixel.x / float(shrink) + 0.5f);
        const int y = int(one.pixel.y / float(shrink) + 0.5f);
        if(x < 0 || y < 0 || x >= int(image.width) || y >= int(image.height)) continue;

        const uint32_t stride = image.stride ? image.stride : image.width;
        const uint8_t* pixel = image.pixels + (size_t(y) * stride + size_t(x)) * 4;
        samples[one.point].push_back(glm::u8vec3(pixel[0], pixel[1], pixel[2]));
    }

    for(size_t point = 0; point < samples.size(); ++point){
        std::vector<glm::u8vec3>& mine = samples[point];
        if(mine.empty()) continue;

        //Medijan po kanalu zasebno - vidi zaglavlje
        glm::u8vec3 middle(200, 200, 200);
        for(int channel = 0; channel < 3; ++channel){
            std::vector<uint8_t> values;
            values.reserve(mine.size());
            for(const glm::u8vec3& one : mine) values.push_back(one[channel]);
            std::sort(values.begin(), values.end());
            middle[channel] = values[values.size() / 2];
        }
        out[point] = middle;
    }
    return out;
}

bool writeColmapText(const std::string& directory,
                     const Reconstruction& reconstruction,
                     const Intrinsics& intrinsics,
                     const std::vector<Observation>& observations,
                     const std::vector<std::string>& imageNames,
                     const std::vector<glm::u8vec3>& colours){
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

        char generated[32];
        std::snprintf(generated, sizeof(generated), "frame_%04u.png", uint32_t(camera));
        const std::string name = camera < imageNames.size() && !imageNames[camera].empty()
                               ? imageNames[camera] : std::string(generated);

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

        //Boja iz slike ako ju je pozivatelj dao; inace siva kao prije - vidi zaglavlje
        glm::u8vec3 colour(200, 200, 200);
        if(point < colours.size()) colour = colours[point];

        points << (point + 1) << " " << position.x << " " << position.y << " " << position.z
               << " " << int(colour.r) << " " << int(colour.g) << " " << int(colour.b) << " 0";
        for(const auto& entry : tracks[point]) points << " " << entry.first << " " << entry.second;
        points << "\n";
    }

    return bool(cameras) && bool(images) && bool(points);
}

}
