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
    {
        //descriptors
        createDescriptorPool(config);
     

    }


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
    vk::raii::DescriptorPool descriptorPool = nullptr;
    VulkanGraphicsPipeline vulkanGraphicsPipeline;
    VulkanRenderer renderer;


    //getters
    const vk::raii::DescriptorPool& getDescriptorPool() const {return descriptorPool;}



    void waitIdle() const{device.getDevice().waitIdle();}
    VulkanGraphicsPipeline createPipeline(const PipelineConfig& pipelineConfig) const {
        return VulkanGraphicsPipeline(device , swapchain, pipelineConfig,
        depthImage ? depthImage ->getFormat() : vk::Format::eUndefined);
    }

    void createDescriptorPool(const LoomConfig& config){
       vk::DescriptorPoolSize poolSize;
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = config.maxDescriptorSets;
        

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo.maxSets = config.maxDescriptorSets;
        poolInfo.setPoolSizes(poolSize);
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

        descriptorPool = vk::raii::DescriptorPool(device.getDevice(), poolInfo);
        }
  



};