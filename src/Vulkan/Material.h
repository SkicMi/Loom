#pragma once
#include "VulkanGraphicsPipeline.h"
#include "SampledImage.h"
#include "VulkanCommand.h"
#include "VulkanBuffer.h"
#include "Core/MaterialData.h"
#include "Core/ShadingRate.h"
#include <vector>

class Material{
    public:
    Material(const VulkanGraphicsPipeline& pipeline) : pipeline(&pipeline) {};
    Material(const VulkanDevice& device, 
            const VulkanCommand& command,
            const vk::raii::DescriptorPool& pool,
            const VulkanGraphicsPipeline& pipeline, 
            SampledImage image, 
            const MaterialData& data = {});
            
    Material(const VulkanDevice& device, 
        const VulkanCommand& command, 
        const vk::raii::DescriptorPool& pool,
        const VulkanGraphicsPipeline& pipeline, 
        const MaterialData& data);

    //Any payload the shader declares at set 1 binding 1. MaterialData is what Loom offers
    //by default, not what the library is able to carry
    Material(const VulkanDevice& device,
        const VulkanCommand& command,
        const vk::raii::DescriptorPool& pool,
        const VulkanGraphicsPipeline& pipeline,
        SampledImage image,
        const void* data,
        size_t size);

    //Several images at once, bound in order to bindings 0, 1, 2... and the payload to the one after them.
    //
    //Exists for the G-buffer: positions and normals are two images describing the same surface and
    //neither of them is "this object's texture". A material with a single image is still the same
    //material - this is an upgrade, not a replacement
    Material(const VulkanDevice& device,
        const VulkanCommand& command,
        const vk::raii::DescriptorPool& pool,
        const VulkanGraphicsPipeline& pipeline,
        std::vector<SampledImage> images,
        const void* data,
        size_t size);

    

    Material(const Material&) = delete;
    Material& operator = (const Material&) = delete; 
    Material(Material&&) = default;
    

    //How many pixels share a single shading of this material.
    //
    //Lives on the material, not on the pipeline, because it is a decision about IMPORTANCE: the floor in the
    //distance and a mirror in the foreground can share a pipeline and should not share a rate. A device that
    //does not support it draws the same, just without the saving
    void setShadingRate(ShadingRate rate) {shadingRate = rate;}
    ShadingRate getShadingRate() const {return shadingRate;}

    //May the rate image coarsen this material. Critical is a way for a reflection to stay
    //sharp even when it is far - distance is a good estimate of importance, but not a perfect one
    void setImportance(ShadingImportance value) {importance = value;}
    ShadingImportance getImportance() const {return importance;}

    static vk::DescriptorSetLayoutBinding getDataLayoutBinding(uint32_t binding = 1){
        vk::DescriptorSetLayoutBinding layoutBinding;
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        return layoutBinding;
    }

    //Runtime setters, take effect the next frame this material is drawn in. Withing one frame : Last value set wins for every draw using this material
    void setData(const MaterialData& newData);
    void setData(const void* newData, size_t size);
    void setSampledImage(const SampledImage& newImage);
    void setSampledImage(size_t index, const SampledImage& newImage);
    void setBaseColor(const glm::vec4& newBaseColor);
    void setShininess(float newShininess);
    void setSpecularStrength(float newSpecularStrength);

    //getters
    const MaterialData& getData() const;
    size_t getDataSize() const {return payload.size();}
    const VulkanGraphicsPipeline& getPipeline() const{return *pipeline;}
    bool hasDescriptorSet() const {return !descriptorSets.empty();}
    const vk::raii::DescriptorSet& getDescriptorSet(size_t frame) const {return descriptorSets[frame];}
    void uploadIfDirty(size_t frame) const;


    private:
    const VulkanGraphicsPipeline* pipeline;
    const VulkanDevice* device = nullptr;
    std::vector<uint8_t> payload;
    ShadingRate shadingRate = ShadingRate::Full;
    ShadingImportance importance = ShadingImportance::Normal;
    mutable std::vector<SampledImage> images;

    //Which binding the payload goes to. One image leaves it at 1, as it always was; N
    //images push it to N, because the bindings below belong to images
    uint32_t dataBinding = 1;
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    mutable std::vector<VulkanBuffer> dataBuffers;
    mutable std::vector<uint8_t> dirty;
    mutable std::vector<uint8_t> imageDirty;

    void build(const VulkanDevice& device,
                const VulkanCommand& command,
                const vk::raii::DescriptorPool& pool,
                std::vector<SampledImage> images);

    void refreshImageIfStale() const;

    void writeImages(size_t frame) const;

};