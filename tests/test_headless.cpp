// headless: Loom runs a whole frame with no window, no surface and no swapchain, and the
//           picture it produces is byte for byte the one the windowed path produces.
//
// That equality is the whole claim. A headless renderer that draws almost the same thing is
// useless for a sequence export and worse than useless for CI - it would fail the exact
// pixel tests for reasons that have nothing to do with the code being tested.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/Light.h"
#include "Core/LoomShapes.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include <glm/gtc/matrix_transform.hpp>

struct Shot{
    std::vector<uint8_t> pixels;
    bool hadWindow = false;
    bool hadSwapchain = false;
};

//One frame of the same scene, rendered either way. Everything in here is identical between
//the two modes except that the windowed run also draws to its window - which it has to,
//because a swapchain image that is acquired and never rendered cannot be presented
static Shot renderScene(bool headless, vk::Extent2D size, float time){
    LoomConfig config;
    config.width = size.width;
    config.height = size.height;
    config.appName = headless ? "headless" : "windowed";
    config.engineName = "Loom tests";
    config.headless = headless;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    //Pinned rather than left to the swapchain, so the two runs cannot differ by their format
    //negotiation instead of by the thing being tested
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;

    LoomInitializer loom(config);
    LoomShapes::Primitives shapes(loom);

    CameraConfig camConfig;
    camConfig.position = {2.5f, 2.0f, 3.5f};
    camConfig.target = {0.0f, 0.3f, 0.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig envConfig; envConfig.ambientColor = {0.1f, 0.1f, 0.12f};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig sunConfig;
    sunConfig.type = LightType::Directional;
    sunConfig.direction = {-0.4f, -1.0f, -0.35f};
    Light sun(sunConfig);
    loom.renderer.addLight(sun);

    RenderTarget shadowMap(loom.device, {1024,1024}, makeShadowMapConfig());

    PipelineConfig shadowPipelineConfig;
    shadowPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/shadow.vert.spv";
    shadowPipelineConfig.fragShaderPath = "";
    shadowPipelineConfig.enableColor = false;
    shadowPipelineConfig.vertexAttributes = Vertex::getPositionAttribute();
    shadowPipelineConfig.cullMode = vk::CullModeFlagBits::eFront;
    shadowPipelineConfig.depthTestEnable = true;
    shadowPipelineConfig.depthWriteEnable = true;

    VulkanGraphicsPipeline shadowPipeline(loom.device, shadowPipelineConfig,
        loom.getColorFormat(), shadowMap.getDepthFormat());
    Material shadowMaterial(shadowPipeline);

    //Headless has no window to take a viewport from, so the fit is told which one to use.
    //The windowed run is given the same numbers rather than its default, because otherwise
    //the two would be fitting to different aspect ratios and the shadows would not match
    ShadowConfig shadowConfig;
    shadowConfig.fitToCamera = true;
    shadowConfig.distance = 12.0f;
    shadowConfig.depthBias = 0.0015f;
    shadowConfig.viewportWidth = size.width;
    shadowConfig.viewportHeight = size.height;
    loom.renderer.setShadowMap(shadowMap, sun, shadowConfig);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    RenderTarget out(loom.device, size, readConfig);

    //Time is a parameter, not a clock. A sequence export that read the wall clock would give
    //a different film every run
    const glm::mat4 floorModel = glm::scale(glm::mat4(1.0f), glm::vec3(8.0f,1.0f,8.0f));
    const glm::mat4 cubeModel = glm::rotate(glm::translate(glm::mat4(1.0f), {-0.9f,0.5f,0.0f}),
                                            time, glm::vec3(0.2f,1.0f,0.1f));
    const glm::mat4 sphereModel = glm::translate(glm::mat4(1.0f), {0.9f, 0.5f + 0.2f * std::sin(time), 0.0f});

    int drawn = 0;
    while(drawn < 2 && !loom.shouldClose()){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.beginPass(shadowMap, sun);
        shapes.cube(shadowMaterial, cubeModel);
        shapes.sphere(shadowMaterial, sphereModel);
        loom.renderer.endPass();

        loom.renderer.beginPass(out);
        shapes.plane(floorModel);
        shapes.cube(cubeModel);
        shapes.sphere(sphereModel);
        loom.renderer.endPass();

        if(loom.hasWindow()){
            loom.renderer.beginPass();
            shapes.plane(floorModel);
            shapes.cube(cubeModel);
            shapes.sphere(sphereModel);
            loom.renderer.endPass();
        }

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    Shot shot;
    shot.pixels = out.readPixels(loom.command).pixels;
    shot.hadWindow = loom.hasWindow();
    shot.hadSwapchain = loom.renderer.hasSwapchain();
    return shot;
}

int main(){
    TestReport report("headless");

    const vk::Extent2D size{384,384};

    //Headless first, and the windowed run in its own scope afterwards: Window's destructor
    //calls glfwTerminate, so two of them must never be alive at once
    const Shot headless = renderScene(true, size, 0.0f);
    const Shot windowed = renderScene(false, size, 0.0f);

    report.check("bez prozora", !headless.hadWindow && !headless.hadSwapchain,
        "ni prozora ni swapchaina");

    report.check("s prozorom", windowed.hadWindow && windowed.hadSwapchain,
        "prozor i swapchain postoje");

    report.check("nesto je nacrtano", countNonBlack(headless.pixels) > 5000,
        fmt("%zu ne-crnih piksela bez prozora", countNonBlack(headless.pixels)));

    //The claim. Not "similar", not "within a tolerance" - the same bytes
    const ByteDiff difference = diffBytes(headless.pixels, windowed.pixels);
    report.check("ista slika", difference.different == 0,
        fmt("%zu razlicitih od %zu bajtova, max delta %zu",
            difference.different, headless.pixels.size(), difference.maxDelta));

    // -------------------------------------------------------------------------------
    // Sequence export: vrijeme je parametar, ne sat
    // -------------------------------------------------------------------------------

    const Shot later = renderScene(true, size, 1.0f);
    const ByteDiff moved = diffBytes(headless.pixels, later.pixels);

    //If the scene did not move, determinism below would be proving nothing
    report.check("scena se mice", moved.different > 0,
        fmt("t = 0 i t = 1 razlikuju se u %zu bajtova", moved.different));

    const Shot again = renderScene(true, size, 0.0f);
    const ByteDiff repeat = diffBytes(headless.pixels, again.pixels);

    //The same frame number has to give the same frame, every run, or an export cannot be
    //resumed, cached, or compared against a previous one
    report.check("isti frame dvaput", repeat.different == 0,
        fmt("%zu razlicitih bajtova izmedu dva pokretanja istog trenutka", repeat.different));

    // -------------------------------------------------------------------------------
    // Sto headless odbija napraviti, i kako to kaze
    // -------------------------------------------------------------------------------

    LoomConfig guardConfig;
    guardConfig.width = 64; guardConfig.height = 64;
    guardConfig.appName = "headless guards"; guardConfig.engineName = "Loom tests";
    guardConfig.headless = true;
    guardConfig.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;
    LoomInitializer guard(guardConfig);

    report.check("initializer zna", guard.isHeadless() && !guard.hasWindow(),
        fmt("format %u, extent %u x %u",
            static_cast<uint32_t>(guard.getColorFormat()),
            guard.getExtent().width, guard.getExtent().height));

    //A window pass with no window. The frame itself still starts - that is the point
    bool windowPassThrew = false;
    if(guard.renderer.beginFrame()){
        try{
            guard.renderer.beginPass();
        }
        catch(const std::exception&){
            windowPassThrew = true;
        }
        guard.renderer.endFrame();
    }
    guard.waitIdle();
    report.check("prolaz u prozor", windowPassThrew, "baca iznimku, ali frame je zapocet");

    bool readLastThrew = false;
    try{ guard.renderer.readLastFrame(); }
    catch(const std::exception&){ readLastThrew = true; }
    report.check("citanje prozora", readLastThrew, "baca iznimku");

    //Fitting needs an aspect ratio, and headless has no window to borrow one from
    bool fitWithoutViewportThrew = false;
    try{
        RenderTarget map(guard.device, {256,256}, makeShadowMapConfig());
        LightConfig sunConfig; sunConfig.type = LightType::Directional;
        Light sun(sunConfig);
        Camera camera;
        guard.renderer.setCamera(camera);

        ShadowConfig noViewport;
        noViewport.fitToCamera = true; //and no viewport given
        guard.renderer.setShadowMap(map, sun, noViewport);
    }
    catch(const std::exception&){
        fitWithoutViewportThrew = true;
    }
    report.check("fit bez viewporta", fitWithoutViewportThrew, "baca iznimku umjesto krivog aspecta");

    //A pipeline that draws colour with nobody to say what format
    bool noFormatThrew = false;
    try{
        PipelineConfig orphan;
        VulkanGraphicsPipeline pipeline(guard.device, orphan, vk::Format::eUndefined, vk::Format::eUndefined);
    }
    catch(const std::exception&){
        noFormatThrew = true;
    }
    report.check("pipeline bez formata", noFormatThrew, "baca iznimku");

    report.checkNoValidationMessages();
    return report.result();
}
