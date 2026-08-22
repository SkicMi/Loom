#include "Material.h"



Material::Material(const VulkanDevice& device,
    const VulkanCommand& command,
    const vk::raii::DescriptorPool& pool,
    const VulkanGraphicsPipeline& pipeline,
    SampledImage image,
    const MaterialData& data) : pipeline(&pipeline), data(data) {
            build(device, command, pool, image);

}

Material::Material(const VulkanDevice& device,
    const VulkanCommand& command,
    const vk::raii::DescriptorPool& pool,
    const VulkanGraphicsPipeline& pipeline,
    const MaterialData& data) : pipeline(&pipeline), data(data) {
            build(device, command, pool, SampledImage{});

}

void Material::build(const VulkanDevice& device,
                    const VulkanCommand& command,
                    const vk::raii::DescriptorPool& pool,
                    SampledImage image) {

    if(!pipeline-> hasDescriptors()){
        throw std::runtime_error("Material : pipeline has no descriptor set layout");
    }

    size_t framesInFlight = command.getCommandBuffers().size();

    descriptorSets.reserve(framesInFlight);
    dataBuffers.reserve(framesInFlight);
    dirty.assign(framesInFlight,0);

    vk::DescriptorSetLayout layout = *pipeline->getMaterialSetLayout();

    for(size_t i = 0; i < framesInFlight; ++i){
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.descriptorPool = *pool;
        allocInfo.setSetLayouts(layout);

        vk::raii::DescriptorSets sets(device.getDevice(), allocInfo);
        descriptorSets.push_back(std::move(sets[0]));

        dataBuffers.emplace_back(device, sizeof(MaterialData),
                                vk::BufferUsageFlagBits::eUniformBuffer,
                                MemoryUsage::CPU_TO_GPU);
        dataBuffers[i].upload(&data, sizeof(MaterialData));

        std::vector<vk::WriteDescriptorSet> writes;

        vk::DescriptorImageInfo imageInfo;
        if(image.isValid()){
            imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imageInfo.imageView = image.view;
            imageInfo.sampler = image.sampler;

            vk::WriteDescriptorSet imageWrite;
            imageWrite.dstSet = *descriptorSets[i];
            imageWrite.dstBinding = 0;
            imageWrite.dstArrayElement = 0;
            imageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            imageWrite.setImageInfo(imageInfo);
            writes.push_back(imageWrite);
        }

        vk::DescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = *dataBuffers[i].getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MaterialData);

        vk::WriteDescriptorSet bufferWrite;
        bufferWrite.dstSet = *descriptorSets[i];
        bufferWrite.dstBinding = 1;
        bufferWrite.dstArrayElement = 0;
        bufferWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        bufferWrite.setBufferInfo(bufferInfo);
        writes.push_back(bufferWrite);

        device.getDevice().updateDescriptorSets(writes,nullptr);

    }         
}

void Material::uploadIfDirty(size_t frame) const{
    if(dirty[frame]){
        dataBuffers[frame].upload(&data, sizeof(MaterialData));
        dirty[frame] = 0;
    }
}

void Material::setData(const MaterialData& newData){
    data = newData;
    std::fill(dirty.begin(), dirty.end(), uint8_t(1));
}

void Material::setBaseColor(const glm::vec4& newBaseColor){
    data.baseColor = newBaseColor;
    std::fill(dirty.begin(), dirty.end(), uint8_t(1));
}

void Material::setShininess(float newShininess){
    data.shininess = newShininess;
    std::fill(dirty.begin(), dirty.end(), uint8_t(1));
}

void Material::setSpecularStrength(float newSpecularStrength){
    data.specularStrength = newSpecularStrength;
    std::fill(dirty.begin(), dirty.end(), uint8_t(1));
}
