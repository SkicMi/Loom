// v6d: atomics into shared bins, a workgroup reduction over them, and the window read back
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Vulkan/ComputeMaterial.h"

struct ImageParams{ uint32_t sizeX, sizeY, mode, padding0; };
struct HistogramParams{ uint32_t sizeX, sizeY, mode, padding0; };
struct ExposureParams{ float key, p0, p1, p2; };

int main(){
    TestReport report("v6d histogram and reduction");

    LoomConfig config;
    config.width = 256; config.height = 256;
    config.appName = "v6d"; config.engineName = "Loom tests";
    config.rendererConfig.clearColor = {0.25f, 0.5f, 0.75f, 1.0f}; //so the window has content to check

    LoomInitializer loom(config);

    const uint32_t width = 517, height = 301, regionW = 300, regionH = 200;
    const vk::Extent2D extent{width, height};
    const uint32_t bins = 256;

    ImageConfig storageConfig;
    storageConfig.format = vk::Format::eR8G8B8A8Unorm;
    storageConfig.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
    VulkanImage pattern(loom.device, extent, storageConfig);

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    vk::raii::Sampler sampler(loom.device.getDevice(), samplerInfo);

    SampledImage patternSampled;
    patternSampled.view = *pattern.getImageView();
    patternSampled.sampler = *sampler;

    const vk::BufferUsageFlags bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer
                                           | vk::BufferUsageFlagBits::eTransferSrc
                                           | vk::BufferUsageFlagBits::eTransferDst;
    VulkanBuffer histogram(loom.device, bins * sizeof(uint32_t), bufferUsage, MemoryUsage::GPU_ONLY);
    VulkanBuffer exposure(loom.device, 2 * sizeof(float), bufferUsage, MemoryUsage::GPU_ONLY);
    VulkanBuffer staging(loom.device, bins * sizeof(uint32_t),
        vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);

    std::vector<uint32_t> zeros(bins, 0);
    staging.upload(zeros.data(), bins * sizeof(uint32_t));
    loom.command.copyBuffer(staging.getBuffer(), histogram.getBuffer(), bins * sizeof(uint32_t));

    vk::DescriptorSetLayoutBinding imageBinding;
    imageBinding.binding = 0;
    imageBinding.descriptorType = vk::DescriptorType::eStorageImage;
    imageBinding.descriptorCount = 1;
    imageBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding sourceBinding = imageBinding;
    sourceBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    vk::DescriptorSetLayoutBinding bufferBinding = imageBinding;
    bufferBinding.binding = 1;
    bufferBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    vk::DescriptorSetLayoutBinding histogramIn = bufferBinding; histogramIn.binding = 0;

    ComputePipelineConfig writeConfig;
    writeConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/imagewrite.comp.spv";
    writeConfig.descriptorBindings = {imageBinding};
    writeConfig.pushConstantSize = sizeof(ImageParams);
    VulkanComputePipeline writePipeline = loom.createComputePipeline(writeConfig);

    ComputePipelineConfig histogramConfig;
    histogramConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/histogram.comp.spv";
    histogramConfig.descriptorBindings = {sourceBinding, bufferBinding};
    histogramConfig.pushConstantSize = sizeof(HistogramParams);
    VulkanComputePipeline histogramPipeline = loom.createComputePipeline(histogramConfig);

    ComputePipelineConfig exposureConfig;
    exposureConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/exposure.comp.spv";
    exposureConfig.descriptorBindings = {histogramIn, bufferBinding};
    exposureConfig.pushConstantSize = sizeof(ExposureParams);
    VulkanComputePipeline exposurePipeline = loom.createComputePipeline(exposureConfig);

    ComputeMaterial write(loom.device, loom.getDescriptorPool(), writePipeline);
    write.setStorageImage(0, pattern, vk::ImageLayout::eShaderReadOnlyOptimal);

    ComputeMaterial count(loom.device, loom.getDescriptorPool(), histogramPipeline);
    count.setSampledImage(0, patternSampled);
    count.setStorageBuffer(1, histogram);

    ComputeMaterial reduce(loom.device, loom.getDescriptorPool(), exposurePipeline);
    reduce.setStorageBuffer(0, histogram);
    reduce.setStorageBuffer(1, exposure);

    ImageParams whole{width, height, 0, 0};
    ImageParams region{regionW, regionH, 1, 0};
    HistogramParams redChannel{width, height, 0, 0};
    ExposureParams key{0.18f, 0, 0, 0};
    const uint32_t gx = (width + 7) / 8, gy = (height + 7) / 8;
    const uint32_t rx = (regionW + 7) / 8, ry = (regionH + 7) / 8;

    int drawn = 0;
    while(drawn < 1 && !loom.shouldClose()){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.dispatch(write, gx, gy, 1, &whole, sizeof(whole));
        loom.renderer.dispatch(write, rx, ry, 1, &region, sizeof(region));
        loom.renderer.dispatch(count, gx, gy, 1, &redChannel, sizeof(redChannel));
        loom.renderer.dispatch(reduce, 1, 1, 1, &key, sizeof(key));

        loom.renderer.beginPass();
        loom.renderer.endPass();
        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const ImageData window = loom.renderer.readLastFrame();

    loom.command.copyBuffer(histogram.getBuffer(), staging.getBuffer(), bins * sizeof(uint32_t));
    std::vector<uint32_t> gpuBins(bins, 0);
    staging.download(gpuBins.data(), bins * sizeof(uint32_t));

    VulkanBuffer floatStaging(loom.device, 2 * sizeof(float), vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
    loom.command.copyBuffer(exposure.getBuffer(), floatStaging.getBuffer(), 2 * sizeof(float));
    float gpuExposure[2] = {0.0f, 0.0f};
    floatStaging.download(gpuExposure, 2 * sizeof(float));

    const std::vector<uint8_t> cpuPattern = patternPixels(width, height, regionW, regionH);
    std::vector<uint32_t> cpuBins(bins, 0);
    for(size_t p = 0; p * 4 < cpuPattern.size(); ++p){
        ++cpuBins[cpuPattern[p*4]];
    }

    size_t total = 0;
    for(uint32_t b = 0; b < bins; ++b) total += gpuBins[b];
    report.check("zbroj binova", total == size_t(width) * height,
        fmt("%zu od %zu piksela", total, size_t(width) * height));

    size_t binsWrong = 0;
    for(uint32_t b = 0; b < bins; ++b){
        if(gpuBins[b] != cpuBins[b]) ++binsWrong;
    }
    report.check("bin po bin", binsWrong == 0, fmt("%zu od %u binova se razlikuje", binsWrong, bins));

    double weighted = 0.0, counted = 0.0;
    for(uint32_t b = 0; b < bins; ++b){ weighted += double(b) * cpuBins[b]; counted += cpuBins[b]; }
    const double cpuAverage = weighted / counted / 255.0;
    const double difference = fabs(double(gpuExposure[0]) - cpuAverage);
    report.check("redukcija", difference < 1e-6,
        fmt("GPU %.9f, CPU %.9f, razlika %.1e", gpuExposure[0], cpuAverage, difference));

    //the window was cleared to a known linear colour and the swapchain is sRGB
    const uint8_t wantB = encodeByte(0.75), wantG = encodeByte(0.50), wantR = encodeByte(0.25);
    size_t windowWrong = 0;
    for(size_t i = 0; i + 3 < window.pixels.size(); i += 4){
        if(window.pixels[i] != wantB || window.pixels[i+1] != wantG || window.pixels[i+2] != wantR) ++windowWrong;
    }
    report.check("prozor", windowWrong == 0 && window.pixels.size() > 0,
        fmt("%ux%u, ocekivano BGR %u,%u,%u, %zu piksela odstupa",
            window.extent.width, window.extent.height, wantB, wantG, wantR, windowWrong));

    report.checkNoValidationMessages();
    return report.result();
}
