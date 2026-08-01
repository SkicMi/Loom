#pragma once
#include "../Vulkan/VulkanDevice.h"
#include "../Vulkan/Window.h"
#include "../Vulkan/VulkanInstance.h"
#include "../Vulkan/VulkanSwapchain.h"
#include "../Vulkan/VulkanCommand.h"
#include "../Vulkan/VulkanRenderer.h"
#include "LoomConfig.h"

class LoomInitializer{

    public:
    LoomInitializer(const LoomConfig& config) : window(config.width,config.height,config.appName),
    instance(config.appName,config.engineName,window),
    device(instance),
    swapchain(instance,window,device,config.swapchainConfig),
    command(device,config.commandConfig),
    renderer(device,swapchain,command,config.rendererConfig)
    {}


    LoomInitializer(const LoomInitializer&) = delete;
    LoomInitializer& operator=(const LoomInitializer&) = delete;
    LoomInitializer(LoomInitializer&&) = delete;
    LoomInitializer& operator=(LoomInitializer&&) = delete;


    Window window;
    VulkanInstance instance;
    VulkanDevice device;
    VulkanSwapchain swapchain;
    VulkanCommand command;
    VulkanRenderer renderer;




};