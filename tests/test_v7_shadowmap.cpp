// v7a: a pass can render depth and nothing else, and that depth survives to be read
// v7b: a pass can be driven by a light, and the depth it writes is what the light's own
//      matrices say it should be
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/Light.h"
#include "Vulkan/Material.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/RenderTarget.h"
#include <glm/gtc/matrix_transform.hpp>

//A square in the XY plane at z = 0, facing +Z. Flat and perpendicular to the camera, so
//every pixel it covers carries the same depth and one number can be predicted for all of them
static std::vector<Vertex> facingQuad(){
    return {
        {{-0.5f,-0.5f,0.0f},{1,1,1},{0,1},{0,0,1}},
        {{ 0.5f,-0.5f,0.0f},{1,1,1},{1,1},{0,0,1}},
        {{ 0.5f, 0.5f,0.0f},{1,1,1},{1,0},{0,0,1}},
        {{-0.5f, 0.5f,0.0f},{1,1,1},{0,0},{0,0,1}},
    };
}

//The same square laid flat in the XZ plane at y = 0. A light shining straight down sees
//this one face on, and the facing quad above edge on - which is why there are two
static std::vector<Vertex> floorQuad(){
    return {
        {{-0.5f,0.0f,-0.5f},{1,1,1},{0,1},{0,1,0}},
        {{ 0.5f,0.0f,-0.5f},{1,1,1},{1,1},{0,1,0}},
        {{ 0.5f,0.0f, 0.5f},{1,1,1},{1,0},{0,1,0}},
        {{-0.5f,0.0f, 0.5f},{1,1,1},{0,0},{0,1,0}},
    };
}

static std::vector<uint16_t> quadIndices(){
    return {0,1,2, 2,3,0};
}

//Where a world point lands, computed on the CPU with the same matrices the GPU was given.
//The GPU rasterises and writes a depth buffer; this multiplies four floats. Two entirely
//different roads to the same number, which is the only reason comparing them proves anything
struct Projected{
    float x = 0.0f; //pixels
    float y = 0.0f; //pixels
    float depth = 0.0f;
};

static Projected project(const glm::mat4& viewProjection, const glm::vec3& world, vk::Extent2D extent){
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;

    Projected out;
    out.x = (ndc.x * 0.5f + 0.5f) * float(extent.width);
    out.y = (ndc.y * 0.5f + 0.5f) * float(extent.height);
    out.depth = ndc.z;
    return out;
}

//How many pixel centres fall inside an axis aligned rectangle. Vulkan covers a pixel when
//its centre is inside the primitive, so this is not an estimate of the coverage - it is the
//coverage, as long as no edge lands exactly on a centre
static size_t centresInside(float x0, float x1, float y0, float y1, vk::Extent2D extent){
    if(x1 < x0) std::swap(x0, x1);
    if(y1 < y0) std::swap(y0, y1);

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

//Every pixel whose depth is not the cleared 1.0 - the silhouette of whatever was drawn
static size_t countWritten(const ImageData& depth, float clearValue = 1.0f){
    size_t count = 0;
    for(uint32_t y = 0; y < depth.extent.height; ++y){
        for(uint32_t x = 0; x < depth.extent.width; ++x){
            if(depthAt(depth, x, y) != clearValue) ++count;
        }
    }
    return count;
}

//The largest distance between any written pixel and the one value they should all carry
static float worstDepthError(const ImageData& depth, float expected, float clearValue = 1.0f){
    float worst = 0.0f;
    for(uint32_t y = 0; y < depth.extent.height; ++y){
        for(uint32_t x = 0; x < depth.extent.width; ++x){
            const float value = depthAt(depth, x, y);
            if(value == clearValue) continue;
            const float error = std::fabs(value - expected);
            if(error > worst) worst = error;
        }
    }
    return worst;
}

int main(){
    TestReport report("v7 shadow map");

    const vk::Extent2D size{256,256};

    LoomConfig config;
    config.width = 256; config.height = 256;
    config.appName = "v7"; config.engineName = "Loom tests";
    config.enableDepth = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    Mesh facing(loom.device, loom.command, facingQuad(), quadIndices());
    Mesh floor(loom.device, loom.command, floorQuad(), quadIndices());

    CameraConfig camConfig; camConfig.position = {0.0f, 0.0f, 2.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig envConfig; envConfig.ambientColor = {0.2f,0.2f,0.2f};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);

    // -------------------------------------------------------------------------------
    // Ciljevi
    // -------------------------------------------------------------------------------

    //Depth and nothing else
    RenderTarget depthOnly(loom.device, size, makeShadowMapConfig());

    //Colour as well, depth kept. The control: if the depth here matches, then dropping the
    //colour attachment changed nothing about what the rasteriser wrote
    RenderTargetConfig withColorConfig;
    withColorConfig.keepDepth = true;
    RenderTarget withColor(loom.device, size, withColorConfig);

    //An ordinary target, depth not kept - the case that must refuse to be read
    RenderTarget scratch(loom.device, size);

    //The shadow map the light renders into
    RenderTarget shadowMap(loom.device, size, makeShadowMapConfig());

    // -------------------------------------------------------------------------------
    // Pipeline bez fragment stagea
    // -------------------------------------------------------------------------------

    PipelineConfig shadowPipelineConfig;
    shadowPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/shadow.vert.spv";
    shadowPipelineConfig.fragShaderPath = "";
    shadowPipelineConfig.enableColor = false;
    shadowPipelineConfig.vertexAttributes = Vertex::getPositionAttribute();
    shadowPipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    shadowPipelineConfig.depthTestEnable = true;
    shadowPipelineConfig.depthWriteEnable = true;

    VulkanGraphicsPipeline shadowPipeline(loom.device, loom.swapchain,
        shadowPipelineConfig, depthOnly.getDepthFormat());

    Material shadowMaterial(shadowPipeline);

    report.check("depth-only pipeline", true, "sagraden bez fragment stagea");

    // -------------------------------------------------------------------------------
    // Svjetlo
    // -------------------------------------------------------------------------------

    LightConfig lightConfig;
    lightConfig.type = LightType::Directional;
    lightConfig.direction = {0.0f, -1.0f, 0.0f}; //straight down
    lightConfig.shadowCenter = {0.0f, 0.0f, 0.0f};
    lightConfig.shadowExtent = 1.0f;             //the floor quad is half a unit, so it fills a quarter of the box
    lightConfig.shadowNear = 0.1f;
    lightConfig.shadowFar = 50.0f;
    Light light(lightConfig);
    loom.renderer.addLight(light);

    // -------------------------------------------------------------------------------
    // Crtanje
    // -------------------------------------------------------------------------------

    const glm::mat4 identity = glm::mat4(1.0f);

    int drawn = 0;
    while(drawn < 3 && !loom.window.shouldClose()){
        loom.window.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        //depth only, driven by the camera
        loom.renderer.beginPass(depthOnly);
        loom.renderer.draw(facing, identity, shadowMaterial);
        loom.renderer.endPass();

        //colour and depth, the ordinary pipeline, the same quad from the same camera
        loom.renderer.beginPass(withColor);
        loom.renderer.draw(facing, identity);
        loom.renderer.endPass();

        //depth that is not kept
        loom.renderer.beginPass(scratch);
        loom.renderer.draw(facing, identity);
        loom.renderer.endPass();

        //depth only, driven by the light
        loom.renderer.beginPass(shadowMap, light);
        loom.renderer.draw(floor, identity, shadowMaterial);
        loom.renderer.endPass();

        //the window, so the frame has something to present
        loom.renderer.beginPass();
        loom.renderer.draw(facing, identity);
        loom.renderer.endPass();

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    // -------------------------------------------------------------------------------
    // v7a: dubina se spremila i moze se procitati
    // -------------------------------------------------------------------------------

    const ImageData cameraDepth = depthOnly.readDepthPixels(loom.command);

    report.check("format dubine", cameraDepth.format == vk::Format::eD32Sfloat,
        fmt("%u bajta po pikselu, %zu ukupno", bytesPerPixel(cameraDepth.format), cameraDepth.pixels.size()));

    //What the CPU says, from the same matrices the pass was given
    const glm::mat4 cameraViewProjection = camera.getProjection(size.width, size.height) * camera.getView();
    const Projected lowLeft  = project(cameraViewProjection, {-0.5f,-0.5f,0.0f}, size);
    const Projected topRight = project(cameraViewProjection, { 0.5f, 0.5f,0.0f}, size);

    const size_t expectedCoverage = centresInside(lowLeft.x, topRight.x, lowLeft.y, topRight.y, size);
    const size_t coverage = countWritten(cameraDepth);

    report.check("silueta", coverage == expectedCoverage,
        fmt("%zu piksela zapisano, analiticki %zu", coverage, expectedCoverage));

    //The quad is flat and perpendicular to the camera, so one number covers all of it
    const float worst = worstDepthError(cameraDepth, lowLeft.depth);
    report.check("vrijednost dubine", worst < 1e-6f,
        fmt("ocekivano %.7f, najveca razlika %.3e", double(lowLeft.depth), double(worst)));

    //A cleared pixel is exactly the clear value, not almost
    size_t background = 0;
    bool backgroundExact = true;
    for(uint32_t y = 0; y < size.height; ++y){
        for(uint32_t x = 0; x < size.width; ++x){
            const float value = depthAt(cameraDepth, x, y);
            if(value != 1.0f){
                if(value > 1.0f) backgroundExact = false;
                continue;
            }
            ++background;
        }
    }
    report.check("pozadina", backgroundExact && background == size_t(size.width) * size.height - coverage,
        fmt("%zu piksela tocno na 1.0", background));

    // -------------------------------------------------------------------------------
    // Boja se moze maknuti a da se dubina ne promijeni
    // -------------------------------------------------------------------------------

    const ImageData colorDepth = withColor.readDepthPixels(loom.command);

    const size_t colorCoverage = countWritten(colorDepth);
    const float colorWorst = worstDepthError(colorDepth, lowLeft.depth);

    report.check("boja ne mijenja dubinu", colorCoverage == coverage && colorWorst < 1e-6f,
        fmt("%zu piksela vs %zu, najveca razlika %.3e", colorCoverage, coverage, double(colorWorst)));

    // -------------------------------------------------------------------------------
    // Dubina koja se ne cuva se ne smije citati
    // -------------------------------------------------------------------------------

    bool scratchThrew = false;
    try{
        scratch.readDepthPixels(loom.command);
    }
    catch(const std::exception&){
        scratchThrew = true;
    }
    report.check("dubina bez keepDepth", scratchThrew, "baca iznimku");

    bool noColorThrew = false;
    try{
        depthOnly.readPixels(loom.command);
    }
    catch(const std::exception&){
        noColorThrew = true;
    }
    report.check("boja koje nema", noColorThrew, "baca iznimku");

    // -------------------------------------------------------------------------------
    // v7b: prolaz vodi svjetlo, i dubina se slaze s njegovim matricama
    // -------------------------------------------------------------------------------

    const ImageData lightDepth = shadowMap.readDepthPixels(loom.command);

    const glm::mat4 lightViewProjection = light.getViewProjection();
    const Projected lightCenter = project(lightViewProjection, {0.0f,0.0f,0.0f}, size);
    const Projected lightCornerA = project(lightViewProjection, {-0.5f,0.0f,-0.5f}, size);
    const Projected lightCornerB = project(lightViewProjection, { 0.5f,0.0f, 0.5f}, size);

    //The one pixel the world origin lands on, read straight out of the shadow map
    const float atCenter = depthAt(lightDepth,
        uint32_t(lightCenter.x), uint32_t(lightCenter.y));

    report.check("matrica svjetla", std::fabs(atCenter - lightCenter.depth) < 1e-6f,
        fmt("CPU kaze %.7f na pikselu (%u,%u), shadow map ima %.7f",
            double(lightCenter.depth), uint32_t(lightCenter.x), uint32_t(lightCenter.y), double(atCenter)));

    const size_t lightExpected = centresInside(lightCornerA.x, lightCornerB.x, lightCornerA.y, lightCornerB.y, size);
    const size_t lightCoverage = countWritten(lightDepth);

    //Half a unit of quad inside a box one unit wide: a quarter of the width, a quarter of
    //the height, a sixteenth of the image
    report.check("otisak svjetla", lightCoverage == lightExpected,
        fmt("%zu piksela, analiticki %zu, od %u ukupno",
            lightCoverage, lightExpected, size.width * size.height));

    const float lightWorst = worstDepthError(lightDepth, lightCenter.depth);
    report.check("dubina iz svjetla", lightWorst < 1e-6f,
        fmt("ocekivano %.7f, najveca razlika %.3e", double(lightCenter.depth), double(lightWorst)));

    //If the light and the camera produced the same picture, none of the above proves a pass
    //was driven by anything at all
    report.check("kontrola", lightCoverage != coverage || std::fabs(lightCenter.depth - lowLeft.depth) > 1e-3f,
        fmt("kamera %zu piksela na %.4f, svjetlo %zu na %.4f",
            coverage, double(lowLeft.depth), lightCoverage, double(lightCenter.depth)));

    // -------------------------------------------------------------------------------
    // Granice
    // -------------------------------------------------------------------------------

    bool pointThrew = false;
    try{
        LightConfig pointConfig;
        pointConfig.type = LightType::Point;
        Light point(pointConfig);
        point.getViewProjection();
    }
    catch(const std::exception&){
        pointThrew = true;
    }
    report.check("tockasto svjetlo", pointThrew, "baca iznimku umjesto krive matrice");

    bool emptyTargetThrew = false;
    try{
        RenderTargetConfig nothingConfig;
        nothingConfig.enableColor = false;
        nothingConfig.enableDepth = false;
        RenderTarget nothing(loom.device, size, nothingConfig);
    }
    catch(const std::exception&){
        emptyTargetThrew = true;
    }
    report.check("cilj bez icega", emptyTargetThrew, "baca iznimku");

    report.checkNoValidationMessages();
    return report.result();
}
