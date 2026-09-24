// Potpisi prostora mjerila na kartici (Loomov SiftDescriber) protiv procesora (Engine::describeSiftScaled).
//
// ZASTO SE OVO TESTIRA. Zamucenje na kartici mora biti isto do bita, a potpis se smije razlikovati
// samo onoliko koliko se sqrt, atan2, exp, sin i cos kartice razlikuju u zadnjem bitu - dakle
// pokoja vrijednost za jedan. Ako se razlikuje vise, pogresan je racun (pojas, zakrpa, rub), a
// graf poklapanja s njim bio bi drugi graf. Mjeri se i ono sto se na kraju broji: poklapanja
// izmedju dva kadra s potpisima s procesora i s kartice.
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Vulkan/SiftDescriber.h"

#include <Engine/ScaleSpace.h>
#include <Engine/Sift.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <set>

namespace{

//Tekstura: mrlje i kvadrati raznih velicina, pomaknuta i zakrenuta za svaki kadar
std::vector<uint8_t> render(uint32_t w, uint32_t h, float shiftX, float shiftY, float turn){
    std::vector<uint8_t> image(size_t(w) * h);
    const float c = std::cos(turn), s = std::sin(turn);
    for(uint32_t y = 0; y < h; ++y){
        for(uint32_t x = 0; x < w; ++x){
            const float px = c * (float(x) - shiftX) - s * (float(y) - shiftY);
            const float py = s * (float(x) - shiftX) + c * (float(y) - shiftY);
            float v = 0.0f;
            uint32_t state = 12345u;
            for(int k = 0; k < 160; ++k){
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                const float cx = float(state % (w + 200)) - 100.0f;
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                const float cy = float(state % (h + 200)) - 100.0f;
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                const float r = 4.0f + float(state % 90);
                const float level = (k % 3 == 0) ? 1.0f : -0.6f;
                const float d = (k % 2) ? std::max(std::fabs(px - cx), std::fabs(py - cy)) : std::hypot(px - cx, py - cy);
                v += level * 0.5f * (1.0f + std::tanh((r - d) / 1.5f));
            }
            image[size_t(y) * w + x] = uint8_t(std::max(0.0f, std::min(255.0f, 128.0f + 50.0f * v)));
        }
    }
    return image;
}

}

int main(){
    TestReport report("G2 potpisi prostora mjerila na kartici");

    LoomConfig config;
    config.width = 64; config.height = 64;
    config.appName = "gpu sift"; config.engineName = "Loom tests";
    config.headless = true;
    config.maxDescriptorSets = 64;
    LoomInitializer loom(config);
    SiftDescriber describer(loom);

    const uint32_t w = 1920, h = 1080;
    const std::vector<std::vector<uint8_t>> images = {render(w, h, 0, 0, 0), render(w, h, 23.0f, -9.0f, 0.03f)};
    Engine::ScaleSpaceConfig detect;
    detect.maxKeypoints = 6000;
    Engine::SiftConfig sift;
    SiftDescriber::Settings settings;
    settings.scaleBands = sift.scaleBands; settings.patchPerScale = sift.patchPerScale;
    settings.clamp = sift.clamp; settings.orient = sift.orient;

    std::vector<std::vector<glm::vec2>> points(images.size());
    std::vector<std::vector<Engine::SiftDescriptor>> cpu(images.size()), gpu(images.size());
    double cpuSeconds = 0.0, gpuSeconds = 0.0;
    size_t total = 0, sameValid = 0, identical = 0, bytesOff = 0, bytesTotal = 0;
    int worst = 0;
    for(size_t f = 0; f < images.size(); ++f){
        const Engine::GrayImage gray{images[f].data(), w, h, w};
        std::vector<float> scales;
        for(const Engine::Keypoint& key : Engine::detectScaleSpace(gray, detect)){ points[f].push_back(key.pixel); scales.push_back(key.scale); }

        auto started = std::chrono::steady_clock::now();
        cpu[f] = Engine::describeSiftScaled(gray, points[f], scales, sift);
        cpuSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        started = std::chrono::steady_clock::now();
        const SiftDescriber::Output out = describer.describe(images[f].data(), w, h, w,
            reinterpret_cast<const float*>(points[f].data()), scales.data(), uint32_t(points[f].size()), settings);
        gpuSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        gpu[f].resize(points[f].size());
        for(size_t i = 0; i < points[f].size(); ++i){
            Engine::SiftDescriptor& d = gpu[f][i];
            d.valid = out.valid[i] != 0;
            d.angle = out.angles[i];
            std::copy(out.values.begin() + i * 128, out.values.begin() + (i + 1) * 128, d.values.begin());

            ++total;
            if(d.valid == cpu[f][i].valid) ++sameValid;
            if(!d.valid || !cpu[f][i].valid) continue;
            bool same = true;
            for(size_t k = 0; k < 128; ++k){
                const int off = std::abs(int(d.values[k]) - int(cpu[f][i].values[k]));
                worst = std::max(worst, off);
                if(off){ ++bytesOff; same = false; }
                ++bytesTotal;
            }
            if(same) ++identical;
            if(std::getenv("GPU_SIFT_EXPLAIN") && !same){
                int most = 0; for(size_t k = 0; k < 128; ++k) most = std::max(most, std::abs(int(d.values[k]) - int(cpu[f][i].values[k])));
                std::printf("  %zu: mjerilo %.2f kut cpu %.5f gpu %.5f, najvise %d\n", i, scales[i], cpu[f][i].angle, d.angle, most);
            }
        }
    }

    report.check("iste znacajke imaju potpis", total > 800 && sameValid == total,
        fmt("%zu od %zu", sameValid, total));
    report.check("potpisi se razlikuju tek u zadnjem bitu", bytesTotal > 0 && double(bytesOff) / double(bytesTotal) < 0.01,
        fmt("%.3f %% vrijednosti razlicito, najvise za %d; posve istih potpisa %zu od %zu", 100.0 * double(bytesOff) / double(bytesTotal), worst, identical, total));

    //Ono sto se broji: poklapanja dvaju kadrova
    const float radius = 0.25f * float(w);
    std::set<std::pair<uint32_t, uint32_t>> a, b;
    for(const auto& m : Engine::matchSiftNear(cpu[0], points[0], cpu[1], points[1], radius, sift)) a.insert({m.from, m.to});
    for(const auto& m : Engine::matchSiftNear(gpu[0], points[0], gpu[1], points[1], radius, sift)) b.insert({m.from, m.to});
    size_t common = 0;
    for(const auto& m : a) common += b.count(m);
    report.check("poklapanja su gotovo ista", a.size() > 300 && double(common) >= 0.99 * double(std::max(a.size(), b.size())),
        fmt("procesor %zu, kartica %zu, zajednickih %zu (potpisi: procesor %.2f s, kartica %.2f s s prijenosom)",
            a.size(), b.size(), common, cpuSeconds, gpuSeconds));
    report.checkNoValidationMessages();
    return report.result();
}
