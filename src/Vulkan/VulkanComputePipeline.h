#pragma once
#include "VulkanDevice.h"
#include <string>
#include <vector>

struct ComputePipelineConfig{
    std::string shaderPath; //no default, a compute shader has no sensible fallback

    //set 0 - the dispatch's own resources. Compute has no per-frame set
    std::vector<vk::DescriptorSetLayoutBinding> descriptorBindings;

    uint32_t pushConstantSize = 0; //size only, the library never knows what is inside
};

class VulkanComputePipeline{
    public:
    VulkanComputePipeline(const VulkanDevice& device, const ComputePipelineConfig& config);

    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator = (const VulkanComputePipeline&) = delete;
    VulkanComputePipeline(VulkanComputePipeline&&) = default;

    static constexpr uint32_t resourceSet = 0;

    //getters
    const vk::raii::Pipeline& getPipeline() const {return pipeline;}
    const vk::raii::PipelineLayout& getPipelineLayout() const {return pipelineLayout;}
    const vk::raii::DescriptorSetLayout& getResourceSetLayout() const {return setLayout;}
    uint32_t getPushConstantSize() const {return config.pushConstantSize;}

    private:
    const VulkanDevice& device;
    ComputePipelineConfig config;

    vk::raii::DescriptorSetLayout setLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    void createPipeline();

};
