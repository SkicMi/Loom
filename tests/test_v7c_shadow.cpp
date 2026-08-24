// v7c: a shadow map is sampled, and a shadowed surface keeps exactly its ambient term
// v7d: depth bias removes acne, and paying too much of it detaches the shadow
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/Light.h"
#include "Vulkan/Material.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/RenderTarget.h"
#include <glm/gtc/matrix_transform.hpp>

//A flat square in the XZ plane at y = 0, white, facing up. The surface the shadow lands on
static std::vector<Vertex> floorQuad(float half){
    return {
        {{-half,0.0f,-half},{1,1,1},{0,1},{0,1,0}},
        {{ half,0.0f,-half},{1,1,1},{1,1},{0,1,0}},
        {{ half,0.0f, half},{1,1,1},{1,0},{0,1,0}},
        {{-half,0.0f, half},{1,1,1},{0,0},{0,1,0}},
    };
}

static std::vector<uint16_t> quadIndices(){
    return {0,1,2, 2,3,0};
}

//Where a world point lands on screen, from the same matrices the GPU was handed
static glm::vec2 projectToPixels(const glm::mat4& viewProjection, const glm::vec3& world, vk::Extent2D extent){
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * float(extent.width),
            (ndc.y * 0.5f + 0.5f) * float(extent.height)};
}

static size_t centresInside(glm::vec2 a, glm::vec2 b, vk::Extent2D extent){
    const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
    const float y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);

    size_t count = 0;
    for(uint32_t y = 0; y < extent.height; ++y){
        const float cy = float(y) + 0.5f;
        if(cy <= y0 || cy >= y1) continue;
        for(uint32_t x = 0; x < extent.width; ++x){
            const float cx = float(x) + 0.5f;
            if(cx > x0 && cx < x1) ++count;
        }
    }
    return count;
}

//The swapchain and the targets are BGRA, so the blue channel is byte 0. Every surface here
//is grey, so one channel says everything
static uint8_t channelAt(const std::vector<uint8_t>& pixels, vk::Extent2D extent, uint32_t x, uint32_t y){
    return pixels[(size_t(y) * extent.width + x) * 4];
}

struct Counts{
    size_t shadowed = 0; //exactly the ambient term
    size_t lit = 0;      //saturated white
    size_t partial = 0;  //the filtered band along a shadow edge
    size_t background = 0;
};

static Counts classify(const std::vector<uint8_t>& pixels, vk::Extent2D extent, uint8_t ambientByte){
    Counts counts;
    for(uint32_t y = 0; y < extent.height; ++y){
        for(uint32_t x = 0; x < extent.width; ++x){
            const uint8_t value = channelAt(pixels, extent, x, y);
            if(value == 0) ++counts.background;         //nothing drawn there, the clear colour
            else if(value == ambientByte) ++counts.shadowed;
            else if(value == 255) ++counts.lit;
            else ++counts.partial;
        }
    }
    return counts;
}

int main(){
    TestReport report("v7c shadow lookup");

    const vk::Extent2D size{512,512};
    const vk::Extent2D mapSize{1024,1024};

    LoomConfig config;
    config.width = 512; config.height = 512;
    config.appName = "v7c"; config.engineName = "Loom tests";
    config.enableDepth = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    const float floorHalf = 1.5f;
    Mesh floor(loom.device, loom.command, floorQuad(floorHalf), quadIndices());
    Mesh cube(loom.device, loom.command, cubeVertices(), cubeIndices());

    //The cube hangs above the floor and is never drawn into the camera's picture - only into
    //the shadow map. What the camera sees is a bare floor with a square of shadow on it
    const glm::mat4 cubeModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 identity = glm::mat4(1.0f);

    //Straight down at the floor. Up is +Z because a camera looking along -Y cannot also call
    //+Y up - quatLookAt would be handed two parallel vectors
    CameraConfig camConfig;
    camConfig.position = {0.0f, 5.0f, 0.0f};
    camConfig.target = {0.0f, 0.0f, 0.0f};
    camConfig.up = {0.0f, 0.0f, 1.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);

    const float ambient = 0.2f;
    EnvironmentConfig envConfig; envConfig.ambientColor = {ambient, ambient, ambient};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig lightConfig;
    lightConfig.type = LightType::Directional;
    lightConfig.direction = {0.0f, -1.0f, 0.0f};
    lightConfig.shadowCenter = {0.0f, 0.0f, 0.0f};
    lightConfig.shadowExtent = 2.0f;
    lightConfig.shadowNear = 0.1f;
    lightConfig.shadowFar = 50.0f;
    Light light(lightConfig);
    loom.renderer.addLight(light);

    // -------------------------------------------------------------------------------
    // Pipelineovi za shadow prolaz: bez biasa i s njim
    // -------------------------------------------------------------------------------

    RenderTarget shadowCube(loom.device, mapSize, makeShadowMapConfig());
    RenderTarget shadowSelfNoBias(loom.device, mapSize, makeShadowMapConfig());
    RenderTarget shadowSelfBias(loom.device, mapSize, makeShadowMapConfig());

    PipelineConfig shadowPipelineConfig;
    shadowPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/shadow.vert.spv";
    shadowPipelineConfig.fragShaderPath = "";
    shadowPipelineConfig.enableColor = false;
    shadowPipelineConfig.vertexAttributes = Vertex::getPositionAttribute();
    shadowPipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    shadowPipelineConfig.depthTestEnable = true;
    shadowPipelineConfig.depthWriteEnable = true;

    VulkanGraphicsPipeline shadowPipeline(loom.device, shadowPipelineConfig,
        loom.getColorFormat(), shadowCube.getDepthFormat());

    //The same pipeline with the rasteriser pushing every fragment away from the light
    PipelineConfig biasedConfig = shadowPipelineConfig;
    biasedConfig.depthBiasEnable = true;
    biasedConfig.depthBiasConstant = 4.0f;
    biasedConfig.depthBiasSlope = 4.0f;

    VulkanGraphicsPipeline biasedPipeline(loom.device, biasedConfig,
        loom.getColorFormat(), shadowCube.getDepthFormat());

    Material shadowMaterial(shadowPipeline);
    Material biasedMaterial(biasedPipeline);

    // -------------------------------------------------------------------------------
    // Ciljevi u koje kamera crta
    // -------------------------------------------------------------------------------

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;

    RenderTarget outShadowed(loom.device, size, readConfig);
    RenderTarget outNoMap(loom.device, size, readConfig);
    RenderTarget outTiltedClean(loom.device, size, readConfig);
    RenderTarget outAcne(loom.device, size, readConfig);
    RenderTarget outBiased(loom.device, size, readConfig);
    RenderTarget outPeterPan(loom.device, size, readConfig);

    // -------------------------------------------------------------------------------
    // Jedan prolaz kroz scenu po konfiguraciji
    // -------------------------------------------------------------------------------

    //One shadow map is sampled per frame, because the descriptor for binding 2 is rewritten
    //in beginFrame - so each configuration gets its own frames
    auto render = [&](const RenderTarget* map, const Material& shadowMat, bool cubeCasts,
                      bool floorCasts, float bias, RenderTarget& out, const glm::mat4& floorModel){
        if(map){
            //Fitting is deliberately off here: every number below is predicted from the
            //light's own shadowExtent of 2.0, and a box refitted to the camera would be a
            //different box. Fitting has its own test
            ShadowConfig shadowConfig;
            shadowConfig.depthBias = bias;
            shadowConfig.fitToCamera = false;
            loom.renderer.setShadowMap(*map, light, shadowConfig);
        }
        else{
            loom.renderer.clearShadowMap();
        }

        int drawn = 0;
        while(drawn < 3 && !loom.shouldClose()){
            loom.pollEvents();
            if(!loom.renderer.beginFrame()) continue;

            if(map){
                loom.renderer.beginPass(*map, light);
                if(cubeCasts){
                    loom.renderer.draw(cube, cubeModel, shadowMat);
                }
                if(floorCasts){
                    loom.renderer.draw(floor, floorModel, shadowMat);
                }
                loom.renderer.endPass();
            }

            loom.renderer.beginPass(out);
            loom.renderer.draw(floor, floorModel);
            loom.renderer.endPass();

            loom.renderer.beginPass();
            loom.renderer.draw(floor, floorModel);
            loom.renderer.endPass();

            loom.renderer.endFrame();
            ++drawn;
        }
        loom.waitIdle();
    };

    //Acne needs a slope. A plane square on to the light stores one depth across a whole
    //texel and compares against exactly that depth, so it can never shadow itself - which is
    //why the flat floor above is the wrong surface to look for acne on. Tilted sixty degrees,
    //one texel of the shadow map covers a range of depths, and half of that range is nearer
    //than what got stored
    const glm::mat4 tiltedFloor = glm::rotate(glm::mat4(1.0f), glm::radians(60.0f), glm::vec3(0.0f,0.0f,1.0f));

    //The cube alone in the map: a clean shadow, nothing self shadowing
    render(&shadowCube, shadowMaterial, true, false, 0.0005f, outShadowed, identity);

    //No map at all: the control, nothing may be shadowed
    render(nullptr, shadowMaterial, false, false, 0.0f, outNoMap, identity);

    //The tilted floor with no shadow map at all - the reference picture acne is measured against
    render(nullptr, shadowMaterial, false, false, 0.0f, outTiltedClean, tiltedFloor);

    //The same floor, now its own occluder, with nothing biasing the comparison
    render(&shadowSelfNoBias, shadowMaterial, false, true, 0.0f, outAcne, tiltedFloor);

    //And again with the rasteriser pushing the shadow pass away from the light
    render(&shadowSelfBias, biasedMaterial, false, true, 0.0f, outBiased, tiltedFloor);

    //Far too much bias: the shadow should let go of the cube entirely
    render(&shadowCube, shadowMaterial, true, false, 0.05f, outPeterPan, identity);

    // -------------------------------------------------------------------------------
    // v7c: sjena postoji, i tocno je ambijent
    // -------------------------------------------------------------------------------

    //A shadowed pixel loses the whole contribution of the light and keeps the ambient term
    //untouched, so its value is computable before a single pixel is read
    const uint8_t ambientByte = encodeByte(ambient);

    const std::vector<uint8_t> shadowed = outShadowed.readPixels(loom.command).pixels;
    const Counts shadowedCounts = classify(shadowed, size, ambientByte);

    const glm::mat4 cameraViewProjection = camera.getProjection(size.width, size.height) * camera.getView();
    const glm::vec2 cornerA = projectToPixels(cameraViewProjection, {-0.5f, 0.0f, -0.5f}, size);
    const glm::vec2 cornerB = projectToPixels(cameraViewProjection, { 0.5f, 0.0f,  0.5f}, size);
    const size_t footprint = centresInside(cornerA, cornerB, size);

    report.check("sjena postoji", shadowedCounts.shadowed > 0,
        fmt("%zu piksela u sjeni, %zu osvijetljenih, %zu na rubu",
            shadowedCounts.shadowed, shadowedCounts.lit, shadowedCounts.partial));

    //The light shines straight down, so the cube's shadow on the floor is exactly the cube's
    //own square. The filtered edge is the only thing between the two counts
    report.check("otisak", shadowedCounts.shadowed <= footprint &&
                           shadowedCounts.shadowed + shadowedCounts.partial >= footprint,
        fmt("%zu <= %zu <= %zu, analiticki otisak %zu",
            shadowedCounts.shadowed, footprint, shadowedCounts.shadowed + shadowedCounts.partial, footprint));

    //The edge is a band around the square, not a smear across it
    report.check("rub je uzak", shadowedCounts.partial < footprint / 4,
        fmt("%zu rubnih piksela na otisak od %zu", shadowedCounts.partial, footprint));

    //The value itself, and this is the whole claim of v7c: shadow removes the light and
    //nothing else. Not "darker" - exactly the ambient term, to the byte
    const uint32_t centreX = uint32_t((cornerA.x + cornerB.x) * 0.5f);
    const uint32_t centreY = uint32_t((cornerA.y + cornerB.y) * 0.5f);
    const uint8_t centreValue = channelAt(shadowed, size, centreX, centreY);

    report.check("tocno ambijent", centreValue == ambientByte,
        fmt("sredina sjene je %u, ambijent %.2f kodiran je %u", centreValue, double(ambient), ambientByte));

    // -------------------------------------------------------------------------------
    // Kontrola: bez karte nema sjene
    // -------------------------------------------------------------------------------

    const std::vector<uint8_t> noMap = outNoMap.readPixels(loom.command).pixels;
    const Counts noMapCounts = classify(noMap, size, ambientByte);

    report.check("bez karte", noMapCounts.shadowed == 0 && noMapCounts.partial == 0,
        fmt("%zu u sjeni, %zu na rubu, %zu osvijetljenih",
            noMapCounts.shadowed, noMapCounts.partial, noMapCounts.lit));

    // -------------------------------------------------------------------------------
    // v7d: akne, i lijek za njih
    // -------------------------------------------------------------------------------

    //Acne is not "a dark pixel" - it is a pixel that changed when the floor became its own
    //occluder. Measured as a difference against the very same floor rendered with no shadow
    //map at all, so no lighting value has to be predicted and no shadow has to be excluded
    const std::vector<uint8_t> tiltedClean = outTiltedClean.readPixels(loom.command).pixels;
    const std::vector<uint8_t> acne = outAcne.readPixels(loom.command).pixels;
    const std::vector<uint8_t> biased = outBiased.readPixels(loom.command).pixels;

    const ByteDiff acneDiff = diffBytes(tiltedClean, acne);
    const ByteDiff biasedDiff = diffBytes(tiltedClean, biased);

    //If the version without bias were clean, the version with it would prove nothing
    report.check("akne bez biasa", acneDiff.different > 0,
        fmt("%zu bajtova se razlikuje od iste plohe bez karte, max delta %zu",
            acneDiff.different, acneDiff.maxDelta));

    report.check("bias cisti akne", biasedDiff.different == 0,
        fmt("%zu bajtova razlike, bez biasa ih je bilo %zu", biasedDiff.different, acneDiff.different));

    //The rasteriser bias has to be visible in the map itself, not only in the picture
    const ImageData mapNoBias = shadowSelfNoBias.readDepthPixels(loom.command);
    const ImageData mapBias = shadowSelfBias.readDepthPixels(loom.command);

    size_t pushedBack = 0;
    size_t compared = 0;
    for(uint32_t y = 0; y < mapSize.height; y += 8){
        for(uint32_t x = 0; x < mapSize.width; x += 8){
            const float plain = depthAt(mapNoBias, x, y);
            const float shifted = depthAt(mapBias, x, y);
            if(plain == 1.0f || shifted == 1.0f) continue; //nothing was drawn there
            ++compared;
            if(shifted > plain) ++pushedBack;
        }
    }

    report.check("bias u rasterizeru", compared > 0 && pushedBack == compared,
        fmt("%zu od %zu uzoraka karte gurnuto dalje od svjetla", pushedBack, compared));

    // -------------------------------------------------------------------------------
    // Bias nije besplatan: previse ga i sjena se otkaci
    // -------------------------------------------------------------------------------

    const std::vector<uint8_t> peterPan = outPeterPan.readPixels(loom.command).pixels;
    const Counts peterPanCounts = classify(peterPan, size, ambientByte);

    report.check("peter panning", peterPanCounts.shadowed == 0,
        fmt("uz bias 0.05 ostalo %zu piksela sjene, bez njega ih je %zu",
            peterPanCounts.shadowed, shadowedCounts.shadowed));

    // -------------------------------------------------------------------------------
    // Granice
    // -------------------------------------------------------------------------------

    bool scratchMapThrew = false;
    try{
        RenderTarget notKept(loom.device, size);
        loom.renderer.setShadowMap(notKept, light);
    }
    catch(const std::exception&){
        scratchMapThrew = true;
    }
    report.check("karta bez keepDepth", scratchMapThrew, "baca iznimku");

    bool pointMapThrew = false;
    try{
        LightConfig pointConfig;
        pointConfig.type = LightType::Point;
        Light point(pointConfig);
        loom.renderer.setShadowMap(shadowCube, point);
    }
    catch(const std::exception&){
        pointMapThrew = true;
    }
    report.check("karta za tockasto", pointMapThrew, "baca iznimku");

    //setShadowMap above may have left the renderer pointing somewhere; put it back
    loom.renderer.clearShadowMap();

    report.checkNoValidationMessages();
    return report.result();
}
