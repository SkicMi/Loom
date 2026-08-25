// tier 1 override: a preset and a config are not a choice between two styles - they combine.
//
// The preset fills a config in; the override is the moment between that and its use. What
// the override does not touch keeps whatever the preset chose, which is what lets someone
// start on tier 1 and descend for one setting instead of rewriting from scratch.
//
// This file includes <Loom/Preset_Advanced.h>, and therefore sees Vulkan. That is the point:
// the door is a door, and tier1_advanced_is_dirty measures that it really opens.
#include "TestHarness.h"
#include "TestScene.h"

#include <Loom/Loom.h>
#include <Loom/Preset_Advanced.h>

#include "Core/LoomShapes.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"
#include "Spool/ImageFile.h"

#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 320;
const uint32_t sceneHeight = 240;

glm::mat4 planeTransform(){ return glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)); }
glm::mat4 cubeTransform(){
    return glm::rotate(glm::translate(glm::mat4(1.0f), {0.0f,0.5f,0.0f}), 0.6f, glm::vec3(0.25f,1.0f,0.1f));
}

//Draws the same two shapes whatever built the scene
void drawInto(Loom::Scene& scene, Loom::TextureHandle texture){
    scene.startRendering();
        scene.drawPlane(texture, planeTransform());
        scene.drawCube(texture, cubeTransform());
    scene.endRendering();
}

size_t brightness(const std::vector<uint8_t>& pixels){
    size_t sum = 0;
    for(uint8_t value : pixels) sum += value;
    return sum;
}

}

int main(){
    TestReport report("tier1 override");

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_tier1_override";
    std::filesystem::remove_all(work);
    const std::string texturePath = (work / "checker.png").string();
    {
        const std::vector<uint8_t> checker = makeCheckerboard(64, 8);
        Spool::savePng(texturePath, Spool::imageFromPixels(checker.data(), 64, 64, Spool::ChannelOrder::RGBA));
    }

    // -------------------------------------------------------------------------------
    // Override stize do configa, a ostalo ostaje presetovo
    // -------------------------------------------------------------------------------

    const glm::vec4 wantedClear{0.4f, 0.1f, 0.7f, 1.0f};

    Loom::Scene adjusted(Loom::Preset::Offscreen, [&](LoomConfig& config){
        config.rendererConfig.clearColor = {wantedClear.r, wantedClear.g, wantedClear.b, wantedClear.a};
        config.rendererConfig.maxLights = 42;
    });
    adjusted.setSize(sceneWidth, sceneHeight);

    const LoomConfig& touched = adjusted.config();

    report.check("override stigao", touched.rendererConfig.maxLights == 42 &&
        touched.rendererConfig.clearColor[0] == wantedClear.r,
        fmt("maxLights %u, clear %.2f", touched.rendererConfig.maxLights, double(touched.rendererConfig.clearColor[0])));

    //And everything the override did not name still holds what Preset::Offscreen chose.
    //This is the whole "combine rather than replace" claim
    report.check("preset je ostao", touched.headless && touched.enableDepth &&
        touched.pipelineConfig.depthTestEnable &&
        touched.pipelineConfig.cullMode == vk::CullModeFlagBits::eBack &&
        touched.rendererConfig.maxPassesPerFrame == 8,
        "headless, dubina, cullBack i maxPassesPerFrame netaknuti");

    // -------------------------------------------------------------------------------
    // Override koji nista ne mijenja ne smije nista promijeniti
    // -------------------------------------------------------------------------------

    std::vector<uint8_t> plain;
    {
        Loom::Scene scene(Loom::Preset::Offscreen);
        scene.setSize(sceneWidth, sceneHeight);
        drawInto(scene, scene.loadTexture(texturePath));
        plain = scene.readPixels();
    }

    std::vector<uint8_t> throughEmptyOverride;
    {
        Loom::Scene scene(Loom::Preset::Offscreen, [](LoomConfig&){});
        scene.setSize(sceneWidth, sceneHeight);
        drawInto(scene, scene.loadTexture(texturePath));
        throughEmptyOverride = scene.readPixels();
    }

    const ByteDiff noopDifference = diffBytes(plain, throughEmptyOverride);
    report.check("prazan override", noopDifference.different == 0,
        fmt("%zu razlicitih od %zu bajtova", noopDifference.different, plain.size()));

    // -------------------------------------------------------------------------------
    // Override koji nesto mijenja mora se vidjeti, i to bas ono
    // -------------------------------------------------------------------------------

    std::vector<uint8_t> culled;
    {
        //Front faces culled instead of back: every solid turns inside out, which nothing
        //else in the pipeline could have caused
        Loom::Scene scene(Loom::Preset::Offscreen, [](LoomConfig& config){
            config.pipelineConfig.cullMode = vk::CullModeFlagBits::eFront;
        });
        scene.setSize(sceneWidth, sceneHeight);
        drawInto(scene, scene.loadTexture(texturePath));
        culled = scene.readPixels();
    }

    const ByteDiff culledDifference = diffBytes(plain, culled);
    report.check("override se vidi", culledDifference.different > 0,
        fmt("obrnuti culling mijenja %zu bajtova", culledDifference.different));

    // -------------------------------------------------------------------------------
    // Preset + override == stepenica 2 s istom izmjenom, bajt za bajt
    // -------------------------------------------------------------------------------

    std::vector<uint8_t> byHand;
    {
        //Everything Preset::Offscreen expands to, with the same one line changed
        LoomConfig config;
        config.appName = "Loom";
        config.engineName = "Loom";
        config.width = sceneWidth;
        config.height = sceneHeight;
        config.swapchainConfig.allowReadback = true;
        config.headless = true;
        config.enableDepth = true;
        config.pipelineConfig.depthTestEnable = true;
        config.pipelineConfig.depthWriteEnable = true;
        config.pipelineConfig.cullMode = vk::CullModeFlagBits::eFront;   //the override
        config.rendererConfig.clearColor = {0.02f, 0.02f, 0.04f, 1.0f};
        config.rendererConfig.maxPassesPerFrame = 8;
        config.headlessColorFormat = vk::Format::eB8G8R8A8Srgb;
        config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;

        LoomInitializer loom(config);

        //The same rule the preset follows: Primitives makes its own pipeline, so it is given
        //the same cull mode. Leaving it at the default here would compare two different
        //scenes and call them equal
        LoomShapes::PrimitivesConfig primitivesConfig;
        primitivesConfig.cullMode = config.pipelineConfig.cullMode;
        primitivesConfig.depthTest = config.pipelineConfig.depthTestEnable;
        LoomShapes::Primitives shapes(loom, primitivesConfig);

        CameraConfig cameraConfig;
        cameraConfig.position = {3.5f, 2.5f, 4.5f};
        cameraConfig.target = {0.0f, 0.5f, 0.0f};
        Camera camera(cameraConfig);
        loom.renderer.setCamera(camera);

        EnvironmentConfig environmentConfig;
        environmentConfig.ambientColor = {0.10f, 0.11f, 0.14f};
        Environment environment(environmentConfig);
        loom.renderer.setEnvironment(environment);

        LightConfig sunConfig;
        sunConfig.type = LightType::Directional;
        sunConfig.direction = {-0.45f, -1.0f, -0.35f};
        sunConfig.color = {1.0f, 0.96f, 0.88f};
        sunConfig.intensity = 1.0f;
        Light sun(sunConfig);
        loom.renderer.addLight(sun);

        RenderTargetConfig targetConfig;
        targetConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
        targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
        RenderTarget offscreen(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, targetConfig);
        RenderTarget shadowMap(loom.device, vk::Extent2D{2048,2048}, makeShadowMapConfig());

        PipelineConfig shadowPipelineConfig;
        shadowPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/shadow.vert.spv";
        shadowPipelineConfig.fragShaderPath = "";
        shadowPipelineConfig.enableColor = false;
        shadowPipelineConfig.vertexAttributes = Vertex::getPositionAttribute();
        shadowPipelineConfig.depthTestEnable = true;
        shadowPipelineConfig.depthWriteEnable = true;
        shadowPipelineConfig.cullMode = vk::CullModeFlagBits::eFront;

        VulkanGraphicsPipeline shadowPipeline(loom.device, shadowPipelineConfig,
            loom.getColorFormat(), shadowMap.getDepthFormat());
        Material shadowMaterial(shadowPipeline);

        ShadowConfig shadow;
        shadow.fitToCamera = true;
        shadow.distance = 14.0f;
        shadow.depthBias = 0.0015f;
        shadow.viewportWidth = sceneWidth;
        shadow.viewportHeight = sceneHeight;
        loom.renderer.setShadowMap(shadowMap, sun, shadow);

        const Spool::Image decoded = Spool::loadImage(texturePath);
        Texture texture(loom.device, loom.command, decoded.pixels.data(),
            vk::Extent2D{decoded.width, decoded.height});

        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(shadowMap, sun);
                shapes.plane(shadowMaterial, planeTransform());
                shapes.cube(shadowMaterial, cubeTransform());
            loom.renderer.endPass();

            loom.renderer.beginPass(offscreen);
                shapes.plane(texture, planeTransform());
                shapes.cube(texture, cubeTransform());
            loom.renderer.endPass();

            loom.renderer.endFrame();
        }
        loom.waitIdle();

        const ImageData shot = offscreen.readPixels(loom.command);
        byHand = Spool::imageFromPixels(shot.pixels.data(), shot.extent.width, shot.extent.height,
            Spool::ChannelOrder::BGRA).pixels;
    }

    const ByteDiff combined = diffBytes(culled, byHand);
    report.check("preset+override = rucno", combined.different == 0,
        fmt("%zu razlicitih od %zu bajtova, max delta %zu",
            combined.different, culled.size(), combined.maxDelta));

    // -------------------------------------------------------------------------------
    // Izlaz na zive objekte
    // -------------------------------------------------------------------------------

    {
        Loom::Scene scene(Loom::Preset::Offscreen);
        scene.setSize(sceneWidth, sceneHeight);
        const Loom::TextureHandle texture = scene.loadTexture(texturePath);

        //Same object every time, not a copy handed out per call
        report.check("isti objekt", &scene.loom() == &scene.loom() &&
            !scene.loom().device.getDeviceName().empty(),
            scene.loom().device.getDeviceName());

        drawInto(scene, texture);
        const std::vector<uint8_t> lit = scene.readPixels();

        //Reaching past the preset and changing something while it runs. Nothing on tier 1
        //offers this, and that is exactly when the door is for
        scene.loom().renderer.clearLights();

        drawInto(scene, texture);
        const std::vector<uint8_t> unlit = scene.readPixels();

        const ByteDiff extinguished = diffBytes(lit, unlit);
        report.check("loom() dopire do zivog",
            extinguished.different > 0 && brightness(unlit) < brightness(lit),
            fmt("bez svjetla %zu bajtova drukcije, svjetlina pala s %zu na %zu",
                extinguished.different, brightness(lit), brightness(unlit)));
    }

    // -------------------------------------------------------------------------------
    // Granice
    // -------------------------------------------------------------------------------

    bool constLoomBeforeBuildThrew = false;
    try{
        const Loom::Scene fresh(Loom::Preset::Offscreen);
        fresh.loom();
    }
    catch(const std::exception&){ constLoomBeforeBuildThrew = true; }
    report.check("const loom() prerano", constLoomBeforeBuildThrew, "baca iznimku");

    //config() answers before anything is built, because that is when it is most useful
    const Loom::Scene unbuilt(Loom::Preset::Flat2D);
    report.check("config() prije gradnje", !unbuilt.config().enableDepth,
        "Flat2D nema dubinu, i to se vidi prije nego je ista sagradeno");

    std::filesystem::remove_all(work);

    report.checkNoValidationMessages();
    return report.result();
}
