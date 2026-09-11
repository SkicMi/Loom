// G8: budzet kadra splatanja - koliko crtanje oblaka smije kostati.
//
// Svaki drugi splat test mjeri PIKSELE: slika je ista kao gruba sila, brojevi se slazu, poredak
// je tocan. Dvostruko sporiji rasterizator prolazi ih sve bez rijeci. Ovaj pita koliko je posla
// trebalo, i radi to na istom obrascu kao B1 (test_budget) - jer su iste zamke.
//
// STO SE MJERI: vrijeme s KARTICINOG sata, po koraku (G5). Stoperica na procesoru ovdje ne bi
// mjerila crtanje nego slanje naredbi, a splat kadar je gotovo sav na kartici.
//
// TRI TVRDNJE, i dvije od njih ne ovise o stroju:
//
//   kadar ne alocira        radna polja se grade jednom; kadar koji alocira je kadar koji ce
//                           jednom stati na alokatoru, i to se ne vidi ni na jednoj slici
//   cijena je linearna      cetiri puta vise splatova smije kostati cetiri puta vise, ne
//     u broju splatova      sesnaest. Priprema i sort po dubini su po splatu, i to je tvrdnja
//                           o algoritmu, ne o kartici
//   parova po splatu        koliko parova scena uopce napravi. NE OVISI O KARTICI - isti broj
//                           na Intelu i na lavapipeu - pa je to najostrija provjera ovdje
//   strop                   broj o OVOM stroju, pa ga drzi klasa uredjaja - kao u B1
//
// PROVJERA "PAROVA PO SPLATU" POSTOJI ZATO STO JE PRVA VERZIJA OVOG TESTA BILA SLIJEPA. Strop je
// bio dvostruko iznad izmjerenog, a mutacija koja izbaci odbacivanje parova (G7) digla je kadar
// sa 41.0 na 56.7 ms - ispod stropa, pa je prosla. Slika se pritom ne mijenja (odbacivanje je
// konzervativno), pa je nijedan drugi test ne bi vidio kao regresiju brzine. Parova po splatu
// istovremeno skoci s 15.9 na 26.2, i to je broj koji se ne da sakriti.
//
// Stropovi su izmjereni, ne izmisljeni; brojke i uredjaji stoje uz njih dolje.
#include "TestHarness.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/SplatRenderer.h"
#include "Vulkan/VulkanAllocator.h"
#include "Vulkan/VulkanImage.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace{

const vk::Extent2D size{640, 480};

double median(std::vector<double> values){
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}

int main(){
    TestReport report("G8 budzet splat kadra");

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "splat budget"; config.engineName = "Loom tests";
    config.headless = true;
    config.maxDescriptorSets = 128;
    config.rendererConfig.maxTimestamps = 32;
    LoomInitializer loom(config);

    report.check("kartica mjeri vrijeme", loom.renderer.measuresTime(),
        "bez timestamp queryja ovaj test nema sto mjeriti");
    if(!loom.renderer.measuresTime()){
        return report.result();
    }

    ImageConfig imageConfig;
    imageConfig.format = vk::Format::eR32G32B32A32Sfloat;
    imageConfig.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
    VulkanImage target(loom.device, size, imageConfig);

    // -------------------------------------------------------------------------------
    // Oblak: gust koliko treba da crtanje ima sto raditi, i uvijek isti
    // -------------------------------------------------------------------------------

    const uint32_t coeffsPerChannel = 15;
    const uint32_t degree = 3;
    const uint32_t bigCount = 80000;
    const uint32_t smallCount = bigCount / 4;

    std::mt19937 random(20260912);
    auto uniform = [&](float low, float high){
        return low + (high - low) * float(random() % 1000000) / 1000000.0f;
    };

    std::vector<SplatMath::RawSplat> raw;
    std::vector<float> rest;
    raw.reserve(bigCount);
    rest.reserve(size_t(bigCount) * coeffsPerChannel * 3);
    for(uint32_t i = 0; i < bigCount; ++i){
        const glm::vec3 position(uniform(-3.0f, 3.0f), uniform(-2.0f, 2.0f), uniform(-3.0f, 3.0f));
        const float base = uniform(0.02f, 0.16f);
        const glm::quat rotation = glm::normalize(glm::quat(uniform(-1,1), uniform(-1,1), uniform(-1,1), uniform(-1,1)));

        SplatMath::RawSplat one;
        one.positionOpacity = glm::vec4(position, uniform(0.1f, 0.9f));
        one.scale = glm::vec4(base, base * uniform(0.3f, 2.0f), base * uniform(0.3f, 2.0f), 0.0f);
        one.rotation = glm::vec4(rotation.w, rotation.x, rotation.y, rotation.z);
        one.dc = glm::vec4(uniform(-1.5f, 1.5f), uniform(-1.5f, 1.5f), uniform(-1.5f, 1.5f), 0.0f);
        raw.push_back(one);

        for(uint32_t k = 0; k < coeffsPerChannel * 3; ++k) rest.push_back(uniform(-0.3f, 0.3f));
    }

    const glm::vec3 eye(0.0f, 0.0f, 7.0f);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    const float focalX = 520.0f, focalY = -520.0f;

    auto makeRenderer = [&](uint32_t count){
        SplatRendererConfig rendererConfig;
        rendererConfig.maxSplats = count;
        rendererConfig.maxPairs = 4u << 20;
        rendererConfig.maxShCoefficients = coeffsPerChannel * 3;
        auto renderer = std::make_unique<SplatRenderer>(loom.device, loom.getDescriptorPool(), target, size, rendererConfig);
        renderer->uploadRaw(std::vector<SplatMath::RawSplat>(raw.begin(), raw.begin() + count),
                            std::vector<float>(rest.begin(), rest.begin() + size_t(count) * coeffsPerChannel * 3),
                            degree, coeffsPerChannel);
        renderer->setCamera(view, eye, focalX, focalY, 0.5f * size.width, 0.5f * size.height);
        return renderer;
    };

    //Jedan kadar, i vrijeme po koraku s kartice. Cijeli kadar je zbroj koraka: sve izmedju prve
    //i zadnje oznake, bez cekanja procesora
    struct Frame{
        std::vector<std::string> labels;
        std::vector<double> spans;
        double total = 0.0;
    };
    auto drawFrame = [&](SplatRenderer& renderer, uint32_t count){
        loom.renderer.beginFrame();
        renderer.prepare(loom.renderer, count);
        renderer.draw(loom.renderer, count);
        loom.renderer.endFrame();
        loom.waitIdle();

        Frame frame;
        const std::vector<GpuTimestamp> marks = loom.renderer.readFrameTimes();
        for(size_t i = 1; i < marks.size(); ++i){
            frame.labels.push_back(marks[i].label);
            frame.spans.push_back(marks[i].milliseconds - marks[i - 1].milliseconds);
        }
        if(!marks.empty()) frame.total = marks.back().milliseconds;
        return frame;
    };

    auto measure = [&](SplatRenderer& renderer, uint32_t count, int frames){
        std::vector<std::string> labels;
        std::vector<std::vector<double>> spans;
        std::vector<double> totals;
        for(int i = 0; i < frames; ++i){
            const Frame frame = drawFrame(renderer, count);
            if(labels.empty()){
                labels = frame.labels;
                spans.resize(labels.size());
            }
            for(size_t s = 0; s < frame.spans.size() && s < spans.size(); ++s) spans[s].push_back(frame.spans[s]);
            totals.push_back(frame.total);
        }
        std::vector<double> medians;
        for(auto& s : spans) medians.push_back(median(s));
        return std::make_tuple(labels, medians, median(totals), *std::min_element(totals.begin(), totals.end()));
    };

    auto big = makeRenderer(bigCount);
    auto small = makeRenderer(smallCount);

    //Zagrijavanje: prvi kadrovi grade cjevovode i descriptor setove
    for(int i = 0; i < 3; ++i){ drawFrame(*big, bigCount); drawFrame(*small, smallCount); }

    // -------------------------------------------------------------------------------
    // Kadar ne smije alocirati
    // -------------------------------------------------------------------------------

    const MemoryStats before = loom.device.getAllocator().getStats();
    VulkanAllocator::resetAllocationsMade();

    const int frames = 20;
    const auto [labels, spans, totalMedian, totalBest] = measure(*big, bigCount, frames);
    const auto [smallLabels, smallSpans, smallMedian, smallBest] = measure(*small, smallCount, frames);

    const MemoryStats after = loom.device.getAllocator().getStats();
    const uint64_t made = VulkanAllocator::getAllocationsMade();

    report.check("kadar ne alocira",
        made == 0 && after.allocationCount == before.allocationCount && after.blockCount == before.blockCount,
        fmt("%llu novih alokacija kroz %d kadrova; stanje %u -> %u alokacija, %u -> %u blokova",
            (unsigned long long)made, 2 * frames, before.allocationCount, after.allocationCount,
            before.blockCount, after.blockCount));

    // -------------------------------------------------------------------------------
    // Cijena po splatu ne smije rasti s brojem splatova
    // -------------------------------------------------------------------------------

    auto spanOf = [&](const std::vector<std::string>& names, const std::vector<double>& values, const std::string& name){
        for(size_t i = 0; i < names.size() && i < values.size(); ++i) if(names[i] == name) return values[i];
        return 0.0;
    };

    std::string trace;
    for(size_t i = 0; i < labels.size(); ++i) trace += fmt("%s %.2f ", labels[i].c_str(), spans[i]);

    //Priprema i sort po dubini su POSAO PO SPLATU. Cetiri puta vise splatova smije kostati
    //cetiri puta vise, pa je omjer cijene po splatu ono sto se gleda - kvadratna cijena bi ovdje
    //dala cetvorku
    const double preparePerSplat = spanOf(labels, spans, "priprema") / bigCount;
    const double preparePerSplatSmall = spanOf(smallLabels, smallSpans, "priprema") / smallCount;
    const double sortPerSplat = spanOf(labels, spans, "sort po dubini") / bigCount;
    const double sortPerSplatSmall = spanOf(smallLabels, smallSpans, "sort po dubini") / smallCount;

    const double prepareGrowth = preparePerSplatSmall > 0.0 ? preparePerSplat / preparePerSplatSmall : 0.0;
    const double sortGrowth = sortPerSplatSmall > 0.0 ? sortPerSplat / sortPerSplatSmall : 0.0;

    report.check("cijena je linearna u broju splatova",
        prepareGrowth > 0.0 && prepareGrowth < 2.0 && sortGrowth > 0.0 && sortGrowth < 2.0,
        fmt("priprema %.1f -> %.1f ns/splat (omjer %.2f), sort po dubini %.1f -> %.1f ns/splat (omjer %.2f), %u naspram %u splatova",
            1e6 * preparePerSplatSmall, 1e6 * preparePerSplat, prepareGrowth,
            1e6 * sortPerSplatSmall, 1e6 * sortPerSplat, sortGrowth, smallCount, bigCount));

    // -------------------------------------------------------------------------------
    // I strop, koji je broj o OVOM stroju
    // -------------------------------------------------------------------------------

    const uint32_t pairs = big->requestedPairs();
    const double perPair = 1e6 * spanOf(labels, spans, "sort po plocici") / double(pairs);

    //KOLIKO PAROVA SCENA NAPRAVI PO SPLATU. Isto na Intelu i na lavapipeu, jer je to posljedica
    //geometrije a ne kartice. Izmjereno 15.93; bez odbacivanja iz G7 skoci na 26.19, pa granica
    //od 18 hvata taj povratak na svakom uredjaju - i hvata ga ostrije nego ijedan strop vremena
    const double pairsPerSplat = double(pairs) / double(bigCount);

    report.check("parova po splatu ostaje nizak", pairsPerSplat < 18.0,
        fmt("%.2f parova po splatu (%u parova od %u splatova); bez odbacivanja iz G7 bilo bi 26.19",
            pairsPerSplat, pairs, bigCount));

    const vk::PhysicalDeviceType deviceType = loom.device.getDeviceType();
    const bool software = deviceType == vk::PhysicalDeviceType::eCpu;
    const bool integrated = deviceType == vk::PhysicalDeviceType::eIntegratedGpu;

    //IZMJERENO (12.09.2026.), po klasi uredjaja:
    //
    //                          kadar (najbolji)      sort po plocici
    //   Intel UHD (CML GT2)    40.97 41.00 40.99 ms      17.2 ns/par, sva tri puta
    //   llvmpipe (LLVM 21.1)   430.12 ms                138.8 ns/par
    //
    //STROP JE 1.35 PUTA IZNAD IZMJERENOG, a ne tri ili cetiri puta kao u B1 - i to je zasluga
    //mjerenja, ne hrabrosti: B1 mjeri stopericom na procesoru pa mu brojka skace, a ovdje vrijeme
    //dolazi s KARTICINOG sata i kroz cetiri pokretanja se razlikuje za 0.06 ms. Tako stegnut,
    //strop hvata i mutaciju koja izbaci odbacivanje parova (56.7 ms) - s dvostrukim stropom je
    //prolazila.
    //
    //CIJENA PO PARU se pritom NE mijenja (17.1 naspram 17.2 ns): parova ima vise, a svaki kosta
    //isto. Zato je taj strop drugi broj i lovi drugu vrstu regresije - sporiji sort, ne vise posla
    //
    //DISKRETNA KARTICA NIJE MJERENA. Dobiva isti broj kao integrirana, sto je sigurno prelabavo
    //(integrirana dijeli memoriju s procesorom i crta desktop). Ne izmisljam broj koji nisam
    //vidio - ceka RTX 5070, kao i cetiri VRS testa
    const double ceiling = software ? 580.0 : integrated ? 55.0 : 55.0;
    const double pairCeiling = software ? 280.0 : integrated ? 35.0 : 35.0;

    report.check("kadar ostaje unutar stropa",
        totalBest < ceiling,
        fmt("%s: najbolji %.2f ms, medijan %.2f, %u splatova i %u parova, strop %.1f ms | %s",
            loom.device.getDeviceName().c_str(), totalBest, totalMedian, bigCount, pairs, ceiling, trace.c_str()));

    report.check("sort po plocici ostaje unutar stropa",
        perPair > 0.0 && perPair < pairCeiling,
        fmt("%.1f ns po paru (%u parova), strop %.1f", perPair, pairs, pairCeiling));

    report.checkNoValidationMessages();
    return report.result();
}
