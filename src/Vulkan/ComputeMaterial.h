#pragma once
#include "VulkanComputePipeline.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include <vector>

//One storage image as the renderer needs to see it: the handle to put a barrier on,
//where the image should end up after the dispatch, and where it is right now
struct StorageImageSlot{
    vk::Image image = nullptr;
    vk::ImageLayout finalLayout = vk::ImageLayout::eGeneral;
    mutable vk::ImageLayout currentLayout = vk::ImageLayout::eUndefined;
};

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

    //finalLayout is where dispatch leaves the image. eTransferSrcOptimal to read it back,
    //eShaderReadOnlyOptimal to sample it in a later pass, eGeneral to keep writing to it
    void setStorageImage(uint32_t binding, const VulkanImage& image,
                         vk::ImageLayout finalLayout = vk::ImageLayout::eGeneral);

    //getters
    const VulkanComputePipeline& getPipeline() const {return *pipeline;}
    const vk::raii::DescriptorSet& getDescriptorSet() const {return descriptorSet;}
    const std::vector<StorageImageSlot>& getStorageImages() const {return storageImages;}

    private:
    const VulkanDevice& device;
    const VulkanComputePipeline* pipeline;
    vk::raii::DescriptorSet descriptorSet = nullptr;
    std::vector<StorageImageSlot> storageImages;

};
