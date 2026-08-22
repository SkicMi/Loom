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

    this->device = &device;
    this->image = image;

    size_t framesInFlight = command.getCommandBuffers().size();

    descriptorSets.reserve(framesInFlight);
    dataBuffers.reserve(framesInFlight);
    dirty.assign(framesInFlight,0);
    imageDirty.assign(framesInFlight,0);

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

        device.getDevice().updateDescriptorSets(bufferWrite,nullptr);

        if(this->image.isValid()){
            writeImage(i);
        }

    }         
}

void Material::writeImage(size_t frame) const{
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = image.view;
    imageInfo.sampler = image.sampler;

    vk::WriteDescriptorSet imageWrite;
    imageWrite.dstSet = *descriptorSets[frame];
    imageWrite.dstBinding = 0;
    imageWrite.dstArrayElement = 0;
    imageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    imageWrite.setImageInfo(imageInfo);

    device->getDevice().updateDescriptorSets(imageWrite,nullptr);
}

void Material::uploadIfDirty(size_t frame) const{
    if(dirty[frame]){
        dataBuffers[frame].upload(&data, sizeof(MaterialData));
        dirty[frame] = 0;
    }
    if(imageDirty[frame]){
        writeImage(frame);
        imageDirty[frame] = 0;
    }
}

void Material::setSampledImage(const SampledImage& newImage){
    if(!newImage.isValid()){
        throw std::runtime_error("setSampledImage: image has no view or no sampler");
    }
    if(!image.isValid()){
        throw std::runtime_error("setSampledImage: material was built without an image");
    }
    image = newImage;
    std::fill(imageDirty.begin(), imageDirty.end(), uint8_t(1));
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
