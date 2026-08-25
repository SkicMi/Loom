// tier 1 presets: the other two presets, held to the same standard as the first.
//
// Preset::Offscreen was proved byte identical to hand written tier 2 code already. Lit3D and
// Flat2D were only ever proved to compile, and a preset that compiles is not a preset that
// agrees with the library underneath it. Loom::Sequence was in the same position: Spool's
// writer is tested, but nobody had checked that tier 1 hands it the frame it just drew.
//
// Each tier 2 half below is also documentation - it is what the preset expands to, written
// by reading presetConfig() and nothing else.
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

glm::mat4 floorTransform(){ return glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)); }
glm::mat4 blockTransform(){
    return glm::rotate(glm::translate(glm::mat4(1.0f), {0.0f,0.5f,0.0f}), 0.6f, glm::vec3(0.25f,1.0f,0.1f));
}
glm::mat4 spriteTransform(float scale){ return glm::scale(glm::mat4(1.0f), glm::vec3(scale)); }

//Flat2D turns a plane to face the camera. The tier 2 half has to do the same or it is
//drawing something else and calling it equal
glm::mat4 asSprite(const glm::mat4& transform){
    return transform * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0));
}

std::string writeSolid(const std::filesystem::path& path, uint8_t r, uint8_t g, uint8_t b){
    std::vector<uint8_t> pixels(16 * 16 * 4);
    for(size_t i = 0; i + 3 < pixels.size(); i += 4){
        pixels[i+0] = r; pixels[i+1] = g; pixels[i+2] = b; pixels[i+3] = 255;
    }
    Spool::savePng(path.string(), Spool::imageFromPixels(pixels.data(), 16, 16, Spool::ChannelOrder::RGBA));
    return path.string();
}

//-------------------------------------------------------------------------------------------
// Lit3D, rukom
//-------------------------------------------------------------------------------------------
std::vector<uint8_t> handWrittenLit3D(const std::string& texturePath){
    LoomConfig config;
    config.appName = "Loom";
    config.engineName = "Loom";
    config.width = sceneWidth;
    config.height = sceneHeight;
    config.swapchainConfig.allowReadback = true;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eBack;
    config.rendererConfig.clearColor = {0.02f, 0.02f, 0.04f, 1.0f};
    config.rendererConfig.maxPassesPerFrame = 16;

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

    LightConfig sunConfig;
    sunConfig.type = LightType::Directional;
    sunConfig.direction = {-0.45f, -1.0f, -0.35f};
    sunConfig.color = {1.0f, 0.96f, 0.88f};
    sunConfig.intensity = 1.0f;
    Light sun(sunConfig);
    loom.renderer.addLight(sun);

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

    int drawn = 0;
    while(drawn < 2 && !loom.shouldClose()){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.beginPass(shadowMap, sun);
            shapes.plane(shadowMaterial, floorTransform());
            shapes.cube(shadowMaterial, blockTransform());
        loom.renderer.endPass();

        loom.renderer.beginPass();
            shapes.plane(texture, floorTransform());
            shapes.cube(texture, blockTransform());
        loom.renderer.endPass();

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const ImageData shot = loom.renderer.readLastFrame();
    return Spool::imageFromPixels(shot.pixels.data(), shot.extent.width, shot.extent.height,
        Spool::ChannelOrder::BGRA).pixels;
}

//-------------------------------------------------------------------------------------------
// Flat2D, rukom
//-------------------------------------------------------------------------------------------
std::vector<uint8_t> handWrittenFlat2D(const std::string& firstPath, const std::string& secondPath){
    LoomConfig config;
    config.appName = "Loom";
    config.engineName = "Loom";
    config.width = sceneWidth;
    config.height = sceneHeight;
    config.swapchainConfig.allowReadback = true;
    config.enableDepth = false;
    config.pipelineConfig.depthTestEnable = false;
    config.pipelineConfig.depthWriteEnable = false;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.rendererConfig.clearColor = {0.05f, 0.05f, 0.08f, 1.0f};

    LoomInitializer loom(config);

    LoomShapes::PrimitivesConfig primitivesConfig;
    primitivesConfig.cullMode = config.pipelineConfig.cullMode;
    primitivesConfig.depthTest = config.pipelineConfig.depthTestEnable;
    LoomShapes::Primitives shapes(loom, primitivesConfig);

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 2.0f};
    cameraConfig.target = {0.0f, 0.0f, 0.0f};
    Camera camera(cameraConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {1.0f, 1.0f, 1.0f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    //No light at all: Flat2D's ambient carries the whole picture, which is why a sprite
    //shows up as its own colour rather than as a shaded surface
    const Spool::Image firstDecoded = Spool::loadImage(firstPath);
    const Spool::Image secondDecoded = Spool::loadImage(secondPath);
    Texture first(loom.device, loom.command, firstDecoded.pixels.data(),
        vk::Extent2D{firstDecoded.width, firstDecoded.height});
    Texture second(loom.device, loom.command, secondDecoded.pixels.data(),
        vk::Extent2D{secondDecoded.width, secondDecoded.height});

    int drawn = 0;
    while(drawn < 2 && !loom.shouldClose()){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.beginPass();
            shapes.plane(first, asSprite(spriteTransform(1.4f)));
            shapes.plane(second, asSprite(spriteTransform(0.7f)));
        loom.renderer.endPass();

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const ImageData shot = loom.renderer.readLastFrame();
    return Spool::imageFromPixels(shot.pixels.data(), shot.extent.width, shot.extent.height,
        Spool::ChannelOrder::BGRA).pixels;
}

//The colour at the very centre of the picture, where the small sprite sits on top of the big one
struct Rgb{ uint8_t r,g,b; };
Rgb centre(const std::vector<uint8_t>& pixels){
    const size_t i = (size_t(sceneHeight/2) * sceneWidth + sceneWidth/2) * 4;
    return {pixels[i], pixels[i+1], pixels[i+2]};
}

}

int main(){
    TestReport report("tier1 presets");

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_tier1_presets";
    std::filesystem::remove_all(work);

    const std::string checkerPath = (work / "checker.png").string();
    {
        const std::vector<uint8_t> checker = makeCheckerboard(64, 8);
        Spool::savePng(checkerPath, Spool::imageFromPixels(checker.data(), 64, 64, Spool::ChannelOrder::RGBA));
    }
    const std::string redPath = writeSolid(work / "red.png", 220, 30, 30);
    const std::string bluePath = writeSolid(work / "blue.png", 30, 30, 220);

    // -------------------------------------------------------------------------------
    // Lit3D
    // -------------------------------------------------------------------------------

    std::vector<uint8_t> lit3dTier1;
    {
        Loom::Scene scene(Loom::Preset::Lit3D);
        scene.setSize(sceneWidth, sceneHeight);
        const Loom::TextureHandle texture = scene.loadTexture(checkerPath);

        //Two frames, the same as the hand written half: a window hands out a different image
        //each frame, and one frame apart would be comparing different swapchain images
        for(int frame = 0; frame < 2; ++frame){
            scene.startRendering();
                scene.drawPlane(texture, floorTransform());
                scene.drawCube(texture, blockTransform());
            scene.endRendering();
        }
        lit3dTier1 = scene.readPixels();
    }

    const std::vector<uint8_t> lit3dTier2 = handWrittenLit3D(checkerPath);

    report.check("Lit3D nesto crta", countNonBlack(lit3dTier1) > 5000,
        fmt("%zu ne-crnih piksela", countNonBlack(lit3dTier1)));

    const ByteDiff lit3dDifference = diffBytes(lit3dTier1, lit3dTier2);
    report.check("Lit3D = rucno", lit3dDifference.different == 0 && lit3dTier1.size() == lit3dTier2.size(),
        fmt("%zu razlicitih od %zu bajtova, max delta %zu",
            lit3dDifference.different, lit3dTier1.size(), lit3dDifference.maxDelta));

    // -------------------------------------------------------------------------------
    // Flat2D
    // -------------------------------------------------------------------------------

    std::vector<uint8_t> flat2dTier1;
    {
        Loom::Scene scene(Loom::Preset::Flat2D);
        scene.setSize(sceneWidth, sceneHeight);
        const Loom::TextureHandle red = scene.loadTexture(redPath);
        const Loom::TextureHandle blue = scene.loadTexture(bluePath);

        for(int frame = 0; frame < 2; ++frame){
            scene.startRendering();
                scene.drawSprite(red, spriteTransform(1.4f));
                scene.drawSprite(blue, spriteTransform(0.7f));
            scene.endRendering();
        }
        flat2dTier1 = scene.readPixels();
    }

    const std::vector<uint8_t> flat2dTier2 = handWrittenFlat2D(redPath, bluePath);

    const ByteDiff flat2dDifference = diffBytes(flat2dTier1, flat2dTier2);
    report.check("Flat2D = rucno", flat2dDifference.different == 0 && flat2dTier1.size() == flat2dTier2.size(),
        fmt("%zu razlicitih od %zu bajtova, max delta %zu",
            flat2dDifference.different, flat2dTier1.size(), flat2dDifference.maxDelta));

    // -------------------------------------------------------------------------------
    // I ono sto Flat2D zapravo JEST: redoslijed odlucuje, ne udaljenost
    // -------------------------------------------------------------------------------

    std::vector<uint8_t> otherOrder;
    {
        Loom::Scene scene(Loom::Preset::Flat2D);
        scene.setSize(sceneWidth, sceneHeight);
        const Loom::TextureHandle red = scene.loadTexture(redPath);
        const Loom::TextureHandle blue = scene.loadTexture(bluePath);

        //The same two sprites at the same two sizes, drawn the other way round. Both sit at
        //z = 0, so nothing but the order can decide which one is seen
        for(int frame = 0; frame < 2; ++frame){
            scene.startRendering();
                scene.drawSprite(blue, spriteTransform(0.7f));
                scene.drawSprite(red, spriteTransform(1.4f));
            scene.endRendering();
        }
        otherOrder = scene.readPixels();
    }

    const Rgb blueOnTop = centre(flat2dTier1);
    const Rgb redOnTop = centre(otherOrder);

    report.check("redoslijed odlucuje",
        blueOnTop.b > blueOnTop.r && redOnTop.r > redOnTop.b,
        fmt("plavi zadnji -> (%u,%u,%u), crveni zadnji -> (%u,%u,%u)",
            blueOnTop.r, blueOnTop.g, blueOnTop.b, redOnTop.r, redOnTop.g, redOnTop.b));

    //Without depth there is nothing to sort by, and that is the whole of what makes it 2D
    const Loom::Scene flatConfig(Loom::Preset::Flat2D);
    const Loom::Scene litConfig(Loom::Preset::Lit3D);
    report.check("Flat2D nema dubinu",
        !flatConfig.config().enableDepth && litConfig.config().enableDepth,
        "Flat2D bez dubine, Lit3D s njom");

    // -------------------------------------------------------------------------------
    // Loom::Sequence - stepenica 1 predaje Spoolu ono sto je upravo nacrtala
    // -------------------------------------------------------------------------------

    const std::filesystem::path outputDirectory = work / "seq";

    std::vector<std::vector<uint8_t>> drawn;
    std::vector<std::string> paths;
    {
        Loom::Scene scene(Loom::Preset::Offscreen);
        scene.setSize(160, 120);
        const Loom::TextureHandle texture = scene.loadTexture(checkerPath);

        Loom::Sequence sequence;
        sequence.setDirectory(outputDirectory.string());
        sequence.setPrefix("tier1_");

        for(uint32_t frame = 0; frame < 4; ++frame){
            scene.setFrame(frame, 24.0f);
            const float time = scene.time();

            scene.startRendering();
                scene.drawCube(texture, glm::rotate(glm::mat4(1.0f), time * 4.0f, glm::vec3(0.2f,1.0f,0.1f)));
            scene.endRendering();

            drawn.push_back(scene.readPixels());
            paths.push_back(sequence.write(scene));
        }

        report.check("Sequence broji", sequence.frameCount() == 4,
            fmt("%u frameova, zadnji %s", sequence.frameCount(),
                std::filesystem::path(paths.back()).filename().string().c_str()));
    }

    size_t mismatched = 0;
    size_t worstDelta = 0;
    for(size_t frame = 0; frame < paths.size(); ++frame){
        const Spool::Image fromDisk = Spool::loadImage(paths[frame]);
        const ByteDiff difference = diffBytes(drawn[frame], fromDisk.pixels);
        if(difference.different > 0) ++mismatched;
        worstDelta = std::max(worstDelta, difference.maxDelta);
    }
    report.check("disk = nacrtano", mismatched == 0 && worstDelta == 0,
        fmt("%zu od %zu frameova se razlikuje, najveca delta %zu", mismatched, paths.size(), worstDelta));

    //setFrame really drives the picture, so the comparison above is not passing on four
    //copies of one still
    const ByteDiff moved = diffBytes(drawn.front(), drawn.back());
    report.check("sekvenca se mice", moved.different > 0,
        fmt("prvi i zadnji frame razlikuju se u %zu bajtova", moved.different));

    // -------------------------------------------------------------------------------
    // Presetovi se stvarno razlikuju
    // -------------------------------------------------------------------------------

    report.check("presetovi nisu isti", diffBytes(lit3dTier1, flat2dTier1).different > 0,
        "Lit3D i Flat2D daju razlicite slike iz istog API-ja");

    std::filesystem::remove_all(work);

    report.checkNoValidationMessages();
    return report.result();
}
