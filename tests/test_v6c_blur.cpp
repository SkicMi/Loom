// v6c: a dispatch reads one image and writes another
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Vulkan/ComputeMaterial.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/RenderTarget.h"
#include "Core/Camera.h"
#include "Core/Environment.h"
#include <glm/gtc/matrix_transform.hpp>

struct ImageParams{ uint32_t sizeX, sizeY, mode, padding0; };
struct BlurParams{ uint32_t sizeX, sizeY; };

int main(){
    TestReport report("v6c compute blur");

    LoomConfig config;
    config.width = 512; config.height = 512;
    config.appName = "v6c"; config.engineName = "Loom tests";
    config.enableDepth = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    const uint32_t width = 517, height = 301, regionW = 300, regionH = 200;
    const vk::Extent2D extent{width, height};

    ImageConfig storageConfig;
    storageConfig.format = vk::Format::eR8G8B8A8Unorm;
    storageConfig.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;

    VulkanImage pattern(loom.device, extent, storageConfig);
    VulkanImage blurredOnce(loom.device, extent, storageConfig);
    VulkanImage blurredStorage(loom.device, extent, storageConfig);
    VulkanImage blurredChain(loom.device, extent, storageConfig);
    VulkanImage blurredTwice(loom.device, extent, storageConfig);
    VulkanImage blurredScene(loom.device, extent, storageConfig);

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    vk::raii::Sampler sampler(loom.device.getDevice(), samplerInfo);

    auto sampled = [&](const VulkanImage& image){
        SampledImage s; s.view = *image.getImageView(); s.sampler = *sampler; return s;
    };

    vk::DescriptorSetLayoutBinding storageBinding;
    storageBinding.binding = 0;
    storageBinding.descriptorType = vk::DescriptorType::eStorageImage;
    storageBinding.descriptorCount = 1;
    storageBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding sourceBinding = storageBinding;
    sourceBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    vk::DescriptorSetLayoutBinding destBinding = storageBinding;
    destBinding.binding = 1;

    ComputePipelineConfig writeConfig;
    writeConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/imagewrite.comp.spv";
    writeConfig.descriptorBindings = {storageBinding};
    writeConfig.pushConstantSize = sizeof(ImageParams);
    VulkanComputePipeline writePipeline = loom.createComputePipeline(writeConfig);

    ComputePipelineConfig blurConfig;
    blurConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/blur.comp.spv";
    blurConfig.descriptorBindings = {sourceBinding, destBinding};
    blurConfig.pushConstantSize = sizeof(BlurParams);
    VulkanComputePipeline blurPipeline = loom.createComputePipeline(blurConfig);

    ComputePipelineConfig blurStorageConfig = blurConfig;
    blurStorageConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/blurstorage.comp.spv";
    blurStorageConfig.descriptorBindings = {storageBinding, destBinding};
    VulkanComputePipeline blurStoragePipeline = loom.createComputePipeline(blurStorageConfig);

    ComputeMaterial write(loom.device, loom.getDescriptorPool(), writePipeline);
    write.setStorageImage(0, pattern, vk::ImageLayout::eShaderReadOnlyOptimal);

    ComputeMaterial blurOnce(loom.device, loom.getDescriptorPool(), blurPipeline);
    blurOnce.setSampledImage(0, sampled(pattern));
    blurOnce.setStorageImage(1, blurredOnce, vk::ImageLayout::eTransferSrcOptimal);

    ComputeMaterial blurViaStorage(loom.device, loom.getDescriptorPool(), blurStoragePipeline);
    blurViaStorage.setStorageImage(0, pattern, vk::ImageLayout::eShaderReadOnlyOptimal);
    blurViaStorage.setStorageImage(1, blurredStorage, vk::ImageLayout::eTransferSrcOptimal);

    ComputeMaterial blurChain(loom.device, loom.getDescriptorPool(), blurPipeline);
    blurChain.setSampledImage(0, sampled(pattern));
    blurChain.setStorageImage(1, blurredChain, vk::ImageLayout::eShaderReadOnlyOptimal);

    ComputeMaterial blurTwice(loom.device, loom.getDescriptorPool(), blurPipeline);
    blurTwice.setSampledImage(0, sampled(blurredChain));
    blurTwice.setStorageImage(1, blurredTwice, vk::ImageLayout::eTransferSrcOptimal);

    //the useful case: a rendered scene blurred by compute
    Mesh cube(loom.device, loom.command, cubeVertices(), cubeIndices());
    CameraConfig camConfig; camConfig.position = {0.0f,0.0f,3.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);
    EnvironmentConfig envConfig; envConfig.ambientColor = {0.05f,0.05f,0.05f};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);
    LightConfig lightConfig;
    Light light(lightConfig);
    loom.renderer.addLight(light);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    RenderTarget sceneRead(loom.device, extent, readConfig);
    RenderTarget sceneSampled(loom.device, extent);

    ComputeMaterial blurScene(loom.device, loom.getDescriptorPool(), blurPipeline);
    blurScene.setSampledImage(0, sceneSampled.getSampled());
    blurScene.setStorageImage(1, blurredScene, vk::ImageLayout::eTransferSrcOptimal);

    ImageParams whole{width, height, 0, 0};
    ImageParams region{regionW, regionH, 1, 0};
    BlurParams blurAll{width, height};
    const uint32_t gx = (width + 7) / 8, gy = (height + 7) / 8;
    const uint32_t rx = (regionW + 7) / 8, ry = (regionH + 7) / 8;
    const glm::mat4 model = glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0.5f,1.0f,0.0f));

    int drawn = 0;
    while(drawn < 2 && !loom.window.shouldClose()){
        loom.window.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.dispatch(write, gx, gy, 1, &whole, sizeof(whole));
        loom.renderer.dispatch(write, rx, ry, 1, &region, sizeof(region));

        loom.renderer.dispatch(blurOnce, gx, gy, 1, &blurAll, sizeof(blurAll));
        loom.renderer.dispatch(blurViaStorage, gx, gy, 1, &blurAll, sizeof(blurAll));
        loom.renderer.dispatch(blurChain, gx, gy, 1, &blurAll, sizeof(blurAll));
        loom.renderer.dispatch(blurTwice, gx, gy, 1, &blurAll, sizeof(blurAll));

        loom.renderer.beginPass(sceneRead);
        loom.renderer.draw(cube, model);
        loom.renderer.endPass();

        loom.renderer.beginPass(sceneSampled);
        loom.renderer.draw(cube, model);
        loom.renderer.endPass();

        loom.renderer.dispatch(blurScene, gx, gy, 1, &blurAll, sizeof(blurAll));

        loom.renderer.beginPass();
        loom.renderer.endPass();

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const std::vector<uint8_t> cpuPattern = patternPixels(width, height, regionW, regionH);
    const std::vector<uint8_t> cpuOnce = blurBytes(cpuPattern, width, height);
    const std::vector<uint8_t> cpuTwice = blurBytes(cpuOnce, width, height);

    const std::vector<uint8_t> gpuOnce = readImagePixels(loom, blurredOnce, extent);
    const std::vector<uint8_t> gpuStorage = readImagePixels(loom, blurredStorage, extent);
    const std::vector<uint8_t> gpuTwice = readImagePixels(loom, blurredTwice, extent);
    const std::vector<uint8_t> gpuScene = readImagePixels(loom, blurredScene, extent);

    //this hardware does not round float to unorm exactly at .5, so a blur cannot be byte
    //exact. What can be demanded is that every disagreement sits on that boundary
    const ByteDiff once = diffBytes(cpuOnce, gpuOnce);
    size_t onBoundary = 0, elsewhere = 0;
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            for(uint32_t c = 0; c < 4; ++c){
                size_t i = (size_t(y) * width + x) * 4 + c;
                if(cpuOnce[i] == gpuOnce[i]) continue;
                uint32_t sum = 0;
                for(int dy = -1; dy <= 1; ++dy){
                    for(int dx = -1; dx <= 1; ++dx){
                        int tx = int(x)+dx, ty = int(y)+dy;
                        tx = tx<0?0:(tx>int(width)-1?int(width)-1:tx);
                        ty = ty<0?0:(ty>int(height)-1?int(height)-1:ty);
                        sum += cpuPattern[(size_t(ty)*width+tx)*4+c];
                    }
                }
                double frac = sum / 9.0 - floor(sum / 9.0);
                if(frac > 0.5 && frac < 0.6) ++onBoundary; else ++elsewhere;
            }
        }
    }
    report.check("blur", once.overOne == 0 && elsewhere == 0,
        fmt("%zu razlicitih od %zu, max delta %zu, sve na granici zaokruzivanja",
            once.different, cpuOnce.size(), once.maxDelta));

    const ByteDiff paths = diffBytes(gpuOnce, gpuStorage);
    report.check("sampler = storage", paths.different == 0,
        fmt("dva puta citanja istog izvora, %zu razlicitih", paths.different));

    //a constant area must come back exactly, or the filter drifts
    size_t flatWrong = 0, flatCount = 0;
    for(uint32_t y = regionH + 2; y + 1 < height; ++y){
        for(uint32_t x = regionW + 2; x + 1 < width; ++x){
            size_t i = (size_t(y) * width + x) * 4;
            ++flatCount;
            if(!(gpuOnce[i]==255 && gpuOnce[i+1]==0 && gpuOnce[i+2]==255 && gpuOnce[i+3]==255)) ++flatWrong;
        }
    }
    report.check("ravna ploha", flatWrong == 0, fmt("%zu od %zu piksela pomaknuto", flatWrong, flatCount));

    const ByteDiff twice = diffBytes(cpuTwice, gpuTwice);
    report.check("lanac", twice.overOne == 0, fmt("blur blura, max delta %zu", twice.maxDelta));

    //the scene is sRGB: sampling decodes, the blur averages light, the unorm target stores raw
    const std::vector<uint8_t> scene = sceneRead.readPixels(loom.command).pixels;
    ByteDiff sceneDiff;
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            for(uint32_t c = 0; c < 3; ++c){
                double sum = 0.0;
                for(int dy = -1; dy <= 1; ++dy){
                    for(int dx = -1; dx <= 1; ++dx){
                        int tx = int(x)+dx, ty = int(y)+dy;
                        tx = tx<0?0:(tx>int(width)-1?int(width)-1:tx);
                        ty = ty<0?0:(ty>int(height)-1?int(height)-1:ty);
                        sum += srgbToLinear(scene[(size_t(ty)*width+tx)*4 + (2-c)] / 255.0);
                    }
                }
                uint8_t want = uint8_t(sum / 9.0 * 255.0 + 0.5);
                uint8_t got = gpuScene[(size_t(y)*width+x)*4 + c];
                size_t d = size_t(want > got ? want - got : got - want);
                if(d){ ++sceneDiff.different; if(d > 1) ++sceneDiff.overOne; if(d > sceneDiff.maxDelta) sceneDiff.maxDelta = d; }
            }
        }
    }
    report.check("grafika -> compute", sceneDiff.overOne == 0 && sceneDiff.maxDelta <= 1,
        fmt("scena zamucena computeom, max delta %zu, %zu preko 1", sceneDiff.maxDelta, sceneDiff.overOne));

    report.checkNoValidationMessages();
    return report.result();
}
