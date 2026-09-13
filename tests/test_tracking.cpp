// Pracenje uglova kroz kadrove: od piksela do opazanja.
//
// Ovo je zadnji komad koji fali da prava snimka udje u lanac. Testira se na slikama koje su
// ZADANE FORMULOM, pa se pomak zna na proizvoljan broj decimala: kadar B je ista funkcija
// racunata u pomaknutoj tocki, bez presemplavanja. Tako se mjeri tocnost pracenja, a ne kvaliteta
// moje pripreme slike.
//
// Sto se brani:
//
//   uglovi           sahovnica ima uglove na poznatim mjestima; detektor ih mora naci tamo
//   pomak            poznat pomak od 3.4 i -2.1 piksela mora se izmjeriti na desetinu piksela
//   piramida         pomak od 25 piksela je veci od prozora. S piramidom se prati, bez nje ne -
//                    i to je razlog zasto piramida postoji, pa se mjeri a ne tvrdi
//   odbijanje        pracenje u nepovezanu sliku mora reci da nije uspjelo
//   tragovi          kroz niz kadrova isti trag mora zadrzati svoj broj, a opazanja pratiti gibanje
#include "TestHarness.h"

#include <Engine/Track.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace{

const uint32_t width = 480, height = 360;

//Uzorak s gradijentom u oba smjera: zbroj dviju sinusoida pod razlicitim kutovima. Jedna sama bi
//dala pruge, a pruga se ne da pratiti uzduz sebe (problem otvora)
float pattern(float x, float y){
    return 128.0f
         + 60.0f * std::sin(0.21f * x) * std::cos(0.17f * y)
         + 45.0f * std::sin(0.07f * x + 0.11f * y)
         + 20.0f * std::sin(0.53f * x - 0.31f * y);
}

float checkerboard(float x, float y){
    const int cell = 40;
    const int cx = int(std::floor(x / float(cell)));
    const int cy = int(std::floor(y / float(cell)));
    return ((cx + cy) % 2 == 0) ? 40.0f : 215.0f;
}

std::vector<uint8_t> render(float (*source)(float, float), float shiftX, float shiftY){
    std::vector<uint8_t> pixels(size_t(width) * height);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const float value = source(float(x) - shiftX, float(y) - shiftY);
            pixels[size_t(y) * width + x] = uint8_t(std::max(0.0f, std::min(255.0f, value)));
        }
    }
    return pixels;
}

Engine::GrayImage view(const std::vector<uint8_t>& pixels){
    return Engine::GrayImage{pixels.data(), width, height, width};
}

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}

int main(){
    TestReport report("pracenje uglova");

    // -------------------------------------------------------------------------------
    // Uglovi sahovnice su na poznatim mjestima
    // -------------------------------------------------------------------------------

    {
        const std::vector<uint8_t> board = render(checkerboard, 0.0f, 0.0f);
        Engine::TrackConfig config;
        config.minDistance = 20.0f;
        const std::vector<glm::vec2> corners = Engine::detectCorners(view(board), config);

        double worst = 0.0;
        for(const glm::vec2& corner : corners){
            const double x = std::fabs(std::round(corner.x / 40.0) * 40.0 - double(corner.x));
            const double y = std::fabs(std::round(corner.y / 40.0) * 40.0 - double(corner.y));
            worst = std::max(worst, std::max(x, y));
        }

        report.check("uglovi su nadjeni tamo gdje jesu",
            corners.size() > 30 && worst < 2.0,
            fmt("%zu uglova, najdalji %.2f px od krizista", corners.size(), worst));
    }

    // -------------------------------------------------------------------------------
    // Poznat pomak
    // -------------------------------------------------------------------------------

    const std::vector<uint8_t> first = render(pattern, 0.0f, 0.0f);

    {
        const float shiftX = 3.4f, shiftY = -2.1f;
        const std::vector<uint8_t> second = render(pattern, shiftX, shiftY);

        const std::vector<glm::vec2> corners = Engine::detectCorners(view(first));
        std::vector<double> errors;
        size_t lost = 0;

        for(const glm::vec2& corner : corners){
            glm::vec2 moved;
            if(!Engine::trackPoint(view(first), view(second), corner, moved)){
                ++lost;
                continue;
            }
            errors.push_back(double(glm::length(moved - corner - glm::vec2(shiftX, shiftY))));
        }

        report.check("poznat pomak se izmjeri na desetinu piksela",
            errors.size() > 50 && medianOf(errors) < 0.1,
            fmt("medijan %.4f px kroz %zu uglova, izgubljeno %zu", medianOf(errors), errors.size(), lost));
    }

    // -------------------------------------------------------------------------------
    // Veliki pomak: s piramidom i bez nje
    // -------------------------------------------------------------------------------

    {
        const float shiftX = 25.0f, shiftY = 14.0f;
        const std::vector<uint8_t> far = render(pattern, shiftX, shiftY);
        const std::vector<glm::vec2> corners = Engine::detectCorners(view(first));

        Engine::TrackConfig flat;
        flat.levels = 1;

        std::vector<double> withPyramid, withoutPyramid;
        for(const glm::vec2& corner : corners){
            glm::vec2 moved;
            if(Engine::trackPoint(view(first), view(far), corner, moved)){
                withPyramid.push_back(double(glm::length(moved - corner - glm::vec2(shiftX, shiftY))));
            }
            if(Engine::trackPoint(view(first), view(far), corner, moved, flat)){
                withoutPyramid.push_back(double(glm::length(moved - corner - glm::vec2(shiftX, shiftY))));
            }
        }

        const double good = medianOf(withPyramid);
        const double flatMedian = medianOf(withoutPyramid);

        report.check("piramida hvata pomak veci od prozora",
            withPyramid.size() > 40 && good < 0.2,
            fmt("medijan %.4f px kroz %zu uglova", good, withPyramid.size()));

        report.check("bez piramide isti pomak ne ide",
            withoutPyramid.size() < withPyramid.size() / 2 || flatMedian > 2.0,
            fmt("bez piramide %zu pracenih (medijan %.3f px), s piramidom %zu",
                withoutPyramid.size(), flatMedian, withPyramid.size()));
    }

    // -------------------------------------------------------------------------------
    // Nepovezana slika mora biti odbijena
    // -------------------------------------------------------------------------------

    {
        const std::vector<uint8_t> other = render(checkerboard, 0.0f, 0.0f);
        const std::vector<glm::vec2> corners = Engine::detectCorners(view(first));

        size_t accepted = 0;
        for(const glm::vec2& corner : corners){
            glm::vec2 moved;
            if(Engine::trackPoint(view(first), view(other), corner, moved)) ++accepted;
        }

        report.check("pracenje u nepovezanu sliku se odbija",
            accepted < corners.size() / 10,
            fmt("prihvaceno %zu od %zu", accepted, corners.size()));
    }

    // -------------------------------------------------------------------------------
    // Niz kadrova: tragovi zadrzavaju svoj broj
    // -------------------------------------------------------------------------------

    {
        Engine::Tracker tracker;
        const uint32_t frames = 6;
        const float stepX = 2.5f, stepY = -1.5f;

        std::vector<std::vector<uint8_t>> sequence;
        for(uint32_t frame = 0; frame < frames; ++frame){
            sequence.push_back(render(pattern, stepX * float(frame), stepY * float(frame)));
            tracker.addFrame(view(sequence.back()));
        }

        //Za svaki trag: koliko se pomaknuo izmedju prvog i zadnjeg kadra u kojem se vidi
        std::vector<glm::vec2> firstSeen(tracker.trackCount(), glm::vec2(0.0f));
        std::vector<uint32_t> firstFrame(tracker.trackCount(), 0);
        std::vector<double> errors;

        for(const Engine::Observation& observation : tracker.observations()){
            if(firstSeen[observation.point] == glm::vec2(0.0f) && firstFrame[observation.point] == 0){
                firstSeen[observation.point] = observation.pixel;
                firstFrame[observation.point] = observation.camera;
                continue;
            }
            const float steps = float(observation.camera) - float(firstFrame[observation.point]);
            const glm::vec2 expected = firstSeen[observation.point] + glm::vec2(stepX, stepY) * steps;
            errors.push_back(double(glm::length(observation.pixel - expected)));
        }

        report.check("tragovi prate gibanje kroz niz kadrova",
            tracker.frameCount() == frames && tracker.activeTracks() > 50 && medianOf(errors) < 0.15,
            fmt("%u kadrova, %u tragova pokrenuto, %u jos zivo, medijan %.4f px kroz %zu opazanja",
                tracker.frameCount(), tracker.trackCount(), tracker.activeTracks(), medianOf(errors), errors.size()));
    }

    return report.result();
}
