#pragma once
#include "VulkanGraphicsPipeline.h"
#include "SampledImage.h"
#include "VulkanCommand.h"
#include "VulkanBuffer.h"
#include "Core/MaterialData.h"
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

    

    Material(const Material&) = delete;
    Material& operator = (const Material&) = delete; 
    Material(Material&&) = default;
    

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
    void setBaseColor(const glm::vec4& newBaseColor);
    void setShininess(float newShininess);
    void setSpecularStrength(float newSpecularStrength);

    //getters
    const MaterialData& getData() const {return data;}
    const VulkanGraphicsPipeline& getPipeline() const{return *pipeline;}
    bool hasDescriptorSet() const {return !descriptorSets.empty();}
    const vk::raii::DescriptorSet& getDescriptorSet(size_t frame) const {return descriptorSets[frame];}
    void uploadIfDirty(size_t frame) const;


    private:
    const VulkanGraphicsPipeline* pipeline;
    MaterialData data;
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    mutable std::vector<VulkanBuffer> dataBuffers;
    mutable std::vector<uint8_t> dirty;

    void build(const VulkanDevice& device,
                const VulkanCommand& command,
                const vk::raii::DescriptorPool& pool,
                SampledImage image);

};