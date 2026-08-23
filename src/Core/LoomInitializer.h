#pragma once
#include "../Vulkan/VulkanDevice.h"
#include "../Vulkan/Window.h"
#include "../Vulkan/VulkanInstance.h"
#include "../Vulkan/VulkanSwapchain.h"
#include "../Vulkan/VulkanCommand.h"
#include "../Vulkan/VulkanRenderer.h"
#include "../Vulkan/VulkanGraphicsPipeline.h"
#include "../Vulkan/VulkanComputePipeline.h"
#include "LoomConfig.h"

#include <optional>

class LoomInitializer{

    public:
    LoomInitializer(const LoomConfig& config) : window(config.width,config.height,config.appName),
    instance(config.appName,config.engineName,window),
    device(instance,config.deviceConfig),
    swapchain(instance,window,device,config.swapchainConfig),
    command(device,config.commandConfig),
    depthImage(config.enableDepth ? std::optional<VulkanImage>(std::in_place, device, swapchain.getExtent(), makeDepthConfig(device, config.depthConfig)) : std::nullopt),
    descriptorPool(makeDescriptorPool(device,config)),
    vulkanGraphicsPipeline(device,swapchain,config.pipelineConfig, depthImage ? depthImage->getFormat() : vk::Format::eUndefined),
    renderer(device,swapchain,command,vulkanGraphicsPipeline,depthImage ? &*depthImage : nullptr, descriptorPool, config.rendererConfig)
    {

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

    VulkanComputePipeline createComputePipeline(const ComputePipelineConfig& computeConfig) const {
        return VulkanComputePipeline(device, computeConfig);
    }

    static vk::raii::DescriptorPool makeDescriptorPool(const VulkanDevice& device, const LoomConfig& config){
        std::array<vk::DescriptorPoolSize,4> poolSizes;
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = config.maxDescriptorSets;
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = config.maxDescriptorSets;
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = config.maxDescriptorSets;
        poolSizes[3].type = vk::DescriptorType::eUniformBufferDynamic;
        poolSizes[3].descriptorCount = config.maxDescriptorSets;

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo.maxSets = config.maxDescriptorSets;
        poolInfo.setPoolSizes(poolSizes);
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

        return vk::raii::DescriptorPool(device.getDevice(), poolInfo);
     
        }
  



};