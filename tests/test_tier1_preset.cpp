// tier 1: a program written against <Loom/Loom.h> draws the same image, byte for byte, as
//         the tier 2 program that does the same thing by hand.
//
// That equality is the whole claim of a preset. A convenience layer that quietly does
// something slightly different is not a shortcut, it is a second renderer to debug - and the
// first time its picture disagreed with the explicit one, nobody would know which was right.
//
// The tier 2 half below is also documentation: it is what Preset::Offscreen expands to, and
// it was written by reading presetConfig() and nothing else.
#include "TestHarness.h"
#include "TestScene.h"

#include <Loom/Loom.h>

#include "Core/LoomConfig.h"
#include "Core/LoomShapes.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"
#include "Spool/ImageFile.h"

#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 480;
const uint32_t sceneHeight = 320;

//The same three transforms for both halves, so nothing but the API differs
glm::mat4 planeTransform(){ return glm::scale(glm::mat4(1.0f), glm::vec3(12.0f)); }
glm::mat4 cubeTransform(){
    return glm::rotate(glm::translate(glm::mat4(1.0f), {-1.6f,0.5f,0.0f}), 0.7f, glm::vec3(0.3f,1.0f,0.15f));
}
glm::mat4 sphereTransform(){
    return glm::scale(glm::translate(glm::mat4(1.0f), {0.0f,0.8f,0.0f}), glm::vec3(1.2f));
}
glm::mat4 pyramidTransform(){
    return glm::rotate(glm::translate(glm::mat4(1.0f), {1.6f,0.5f,0.0f}), -0.5f, glm::vec3(0.0f,1.0f,0.0f));
}

//-------------------------------------------------------------------------------------------
// Stepenica 1
//-------------------------------------------------------------------------------------------
std::vector<uint8_t> renderTier1(const std::string& texturePath){
    Loom::Scene scene(Loom::Preset::Offscreen);
    scene.setSize(sceneWidth, sceneHeight);

    const Loom::TextureHandle texture = scene.loadTexture(texturePath);

    //Camera, sun and ambient are left exactly as the preset set them - that is what is being
    //compared. Touching them here would be testing the setters instead
    scene.startRendering();
        scene.drawPlane(texture, planeTransform());
        scene.drawCube(texture, cubeTransform());
        scene.drawSphere(texture, sphereTransform());
        scene.drawPyramid(texture, pyramidTransform());
    scene.endRendering();

    return scene.readPixels();
}

//-------------------------------------------------------------------------------------------
// Stepenica 2 - isto, rukom. Ovo je ono u sto se Preset::Offscreen razvija
//-------------------------------------------------------------------------------------------
std::vector<uint8_t> renderTier2(const std::string& texturePath){
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
    config.rendererConfig.maxPassesPerFrame = 8;
    config.headlessColorFormat = vk::Format::eB8G8R8A8Srgb;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;

    LoomInitializer loom(config);
    LoomShapes::Primitives shapes(loom);

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
    targetConfig.enableDepth = true;
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
        //Same order the queue is replayed in: shadow pass first, then the camera pass
        loom.renderer.beginPass(shadowMap, sun);
            shapes.plane(shadowMaterial, planeTransform());
            shapes.cube(shadowMaterial, cubeTransform());
            shapes.sphere(shadowMaterial, sphereTransform());
            shapes.pyramid(shadowMaterial, pyramidTransform());
        loom.renderer.endPass();

        loom.renderer.beginPass(offscreen);
            shapes.plane(texture, planeTransform());
            shapes.cube(texture, cubeTransform());
            shapes.sphere(texture, sphereTransform());
            shapes.pyramid(texture, pyramidTransform());
        loom.renderer.endPass();

        loom.renderer.endFrame();
    }
    loom.waitIdle();

    const ImageData shot = offscreen.readPixels(loom.command);

    //Tier 1 hands back RGBA, so this half is converted to match rather than the other way
    return Spool::imageFromPixels(shot.pixels.data(), shot.extent.width, shot.extent.height,
        Spool::ChannelOrder::BGRA).pixels;
}

}

int main(){
    TestReport report("tier1 preset");

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_tier1";
    std::filesystem::remove_all(work);
    const std::string texturePath = (work / "checker.png").string();

    {
        const std::vector<uint8_t> checker = makeCheckerboard(64, 8);
        Spool::savePng(texturePath,
            Spool::imageFromPixels(checker.data(), 64, 64, Spool::ChannelOrder::RGBA));
    }

    // -------------------------------------------------------------------------------
    // Transform daje istu matricu kao glm napisan rukom
    // -------------------------------------------------------------------------------

    const glm::mat4 byHand = glm::scale(
        glm::rotate(glm::translate(glm::mat4(1.0f), {1.0f,2.0f,3.0f}), 0.4f, glm::vec3(0,1,0)),
        glm::vec3(2.0f));

    const glm::mat4 byTransform = Loom::Transform().at(1.0f,2.0f,3.0f).spun(0.4f, {0,1,0}).scaled(2.0f);

    size_t elementsApart = 0;
    for(int column = 0; column < 4; ++column){
        for(int row = 0; row < 4; ++row){
            if(byHand[column][row] != byTransform[column][row]) ++elementsApart;
        }
    }
    report.check("Transform = glm", elementsApart == 0,
        fmt("%zu od 16 elemenata matrice se razlikuje", elementsApart));

    // -------------------------------------------------------------------------------
    // Ista slika, bajt za bajt
    // -------------------------------------------------------------------------------

    const std::vector<uint8_t> tier1 = renderTier1(texturePath);
    const std::vector<uint8_t> tier2 = renderTier2(texturePath);

    report.check("nesto je nacrtano", countNonBlack(tier1) > 10000,
        fmt("%zu ne-crnih piksela na stepenici 1", countNonBlack(tier1)));

    const ByteDiff difference = diffBytes(tier1, tier2);
    report.check("stepenica 1 = stepenica 2", difference.different == 0 && tier1.size() == tier2.size(),
        fmt("%zu razlicitih od %zu bajtova, max delta %zu",
            difference.different, tier1.size(), difference.maxDelta));

    //If the two halves were both blank, the comparison above would pass on nothing
    report.check("sjena postoji", countNonBlack(tier1) < tier1.size() / 4,
        fmt("%zu ne-crnih od %zu piksela - scena nije puna plocha",
            countNonBlack(tier1), tier1.size() / 4));

    // -------------------------------------------------------------------------------
    // Stepenica 1 govori RGBA, ne BGRA
    // -------------------------------------------------------------------------------

    //The checkerboard is grey, so a swapped channel would not show. This asks the question
    //directly instead: tier 1's bytes and tier 2's raw readback must NOT be the same, and
    //must become the same once the conversion is applied - which the comparison above did
    report.check("RGBA na izlazu", tier1.size() == size_t(sceneWidth) * sceneHeight * 4,
        fmt("%zu bajtova za %u x %u", tier1.size(), sceneWidth, sceneHeight));

    // -------------------------------------------------------------------------------
    // Granice: sto stepenica 1 odbija, i kako to kaze
    // -------------------------------------------------------------------------------

    Loom::Scene scene(Loom::Preset::Offscreen);
    scene.setSize(128, 128);
    const Loom::TextureHandle texture = scene.loadTexture(texturePath);

    //A texture made from pixels and the same pixels through a file have to be the same
    //texture, or "load" and "create" are two different features wearing one name
    {
        Loom::Scene fromMemory(Loom::Preset::Offscreen);
        fromMemory.setSize(sceneWidth, sceneHeight);

        const Spool::Image decoded = Spool::loadImage(texturePath);
        const Loom::TextureHandle made = fromMemory.createTexture(decoded.pixels.data(),
            decoded.width, decoded.height);

        fromMemory.startRendering();
            fromMemory.drawPlane(made, planeTransform());
            fromMemory.drawCube(made, cubeTransform());
            fromMemory.drawSphere(made, sphereTransform());
            fromMemory.drawPyramid(made, pyramidTransform());
        fromMemory.endRendering();

        const ByteDiff sameTexture = diffBytes(tier1, fromMemory.readPixels());
        report.check("createTexture = loadTexture", sameTexture.different == 0,
            fmt("%zu razlicitih od %zu bajtova", sameTexture.different, tier1.size()));

        bool noPixelsThrew = false;
        try{ fromMemory.createTexture(nullptr, 4, 4); }
        catch(const std::exception&){ noPixelsThrew = true; }
        report.check("createTexture bez piksela", noPixelsThrew, "baca iznimku");
    }

    bool sizeAfterBuildThrew = false;
    try{ scene.setSize(64, 64); }
    catch(const std::exception&){ sizeAfterBuildThrew = true; }
    report.check("postavke poslije starta", sizeAfterBuildThrew, "baca iznimku");

    bool drawOutsideThrew = false;
    try{ scene.drawCube(texture); }
    catch(const std::exception&){ drawOutsideThrew = true; }
    report.check("crtanje izvan framea", drawOutsideThrew, "baca iznimku");

    bool badHandleThrew = false;
    scene.startRendering();
    try{ scene.drawCube(Loom::TextureHandle{}); }
    catch(const std::exception&){ badHandleThrew = true; }

    bool doubleStartThrew = false;
    try{ scene.startRendering(); }
    catch(const std::exception&){ doubleStartThrew = true; }
    scene.endRendering();

    report.check("neispravan handle", badHandleThrew, "baca iznimku");
    report.check("dvaput startRendering", doubleStartThrew, "baca iznimku");

    bool endWithoutStartThrew = false;
    try{ scene.endRendering(); }
    catch(const std::exception&){ endWithoutStartThrew = true; }
    report.check("endRendering bez starta", endWithoutStartThrew, "baca iznimku");

    std::filesystem::remove_all(work);

    report.checkNoValidationMessages();
    return report.result();
}
