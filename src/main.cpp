#include "Core/LoomInitializer.h"
#include "Core/LoomConfig.h"
#include "Vulkan/Vertex.h"
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/Mesh.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Core/Camera.h"

int main(){
    const std::vector<Vertex> vertices = {
       {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}}
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
    config.commandConfig;
    config.rendererConfig.clearColor = {0.0f,0.0f,0.0f,1.0f}; 
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);  

    Mesh cube(loom.device, loom.command, vertices,indices);
    
    loom.renderer.setCamera(cam);

    
    while(!loom.window.shouldClose()){
        loom.window.pollEvents();
        float time = static_cast<float>(loom.window.getTime());

        if(!loom.renderer.beginFrame()) continue;
        loom.renderer.draw(cube,glm::rotate(glm::mat4(1.0f),time,glm::vec3(0.5f,1.0f,0.0f)));


        loom.renderer.endFrame();
    }
    loom.waitIdle();
   
    
}