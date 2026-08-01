#include "Core/LoomInitializer.h"
#include "Core/LoomConfig.h"

int main(){

    LoomConfig config;
    config.width = 1080;
    config.height = 720;
    config.appName = "Test";
    config.engineName = "Test Engine";
    config.swapchainConfig.preferredPresentMode = vk::PresentModeKHR::eMailbox;
    config.commandConfig;
    LoomInitializer initializer(config);   
    
    while(!initializer.window.shouldClose()){
        glfwPollEvents();
        initializer.renderer.drawFrame();
    }
    initializer.device.getDevice().waitIdle();
   
    
}