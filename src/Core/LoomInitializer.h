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
    LoomInitializer(const LoomConfig& config) :
    window(config.headless ? std::nullopt
                           : std::optional<Window>(std::in_place, config.width, config.height, config.appName)),
    instance(config.appName, config.engineName, window ? &*window : nullptr),
    device(instance,config.deviceConfig),
    swapchain(config.headless ? std::nullopt
                              : std::optional<VulkanSwapchain>(std::in_place, instance, *window, device, config.swapchainConfig)),
    command(device,config.commandConfig),
    depthImage(config.enableDepth ? std::optional<VulkanImage>(std::in_place, device, pickExtent(config, swapchain), makeDepthConfig(device, config.depthConfig)) : std::nullopt),
    descriptorPool(makeDescriptorPool(device,config)),
    vulkanGraphicsPipeline(device,config.pipelineConfig, pickColorFormat(config, swapchain), depthImage ? depthImage->getFormat() : vk::Format::eUndefined),
    renderer(device, swapchain ? &*swapchain : nullptr, command,vulkanGraphicsPipeline,depthImage ? &*depthImage : nullptr, descriptorPool, config.rendererConfig),
    config(config)
    {

    }


    LoomInitializer(const LoomInitializer&) = delete;
    LoomInitializer& operator=(const LoomInitializer&) = delete;
    LoomInitializer(LoomInitializer&&) = delete;
    LoomInitializer& operator=(LoomInitializer&&) = delete;


    std::optional<Window> window;
    VulkanInstance instance;
    VulkanDevice device;
    std::optional<VulkanSwapchain> swapchain;
    VulkanCommand command;
    std::optional<VulkanImage> depthImage;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    VulkanGraphicsPipeline vulkanGraphicsPipeline;
    VulkanRenderer renderer;

    //Kept so the getters below can answer without the swapchain
    LoomConfig config;


    //getters
    const vk::raii::DescriptorPool& getDescriptorPool() const {return descriptorPool;}

    bool isHeadless() const {return !window.has_value();}
    bool hasWindow() const {return window.has_value();}

    //What a pipeline drawing to the default target should use, and how big that target is.
    //With a window they come from the swapchain; without one, from the config
    vk::Format getColorFormat() const {return pickColorFormat(config, swapchain);}
    vk::Extent2D getExtent() const {return pickExtent(config, swapchain);}

    //Window chores that a headless run simply does not have. Calling them either way means
    //the same loop body works in both modes, which is the whole point of a sequence export
    void pollEvents() const {if(window) window->pollEvents();}
    bool shouldClose() const {return window ? window->shouldClose() : false;}

    //Wall clock, and only meaningful with a window. A sequence export must drive its own
    //time - frame N at N/fps - or the same export twice will not give the same frames
    double getTime() const {return window ? window->getTime() : 0.0;}

    void waitIdle() const{device.getDevice().waitIdle();}
    VulkanGraphicsPipeline createPipeline(const PipelineConfig& pipelineConfig) const {
        return VulkanGraphicsPipeline(device, pipelineConfig, getColorFormat(),
        depthImage ? depthImage ->getFormat() : vk::Format::eUndefined);
    }

    static vk::Extent2D pickExtent(const LoomConfig& config, const std::optional<VulkanSwapchain>& swapchain){
        return swapchain ? swapchain->getExtent() : vk::Extent2D{config.width, config.height};
    }

    static vk::Format pickColorFormat(const LoomConfig& config, const std::optional<VulkanSwapchain>& swapchain){
        return swapchain ? swapchain->getSurfaceFormat().format : config.headlessColorFormat;
    }

    VulkanComputePipeline createComputePipeline(const ComputePipelineConfig& computeConfig) const {
        return VulkanComputePipeline(device, computeConfig);
    }

    static vk::raii::DescriptorPool makeDescriptorPool(const VulkanDevice& device, const LoomConfig& config){
        const uint32_t perType = config.descriptorsPerType > 0 ? config.descriptorsPerType : config.maxDescriptorSets;

        std::array<vk::DescriptorPoolSize,5> poolSizes;
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = perType;
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = perType;
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = perType;
        poolSizes[3].type = vk::DescriptorType::eUniformBufferDynamic;
        poolSizes[3].descriptorCount = perType;
        poolSizes[4].type = vk::DescriptorType::eStorageImage;
        poolSizes[4].descriptorCount = perType;

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo.maxSets = config.maxDescriptorSets;
        poolInfo.setPoolSizes(poolSizes);
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

        return vk::raii::DescriptorPool(device.getDevice(), poolInfo);
     
        }
  



};