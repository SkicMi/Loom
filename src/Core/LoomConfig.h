#pragma once
#include <string>
#include "../Vulkan/VulkanSwapchain.h"
#include "../Vulkan/VulkanCommand.h"
#include "../Vulkan/VulkanGraphicsPipeline.h"
#include "../Vulkan/VulkanRenderer.h"
#include "../Vulkan/VulkanImage.h"


struct LoomConfig{
    //Window and app name configuration
    uint32_t width = 1080;
    uint32_t height = 720;
    std::string appName = "Loom Application";
    std::string engineName = "Loom Engine";

    //Per-subsystem coniguration

    //SwapchainConfiguration
    SwapchainConfig swapchainConfig = {};

    //CommandConfiguration
    CommandConfig commandConfig = {};

    //GraphicsPipelineCOnfig
    PipelineConfig pipelineConfig = {};

    //RendererConfiguration
    RendererConfig rendererConfig = {};

    //Image Configuration
    //Depth
    bool enableDepth = false; //deault for 2D projects
    ImageConfig depthConfig = {};

    //Descriptors
    uint32_t maxDescriptorSets = 64;
    


};