// tier 1 lights: a scene can hold more than the preset's sun, and a point light added on
//                tier 1 casts through a cube map - six passes over the same draw queue.
//
// This is the limitation the tier 1 rewrite of main exposed. Scene queues its draws and
// replays them, so nothing outside could drive the six face passes a point light needs; the
// answer was to give Scene the light rather than to work around Scene.
//
// The scene also owns the light. A renderer holds pointers to its lights, so a Light declared
// beside a Scene has to outlive it - a rule nobody should have to remember and exactly the
// kind of thing that works until the day it does not.
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

//A floor with a block above it, and the bulb between them. Whatever the bulb throws lands on
//the floor, which is the only place a cube shadow can be seen at all
glm::mat4 floorTransform(){ return glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)); }
glm::mat4 blockTransform(){ return glm::translate(glm::mat4(1.0f), {0.0f, 1.0f, 0.0f}); }

LightConfig bulbConfig(){
    LightConfig bulb;
    bulb.type = LightType::Point;
    bulb.position = {0.0f, 3.0f, 0.0f};
    bulb.color = {1.0f, 0.4f, 0.15f};
    bulb.intensity = 20.0f;
    bulb.range = 15.0f;
    bulb.shadowNear = 0.1f;
    return bulb;
}

//-------------------------------------------------------------------------------------------
// Isto, rukom. Sest prolaza po licima i jedan kamerin, sve nad istim oblicima
//-------------------------------------------------------------------------------------------
std::vector<uint8_t> handWritten(const std::string& texturePath){
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
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eBack;
    config.rendererConfig.clearColor = {0.02f, 0.02f, 0.04f, 1.0f};
    config.rendererConfig.maxPassesPerFrame = 16;
    config.headlessColorFormat = vk::Format::eB8G8R8A8Srgb;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;

    LoomInitializer loom(config);

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

    //The sun first, then the bulb - the order Scene adds them in, and the order they sit in
    //the storage buffer
    LightConfig sunConfig;
    sunConfig.type = LightType::Directional;
    sunConfig.direction = {-0.45f, -1.0f, -0.35f};
    sunConfig.color = {1.0f, 0.96f, 0.88f};
    sunConfig.intensity = 1.0f;
    Light sun(sunConfig);
    loom.renderer.addLight(sun);

    Light bulb(bulbConfig());
    loom.renderer.addLight(bulb);

    RenderTarget offscreen(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, [](){
        RenderTargetConfig target;
        target.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
        target.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
        return target;
    }());

    RenderTarget shadowMap(loom.device, vk::Extent2D{2048,2048}, makeShadowMapConfig());
    RenderTarget shadowCube(loom.device, vk::Extent2D{1024,1024}, makeShadowCubeConfig());

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

    ShadowConfig cubeShadow;
    cubeShadow.depthBias = 0.0025f;
    loom.renderer.setShadowCube(shadowCube, bulb, cubeShadow);

    const Spool::Image decoded = Spool::loadImage(texturePath);
    Texture texture(loom.device, loom.command, decoded.pixels.data(),
        vk::Extent2D{decoded.width, decoded.height});

    if(loom.renderer.beginFrame()){
        loom.renderer.beginPass(shadowMap, sun);
            shapes.plane(shadowMaterial, floorTransform());
            shapes.cube(shadowMaterial, blockTransform());
        loom.renderer.endPass();

        for(uint32_t face = 0; face < 6; ++face){
            loom.renderer.beginPass(shadowCube, bulb, face);
                shapes.plane(shadowMaterial, floorTransform());
                shapes.cube(shadowMaterial, blockTransform());
            loom.renderer.endPass();
        }

        loom.renderer.beginPass(offscreen);
            shapes.plane(texture, floorTransform());
            shapes.cube(texture, blockTransform());
        loom.renderer.endPass();

        loom.renderer.endFrame();
    }
    loom.waitIdle();

    const ImageData shot = offscreen.readPixels(loom.command);
    return Spool::imageFromPixels(shot.pixels.data(), shot.extent.width, shot.extent.height,
        Spool::ChannelOrder::BGRA).pixels;
}

size_t brightness(const std::vector<uint8_t>& pixels){
    size_t sum = 0;
    for(uint8_t value : pixels) sum += value;
    return sum;
}

}

int main(){
    TestReport report("tier1 lights");

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_tier1_lights";
    std::filesystem::remove_all(work);
    const std::string texturePath = (work / "checker.png").string();
    {
        const std::vector<uint8_t> checker = makeCheckerboard(64, 8);
        Spool::savePng(texturePath, Spool::imageFromPixels(checker.data(), 64, 64, Spool::ChannelOrder::RGBA));
    }

    auto tier1 = [&](bool bulbCastsShadows, uint32_t* lightsOut = nullptr){
        Loom::Scene scene(Loom::Preset::Offscreen);
        scene.setSize(sceneWidth, sceneHeight);
        const Loom::TextureHandle texture = scene.loadTexture(texturePath);

        //One line. The scene owns it, adds it to the renderer, builds the cube, and drives
        //the six face passes over the draws that have not even been written yet
        scene.addLight(bulbConfig(), bulbCastsShadows);

        if(lightsOut) *lightsOut = scene.lightCount();

        scene.startRendering();
            scene.drawPlane(texture, floorTransform());
            scene.drawCube(texture, blockTransform());
        scene.endRendering();

        return scene.readPixels();
    };

    // -------------------------------------------------------------------------------
    // Svjetlo je stiglo, i scena ga broji
    // -------------------------------------------------------------------------------

    uint32_t lights = 0;
    const std::vector<uint8_t> casting = tier1(true, &lights);

    report.check("dva svjetla", lights == 2,
        fmt("presetovo sunce plus dodano tockasto: %u", lights));

    // -------------------------------------------------------------------------------
    // Bajt za bajt isto kao rucno pisanih sest prolaza
    // -------------------------------------------------------------------------------

    const std::vector<uint8_t> byHand = handWritten(texturePath);
    const ByteDiff difference = diffBytes(casting, byHand);

    report.check("kocka iz stepenice 1 = rucno",
        difference.different == 0 && casting.size() == byHand.size(),
        fmt("%zu razlicitih od %zu bajtova, max delta %zu",
            difference.different, casting.size(), difference.maxDelta));

    // -------------------------------------------------------------------------------
    // I sjena stvarno postoji: bez nje je ista scena svjetlija
    // -------------------------------------------------------------------------------

    const std::vector<uint8_t> notCasting = tier1(false);
    const ByteDiff shadowMakesADifference = diffBytes(casting, notCasting);

    report.check("kocka baca sjenu",
        shadowMakesADifference.different > 0 && brightness(casting) < brightness(notCasting),
        fmt("%zu bajtova drukcije, svjetlina %zu sa sjenom vs %zu bez nje",
            shadowMakesADifference.different, brightness(casting), brightness(notCasting)));

    // -------------------------------------------------------------------------------
    // Svjetlo koje scena posjeduje se smije micati
    // -------------------------------------------------------------------------------

    {
        Loom::Scene scene(Loom::Preset::Offscreen);
        scene.setSize(sceneWidth, sceneHeight);
        const Loom::TextureHandle texture = scene.loadTexture(texturePath);

        Light& bulb = scene.addLight(bulbConfig(), true);

        auto draw = [&](){
            scene.startRendering();
                scene.drawPlane(texture, floorTransform());
                scene.drawCube(texture, blockTransform());
            scene.endRendering();
            return scene.readPixels();
        };

        const std::vector<uint8_t> before = draw();
        bulb.setPosition({2.5f, 3.0f, 1.5f});
        const std::vector<uint8_t> after = draw();

        report.check("svjetlo se mice", diffBytes(before, after).different > 0,
            fmt("pomak zarulje mijenja %zu bajtova", diffBytes(before, after).different));
    }

    // -------------------------------------------------------------------------------
    // Granice: sto renderer ne moze, scena kaze naglas
    // -------------------------------------------------------------------------------

    {
        Loom::Scene scene(Loom::Preset::Offscreen);
        scene.setSize(128, 128);
        scene.addLight(bulbConfig(), true);

        bool secondCubeThrew = false;
        try{ scene.addLight(bulbConfig(), true); }
        catch(const std::exception&){ secondCubeThrew = true; }
        //And the refusal has to leave nothing behind. Before the check moved ahead of the
        //push, a refused light was still in the scene: lighting the picture, counted in
        //lightCount, and with nobody holding a reference to it
        report.check("druga kocka", secondCubeThrew && scene.lightCount() == 2,
            fmt("baca iznimku i ne ostavlja nista za sobom: %u svjetala", scene.lightCount()));

        //A second point light without shadows is fine, and that is the difference worth saying
        LightConfig quiet = bulbConfig();
        quiet.position = {-2.0f, 2.0f, 0.0f};
        scene.addLight(quiet, false);
        report.check("bez sjene ih ide vise", scene.lightCount() == 3,
            fmt("%u svjetala, od kojih jedno baca", scene.lightCount()));

        bool secondDirectionalThrew = false;
        try{
            LightConfig anotherSun;
            anotherSun.type = LightType::Directional;
            scene.addLight(anotherSun, true);
        }
        catch(const std::exception&){ secondDirectionalThrew = true; }
        report.check("drugo usmjereno sa sjenom", secondDirectionalThrew,
            "baca iznimku - presetovo sunce vec drzi kartu");
    }

    //With the preset's sun turned off, a directional light of your own can take the map
    {
        Loom::Scene scene(Loom::Preset::Offscreen);
        scene.setSize(128, 128);
        scene.setShadows(false);

        bool ownSunWorked = true;
        try{
            LightConfig ownSun;
            ownSun.type = LightType::Directional;
            ownSun.direction = {0.0f, -1.0f, -0.2f};
            scene.addLight(ownSun, true);
        }
        catch(const std::exception&){ ownSunWorked = false; }

        report.check("vlastito sunce", ownSunWorked,
            "uz setShadows(false) karta je slobodna");
    }

    std::filesystem::remove_all(work);

    report.checkNoValidationMessages();
    return report.result();
}
