// Znacajke u prostoru mjerila: pogadja li vrh ispod piksela i na pravom mjerilu.
//
// ZASTO OVO POSTOJI. Nase su tocke izmjereno dvadeset pet puta grublje od COLMAP-ovih, i racun
// kaze da je uzrok kvantizacija polozaja na cetiri piksela - jer se znacajke traze na cetiri puta
// smanjenoj slici. Dva puta iz toga (veca radna sirina, naknadno dotjerivanje) izmjerena su i
// pala. Ostaje naci znacajku na mjerilu na kojem ona postoji, a vrh joj odrediti ispod piksela.
//
// Istina je ovdje poznata do zadnje znamenke, jer se mrlje CRTAJU na zadana mjesta i zadane
// sirine.
//
// Sto se brani:
//
//   pogadja vrh        polozaj ispod pola piksela, s mjesta koja NISU na cijelom pikselu
//   pogadja mjerilo    sira mrlja mora biti nadjena na vecem mjerilu, i to monotono
//   rub nije znacajka  duz ravnog ruba polozaj po rubu nije odredjen, pa se takvo mora odbiti
//   ravna ploha        ondje nema ni ekstrema ni kontrasta
//   isto dvaput        dva poziva moraju dati identican niz - inace poredak ovisi o dretvama
#include "TestHarness.h"

#include <Engine/ScaleSpace.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace{

const uint32_t width = 320, height = 320;

struct Blob{
    glm::vec2 at;
    float sigma;
};

std::vector<uint8_t> renderBlobs(const std::vector<Blob>& blobs){
    std::vector<float> value(size_t(width) * height, 0.5f);
    for(const Blob& blob : blobs){
        const int reach = int(std::ceil(4.0f * blob.sigma));
        const int cx = int(std::round(blob.at.x)), cy = int(std::round(blob.at.y));
        for(int y = cy - reach; y <= cy + reach; ++y){
            for(int x = cx - reach; x <= cx + reach; ++x){
                if(x < 0 || y < 0 || x >= int(width) || y >= int(height)) continue;
                const float dx = float(x) - blob.at.x, dy = float(y) - blob.at.y;
                value[size_t(y) * width + size_t(x)] -=
                    0.4f * std::exp(-(dx * dx + dy * dy) / (2.0f * blob.sigma * blob.sigma));
            }
        }
    }

    std::vector<uint8_t> pixels(value.size());
    for(size_t i = 0; i < value.size(); ++i){
        pixels[i] = uint8_t(std::max(0.0f, std::min(255.0f, 255.0f * value[i])));
    }
    return pixels;
}

Engine::GrayImage view(const std::vector<uint8_t>& pixels){
    return Engine::GrayImage{pixels.data(), width, height, width};
}

//Najbliza nadjena znacajka zadanom mjestu
const Engine::Keypoint* nearest(const std::vector<Engine::Keypoint>& found, glm::vec2 at, float within){
    const Engine::Keypoint* best = nullptr;
    float bestDistance = within;
    for(const Engine::Keypoint& one : found){
        const float distance = glm::length(one.pixel - at);
        if(distance < bestDistance){ bestDistance = distance; best = &one; }
    }
    return best;
}

}

int main(){
    TestReport report("znacajke u prostoru mjerila");

    Engine::ScaleSpaceConfig config;
    config.octaves = 3;

    //-- pogadja vrh i mjerilo -----------------------------------------------------------------
    //
    //Mjesta su namjerno IZVAN cijelih piksela: na cijelom bi i kvantiziran odgovor bio tocan, pa
    //provjera ne bi mjerila nista
    {
        const std::vector<Blob> blobs{
            {glm::vec2(80.30f, 90.70f), 3.0f},
            {glm::vec2(180.25f, 110.60f), 6.0f},
            {glm::vec2(120.80f, 220.10f), 12.0f}};

        const std::vector<uint8_t> pixels = renderBlobs(blobs);
        const std::vector<Engine::Keypoint> found = Engine::detectScaleSpace(view(pixels), config);

        double worst = 0.0;
        uint32_t hits = 0;
        std::vector<float> scales;
        for(const Blob& blob : blobs){
            const Engine::Keypoint* one = nearest(found, blob.at, 3.0f);
            if(!one) continue;
            ++hits;
            worst = std::max(worst, double(glm::length(one->pixel - blob.at)));
            scales.push_back(one->scale);
        }

        report.check("pogadja vrh ispod piksela",
            hits == blobs.size() && worst < 0.5,
            fmt("%u od %zu mrlja nadjeno, najgori promasaj %.3f px", hits, blobs.size(), worst));

        report.check("pogadja mjerilo",
            scales.size() == 3 && scales[0] < scales[1] && scales[1] < scales[2],
            fmt("mjerila %.2f, %.2f, %.2f za sirine 3, 6, 12 px",
                scales.size() > 0 ? double(scales[0]) : 0.0,
                scales.size() > 1 ? double(scales[1]) : 0.0,
                scales.size() > 2 ? double(scales[2]) : 0.0));
    }

    //-- rub i ravna ploha ---------------------------------------------------------------------
    {
        std::vector<uint8_t> edge(size_t(width) * height, 200);
        for(uint32_t y = 0; y < height; ++y){
            for(uint32_t x = 0; x < width; ++x){
                if(x > width / 2) edge[size_t(y) * width + x] = 40;
            }
        }
        const std::vector<Engine::Keypoint> onEdge = Engine::detectScaleSpace(view(edge), config);

        report.check("rub nije znacajka",
            onEdge.empty(), fmt("%zu znacajki na ravnom rubu", onEdge.size()));

        const std::vector<uint8_t> flat(size_t(width) * height, 128);
        const std::vector<Engine::Keypoint> onFlat = Engine::detectScaleSpace(view(flat), config);

        report.check("ravna ploha nema znacajki",
            onFlat.empty(), fmt("%zu znacajki na ravnoj plohi", onFlat.size()));
    }

    //-- isto dvaput ---------------------------------------------------------------------------
    {
        const std::vector<uint8_t> pixels = renderBlobs({
            {glm::vec2(60.4f, 70.3f), 4.0f}, {glm::vec2(200.7f, 150.2f), 8.0f}});

        const std::vector<Engine::Keypoint> once = Engine::detectScaleSpace(view(pixels), config);
        const std::vector<Engine::Keypoint> twice = Engine::detectScaleSpace(view(pixels), config);

        bool same = once.size() == twice.size();
        for(size_t i = 0; same && i < once.size(); ++i){
            same = once[i].pixel == twice[i].pixel && once[i].scale == twice[i].scale;
        }

        report.check("dva poziva daju isto",
            same && !once.empty(), fmt("%zu naspram %zu znacajki", once.size(), twice.size()));
    }

    return report.result();
}
