#pragma once
#include "../Vulkan/VulkanDevice.h"
#include "../Vulkan/Window.h"
#include "../Vulkan/VulkanInstance.h"
#include "../Vulkan/VulkanSwapchain.h"
#include "../Vulkan/VulkanCommand.h"
#include "../Vulkan/VulkanRenderer.h"
#include "../Vulkan/VulkanGraphicsPipeline.h"
#include "LoomConfig.h"

#include <optional>

class LoomInitializer{

    public:
    LoomInitializer(const LoomConfig& config) : window(config.width,config.height,config.appName),
    instance(config.appName,config.engineName,window),
    device(instance),
    swapchain(instance,window,device,config.swapchainConfig),
    command(device,config.commandConfig),
    depthImage(config.enableDepth ? std::optional<VulkanImage>(std::in_place, device, swapchain.getExtent(), makeDepthConfig(device, config.depthConfig)) : std::nullopt),
    vulkanGraphicsPipeline(device,swapchain,config.pipelineConfig, depthImage ? depthImage->getFormat() : vk::Format::eUndefined),
    renderer(device,swapchain,command,vulkanGraphicsPipeline,depthImage ? &*depthImage : nullptr, config.rendererConfig)
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
    std::optional<VulkanImage> depthImage;
    VulkanGraphicsPipeline vulkanGraphicsPipeline;
    VulkanRenderer renderer;


    void waitIdle() const{device.getDevice().waitIdle();}
  



};