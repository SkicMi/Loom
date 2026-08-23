#pragma once
#include "VulkanComputePipeline.h"
#include "VulkanBuffer.h"

class ComputeMaterial{
    public:
    ComputeMaterial(const VulkanDevice& device,
                    const vk::raii::DescriptorPool& pool,
                    const VulkanComputePipeline& pipeline);

    ComputeMaterial(const ComputeMaterial&) = delete;
    ComputeMaterial& operator = (const ComputeMaterial&) = delete;
    ComputeMaterial(ComputeMaterial&&) = default;

    //Written straight into the descriptor. One set is enough while resources are set
    //before the first frame and never rewritten while a command buffer is pending
    void setStorageBuffer(uint32_t binding, const VulkanBuffer& buffer);

    //getters
    const VulkanComputePipeline& getPipeline() const {return *pipeline;}
    const vk::raii::DescriptorSet& getDescriptorSet() const {return descriptorSet;}

    private:
    const VulkanDevice& device;
    const VulkanComputePipeline* pipeline;
    vk::raii::DescriptorSet descriptorSet = nullptr;

};
