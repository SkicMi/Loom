// A scene that uses everything Loom has learned to do, written the short way.
//
// The whole of it is four shapes, two lights and three kinds of pass. There is not one
// vertex, one Mesh or one Material written out by hand: LoomShapes::Primitives holds the
// geometry, and a shape is drawn by naming it and handing it a texture.
//
// The two lights shadow by different machinery on purpose. The sun is directional: one
// depth image, its box refitted to the camera's frustum every frame and snapped to whole
// texels, so walking the camera does not make the shadow edges crawl. The bulb is a point
// light: it shines in every direction at once, so its shadow map is a cube of six faces and
// the lookup is a direction rather than a coordinate.

#include "Core/LoomConfig.h"
#include "Core/LoomShapes.h"
#include "Core/Camera.h"
#include "Core/Environment.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

static std::vector<uint8_t> makeCheckerboard(uint32_t size, uint32_t cell,
                                             uint8_t light = 235, uint8_t dark = 40){
    std::vector<uint8_t> pixels(size_t(size) * size * 4);
    for(uint32_t y = 0; y < size; ++y){
        for(uint32_t x = 0; x < size; ++x){
            const uint8_t value = (((x / cell) + (y / cell)) % 2) == 0 ? light : dark;
            const size_t i = (size_t(y) * size + x) * 4;
            pixels[i+0] = value; pixels[i+1] = value; pixels[i+2] = value; pixels[i+3] = 255;
        }
    }
    return pixels;
}

int main(){
    LoomConfig config;
    config.width = 1280;
    config.height = 720;
    config.appName = "Loom";
    config.engineName = "Loom";
    config.swapchainConfig.preferredPresentMode = vk::PresentModeKHR::eMailbox;
    config.rendererConfig.clearColor = {0.02f, 0.02f, 0.04f, 1.0f};
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    //One pass for the sun's map, six for the bulb's cube, one for the window. The default of
    //eight would fit exactly, which is no room at all to add a second look at the scene
    config.rendererConfig.maxPassesPerFrame = 16;

    LoomInitializer loom(config);
    LoomShapes::Primitives shapes(loom);

    // -------------------------------------------------------------------------------
    // Scena
    // -------------------------------------------------------------------------------

    CameraConfig camConfig;
    camConfig.position = {0.0f, 3.0f, 6.0f};
    camConfig.target = {0.0f, 0.6f, 0.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig envConfig;
    envConfig.ambientColor = {0.06f, 0.07f, 0.10f};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig sunConfig;
    sunConfig.type = LightType::Directional;
    sunConfig.direction = {-0.45f, -1.0f, -0.35f};
    sunConfig.color = {1.0f, 0.96f, 0.88f};
    sunConfig.intensity = 0.9f;
    Light sun(sunConfig);
    loom.renderer.addLight(sun);

    LightConfig bulbConfig;
    bulbConfig.type = LightType::Point;
    bulbConfig.position = {2.2f, 2.0f, 0.0f};
    bulbConfig.color = {1.0f, 0.35f, 0.15f};
    bulbConfig.intensity = 14.0f;
    bulbConfig.range = 12.0f;
    bulbConfig.shadowNear = 0.1f;
    Light bulb(bulbConfig);
    loom.renderer.addLight(bulb);

    const std::vector<uint8_t> floorPixels = makeCheckerboard(128, 16, 210, 60);
    const std::vector<uint8_t> shapePixels = makeCheckerboard(64, 8, 245, 90);

    Texture floorTexture(loom.device, loom.command, floorPixels.data(), vk::Extent2D{128,128});
    Texture shapeTexture(loom.device, loom.command, shapePixels.data(), vk::Extent2D{64,64});

    // -------------------------------------------------------------------------------
    // Sjene
    // -------------------------------------------------------------------------------

    RenderTarget sunMap(loom.device, {2048,2048}, makeShadowMapConfig());
    RenderTarget bulbCube(loom.device, {1024,1024}, makeShadowCubeConfig());

    //The pass that fills a shadow map writes depth and nothing else, so this pipeline has no
    //fragment stage and reads only the position out of each vertex
    PipelineConfig shadowPipelineConfig;
    shadowPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/shadow.vert.spv";
    shadowPipelineConfig.fragShaderPath = "";
    shadowPipelineConfig.enableColor = false;
    shadowPipelineConfig.vertexAttributes = Vertex::getPositionAttribute();
    shadowPipelineConfig.depthTestEnable = true;
    shadowPipelineConfig.depthWriteEnable = true;

    //Back faces into the map instead of front ones. The far side of a solid object is a
    //whole object's thickness away from the surface being lit, which is a much bigger margin
    //than any bias could buy
    shadowPipelineConfig.cullMode = vk::CullModeFlagBits::eFront;

    VulkanGraphicsPipeline shadowPipeline(loom.device, shadowPipelineConfig,
        loom.getColorFormat(), sunMap.getDepthFormat());
    Material shadowMaterial(shadowPipeline);

    ShadowConfig sunShadow;
    sunShadow.fitToCamera = true;   //the box follows the camera instead of being placed by hand
    sunShadow.distance = 14.0f;     //shadows reach this far, and the whole map is spent on it
    sunShadow.depthBias = 0.0015f;
    loom.renderer.setShadowMap(sunMap, sun, sunShadow);

    ShadowConfig bulbShadow;
    bulbShadow.depthBias = 0.0025f;
    loom.renderer.setShadowCube(bulbCube, bulb, bulbShadow);

    // -------------------------------------------------------------------------------
    // Petlja
    // -------------------------------------------------------------------------------

    const glm::mat4 floorModel = glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, 1.0f, 16.0f));

    while(!loom.shouldClose()){
        loom.pollEvents();
        const float time = static_cast<float>(loom.getTime());

        //The camera orbits so the fitted shadow box has to keep up. If the texel snapping
        //were not there this is exactly where the edges would start crawling
        camera.setPosition({6.5f * std::sin(time * 0.25f), 3.0f, 6.5f * std::cos(time * 0.25f)});
        camera.lookAt({0.0f, 0.6f, 0.0f});

        bulb.setPosition({2.6f * std::cos(time * 0.7f), 2.0f, 2.6f * std::sin(time * 0.7f)});

        const glm::mat4 cubeModel =
            glm::rotate(glm::translate(glm::mat4(1.0f), {-1.8f, 0.5f, 0.0f}),
                        time * 0.8f, {0.3f, 1.0f, 0.15f});

        const glm::mat4 sphereModel =
            glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, 0.75f + 0.25f * std::sin(time * 1.6f), 0.0f}),
                       glm::vec3(1.3f));

        const glm::mat4 pyramidModel =
            glm::rotate(glm::translate(glm::mat4(1.0f), {1.8f, 0.5f, 0.0f}),
                        -time * 0.6f, {0.0f, 1.0f, 0.0f});

        if(!loom.renderer.beginFrame()) continue;

        //Everything that casts, seen from the sun. The floor is left out: a flat surface has
        //no business shadowing itself, and leaving it out is cheaper than biasing around it
        loom.renderer.beginPass(sunMap, sun);
        shapes.cube(shadowMaterial, cubeModel);
        shapes.sphere(shadowMaterial, sphereModel);
        shapes.pyramid(shadowMaterial, pyramidModel);
        loom.renderer.endPass();

        //The same casters again, six times, once per face of the bulb's cube
        for(uint32_t face = 0; face < 6; ++face){
            loom.renderer.beginPass(bulbCube, bulb, face);
            shapes.cube(shadowMaterial, cubeModel);
            shapes.sphere(shadowMaterial, sphereModel);
            shapes.pyramid(shadowMaterial, pyramidModel);
            loom.renderer.endPass();
        }

        //And the scene itself. One line per shape, and the only thing any of them is handed
        //is a texture - the material behind it was built once and has been cached since
        loom.renderer.beginPass();
        shapes.plane(floorTexture, floorModel);
        shapes.cube(shapeTexture, cubeModel);
        shapes.sphere(shapeTexture, sphereModel);
        shapes.pyramid(shapeTexture, pyramidModel);
        loom.renderer.endPass();

        loom.renderer.endFrame();
    }
    loom.waitIdle();
}
