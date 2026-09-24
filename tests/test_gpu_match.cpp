// Poklapanje opisnika na kartici (Loomov DescriptorMatcher) protiv procesora (Engine::matchSiftNear).
//
// ZASTO SE OVO TESTIRA. Kartica mora dati ISTA poklapanja kao procesor - isti radijus, drugi
// najbolji koji nije susjed najboljeg, prag omjera, najveca udaljenost, uzajamno najbolji, i isti
// redoslijed kod jednakih udaljenosti. Inace se promijeni graf, a s njim i cijeli solve, i ubrzanje
// vise nije ubrzanje nego drugi solver.
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Vulkan/DescriptorMatcher.h"

#include <Engine/Sift.h>
#include <Engine/Track.h>

#include <chrono>
#include <cmath>
#include <set>

namespace{

//Tekstura: zbroj mekanih mrlja i kvadrata, pomaknuta i zakrenuta za svaki kadar
std::vector<uint8_t> render(uint32_t w, uint32_t h, float shiftX, float shiftY, float turn){
    std::vector<uint8_t> image(size_t(w) * h);
    const float c = std::cos(turn), s = std::sin(turn);
    for(uint32_t y = 0; y < h; ++y){
        for(uint32_t x = 0; x < w; ++x){
            const float px = c * (float(x) - shiftX) - s * (float(y) - shiftY);
            const float py = s * (float(x) - shiftX) + c * (float(y) - shiftY);
            float v = 0.0f;
            uint32_t state = 12345u;
            for(int k = 0; k < 90; ++k){
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                const float cx = float(state % 1200) - 100.0f;
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                const float cy = float(state % 800) - 100.0f;
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                const float r = 8.0f + float(state % 40);
                const float level = (k % 3 == 0) ? 1.0f : -0.6f;
                const float d = std::max(std::fabs(px - cx), std::fabs(py - cy));
                v += level * 0.5f * (1.0f + std::tanh((r - d) / 1.5f));
            }
            image[size_t(y) * w + x] = uint8_t(std::max(0.0f, std::min(255.0f, 128.0f + 60.0f * v)));
        }
    }
    return image;
}

}

int main(){
    TestReport report("G1 poklapanje na kartici");

    LoomConfig config;
    config.width = 64; config.height = 64;
    config.appName = "gpu match"; config.engineName = "Loom tests";
    config.headless = true;
    config.maxDescriptorSets = 64;
    LoomInitializer loom(config);

    const uint32_t w = 960, h = 540;
    std::vector<std::vector<uint8_t>> images = {render(w, h, 0, 0, 0), render(w, h, 7.3f, -3.1f, 0.02f), render(w, h, 15.8f, -5.4f, 0.035f)};
    Engine::TrackConfig detect;
    detect.maxCorners = 3000;
    detect.minDistance = 3.0f;
    Engine::SiftConfig sift;
    std::vector<std::vector<Engine::SiftDescriptor>> signatures;
    std::vector<std::vector<glm::vec2>> points;
    for(const auto& image : images){
        const Engine::GrayImage gray{image.data(), w, h, w};
        points.push_back(Engine::detectCorners(gray, detect));
        signatures.push_back(Engine::describeSiftAll(gray, points.back(), sift));
    }
    const std::vector<std::pair<uint32_t, uint32_t>> pairs = {{0, 1}, {0, 2}, {1, 2}};
    const float radius = 0.25f * float(w);

    const auto cpuStarted = std::chrono::steady_clock::now();
    std::vector<std::vector<Engine::SiftMatch>> cpu;
    for(const auto& p : pairs) cpu.push_back(Engine::matchSiftNear(signatures[p.first], points[p.first], signatures[p.second], points[p.second], radius, sift));
    const double cpuSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - cpuStarted).count();

    //Isti podaci na kartici
    std::vector<std::vector<uint8_t>> flat(images.size()), valid(images.size());
    std::vector<DescriptorMatcher::Frame> frames(images.size());
    for(size_t f = 0; f < images.size(); ++f){
        for(const auto& d : signatures[f]){
            flat[f].insert(flat[f].end(), d.values.begin(), d.values.end());
            valid[f].push_back(d.valid ? 1 : 0);
        }
        frames[f] = DescriptorMatcher::Frame{flat[f].data(), reinterpret_cast<const float*>(points[f].data()), valid[f].data(), uint32_t(signatures[f].size())};
    }
    DescriptorMatcher matcher(loom);
    DescriptorMatcher::Rules rules;
    rules.radius = radius; rules.maxDistance = sift.maxDistance; rules.ratio = sift.ratio; rules.secondBestApart = sift.secondBestApart;
    const auto gpuStarted = std::chrono::steady_clock::now();
    const auto gpu = matcher.match(frames, pairs, rules);
    const double gpuSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - gpuStarted).count();

    size_t cpuCount = 0, gpuCount = 0, common = 0;
    for(size_t p = 0; p < pairs.size(); ++p){
        std::set<std::pair<uint32_t, uint32_t>> a, b;
        for(const auto& m : cpu[p]) a.insert({m.from, m.to});
        for(const auto& m : gpu[p]) b.insert({m.from, m.to});
        cpuCount += a.size(); gpuCount += b.size();
        for(const auto& m : a) common += b.count(m);
    }
    report.check("kartica daje ista poklapanja kao procesor", cpuCount > 300 && cpuCount == gpuCount && common == cpuCount,
        fmt("procesor %zu, kartica %zu, zajednickih %zu (procesor %.3f s, kartica %.3f s s pripremom)",
            cpuCount, gpuCount, common, cpuSeconds, gpuSeconds));
    report.checkNoValidationMessages();
    return report.result();
}
