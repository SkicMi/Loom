// v6b: a dispatch writes an image, and the graphics side can read what it wrote
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Vulkan/ComputeMaterial.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"

struct ImageParams{ uint32_t sizeX, sizeY, mode, padding0; };

int main(){
    TestReport report("v6b storage image");

    LoomConfig config;
    config.width = 512; config.height = 512;
    config.appName = "v6b"; config.engineName = "Loom tests";
    config.enableDepth = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;

    LoomInitializer loom(config);

    const uint32_t width = 517, height = 301, regionW = 300, regionH = 200;
    const vk::Extent2D extent{width, height};

    ImageConfig storageConfig;
    storageConfig.format = vk::Format::eR8G8B8A8Unorm;
    storageConfig.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;

    VulkanImage readImage(loom.device, extent, storageConfig);
    VulkanImage sampleImage(loom.device, extent, storageConfig);

    //both guards, before anything is drawn
    bool formatGuard = false, usageGuard = false;
    {
        ImageConfig srgbConfig = storageConfig;
        srgbConfig.format = vk::Format::eR8G8B8A8Srgb;
        try{ VulkanImage srgb(loom.device, extent, srgbConfig); }
        catch(const std::exception&){ formatGuard = true; }
    }

    vk::DescriptorSetLayoutBinding targetBinding;
    targetBinding.binding = 0;
    targetBinding.descriptorType = vk::DescriptorType::eStorageImage;
    targetBinding.descriptorCount = 1;
    targetBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    ComputePipelineConfig writeConfig;
    writeConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/imagewrite.comp.spv";
    writeConfig.descriptorBindings = {targetBinding};
    writeConfig.pushConstantSize = sizeof(ImageParams);
    VulkanComputePipeline writePipeline = loom.createComputePipeline(writeConfig);

    {
        ImageConfig plainConfig = storageConfig;
        plainConfig.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        VulkanImage plain(loom.device, extent, plainConfig);
        ComputeMaterial probe(loom.device, loom.getDescriptorPool(), writePipeline);
        try{ probe.setStorageImage(0, plain, vk::ImageLayout::eGeneral); }
        catch(const std::exception&){ usageGuard = true; }
    }

    ComputeMaterial toRead(loom.device, loom.getDescriptorPool(), writePipeline);
    toRead.setStorageImage(0, readImage, vk::ImageLayout::eTransferSrcOptimal);

    ComputeMaterial toSample(loom.device, loom.getDescriptorPool(), writePipeline);
    toSample.setStorageImage(0, sampleImage, vk::ImageLayout::eShaderReadOnlyOptimal);

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    vk::raii::Sampler sampler(loom.device.getDevice(), samplerInfo);

    PipelineConfig postConfig;
    postConfig.vertexBindings.clear();
    postConfig.vertexAttributes.clear();
    postConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    postConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.vert.spv";
    postConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.frag.spv";
    postConfig.cullMode = vk::CullModeFlagBits::eNone;
    VulkanGraphicsPipeline postPipeline = loom.createPipeline(postConfig);

    SampledImage computeResult;
    computeResult.view = *sampleImage.getImageView();
    computeResult.sampler = *sampler;
    Material postMaterial(loom.device, loom.command, loom.getDescriptorPool(), postPipeline, computeResult);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    RenderTarget postOut(loom.device, extent, readConfig);

    ImageParams whole{width, height, 0, 0};
    ImageParams region{regionW, regionH, 1, 0};
    const uint32_t wholeX = (width + 7) / 8, wholeY = (height + 7) / 8;
    const uint32_t regionX = (regionW + 7) / 8, regionY = (regionH + 7) / 8;

    int drawn = 0;
    while(drawn < 2 && !loom.window.shouldClose()){
        loom.window.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.dispatch(toRead, wholeX, wholeY, 1, &whole, sizeof(whole));
        loom.renderer.dispatch(toRead, regionX, regionY, 1, &region, sizeof(region));
        loom.renderer.dispatch(toSample, wholeX, wholeY, 1, &whole, sizeof(whole));
        loom.renderer.dispatch(toSample, regionX, regionY, 1, &region, sizeof(region));

        loom.renderer.beginPass(postOut);
        loom.renderer.drawFullscreen(postMaterial);
        loom.renderer.endPass();

        loom.renderer.beginPass();
        loom.renderer.drawFullscreen(postMaterial);
        loom.renderer.endPass();

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const std::vector<uint8_t> got = readImagePixels(loom, readImage, extent);
    const std::vector<uint8_t> want = patternPixels(width, height, regionW, regionH);

    size_t wrongPattern = 0, wrongSentinel = 0;
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            size_t i = (size_t(y) * width + x) * 4;
            bool ok = got[i] == want[i] && got[i+1] == want[i+1] && got[i+2] == want[i+2] && got[i+3] == want[i+3];
            if(ok) continue;
            if(x < regionW && y < regionH) ++wrongPattern; else ++wrongSentinel;
        }
    }
    report.check("formula", wrongPattern == 0, fmt("regija %ux%u, %zu krivih piksela", regionW, regionH, wrongPattern));

    //the first dispatch painted everything magenta, the second only the region. Magenta
    //left outside proves the bounds guard and that the second dispatch saw the first
    report.check("granica", wrongSentinel == 0,
        fmt("%zu od %zu piksela izvan regije izgubilo magentu",
            wrongSentinel, size_t(width) * height - size_t(regionW) * regionH));

    //the same image, this time through the graphics pipeline: unorm in, sRGB out
    const ImageData drawnBack = postOut.readPixels(loom.command);
    ByteDiff throughGraphics;
    for(size_t p = 0; p * 4 + 3 < drawnBack.pixels.size(); ++p){
        const uint8_t source[3] = {got[p*4+2], got[p*4+1], got[p*4+0]}; //BGR for the target
        for(int c = 0; c < 3; ++c){
            uint8_t expected = encodeByte(source[c] / 255.0);
            uint8_t actual = drawnBack.pixels[p*4+c];
            size_t d = size_t(expected > actual ? expected - actual : actual - expected);
            if(d){ ++throughGraphics.different; if(d > 1) ++throughGraphics.overOne; if(d > throughGraphics.maxDelta) throughGraphics.maxDelta = d; }
        }
    }
    report.check("compute -> grafika", throughGraphics.overOne == 0 && throughGraphics.maxDelta <= 1,
        fmt("max delta %zu, %zu preko 1", throughGraphics.maxDelta, throughGraphics.overOne));

    report.check("guardovi", formatGuard && usageGuard,
        fmt("sRGB format %s, slika bez eStorage %s", formatGuard ? "baca" : "NE BACA", usageGuard ? "baca" : "NE BACA"));

    report.checkNoValidationMessages();
    return report.result();
}
