#include "Engine/Keyframes.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Engine{

namespace{

double medianOf(std::vector<double>& values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

//Koliko se par (sidro -> kadar) NE DA objasniti jednim afinim preslikom, u pikselima.
//
//Afini preslik ima sest parametara i namjesta se najmanjim kvadratima: za svaku komponentu jedan
//sustav 3x3 nad [x y 1]. Zaokret kamere je afin do na perspektivu i pokupi ga preslik; pomak
//kamere nije, jer bliske tocke odu vise od dalekih - i to ostane u ostatku
double affineResidual(const std::vector<glm::vec2>& from, const std::vector<glm::vec2>& to){
    if(from.size() < 6) return 0.0;

    glm::dmat3 normal(0.0);
    glm::dvec3 rightX(0.0), rightY(0.0);
    for(size_t i = 0; i < from.size(); ++i){
        const glm::dvec3 row(double(from[i].x), double(from[i].y), 1.0);
        for(int r = 0; r < 3; ++r){
            for(int c = 0; c < 3; ++c) normal[c][r] += row[r] * row[c];
            rightX[r] += row[r] * double(to[i].x);
            rightY[r] += row[r] * double(to[i].y);
        }
    }

    //Gotovo singularan sustav znaci da tragovi leze na pravcu; tada se nista ne tvrdi
    if(std::fabs(glm::determinant(normal)) < 1e-9) return 0.0;

    const glm::dmat3 inverse = glm::inverse(normal);
    const glm::dvec3 alongX = inverse * rightX;
    const glm::dvec3 alongY = inverse * rightY;

    std::vector<double> residual;
    residual.reserve(from.size());
    for(size_t i = 0; i < from.size(); ++i){
        const glm::dvec3 row(double(from[i].x), double(from[i].y), 1.0);
        const glm::dvec2 predicted(glm::dot(alongX, row), glm::dot(alongY, row));
        residual.push_back(glm::length(predicted - glm::dvec2(to[i])));
    }
    return medianOf(residual);
}

}

KeyframeSelection chooseKeyframes(const std::vector<Observation>& observations,
                                  uint32_t frameCount,
                                  uint32_t width,
                                  const KeyframeConfig& config){
    KeyframeSelection selection;
    if(frameCount == 0 || observations.empty()) return selection;

    //Opazanja po kadru: trag -> gdje je bio. Ovo je jedini nacin da se dva kadra usporede bez
    //ikakve geometrije - a geometrije ovdje jos nema, tek ce je rekonstrukcija napraviti
    std::vector<std::unordered_map<uint32_t, glm::vec2>> byFrame(frameCount);
    for(const Observation& observation : observations){
        if(observation.camera < frameCount) byFrame[observation.camera][observation.point] = observation.pixel;
    }

    const double threshold = config.minParallaxFraction * double(width);

    std::vector<uint32_t> chosen{0};
    std::vector<double> shifts;

    uint32_t anchor = 0;
    for(uint32_t frame = 1; frame < frameCount; ++frame){
        std::vector<glm::vec2> atAnchor, atFrame;
        for(const auto& entry : byFrame[frame]){
            const auto found = byFrame[anchor].find(entry.first);
            if(found == byFrame[anchor].end()) continue;
            atAnchor.push_back(found->second);
            atFrame.push_back(entry.second);
        }

        const uint32_t shared = uint32_t(atAnchor.size());
        const double shift = affineResidual(atAnchor, atFrame);

        //Veza se gubi: uzima se PRETHODNI kadar, jos dok tragova ima. Ne vrijedi za kadar odmah
        //iza sidra - tamo prethodni JE sidro
        if(shared < config.minSharedTracks && frame > anchor + 1){
            chosen.push_back(frame - 1);
            shifts.push_back(shift);
            anchor = frame - 1;
            continue;
        }

        if(shift >= threshold){
            chosen.push_back(frame);
            shifts.push_back(shift);
            anchor = frame;
        }
    }

    //Zadnji kadar ulazi ako donosi nesto novo, a ne ako je vec tu ili tik uz zadnji kljucni
    if(chosen.back() + 2 < frameCount) chosen.push_back(frameCount - 1);

    if(config.maxKeyframes > 0 && chosen.size() > config.maxKeyframes){
        //Prorjedjivanje ravnomjerno, uz zadrzana oba kraja - bolje nego odsjeci rep snimke
        std::vector<uint32_t> thinned;
        for(uint32_t i = 0; i < config.maxKeyframes; ++i){
            const size_t pick = size_t(double(i) * double(chosen.size() - 1) / double(config.maxKeyframes - 1));
            if(thinned.empty() || thinned.back() != chosen[pick]) thinned.push_back(chosen[pick]);
        }
        chosen = thinned;
    }

    //Opazanja se preslikavaju: camera vise nije broj kadra nego mjesto u popisu kljucnih
    std::vector<uint32_t> placeOf(frameCount, uint32_t(-1));
    for(uint32_t i = 0; i < chosen.size(); ++i) placeOf[chosen[i]] = i;

    for(const Observation& observation : observations){
        if(observation.camera >= frameCount) continue;
        const uint32_t place = placeOf[observation.camera];
        if(place == uint32_t(-1)) continue;
        selection.observations.push_back(Observation{place, observation.point, observation.pixel});
    }

    selection.frames = chosen;
    selection.medianParallaxPixels = medianOf(shifts);
    return selection;
}

}
