#pragma once
#include "VulkanGraphicsPipeline.h"
#include "Texture.h"

class Material{
    public:
    Material(const VulkanGraphicsPipeline& pipeline) : pipeline(&pipeline) {};
    Material(const VulkanDevice& device, const vk::raii::DescriptorPool& pool,const VulkanGraphicsPipeline& pipeline, const Texture& texture);

    Material(const Material&) = delete;
    Material& operator = (const Material&) = delete; 
    Material(Material&&) = default;
    Material& operator = (Material&&) = default;




    //getters
    const VulkanGraphicsPipeline& getPipeline() const{return *pipeline;}
    bool hasDescriptorSet() const {return *descriptorSet != nullptr;}
    const vk::raii::DescriptorSet& getDescriptorSet() const {return descriptorSet;}






    private:
    const VulkanGraphicsPipeline* pipeline;
    vk::raii::DescriptorSet descriptorSet = nullptr;

};