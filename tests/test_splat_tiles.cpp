// G3b: rasterizator s pločicama, mjeren protiv grube sile.
//
// Sve sto G3b uvodi je STROJERIJA: okvir u pločicama, brojanje, pomaci, sirenje u parove, dva
// sorta, rasponi. Nijedan od tih koraka ne mijenja sliku - svi zajedno samo postizu da svaka
// pločica gleda samo splatove koji je diraju. Zato je jedina posteno jaka tvrdnja ova:
//
//   ISTA SLIKA KAO GRUBA SILA, BAJT ZA BAJT
//
// i to u floatu, dakle bez ijedne izlike o zaokruzivanju. Gruba sila je dokazana analiticki u
// G3a (vrh, masa, poredak, zaklon), pa se ovdje ne dokazuje ponovno.
//
// Uz to tri stvari koje samo ta usporedba ne bi uhvatila:
//
//   velicina pločice   8, 16 i 32 moraju dati ISTU sliku. Pločica je podjela posla, ne dio
//                      matematike - ako se slika mijenja s njom, negdje se gleda pogresan skup
//   brojanje           procesor i kartica broje pločice neovisno i moraju dati isti broj. Da
//                      se razidju, zbroj bi rezervirao jedan broj mjesta a sirenje zapisalo
//                      drugi, i to bi izgledalo kao greska bilo gdje drugdje
//   kontrola           bez odsijecanja na tri sigme slike se MORAJU razlikovati, i to bas na
//                      repovima. Inace "ista slika" ne bi znacila da odsijecanje uopce radi
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/ComputeMaterial.h"
#include "Vulkan/SplatRenderer.h"
#include "Vulkan/VulkanImage.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <string>
#include <vector>

namespace{

struct BruteParams{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t count = 0;
    uint32_t useRadius = 0;
};

struct Pixel{
    float r = 0, g = 0, b = 0, a = 0;
};

}

int main(){
    TestReport report("G3b rasterizator s pločicama");

    //Namjerno NIJE visekratnik nijedne velicine pločice: 200 nije djeljivo s 16 ni s 32, pa
    //zadnji red i stupac pločica vire izvan slike. Tamo se rubni piksel lako izgubi
    const vk::Extent2D size{200, 150};

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "tiles"; config.engineName = "Loom tests";
    config.headless = true;
    LoomInitializer loom(config);

    ImageConfig imageConfig;
    imageConfig.format = vk::Format::eR32G32B32A32Sfloat;
    imageConfig.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
    VulkanImage bruteTarget(loom.device, size, imageConfig);
    VulkanImage tileTarget(loom.device, size, imageConfig);

    const vk::DeviceSize pixelBytes = vk::DeviceSize(size.width) * size.height * sizeof(Pixel);
    VulkanBuffer readback(loom.device, pixelBytes, vk::BufferUsageFlagBits::eTransferDst,
                          MemoryUsage::GPU_TO_CPU);

    // -------------------------------------------------------------------------------
    // Scena: splatovi razasuti po prostoru, razlicitih velicina i dubina
    // -------------------------------------------------------------------------------

    const uint32_t splatCount = 800;
    const float focal = 300.0f;
    const glm::mat4 view = glm::lookAt(glm::vec3(0,0,0), glm::vec3(0,0,-1), glm::vec3(0,1,0));

    std::vector<SplatMath::PreparedSplat> prepared;
    prepared.reserve(splatCount);

    std::mt19937 random(20260909);
    auto uniform = [&](float low, float high){
        return low + (high - low) * float(random() % 100000) / 100000.0f;
    };

    for(uint32_t i = 0; i < splatCount; ++i){
        Splat splat;
        //Dubine od blizu do daleko, pa poredak stvarno ima sto presloziti
        const float depth = uniform(1.0f, 12.0f);
        splat.position = glm::vec3(uniform(-3.0f, 3.0f), uniform(-2.5f, 2.5f), -depth);

        //Od mrlja manjih od piksela do onih koje pokrivaju pola kadra
        const float radius = uniform(0.01f, 0.12f);
        splat.scale = glm::vec3(radius, radius * uniform(0.4f, 1.6f), radius * uniform(0.4f, 1.6f));
        splat.rotation = glm::normalize(glm::quat(uniform(-1,1), uniform(-1,1), uniform(-1,1), uniform(-1,1)));
        splat.opacity = uniform(0.05f, 0.8f);
        splat.color = glm::vec3(uniform(0,1), uniform(0,1), uniform(0,1));

        SplatMath::PreparedSplat one;
        if(SplatMath::prepare(splat, view, focal, -focal,
                              0.5f * size.width, 0.5f * size.height, 0.3f, one)){
            prepared.push_back(one);
        }
    }

    //REFERENCA MORA BITI SORTIRANA. Gruba sila slaze splatove redom kojim su predani, a
    //pločice ih slazu po dubini - dvije slike se ne daju usporediti dok taj red nije isti.
    //Prva verzija ovog testa to nije radila i svih 30000 piksela se razlikovalo
    std::stable_sort(prepared.begin(), prepared.end(),
        [](const SplatMath::PreparedSplat& a, const SplatMath::PreparedSplat& b){
            return a.conicOpacityDepth.z < b.conicOpacityDepth.z;
        });

    report.check("scena je scena",
        prepared.size() > uint32_t(0.8f * splatCount),
        fmt("%zu splatova od %u je vidljivo, poredani sprijeda natrag", prepared.size(), splatCount));

    // -------------------------------------------------------------------------------
    // Gruba sila, s odsijecanjem - to je referenca
    // -------------------------------------------------------------------------------

    VulkanBuffer bruteBuffer(loom.device, prepared.size() * sizeof(SplatMath::PreparedSplat),
                             vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::CPU_TO_GPU);
    bruteBuffer.upload(prepared.data(), prepared.size() * sizeof(SplatMath::PreparedSplat));

    vk::DescriptorSetLayoutBinding bufferBinding;
    bufferBinding.binding = 0;
    bufferBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    bufferBinding.descriptorCount = 1;
    bufferBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding imageBinding = bufferBinding;
    imageBinding.binding = 1;
    imageBinding.descriptorType = vk::DescriptorType::eStorageImage;

    ComputePipelineConfig bruteConfig;
    bruteConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/splat_composite.comp.spv";
    bruteConfig.descriptorBindings = {bufferBinding, imageBinding};
    bruteConfig.pushConstantSize = sizeof(BruteParams);
    VulkanComputePipeline brutePipeline = loom.createComputePipeline(bruteConfig);

    ComputeMaterial bruteMaterial(loom.device, loom.getDescriptorPool(), brutePipeline);
    bruteMaterial.setStorageBuffer(0, bruteBuffer);
    bruteMaterial.setStorageImage(1, bruteTarget, vk::ImageLayout::eTransferSrcOptimal);

    auto readImage = [&](VulkanImage& image){
        loom.command.copyImageToBuffer(image.getImage(), readback.getBuffer(), size);
        std::vector<Pixel> pixels(size_t(size.width) * size.height);
        readback.download(pixels.data(), pixelBytes);
        return pixels;
    };

    auto drawBrute = [&](bool useRadius){
        BruteParams params;
        params.width = size.width; params.height = size.height;
        params.count = uint32_t(prepared.size());
        params.useRadius = useRadius ? 1u : 0u;

        loom.renderer.beginFrame();
        loom.renderer.dispatch(bruteMaterial, (size.width + 7) / 8, (size.height + 7) / 8, 1,
                               &params, sizeof(params));
        loom.renderer.endFrame();
        loom.waitIdle();
        return readImage(bruteTarget);
    };

    const std::vector<Pixel> reference = drawBrute(true);
    const std::vector<Pixel> noCutoff = drawBrute(false);

    // -------------------------------------------------------------------------------
    // Kontrola: odsijecanje mora nesto raditi
    // -------------------------------------------------------------------------------

    {
        size_t different = 0;
        double worst = 0.0;
        for(size_t i = 0; i < reference.size(); ++i){
            const double delta = std::fabs(double(reference[i].a) - double(noCutoff[i].a));
            if(delta > 0.0) ++different;
            worst = std::max(worst, delta);
        }

        report.check("odsijecanje na tri sigme se vidi",
            different > 0,
            fmt("%zu od %zu piksela se razlikuje bez odsijecanja, najvise za %.6f",
                different, reference.size(), worst));
    }

    // -------------------------------------------------------------------------------
    // I sad pločice, u tri velicine
    // -------------------------------------------------------------------------------

    std::string sizesTrace;
    std::string countsTrace;
    bool allIdentical = true;
    bool allCountsAgree = true;
    std::vector<Pixel> firstTiled;

    for(uint32_t tileSize : {8u, 16u, 32u}){
        SplatRendererConfig rendererConfig;
        rendererConfig.tileSize = tileSize;
        rendererConfig.maxSplats = uint32_t(prepared.size());
        rendererConfig.maxPairs = 1u << 20;

        SplatRenderer splatRenderer(loom.device, loom.getDescriptorPool(), tileTarget, size, rendererConfig);
        splatRenderer.upload(prepared);

        const uint32_t pairCount = splatRenderer.countPairs(prepared);

        loom.renderer.beginFrame();
        splatRenderer.draw(loom.renderer, uint32_t(prepared.size()), pairCount);
        loom.renderer.endFrame();
        loom.waitIdle();

        const std::vector<Pixel> tiled = readImage(tileTarget);

        size_t different = 0;
        double worst = 0.0;
        for(size_t i = 0; i < reference.size(); ++i){
            if(std::memcmp(&reference[i], &tiled[i], sizeof(Pixel)) != 0) ++different;
            worst = std::max(worst, std::fabs(double(reference[i].a) - double(tiled[i].a)));
        }
        if(different != 0) allIdentical = false;

        sizesTrace += fmt("%ux%u:%zu ", tileSize, tileSize, different);

        //Kartica je brojala pločice u istom kadru; procesor ih je brojao prije njega
        VulkanBuffer offsetsBack(loom.device, prepared.size() * sizeof(uint32_t),
                                 vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
        loom.command.copyBuffer(splatRenderer.getOffsets().getBuffer(), offsetsBack.getBuffer(),
                                prepared.size() * sizeof(uint32_t));
        std::vector<uint32_t> offsets(prepared.size());
        offsetsBack.download(offsets.data(), prepared.size() * sizeof(uint32_t));

        //Procesorovi pomaci, slozeni iz njegovih vlastitih brojeva. Kartica nije nikad vidjela
        //nijedan od njih - dva neovisna racuna istog broja, mjesto po mjesto
        const std::vector<uint32_t> cpuCounts = splatRenderer.tileCounts(prepared);
        uint32_t running = 0;
        size_t mismatched = 0;
        for(size_t i = 0; i < cpuCounts.size(); ++i){
            if(offsets[i] != running) ++mismatched;
            running += cpuCounts[i];
        }
        if(mismatched != 0 || running != pairCount) allCountsAgree = false;
        countsTrace += fmt("%u:%zu ", tileSize, mismatched);

        if(tileSize == 8) firstTiled = tiled;
        else if(different == 0 && firstTiled.size() == tiled.size()){
            for(size_t i = 0; i < tiled.size(); ++i){
                if(std::memcmp(&firstTiled[i], &tiled[i], sizeof(Pixel)) != 0){
                    allIdentical = false;
                    break;
                }
            }
        }

        (void)worst;
    }

    report.check("ista slika kao gruba sila", allIdentical,
        fmt("piksela razlike po velicini pločice: %s", sizesTrace.c_str()));

    report.check("procesor i kartica broje isto", allCountsAgree,
        fmt("pomaka koji se razlikuju, po velicini pločice: %s", countsTrace.c_str()));

    // -------------------------------------------------------------------------------
    // I da slika uopce nije prazna
    // -------------------------------------------------------------------------------

    {
        size_t touched = 0;
        double total = 0.0;
        for(const Pixel& p : reference){
            if(p.a > 0.0f) ++touched;
            total += p.a;
        }
        report.check("scena je nacrtana",
            touched > reference.size() / 4,
            fmt("%zu od %zu piksela ima nesto, ukupna alfa %.1f", touched, reference.size(), total));
    }

    report.checkNoValidationMessages();
    return report.result();
}
