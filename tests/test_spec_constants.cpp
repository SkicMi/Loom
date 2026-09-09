// Pipeline prima specijalizacijske konstante.
//
// Nije dio splattinga nego preduvjet za njega: velicina pločice u rasterizatoru JEST velicina
// radne grupe, a ona se u Vulkanu ne da poslati push constantom. Bez ovoga bi jedina mogucnost
// bila prevesti isti shader jednom po velicini.
//
// Test mjeri dvije razlicite stvari, i to je cijela poanta:
//
//   sto konstanta KAZE    shader zapise vrijednost - ovo bi radilo i da je push constant
//   koliko dretvi RADI    svaka dretva se broji, pa zbroj kaze koliko ih je stvarno bilo. Ovo
//                         push constant ne bi mogao, jer se velicina radne grupe odredjuje pri
//                         stvaranju pipelinea i vise se ne mijenja
//
// Kad bi driver konstantu ignorirao i uzeo vrijednost iz shadera (8), druga provjera bi to
// vidjela odmah - a prva ne bi.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Vulkan/ComputeMaterial.h"

#include <string>
#include <vector>

namespace{

struct Params{
    uint32_t slot = 0;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};

}

int main(){
    TestReport report("specijalizacijske konstante");

    LoomConfig config;
    config.width = 64; config.height = 64;
    config.appName = "spec"; config.engineName = "Loom tests";
    config.headless = true;
    LoomInitializer loom(config);

    const std::vector<uint32_t> sizes = {4, 8, 16, 32, 64};
    const uint32_t slots = uint32_t(sizes.size()) * 2;

    VulkanBuffer results(loom.device, slots * sizeof(uint32_t),
                         vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc |
                         vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_ONLY);
    VulkanBuffer zeroes(loom.device, slots * sizeof(uint32_t),
                        vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
    VulkanBuffer readback(loom.device, slots * sizeof(uint32_t),
                          vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);

    const std::vector<uint32_t> zeroData(slots, 0);
    zeroes.upload(zeroData.data(), slots * sizeof(uint32_t));
    loom.command.copyBuffer(zeroes.getBuffer(), results.getBuffer(), slots * sizeof(uint32_t));

    vk::DescriptorSetLayoutBinding binding;
    binding.binding = 0;
    binding.descriptorType = vk::DescriptorType::eStorageBuffer;
    binding.descriptorCount = 1;
    binding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    //Jedan SPIR-V, pet pipelinea. To je cijela tvrdnja
    std::vector<VulkanComputePipeline> pipelines;
    std::vector<ComputeMaterial> materials;
    pipelines.reserve(sizes.size());
    materials.reserve(sizes.size());

    for(uint32_t size : sizes){
        ComputePipelineConfig pipelineConfig;
        pipelineConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/specconstant.comp.spv";
        pipelineConfig.descriptorBindings = {binding};
        pipelineConfig.pushConstantSize = sizeof(Params);
        pipelineConfig.specializationConstants = {size};
        pipelines.push_back(loom.createComputePipeline(pipelineConfig));
    }
    for(VulkanComputePipeline& pipeline : pipelines){
        materials.emplace_back(loom.device, loom.getDescriptorPool(), pipeline);
        materials.back().setStorageBuffer(0, results);
    }

    loom.renderer.beginFrame();
    for(uint32_t i = 0; i < sizes.size(); ++i){
        Params params;
        params.slot = i;
        loom.renderer.dispatch(materials[i], 1, 1, 1, &params, sizeof(params));
    }
    loom.renderer.endFrame();
    loom.waitIdle();

    loom.command.copyBuffer(results.getBuffer(), readback.getBuffer(), slots * sizeof(uint32_t));
    std::vector<uint32_t> got(slots);
    readback.download(got.data(), slots * sizeof(uint32_t));

    std::string threadsTrace, valueTrace;
    bool threadsHold = true, valuesHold = true;
    for(uint32_t i = 0; i < sizes.size(); ++i){
        threadsTrace += fmt("%u->%u ", sizes[i], got[i*2 + 0]);
        valueTrace += fmt("%u->%u ", sizes[i], got[i*2 + 1]);
        if(got[i*2 + 0] != sizes[i]) threadsHold = false;
        if(got[i*2 + 1] != sizes[i]) valuesHold = false;
    }

    report.check("konstanta stigne do koda", valuesHold, valueTrace.c_str());
    report.check("konstanta stigne do radne grupe", threadsHold,
        fmt("trazeno -> koliko dretvi se javilo: %s", threadsTrace.c_str()));

    //KONTROLA: da su sve velicine ispale iste, gornje dvije provjere bi prosle i da je
    //konstanta ignorirana - jer je zadana vrijednost u shaderu 8, a 8 je u nizu
    report.check("velicine su stvarno razlicite",
        got[0] != got[2] && got[2] != got[4] && got[0] == 4,
        fmt("trazene 4, 8 i 16 dale su %u, %u i %u dretvi", got[0], got[2], got[4]));

    report.checkNoValidationMessages();
    return report.result();
}
