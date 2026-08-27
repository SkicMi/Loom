#include "Material.h"
#include <cstring>



Material::Material(const VulkanDevice& device,
    const VulkanCommand& command,
    const vk::raii::DescriptorPool& pool,
    const VulkanGraphicsPipeline& pipeline,
    SampledImage image,
    const MaterialData& data) : pipeline(&pipeline),
    payload(reinterpret_cast<const uint8_t*>(&data), reinterpret_cast<const uint8_t*>(&data) + sizeof(MaterialData)) {
            build(device, command, pool, image.isValid() ? std::vector<SampledImage>{image}
                                                         : std::vector<SampledImage>{});

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
        build(device, command, pool, image.isValid() ? std::vector<SampledImage>{image}
                                                     : std::vector<SampledImage>{});
}

Material::Material(const VulkanDevice& device,
    const VulkanCommand& command,
    const vk::raii::DescriptorPool& pool,
    const VulkanGraphicsPipeline& pipeline,
    std::vector<SampledImage> images,
    const void* data,
    size_t size) : pipeline(&pipeline){

        if(data == nullptr || size == 0){
            throw std::runtime_error("Material: payload is empty");
        }
        if(images.empty()){
            throw std::runtime_error("Material: this constructor is for a material built from several images, and none were given");
        }
        payload.assign(reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + size);
        build(device, command, pool, std::move(images));
}

Material::Material(const VulkanDevice& device,
    const VulkanCommand& command,
    const vk::raii::DescriptorPool& pool,
    const VulkanGraphicsPipeline& pipeline,
    const MaterialData& data) : pipeline(&pipeline),
    payload(reinterpret_cast<const uint8_t*>(&data), reinterpret_cast<const uint8_t*>(&data) + sizeof(MaterialData)) {
            build(device, command, pool, std::vector<SampledImage>{});

}

void Material::build(const VulkanDevice& device,
                    const VulkanCommand& command,
                    const vk::raii::DescriptorPool& pool,
                    std::vector<SampledImage> images) {

    if(!pipeline-> hasDescriptors()){
        throw std::runtime_error("Material : pipeline has no descriptor set layout");
    }

    this->device = &device;
    this->images = std::move(images);

    //Slike zauzimaju bindinge od nule redom, pa payload ide na prvi slobodan iza njih. S
    //jednom slikom to je 1 - tocno ono sto je bilo i prije
    this->dataBinding = this->images.empty() ? 1u : uint32_t(this->images.size());

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
        bufferWrite.dstBinding = dataBinding;
        bufferWrite.dstArrayElement = 0;
        bufferWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        bufferWrite.setBufferInfo(bufferInfo);

        device.getDevice().updateDescriptorSets(bufferWrite,nullptr);

        if(!this->images.empty()){
            writeImages(i);
        }

    }         
}

void Material::writeImages(size_t frame) const{
    for(size_t slot = 0; slot < images.size(); ++slot){
        vk::DescriptorImageInfo imageInfo;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = images[slot].view;
        imageInfo.sampler = images[slot].sampler;

        vk::WriteDescriptorSet imageWrite;
        imageWrite.dstSet = *descriptorSets[frame];
        imageWrite.dstBinding = uint32_t(slot);
        imageWrite.dstArrayElement = 0;
        imageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        imageWrite.setImageInfo(imageInfo);

        device->getDevice().updateDescriptorSets(imageWrite,nullptr);
    }
}

void Material::uploadIfDirty(size_t frame) const{
    refreshImageIfStale();

    if(dirty[frame]){
        dataBuffers[frame].upload(payload.data(), payload.size());
        dirty[frame] = 0;
    }
    if(imageDirty[frame]){
        writeImages(frame);
        imageDirty[frame] = 0;
    }
}

//A render target that resized destroyed its view. The image counts its rebuilds, so the
//material can notice on its own instead of the target having to keep a list of materials
void Material::refreshImageIfStale() const{
    for(SampledImage& image : images){
        if(image.source == nullptr) continue;
        if(image.source->getGeneration() == image.generation) continue;

        image.view = *image.source->getImageView();
        image.generation = image.source->getGeneration();
        std::fill(imageDirty.begin(), imageDirty.end(), uint8_t(1));
    }
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
    setSampledImage(0, newImage);
}

void Material::setSampledImage(size_t index, const SampledImage& newImage){
    if(!newImage.isValid()){
        throw std::runtime_error("setSampledImage: image has no view or no sampler");
    }
    if(index >= images.size()){
        throw std::runtime_error("setSampledImage: this material was built with " +
            std::to_string(images.size()) + " images, so there is no slot " + std::to_string(index));
    }
    images[index] = newImage;
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
