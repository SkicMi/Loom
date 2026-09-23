#include "Warp/UsdCamera.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Warp{

namespace{

//Broj iza "ime =" u retku, kad ga ima
bool valueAfter(const std::string& line, const char* name, double& out){
    const size_t at = line.find(name);
    if(at == std::string::npos) return false;
    const size_t equals = line.find('=', at);
    if(equals == std::string::npos) return false;
    char* end = nullptr;
    const double value = std::strtod(line.c_str() + equals + 1, &end);
    if(end == line.c_str() + equals + 1) return false;
    out = value;
    return true;
}

}

bool readUsdCamera(const std::string& path, UsdCamera& out){
    std::ifstream file(path);
    if(!file) return false;
    out = UsdCamera{};

    std::string line;
    bool inSamples = false;
    double number = 0.0;
    while(std::getline(file, line)){
        if(!inSamples){
            if(valueAfter(line, "startTimeCode", number)) out.startTimeCode = number;
            else if(valueAfter(line, "endTimeCode", number)) out.endTimeCode = number;
            else if(valueAfter(line, "framesPerSecond", number)) out.framesPerSecond = number;
            else if(valueAfter(line, "focalLength", number)) out.focalLength = float(number);
            else if(valueAfter(line, "horizontalAperture", number)) out.horizontalAperture = float(number);
            else if(valueAfter(line, "verticalAperture", number)) out.verticalAperture = float(number);
            if(line.find("xformOp:transform.timeSamples") != std::string::npos) inSamples = true;
            continue;
        }
        if(line.find('}') != std::string::npos && line.find(':') == std::string::npos) break;

        //"  12: ( (a, b, c, 0), (...), (...), (x, y, z, 1) ),"
        const size_t colon = line.find(':');
        if(colon == std::string::npos) continue;
        const double time = std::strtod(line.c_str(), nullptr);
        float values[16];
        int count = 0;
        const char* at = line.c_str() + colon + 1;
        while(*at && count < 16){
            if((*at >= '0' && *at <= '9') || *at == '-' || *at == '+' || *at == '.'){
                char* end = nullptr;
                values[count++] = std::strtof(at, &end);
                at = end;
            }else{
                ++at;
            }
        }
        if(count != 16) continue;
        glm::mat4 m;
        for(int row = 0; row < 4; ++row) for(int column = 0; column < 4; ++column) m[row][column] = values[row * 4 + column];
        out.times.push_back(time);
        out.transforms.push_back(m);
    }
    return !out.times.empty();
}

}
