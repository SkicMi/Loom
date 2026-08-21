#include "Core/LoomInitializer.h"
#include "Core/LoomConfig.h"
#include "Vulkan/Vertex.h"
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/Texture.h"
#include "Vulkan/Material.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Core/Camera.h"
#include "Core/Environment.h"
#include <cstdint>

static std::vector<uint8_t> makeCheckerboard(uint32_t size, uint32_t cell){
    std::vector<uint8_t> px(size * size * 4);
    for(uint32_t y = 0; y < size; ++y){
        for(uint32_t x = 0; x < size; ++x){
            bool white = (((x / cell) + (y / cell)) % 2) == 0;
            uint8_t v = white ? 255 : 0;
            size_t i = (size_t(y) * size + x) * 4;
            px[i+0] = v; px[i+1] = v; px[i+2] = v; px[i+3] = 255;
        }
    }
    return px;
}


int main(){
const std::vector<Vertex> vertices = {
       {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, { 0.0f,  0.0f,  1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, { 0.0f,  0.0f,  1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, { 0.0f,  0.0f,  1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, { 0.0f,  0.0f,  1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, { 0.0f,  0.0f, -1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, { 0.0f,  0.0f, -1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, { 0.0f,  0.0f, -1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, { 1.0f,  0.0f,  0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, { 1.0f,  0.0f,  0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, { 1.0f,  0.0f,  0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, { 1.0f,  0.0f,  0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, { 0.0f,  1.0f,  0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, { 0.0f,  1.0f,  0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, { 0.0f,  1.0f,  0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, { 0.0f,  1.0f,  0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, { 0.0f, -1.0f,  0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, { 0.0f, -1.0f,  0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, { 0.0f, -1.0f,  0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, { 0.0f, -1.0f,  0.0f}}
    };

    const std::vector<uint16_t> indices = {
         0, 1, 2,   2, 3, 0,   // +Z
         4, 5, 6,   6, 7, 4,   // -Z
         8, 9,10,  10,11, 8,   // +X
        12,13,14,  14,15,12,   // -X
        16,17,18,  18,19,16,   // +Y
        20,21,22,  22,23,20    // -Y
    };

    CameraConfig camConfig;
    camConfig.position = {0.0, 0.0f, 3.0f};
    Camera cam(camConfig);

    LoomConfig config;
    config.width = 1080;
    config.height = 720;
    config.appName = "Test";
    config.engineName = "Test Engine";
    config.swapchainConfig.preferredPresentMode = vk::PresentModeKHR::eMailbox;
    config.rendererConfig.clearColor = {0.0f,0.0f,0.0f,1.0f}; 
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);  

    Mesh cube(loom.device, loom.command, vertices,indices);
    
    loom.renderer.setCamera(cam);

    EnvironmentConfig envConfig;
    envConfig.ambientColor = {0.05f, 0.05f, 0.05f};
    Environment env(envConfig);
    loom.renderer.setEnvironment(env);

    LightConfig lightConfig;
    Light light(lightConfig);
    loom.renderer.addLight(light);

    LightConfig lightConfig2;
    lightConfig2.color = {255.0f, 1.0f, 1.0f};
    lightConfig2.type = LightType::Point;
    Light light2(lightConfig2);
    loom.renderer.addLight(light2);

    PipelineConfig texPipelineConfig = config.pipelineConfig;
    texPipelineConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    texPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.vert.spv";
    texPipelineConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.frag.spv";
    VulkanGraphicsPipeline texPipeline = loom.createPipeline(texPipelineConfig);

    std::vector<uint8_t> pixels = makeCheckerboard(64,8);
    Texture checker(loom.device,loom.command,pixels.data(), vk::Extent2D{64,64});
    Material texMat(loom.device,loom.command, loom.getDescriptorPool(),texPipeline,checker);

    
    while(!loom.window.shouldClose()){
        loom.window.pollEvents();
        float time = static_cast<float>(loom.window.getTime());

        if(!loom.renderer.beginFrame()) continue;
        loom.renderer.draw(cube,glm::rotate(glm::mat4(1.0f),time,glm::vec3(0.5f,1.0f,0.0f)), texMat);


        loom.renderer.endFrame();
    }
    loom.waitIdle();
   
    
}