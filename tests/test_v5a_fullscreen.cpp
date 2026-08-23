// v5a: a fullscreen pass draws without a mesh, and changes nothing while doing it
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

int main(){
    TestReport report("v5a fullscreen pass");

    LoomConfig config;
    config.width = 512; config.height = 512;
    config.appName = "v5a"; config.engineName = "Loom tests";
    config.enableDepth = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    Mesh cube(loom.device, loom.command, cubeVertices(), cubeIndices());

    CameraConfig camConfig; camConfig.position = {0.0f, 0.0f, 3.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig envConfig; envConfig.ambientColor = {0.05f, 0.05f, 0.05f};
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

    std::vector<uint8_t> checkerPixels = makeCheckerboard(64, 8);
    Texture checker(loom.device, loom.command, checkerPixels.data(), vk::Extent2D{64,64});
    Material cubeMaterial(loom.device, loom.command, loom.getDescriptorPool(), texturedPipeline, checker.getSampled());

    //a white texture and a 2x2 texture, for coverage and orientation
    std::vector<uint8_t> whitePixels(4*4*4, 255);
    TextureConfig flatConfig;
    flatConfig.filter = vk::Filter::eNearest;
    flatConfig.addressMode = vk::SamplerAddressMode::eClampToEdge;
    Texture white(loom.device, loom.command, whitePixels.data(), vk::Extent2D{4,4}, flatConfig);

    const std::vector<uint8_t> quadPixels = {255,0,0,255,  0,255,0,255,
                                             0,0,255,255,  255,255,255,255};
    Texture quad(loom.device, loom.command, quadPixels.data(), vk::Extent2D{2,2}, flatConfig);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;

    const vk::Extent2D size{512,512};
    RenderTarget sceneRead(loom.device, size, readConfig);
    RenderTarget sceneSampled(loom.device, size);
    RenderTarget postOut(loom.device, size, readConfig);
    RenderTarget coverOut(loom.device, size, readConfig);
    RenderTarget orientOut(loom.device, size, readConfig);

    Material postScene(loom.device, loom.command, loom.getDescriptorPool(), postPipeline, sceneSampled.getSampled());
    Material postWhite(loom.device, loom.command, loom.getDescriptorPool(), postPipeline, white.getSampled());
    Material postQuad (loom.device, loom.command, loom.getDescriptorPool(), postPipeline, quad.getSampled());

    //fixed, so the two scene passes really are the same picture
    const glm::mat4 model = glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0.5f,1.0f,0.0f));

    int drawn = 0;
    while(drawn < 3 && !loom.window.shouldClose()){
        loom.window.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.beginPass(sceneRead);
        loom.renderer.draw(cube, model, cubeMaterial);
        loom.renderer.endPass();

        loom.renderer.beginPass(sceneSampled);
        loom.renderer.draw(cube, model, cubeMaterial);
        loom.renderer.endPass();

        loom.renderer.beginPass(postOut);
        loom.renderer.drawFullscreen(postScene);
        loom.renderer.endPass();

        loom.renderer.beginPass(coverOut);
        loom.renderer.drawFullscreen(postWhite);
        loom.renderer.endPass();

        loom.renderer.beginPass(orientOut);
        loom.renderer.drawFullscreen(postQuad);
        loom.renderer.endPass();

        loom.renderer.beginPass();
        loom.renderer.drawFullscreen(postScene);
        loom.renderer.endPass();

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const std::vector<uint8_t> direct = sceneRead.readPixels(loom.command).pixels;
    const std::vector<uint8_t> throughPost = postOut.readPixels(loom.command).pixels;
    const std::vector<uint8_t> covered = coverOut.readPixels(loom.command).pixels;
    const std::vector<uint8_t> oriented = orientOut.readPixels(loom.command).pixels;

    const ByteDiff identity = diffBytes(direct, throughPost);
    const size_t nonBlack = countNonBlack(direct);

    report.check("identitet", identity.different == 0,
        fmt("%zu razlicitih od %zu bajtova, max delta %zu", identity.different, direct.size(), identity.maxDelta));

    //if the comparison above cannot fail, it proves nothing
    const ByteDiff control = diffBytes(direct, covered);
    report.check("kontrola", control.different > 0,
        fmt("scena vs bijeli cilj: %zu razlicitih", control.different));

    report.check("scena nije crna", nonBlack > 10000, fmt("%zu ne-crnih piksela", nonBlack));

    //the scene background and the clear colour are both black, so identity alone would
    //never notice a triangle that misses pixels. White into a black-cleared target does
    size_t notWhite = 0;
    for(size_t i = 0; i + 3 < covered.size(); i += 4){
        if(!(covered[i]==255 && covered[i+1]==255 && covered[i+2]==255 && covered[i+3]==255)) ++notWhite;
    }
    report.check("pokrivenost", notWhite == 0, fmt("%zu od %zu piksela nije bijelo", notWhite, covered.size()/4));

    auto quadrant = [&](uint32_t x, uint32_t y, int b, int g, int r){
        size_t i = (size_t(y) * 512 + x) * 4;
        return oriented[i] == b && oriented[i+1] == g && oriented[i+2] == r;
    };
    const bool orientation = quadrant(128,128, 0,0,255) && quadrant(384,128, 0,255,0)
                          && quadrant(128,384, 255,0,0) && quadrant(384,384, 255,255,255);
    report.check("orijentacija", orientation, "2x2 tekstura, svaki kvadrant nosi svoj teksel");

    report.checkNoValidationMessages();
    return report.result();
}
