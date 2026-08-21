#pragma once

#include "VulkanImage.h"
#include <optional>

struct RenderTargetConfig{
//Must match the color format the pipeline was created with
vk::Format colorFormat = vk::Format::eB8G8R8A8Srgb;

bool enableDepth = true;
ImageConfig depthConfig = {};

//eColorAttachment always added. eSampled lats a later pass read result, eTransferSrc lets it be copied back to cpu
vk::ImageUsageFlags extraColorUsage = vk::ImageUsageFlagBits::eSampled;

vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
};

inline ImageConfig makeColorConfig( const RenderTargetConfig& config){
    ImageConfig imageConfig;
    imageConfig.format = config.colorFormat;
    imageConfig.usage = vk::ImageUsageFlagBits::eColorAttachment | config.extraColorUsage;
    imageConfig.aspect =vk::ImageAspectFlagBits::eColor;
    return imageConfig;
}

class RenderTarget{
    public:
    RenderTarget(const VulkanDevice& device, vk::Extent2D extent, const RenderTargetConfig& config = {});

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator = (const RenderTarget&) = delete;
    RenderTarget(RenderTarget&&) = default;

    void resize(vk::Extent2D newExtent);

    //getters
    const VulkanImage& getColorImage() const {return colorImage;}
    const VulkanImage* getDepthImage() const {return depthImage ? &*depthImage : nullptr;}
    bool hasDepth() const {return depthImage.has_value();}

    vk::Extent2D getExtent() const {return extent;}
    vk::Format getColorFormat() const {return colorImage.getFormat();}
    vk::Format getDepthFormat() const {return depthImage ? depthImage->getFormat() : vk::Format::eUndefined;}
    vk::ImageLayout getFinalLayout() const {return config.finalLayout;}


    private:
    const VulkanDevice& device;
    RenderTargetConfig config;
    vk::Extent2D extent;
    VulkanImage colorImage;
    std::optional<VulkanImage> depthImage;

};