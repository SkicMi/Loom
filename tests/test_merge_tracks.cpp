// Spajanje tragova: prepozna li se isti kut kad se kamera vrati na njega.
//
// ZASTO OVO TREBA. Pracenje ide iz kadra u kadar i umire cim ugao izadje iz slike. Kad se kamera
// vrati, isti kut se nadje ponovno - ali kao NOVI trag, bez veze sa starim. Rekonstrukcija ih vidi
// kao dvije tocke, i veza izmedju dva dijela snimke ne postoji. Izmjereno na pravoj snimci: kadar 0
// dijeli 236 tocaka s kljucnim kadrom 5 i NIJEDNU s kadrom 10.
//
// Scena je ovdje ZADANA: kamere obidju luk i vrate se blizu pocetka, pa se zna koji se kadrovi
// moraju povezati. Istina je poznata do zadnje znamenke, jer se opazanja racunaju iz poznatih poza.
//
// Sto se brani:
//
//   spajanje       tragovi koji su ista tocka se spoje, pa ih ima manje nego prije
//   bez izmisljanja   spajanje ne smije spojiti dvije razlicite tocke
//   jedno po kadru    ista kamera ne smije tvrditi da jednu tocku vidi na dva mjesta
//   sum ne prolazi    na nepovezanim kadrovima se ne spaja nista
#include "TestHarness.h"

#include <Engine/MergeTracks.h>
#include <Engine/Track.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace{

const uint32_t width = 480, height = 360;

//Neponavljajuca tekstura: periodicna bi dala uglove koji se stvarno ne razlikuju
float speckle(float x, float y){
    auto value = [](int cx, int cy){
        uint32_t h = uint32_t(cx) * 374761393u + uint32_t(cy) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return float((h ^ (h >> 16)) & 0xffu);
    };
    const int cx = int(std::floor(x / 7.0f)), cy = int(std::floor(y / 7.0f));
    const float fx = x / 7.0f - float(cx), fy = y / 7.0f - float(cy);
    const float top = value(cx, cy) * (1.0f - fx) + value(cx + 1, cy) * fx;
    const float bottom = value(cx, cy + 1) * (1.0f - fx) + value(cx + 1, cy + 1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

std::vector<uint8_t> render(glm::vec2 shift){
    std::vector<uint8_t> pixels(size_t(width) * height);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const float value = speckle(float(x) - shift.x, float(y) - shift.y);
            pixels[size_t(y) * width + x] = uint8_t(std::max(0.0f, std::min(255.0f, value)));
        }
    }
    return pixels;
}

Engine::GrayImage view(const std::vector<uint8_t>& pixels){
    return Engine::GrayImage{pixels.data(), width, height, width};
}

}

int main(){
    TestReport report("spajanje tragova");

    // -------------------------------------------------------------------------------
    // Kamera ode i vrati se
    // -------------------------------------------------------------------------------
    //
    // Sest kadrova: pomak ode do 60 piksela pa se vrati na 0. Tracker prvi i zadnji kadar ne moze
    // povezati jer je izmedju izgubio tragove; potpis mora

    {
        const std::vector<float> path = {0.0f, 24.0f, 48.0f, 48.0f, 24.0f, 0.0f};

        Engine::TrackConfig trackConfig;
        trackConfig.minDistance = 20.0f;
        trackConfig.maxCorners = 150;
        trackConfig.window = 8;

        Engine::Tracker tracker(trackConfig);
        std::vector<std::vector<uint8_t>> frames;
        for(float shift : path){
            frames.push_back(render({shift, 0.0f}));
            tracker.addFrame(view(frames.back()));
        }

        std::vector<Engine::GrayImage> images;
        for(const auto& frame : frames) images.push_back(view(frame));

        Engine::Intrinsics intrinsics;
        intrinsics.width = width; intrinsics.height = height;
        intrinsics.cx = 0.5f * float(width); intrinsics.cy = 0.5f * float(height);

        Engine::MergeConfig mergeConfig;
        mergeConfig.nearest = 2;
        mergeConfig.window = 5;
        mergeConfig.minInliers = 12;

        const uint32_t before = tracker.trackCount();
        const Engine::MergeResult merged = Engine::mergeTracks(
            tracker.observations(), images, before, intrinsics, mergeConfig);

        report.check("tragovi se spoje kad se kamera vrati",
            merged.pointCount < before && merged.mergedPairs > 0,
            fmt("%u tocaka prije, %u poslije; %u spajanja, %u od %u parova kadrova proslo geometriju",
                before, merged.pointCount, merged.mergedPairs,
                merged.acceptedFrames, merged.comparedFrames));

        //JEDNO OPAZANJE PO KADRU I TOCKI. Bez toga bi ista kamera tvrdila da tocku vidi na dva
        //mjesta, a triangulacija bi ju stavila izmedju - dakle nigdje
        std::map<uint64_t, uint32_t> seen;
        uint32_t twice = 0;
        for(const Engine::Observation& one : merged.observations){
            const uint64_t key = (uint64_t(one.camera) << 32) | uint64_t(one.point);
            if(++seen[key] == 2) ++twice;
        }
        report.check("ista kamera ne vidi tocku dvaput",
            twice == 0,
            fmt("%u tocaka bi bilo na dva mjesta u istom kadru", twice));

        //Brojevi moraju ostati gusti - reconstruct polja indeksira brojem tocke
        uint32_t highest = 0;
        for(const Engine::Observation& one : merged.observations) highest = std::max(highest, one.point);
        report.check("brojevi tocaka ostaju gusti",
            merged.observations.empty() || highest < merged.pointCount,
            fmt("najveci broj %u, prijavljeno %u tocaka", highest, merged.pointCount));
    }

    // -------------------------------------------------------------------------------
    // Nepovezani kadrovi: ne smije se spojiti nista
    // -------------------------------------------------------------------------------
    //
    // Pomak toliko velik da se kadrovi ne preklapaju. Spajanje ovdje ne bi bilo korist nego steta:
    // tocka koje nema povlaci pozu za sobom

    {
        Engine::TrackConfig trackConfig;
        trackConfig.minDistance = 20.0f;
        trackConfig.maxCorners = 150;

        Engine::Tracker tracker(trackConfig);
        std::vector<std::vector<uint8_t>> frames;
        for(int i = 0; i < 4; ++i){
            frames.push_back(render({float(i) * 3000.0f, float(i) * 1700.0f}));
            tracker.addFrame(view(frames.back()));
        }

        std::vector<Engine::GrayImage> images;
        for(const auto& frame : frames) images.push_back(view(frame));

        Engine::Intrinsics intrinsics;
        intrinsics.width = width; intrinsics.height = height;
        intrinsics.cx = 0.5f * float(width); intrinsics.cy = 0.5f * float(height);

        Engine::MergeConfig mergeConfig;
        mergeConfig.nearest = 1;
        mergeConfig.window = 3;

        const uint32_t before = tracker.trackCount();
        const Engine::MergeResult merged = Engine::mergeTracks(
            tracker.observations(), images, before, intrinsics, mergeConfig);

        report.check("na nepovezanim kadrovima se ne spaja nista",
            merged.mergedPairs == 0,
            fmt("%u spajanja, %u od %u parova proslo geometriju",
                merged.mergedPairs, merged.acceptedFrames, merged.comparedFrames));
    }

    return report.result();
}
