#pragma once
#include <string>
#include "../Vulkan/VulkanSwapchain.h"
#include "../Vulkan/VulkanCommand.h"
#include "../Vulkan/VulkanGraphicsPipeline.h"
#include "../Vulkan/VulkanRenderer.h"
#include "../Vulkan/VulkanImage.h"
#include "../Vulkan/VulkanDevice.h"


struct LoomConfig{
    //Window and app name configuration
    uint32_t width = 1080;
    uint32_t height = 720;
    std::string appName = "Loom Application";
    std::string engineName = "Loom Engine";

    //Per-subsystem coniguration

    //DeviceConfiguration
    DeviceConfig deviceConfig = {};

    //SwapchainConfiguration
    SwapchainConfig swapchainConfig = {};

    //CommandConfiguration
    CommandConfig commandConfig = {};

    //GraphicsPipelineCOnfig
    PipelineConfig pipelineConfig = {};

    //RendererConfiguration
    RendererConfig rendererConfig = {};

    //No window, no surface, no swapchain, no presentation. Everything else - passes, shadow
    //maps, compute, readback - works exactly as it does with a window, which is what makes a
    //sequence export deterministic and a test runnable on a machine with no display
    bool headless = false;

    //What a headless pipeline draws into by default, since there is no swapchain to borrow a
    //format from. Ignored entirely when there is a window
    vk::Format headlessColorFormat = vk::Format::eB8G8R8A8Srgb;

    //Image Configuration
    //Depth
    bool enableDepth = false; //deault for 2D projects
    ImageConfig depthConfig = {};

    

    //Descriptors. descriptorsPerType 0 means "same as maxDescriptorSets", which is what a
    //small app wants; a scene with many materials raises one or both
    uint32_t maxDescriptorSets = 64;
    uint32_t descriptorsPerType = 0;
    


};