// v5b: the post pass multiplies linear light, not stored bytes
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Vulkan/Material.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"
#include "Core/Camera.h"
#include "Core/Environment.h"
#include <glm/gtc/matrix_transform.hpp>

//the two models the measurement decides between
static uint8_t predictLinear(uint8_t src, double factor){
    return uint8_t(linearToSrgb(srgbToLinear(src / 255.0) * factor) * 255.0 + 0.5);
}
static uint8_t predictByte(uint8_t src, double factor){
    double v = src * factor;
    return uint8_t((v > 255.0 ? 255.0 : v) + 0.5);
}

int main(){
    TestReport report("v5b tint");

    LoomConfig config;
    config.width = 512; config.height = 512;
    config.appName = "v5b"; config.engineName = "Loom tests";
    config.enableDepth = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    Mesh cube(loom.device, loom.command, cubeVertices(), cubeIndices());
    CameraConfig camConfig; camConfig.position = {0.0f, 0.0f, 3.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);
    EnvironmentConfig envConfig; envConfig.ambientColor = {0.05f,0.05f,0.05f};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);
    LightConfig lightConfig;
    Light light(lightConfig);
    loom.renderer.addLight(light);

    PipelineConfig texturedConfig = config.pipelineConfig;
    texturedConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    texturedConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.vert.spv";
    texturedConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.frag.spv";
    VulkanGraphicsPipeline texturedPipeline = loom.createPipeline(texturedConfig);

    PipelineConfig postConfig;
    postConfig.vertexBindings.clear();
    postConfig.vertexAttributes.clear();
    postConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    postConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.vert.spv";
    postConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.frag.spv";
    postConfig.cullMode = vk::CullModeFlagBits::eNone;
    VulkanGraphicsPipeline postPipeline = loom.createPipeline(postConfig);

    std::vector<uint8_t> checkerPixels = makeCheckerboard(64,8);
    Texture checker(loom.device, loom.command, checkerPixels.data(), vk::Extent2D{64,64});
    Material cubeMaterial(loom.device, loom.command, loom.getDescriptorPool(), texturedPipeline, checker.getSampled());

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;

    const vk::Extent2D size{512,512};
    RenderTarget sceneRead(loom.device, size, readConfig);
    RenderTarget sceneSampled(loom.device, size);
    RenderTarget neutralOut(loom.device, size, readConfig);
    RenderTarget tintedOut(loom.device, size, readConfig);

    Material neutral(loom.device, loom.command, loom.getDescriptorPool(), postPipeline, sceneSampled.getSampled());

    MaterialData tintData;
    tintData.baseColor = glm::vec4(0.5f, 0.25f, 0.125f, 1.0f);
    Material tinted(loom.device, loom.command, loom.getDescriptorPool(), postPipeline, sceneSampled.getSampled(), tintData);

    const glm::mat4 model = glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0.5f,1.0f,0.0f));

    auto renderFrames = [&](int count){
        int drawn = 0;
        while(drawn < count && !loom.shouldClose()){
            loom.pollEvents();
            if(!loom.renderer.beginFrame()) continue;

            loom.renderer.beginPass(sceneRead);
            loom.renderer.draw(cube, model, cubeMaterial);
            loom.renderer.endPass();

            loom.renderer.beginPass(sceneSampled);
            loom.renderer.draw(cube, model, cubeMaterial);
            loom.renderer.endPass();

            loom.renderer.beginPass(neutralOut);
            loom.renderer.drawFullscreen(neutral);
            loom.renderer.endPass();

            loom.renderer.beginPass(tintedOut);
            loom.renderer.drawFullscreen(tinted);
            loom.renderer.endPass();

            loom.renderer.beginPass();
            loom.renderer.drawFullscreen(tinted);
            loom.renderer.endPass();

            loom.renderer.endFrame();
            ++drawn;
        }
        loom.waitIdle();
    };

    renderFrames(3);

    const std::vector<uint8_t> source = sceneRead.readPixels(loom.command).pixels;
    const std::vector<uint8_t> neutralPixels = neutralOut.readPixels(loom.command).pixels;
    const std::vector<uint8_t> tintedPixels = tintedOut.readPixels(loom.command).pixels;

    const ByteDiff identity = diffBytes(source, neutralPixels);
    report.check("neutralno je nevidljivo", identity.different == 0,
        fmt("baseColor(1,1,1): %zu razlicitih od %zu", identity.different, source.size()));

    //black agrees in both models and would flatter them, so only lit bytes are counted.
    //the target is BGRA, so the factors go in that order
    auto measure = [&](const std::vector<uint8_t>& got, const glm::vec4& tint, uint8_t (*predict)(uint8_t,double)){
        const double factors[4] = {tint.b, tint.g, tint.r, 1.0};
        ByteDiff diff;
        size_t counted = 0;
        for(size_t i = 0; i < source.size(); ++i){
            if(i % 4 == 3 || source[i] == 0) continue;
            ++counted;
            uint8_t want = predict(source[i], factors[i % 4]);
            size_t d = size_t(want > got[i] ? want - got[i] : got[i] - want);
            if(d){ ++diff.different; if(d > 1) ++diff.overOne; if(d > diff.maxDelta) diff.maxDelta = d; }
        }
        return std::pair<ByteDiff,size_t>(diff, counted);
    };

    const auto linear = measure(tintedPixels, tintData.baseColor, predictLinear);
    const auto naive  = measure(tintedPixels, tintData.baseColor, predictByte);

    report.check("linearni model", linear.first.overOne == 0 && linear.first.maxDelta <= 1,
        fmt("max delta %zu, %zu preko 1, nad %zu ne-crnih bajtova",
            linear.first.maxDelta, linear.first.overOne, linear.second));

    report.check("bajt model promasuje", naive.first.maxDelta > 10,
        fmt("max delta %zu, %zu bajtova preko 1", naive.first.maxDelta, naive.first.overOne));

    //the same material, changed while it is in flight
    const glm::vec4 brighter(1.5f, 1.5f, 1.5f, 1.0f);
    tinted.setBaseColor(brighter);
    renderFrames(3);

    const std::vector<uint8_t> brightPixels = tintedOut.readPixels(loom.command).pixels;
    const auto afterChange = measure(brightPixels, brighter, predictLinear);
    const ByteDiff moved = diffBytes(tintedPixels, brightPixels);

    report.check("setBaseColor u letu", afterChange.first.overOne == 0 && moved.different > 1000,
        fmt("max delta %zu, %zu bajtova se promijenilo", afterChange.first.maxDelta, moved.different));

    report.checkNoValidationMessages();
    return report.result();
}
