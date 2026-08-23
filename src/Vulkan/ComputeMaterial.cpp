#include "ComputeMaterial.h"

ComputeMaterial::ComputeMaterial(const VulkanDevice& device,
                                 const vk::raii::DescriptorPool& pool,
                                 const VulkanComputePipeline& pipeline) :
    device(device), pipeline(&pipeline){

    vk::DescriptorSetLayout layout = *pipeline.getResourceSetLayout();

    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *pool;
    allocInfo.setSetLayouts(layout);

    vk::raii::DescriptorSets sets(device.getDevice(), allocInfo);
    descriptorSet = std::move(sets[0]);
}

void ComputeMaterial::setStorageBuffer(uint32_t binding, const VulkanBuffer& buffer){

    vk::DescriptorBufferInfo bufferInfo;
    bufferInfo.buffer = *buffer.getBuffer();
    bufferInfo.offset = 0;
    bufferInfo.range = buffer.getSize();

    vk::WriteDescriptorSet write;
    write.dstSet = *descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eStorageBuffer;
    write.setBufferInfo(bufferInfo);

    device.getDevice().updateDescriptorSets(write,nullptr);
}
