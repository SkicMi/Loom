#include "Engine/UsdExport.h"

#include <cstdio>
#include <fstream>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine{

namespace{

//Broj u obliku koji USD sigurno procita i koji ne gubi tocnost. %.9g jer float nosi otprilike
//sedam znamenki, a poza se u USD pise kao double
std::string number(double value){
    char text[40];
    std::snprintf(text, sizeof(text), "%.9g", value);
    return text;
}

}

bool writeUsdScene(const std::string& path,
                   const Reconstruction& reconstruction,
                   const Intrinsics& intrinsics,
                   uint32_t imageWidth,
                   uint32_t imageHeight,
                   const std::vector<glm::vec3>& colours,
                   const UsdExportConfig& config){
    if(imageWidth == 0 || imageHeight == 0) return false;

    //Koje su kamere rijesene. Nepostavljene se preskacu - vidi zaglavlje
    std::vector<size_t> solvedCameras;
    for(size_t camera = 0; camera < reconstruction.poses.size(); ++camera){
        if(camera < reconstruction.posed.size() && !reconstruction.posed[camera]) continue;
        solvedCameras.push_back(camera);
    }
    if(solvedCameras.empty()) return false;

    std::ofstream file(path);
    if(!file) return false;

    const int firstSample = config.firstFrame + int(solvedCameras.front()) * config.frameStep;
    const int lastSample = config.firstFrame + int(solvedCameras.back()) * config.frameStep;

    //ZARISNA U MILIMETRIMA. USD trazi zarisnu i otvor u istim jedinicama; jedino sto mora vrijediti
    //je omjer zarisna/otvor = fx/sirina. Kad se zna senzor, oba broja su stvarna i umjetnik vidi
    //poznatu brojku; kad se ne zna, uzima se 36 mm i brojka je samo dosljedna
    const double aperture = config.sensorWidthMillimetres > 0.0 ? config.sensorWidthMillimetres : 36.0;
    const double focal = aperture * double(intrinsics.fx) / double(imageWidth);

    //Okomiti otvor iz omjera stranica, uz kvadratni piksel. Racuna se iz fy da anamorfni piksel
    //ne bi tiho postao kvadratni
    const double verticalAperture = aperture * (double(imageHeight) / double(imageWidth)) *
                                    (double(intrinsics.fx) / double(intrinsics.fy));

    file << "#usda 1.0\n"
         << "(\n"
         << "    defaultPrim = \"Loom\"\n"
         << "    upAxis = \"Y\"\n"
         << "    metersPerUnit = 1\n"
         << "    startTimeCode = " << firstSample << "\n"
         << "    endTimeCode = " << lastSample << "\n"
         << "    timeCodesPerSecond = " << number(config.framesPerSecond) << "\n"
         << "    framesPerSecond = " << number(config.framesPerSecond) << "\n"
         << ")\n\n"
         << "# Rijesio Loom. MJERILO JE SLOBODNO: rekonstrukcija iz same snimke ne zna metre, pa\n"
         << "# je sve tocno do jedne skale. Scenu se skalira u alatu.\n"
         << "# Rijeseno " << solvedCameras.size() << " od " << reconstruction.poses.size() << " kamera.\n\n"
         << "def Xform \"Loom\"\n{\n";

    file << "    def Camera \"kamera\"\n    {\n"
         << "        float focalLength = " << number(focal) << "\n"
         << "        float horizontalAperture = " << number(aperture) << "\n"
         << "        float verticalAperture = " << number(verticalAperture) << "\n"
         << "        float2 clippingRange = (0.001, 100000)\n"
         << "        matrix4d xformOp:transform.timeSamples = {\n";

    for(size_t index = 0; index < solvedCameras.size(); ++index){
        const size_t camera = solvedCameras[index];
        const Pose& pose = reconstruction.poses[camera];
        const int sample = config.firstFrame + int(camera) * config.frameStep;

        //Osi kamere u svijetu. orientation vrti IZ kamere U svijet (vidi Pose), pa su ovo bas
        //stupci njezine matrice - a u USD idu kao RETCI, jer je USD po retcima
        const glm::mat3 axes = glm::mat3_cast(pose.orientation);
        const glm::vec3 right = axes[0];
        const glm::vec3 up = axes[1];
        const glm::vec3 back = axes[2];     //+Z je IZA kamere, jer gleda niz -Z
        const glm::vec3& position = pose.position;

        file << "            " << sample << ": ( "
             << "(" << number(right.x) << ", " << number(right.y) << ", " << number(right.z) << ", 0), "
             << "(" << number(up.x) << ", " << number(up.y) << ", " << number(up.z) << ", 0), "
             << "(" << number(back.x) << ", " << number(back.y) << ", " << number(back.z) << ", 0), "
             << "(" << number(position.x) << ", " << number(position.y) << ", " << number(position.z) << ", 1) )";
        file << (index + 1 < solvedCameras.size() ? ",\n" : "\n");
    }

    file << "        }\n"
         << "        uniform token[] xformOpOrder = [\"xformOp:transform\"]\n"
         << "    }\n";

    if(config.writePoints && !reconstruction.points.empty()){
        //Samo rijesene tocke. Nerijesena tocka je ostala ondje gdje je bila inicijalizirana i
        //nacrtala bi oblak koji ne postoji
        std::vector<size_t> solvedPoints;
        for(size_t point = 0; point < reconstruction.points.size(); ++point){
            if(point < reconstruction.solved.size() && !reconstruction.solved[point]) continue;
            solvedPoints.push_back(point);
        }

        if(!solvedPoints.empty()){
            file << "\n    def Points \"tocke\"\n    {\n        point3f[] points = [";
            for(size_t index = 0; index < solvedPoints.size(); ++index){
                const glm::vec3& point = reconstruction.points[solvedPoints[index]];
                file << "(" << number(point.x) << ", " << number(point.y) << ", " << number(point.z) << ")";
                if(index + 1 < solvedPoints.size()) file << ", ";
            }
            file << "]\n";

            if(colours.size() == reconstruction.points.size()){
                file << "        color3f[] primvars:displayColor = [";
                for(size_t index = 0; index < solvedPoints.size(); ++index){
                    const glm::vec3& colour = colours[solvedPoints[index]];
                    file << "(" << number(colour.r) << ", " << number(colour.g) << ", " << number(colour.b) << ")";
                    if(index + 1 < solvedPoints.size()) file << ", ";
                }
                file << "]\n"
                     << "        uniform token primvars:displayColor:interpolation = \"vertex\"\n";
            }
            file << "    }\n";
        }
    }

    file << "}\n";
    return bool(file);
}

}
