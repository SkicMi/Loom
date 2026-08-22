#pragma once
#include "VulkanImage.h"
#include "SampledImage.h"
#include "VulkanCommand.h"

struct TextureConfig{
    vk::Format format = vk::Format::eR8G8B8A8Srgb;
    vk::Filter filter = vk::Filter::eLinear;
    vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat;
    bool anisotropyEnable = false;
    float maxAnisotropy = 1.0f;

    vk::ImageUsageFlags extraUsage = {};
};

class Texture{
    public:
    Texture(const VulkanDevice& device,
    const VulkanCommand& command,
    const void* pixels,
    vk::Extent2D extent,
    const TextureConfig& config = {});
    

    Texture(const Texture&) = delete;
    Texture& operator =(const Texture&) = delete;
    Texture(Texture&&) = default;

    //getters
    const VulkanImage& getImage() const {return image;}
    const vk::raii::Sampler& getSampler() const {return sampler;}
    SampledImage getSampled() const {return SampledImage{*image.getImageView(), *sampler};}
    vk::Extent2D getExtent() const {return image.getExtent();}


        static vk::DescriptorSetLayoutBinding getLayoutBinding(uint32_t binding = 0){
        vk::DescriptorSetLayoutBinding layoutBinding;
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        return layoutBinding;
    }

    private:
    VulkanImage image;
    vk::raii::Sampler sampler = nullptr;


    

};