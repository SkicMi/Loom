// Subpikselni polozaj uglova (TrackConfig::subpixel).
//
// ZASTO SE OVO TESTIRA. Graf poklapanja trazi uglove na slici smanjenoj cetiri puta, i bez
// dotjerivanja je svaki na 4K tocan na cetiri piksela. Ovdje ista scena stoji na dvije slike,
// druga pomaknuta za poznat SUBPIKSELNI pomak; uglovi se traze na smanjenoj slici kao u grafu,
// pa se pomak izmjeri iz parova uglova i usporedi s pravim.
//
//   BEZ DOTJERIVANJA   pomak je cijeli piksel smanjene slike - greska do pola piksela, dakle do
//                      dva na 4K
//   S DOTJERIVANJEM    greska mora pasti bar deset puta, i to za ISTE uglove (izbor se ne mijenja),
//                      bez skakanja na susjedne strukture
#include "TestHarness.h"

#include <Engine/Track.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace{

using namespace Engine;

//Kvadrati razlicite svjetline i velicine, svaki u svojoj celiji (ne preklapaju se), s mekim
//bridom (tanh) - analiticki, pa se moze uzorkovati s bilo kojim pomakom
float pattern(double x, double y){
    const int cellX = int(std::floor(x / 128.0)), cellY = int(std::floor(y / 128.0));
    uint32_t state = 2463534242u ^ uint32_t(cellX * 7919 + cellY * 104729 + 17);
    auto random = [&](){ state ^= state << 13; state ^= state >> 17; state ^= state << 5; return double(state % 100000) / 100000.0; };
    random(); random();
    const double cx = (cellX + 0.3 + 0.4 * random()) * 128.0, cy = (cellY + 0.3 + 0.4 * random()) * 128.0;
    const double half = 18.0 + 22.0 * random(), level = 0.4 + 0.6 * random();
    const double edge = 1.5;
    return float(level * 0.25 * (1.0 + std::tanh((half - std::fabs(x - cx)) / edge)) * (1.0 + std::tanh((half - std::fabs(y - cy)) / edge)));
}

//Slika w x h uzorkovana s pomakom (dx, dy), pa smanjena 'shrink' puta prosjekom (kao MatchGraph)
std::vector<uint8_t> render(uint32_t w, uint32_t h, double dx, double dy, uint32_t shrink, uint32_t& sw, uint32_t& sh){
    std::vector<float> full(size_t(w) * h);
    for(uint32_t y = 0; y < h; ++y) for(uint32_t x = 0; x < w; ++x) full[size_t(y) * w + x] = pattern(double(x) - dx, double(y) - dy);
    sw = w / shrink; sh = h / shrink;
    std::vector<uint8_t> small(size_t(sw) * sh);
    for(uint32_t y = 0; y < sh; ++y) for(uint32_t x = 0; x < sw; ++x){
        double sum = 0.0;
        for(uint32_t a = 0; a < shrink; ++a) for(uint32_t b = 0; b < shrink; ++b) sum += full[size_t(y * shrink + a) * w + x * shrink + b];
        small[size_t(y) * sw + x] = uint8_t(std::max(0.0, std::min(255.0, 128.0 + 100.0 * sum / double(shrink * shrink))));
    }
    return small;
}

//Medijan greske pomaka (u pikselima PUNE slike) preko parova uglova blizih od piksela smanjene
double shiftError(const std::vector<glm::vec2>& a, const std::vector<glm::vec2>& b, glm::vec2 truth, uint32_t shrink, size_t& pairs){
    std::vector<double> errors;
    pairs = 0;
    for(const glm::vec2& p : a){
        double best = 1e9; glm::vec2 match(0.0f);
        for(const glm::vec2& q : b){
            const double d = glm::length(q - p - truth / float(shrink));
            if(d < best){ best = d; match = q; }
        }
        if(best > 1.0) continue;
        errors.push_back(glm::length((match - p) * float(shrink) - truth));
        ++pairs;
    }
    if(errors.empty()) return 1e9;
    std::nth_element(errors.begin(), errors.begin() + long(errors.size() / 2), errors.end());
    return errors[errors.size() / 2];
}

}

int main(){
    TestReport report("S8 subpikselni uglovi");

    const uint32_t shrink = 4;
    const glm::vec2 truth(0.37f * 4.0f, -0.61f * 4.0f);      //pomak na punoj slici, subpikselni i na smanjenoj
    uint32_t sw = 0, sh = 0;
    const std::vector<uint8_t> first = render(1024, 768, 0.0, 0.0, shrink, sw, sh);
    const std::vector<uint8_t> second = render(1024, 768, truth.x, truth.y, shrink, sw, sh);
    const GrayImage a{first.data(), sw, sh, sw}, b{second.data(), sw, sh, sw};

    TrackConfig config;
    config.maxCorners = 200;
    config.minDistance = 8.0f;
    config.window = 3;
    const std::vector<glm::vec2> plainA = detectCorners(a, config), plainB = detectCorners(b, config);
    config.subpixel = true;
    const std::vector<glm::vec2> fineA = detectCorners(a, config), fineB = detectCorners(b, config);

    size_t plainPairs = 0, finePairs = 0;
    const double plainError = shiftError(plainA, plainB, truth, shrink, plainPairs);
    const double fineError = shiftError(fineA, fineB, truth, shrink, finePairs);
    report.check("subpikselni polozaj smanji gresku pomaka bar deset puta (pikseli pune slike)",
        plainPairs > 30 && finePairs > 30 && fineError * 10.0 < plainError && fineError < 0.3,
        fmt("bez %.3f px (%zu parova), s %.3f px (%zu parova)", plainError, plainPairs, fineError, finePairs));

    //Foerstner (refineCorner) na ISTOJ smanjenoj slici, za usporedbu s parabolom
    for(uint32_t window : {3u, 5u, 8u}){
        std::vector<glm::vec2> fa, fb;
        for(const glm::vec2& p : plainA) fa.push_back(refineCorner(a, p, window, 3.0f, 10));
        for(const glm::vec2& p : plainB) fb.push_back(refineCorner(b, p, window, 3.0f, 10));
        size_t pairs = 0;
        const double error = shiftError(fa, fb, truth, shrink, pairs);
        std::printf("   (Foerstner, poluprozor %u: %.3f px, %zu parova)\n", window, error, pairs);
    }

    //Isti uglovi: jednako ih je, i dotjerani je od cijelog udaljen najvise koliko refineCorner dopusta
    bool same = plainA.size() == fineA.size();
    for(size_t i = 0; same && i < plainA.size(); ++i) same = glm::length(fineA[i] - plainA[i]) <= 3.0f;
    report.check("izbor uglova je isti, pomak najvise 3 px smanjene slike", same, fmt("%zu uglova", fineA.size()));

    return report.result();
}
