// v6a: a dispatch writes a buffer, and stops exactly where it was told to
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Vulkan/ComputeMaterial.h"

int main(){
    TestReport report("v6a compute buffer");

    LoomConfig config;
    config.width = 256; config.height = 256;
    config.appName = "v6a"; config.engineName = "Loom tests";

    LoomInitializer loom(config);

    const uint32_t count = 1000003;   //neither a multiple of the workgroup nor a power of two
    const uint32_t tail = 64;
    const size_t total = size_t(count) + tail;
    const vk::DeviceSize bytes = vk::DeviceSize(total) * sizeof(uint32_t);
    const uint32_t sentinel = 0xDEADBEEFu;

    VulkanBuffer target(loom.device, bytes,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
        MemoryUsage::GPU_ONLY);
    //Written once and read back later, so GPU_TO_CPU: that asks for cached memory, which is
    //readable at a sane speed. CPU_TO_GPU would promise VMA the CPU never reads it
    VulkanBuffer staging(loom.device, bytes,
        vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
        MemoryUsage::GPU_TO_CPU);

    std::vector<uint32_t> initial(total, sentinel);
    staging.upload(initial.data(), bytes);
    loom.command.copyBuffer(staging.getBuffer(), target.getBuffer(), bytes);

    vk::DescriptorSetLayoutBinding outputBinding;
    outputBinding.binding = 0;
    outputBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    outputBinding.descriptorCount = 1;
    outputBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    ComputePipelineConfig fillConfig;
    fillConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/fill.comp.spv";
    fillConfig.descriptorBindings = {outputBinding};
    fillConfig.pushConstantSize = sizeof(uint32_t);
    VulkanComputePipeline fillPipeline = loom.createComputePipeline(fillConfig);

    ComputeMaterial fill(loom.device, loom.getDescriptorPool(), fillPipeline);
    fill.setStorageBuffer(0, target);

    const uint32_t localSize = 64;
    const uint32_t groups = (count + localSize - 1) / localSize;

    bool guardThrew = false;
    int drawn = 0;
    while(drawn < 1 && !loom.window.shouldClose()){
        loom.window.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.dispatch(fill, groups, 1, 1, &count, sizeof(count));

        loom.renderer.beginPass();
        try{
            //compute inside a render pass is illegal, and the library says so itself
            loom.renderer.dispatch(fill, 1, 1, 1, &count, sizeof(count));
        }
        catch(const std::exception&){
            guardThrew = true;
        }
        loom.renderer.endPass();

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    loom.command.copyBuffer(target.getBuffer(), staging.getBuffer(), bytes);
    std::vector<uint32_t> got(total, 0);
    staging.download(got.data(), bytes);

    size_t wrong = 0;
    for(uint32_t i = 0; i < count; ++i){
        if(got[i] != i * 2654435761u) ++wrong;
    }
    report.check("formula", wrong == 0, fmt("%u elemenata, %zu krivih", count, wrong));

    //the last group carries 61 idle threads, so an untouched tail is what proves the guard
    size_t touched = 0;
    for(size_t i = count; i < total; ++i){
        if(got[i] != sentinel) ++touched;
    }
    report.check("granica", touched == 0,
        fmt("%u elemenata iza kraja, %zu dirnutih, %u praznih niti u zadnjoj grupi",
            tail, touched, groups * localSize - count));

    report.check("dispatch u prolazu", guardThrew, "baca iznimku");

    report.checkNoValidationMessages();
    return report.result();
}
