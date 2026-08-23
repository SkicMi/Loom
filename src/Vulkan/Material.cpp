#include "Material.h"
#include <cstring>



Material::Material(const VulkanDevice& device,
    const VulkanCommand& command,
    const vk::raii::DescriptorPool& pool,
    const VulkanGraphicsPipeline& pipeline,
    SampledImage image,
    const MaterialData& data) : pipeline(&pipeline),
    payload(reinterpret_cast<const uint8_t*>(&data), reinterpret_cast<const uint8_t*>(&data) + sizeof(MaterialData)) {
            build(device, command, pool, image);

}

Material::Material(const VulkanDevice& device,
    const VulkanCommand& command,
    const vk::raii::DescriptorPool& pool,
    const VulkanGraphicsPipeline& pipeline,
    SampledImage image,
    const void* data,
    size_t size) : pipeline(&pipeline){

        if(data == nullptr || size == 0){
            throw std::runtime_error("Material: payload is empty");
        }
        payload.assign(reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + size);
        build(device, command, pool, image);
}

Material::Material(const VulkanDevice& device,
    const VulkanCommand& command,
    const vk::raii::DescriptorPool& pool,
    const VulkanGraphicsPipeline& pipeline,
    const MaterialData& data) : pipeline(&pipeline),
    payload(reinterpret_cast<const uint8_t*>(&data), reinterpret_cast<const uint8_t*>(&data) + sizeof(MaterialData)) {
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

        dataBuffers.emplace_back(device, payload.size(),
                                vk::BufferUsageFlagBits::eUniformBuffer,
                                MemoryUsage::CPU_TO_GPU);
        dataBuffers[i].upload(payload.data(), payload.size());

        vk::DescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = *dataBuffers[i].getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = payload.size();

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
    refreshImageIfStale();

    if(dirty[frame]){
        dataBuffers[frame].upload(payload.data(), payload.size());
        dirty[frame] = 0;
    }
    if(imageDirty[frame]){
        writeImage(frame);
        imageDirty[frame] = 0;
    }
}

//A render target that resized destroyed its view. The image counts its rebuilds, so the
//material can notice on its own instead of the target having to keep a list of materials
void Material::refreshImageIfStale() const{
    if(image.source == nullptr) return;
    if(image.source->getGeneration() == image.generation) return;

    image.view = *image.source->getImageView();
    image.generation = image.source->getGeneration();
    std::fill(imageDirty.begin(), imageDirty.end(), uint8_t(1));
}

const MaterialData& Material::getData() const{
    if(payload.size() != sizeof(MaterialData)){
        throw std::runtime_error("getData: this material does not carry MaterialData");
    }
    return *reinterpret_cast<const MaterialData*>(payload.data());
}

void Material::setData(const void* newData, size_t size){
    if(size != payload.size()){
        throw std::runtime_error("setData: payload size does not match the one the material was built with");
    }
    std::memcpy(payload.data(), newData, size);
    std::fill(dirty.begin(), dirty.end(), uint8_t(1));
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
    setData(&newData, sizeof(MaterialData));
}

//The three setters below only mean anything for the default payload
static MaterialData& asMaterialData(std::vector<uint8_t>& payload){
    if(payload.size() != sizeof(MaterialData)){
        throw std::runtime_error("Material: this material does not carry MaterialData");
    }
    return *reinterpret_cast<MaterialData*>(payload.data());
}

void Material::setBaseColor(const glm::vec4& newBaseColor){
    asMaterialData(payload).baseColor = newBaseColor;
    std::fill(dirty.begin(), dirty.end(), uint8_t(1));
}

void Material::setShininess(float newShininess){
    asMaterialData(payload).shininess = newShininess;
    std::fill(dirty.begin(), dirty.end(), uint8_t(1));
}

void Material::setSpecularStrength(float newSpecularStrength){
    asMaterialData(payload).specularStrength = newSpecularStrength;
    std::fill(dirty.begin(), dirty.end(), uint8_t(1));
}
