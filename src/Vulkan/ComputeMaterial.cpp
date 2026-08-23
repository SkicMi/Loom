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

void ComputeMaterial::setStorageImage(uint32_t binding, const VulkanImage& image, vk::ImageLayout finalLayout){

    //the format is already guarded in VulkanImage::build, what is left to catch here is an image that was simply never created for this job
    if(!(image.getUsage() & vk::ImageUsageFlagBits::eStorage)){
        throw std::runtime_error("setStorageImage: image was not created with eStorage usage");
    }

    if(finalLayout == vk::ImageLayout::eUndefined){
        throw std::runtime_error("setStorageImage: finalLayout eUndefined would discard the result");
    }

    StorageImageSlot slot;
    slot.image = *image.getImage();
    slot.finalLayout = finalLayout;
    slot.currentLayout = vk::ImageLayout::eUndefined;
    storageImages.push_back(slot);

    //eGeneral is the layout the image is in while the dispatch reads or writes it
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;
    imageInfo.imageView = *image.getImageView();

    vk::WriteDescriptorSet write;
    write.dstSet = *descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eStorageImage;
    write.setImageInfo(imageInfo);

    device.getDevice().updateDescriptorSets(write,nullptr);
}
