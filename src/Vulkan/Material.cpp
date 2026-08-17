#include "Material.h"

Material::Material(const VulkanDevice& device, const vk::raii::DescriptorPool& pool, const VulkanGraphicsPipeline& pipeline, const Texture& texture) : pipeline(&pipeline){
    if(!pipeline.hasDescriptors()){
        throw std::runtime_error("Material : pipeline has no descriptor set layout");
    }

    vk::DescriptorSetLayout layout = *pipeline.getMaterialSetLayout();
    
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *pool;
    allocInfo.setSetLayouts(layout);

    vk::raii::DescriptorSets sets(device.getDevice(), allocInfo);
    descriptorSet = std::move(sets[0]);

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = *texture.getImage().getImageView();
    imageInfo.sampler = texture.getSampler();

    vk::WriteDescriptorSet write;
    write.dstSet = *descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.setImageInfo(imageInfo);

    device.getDevice().updateDescriptorSets(write,nullptr);

}