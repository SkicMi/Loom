#pragma once
#include "VulkanDevice.h"
#include "VulkanBuffer.h"

struct ImageConfig{
    vk::Format format = vk::Format::eUndefined; //must be set
    vk::ImageUsageFlags usage = {};
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
    vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
    MemoryUsage memoryUsage = MemoryUsage::GPU_ONLY;
    uint32_t mipLevels = 1;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
};

class VulkanImage{
    public:
    VulkanImage(const VulkanDevice& device, vk::Extent2D extent, const ImageConfig& config);

    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator = (const VulkanImage&) = delete;
    VulkanImage(VulkanImage&&) = default;

    void recreate(vk::Extent2D newExtent);

    //getters
    const vk::raii::Image& getImage() const {return image;}
    const vk::raii::ImageView& getImageView() const {return imageView;}
    vk::Format getFormat() const {return config.format;}
    vk::ImageUsageFlags getUsage() const {return config.usage;}

    //Where this image currently is. Whoever moves it records it here, so a second user
    //does not transition out of a layout the image left long ago
    vk::ImageLayout getCurrentLayout() const {return currentLayout;}

    //Bumped by recreate. Whoever cached a view can notice it went stale
    uint64_t getGeneration() const {return generation;}
    void setCurrentLayout(vk::ImageLayout layout) const {currentLayout = layout;}
    vk::Extent2D getExtent() const {return extent;}

    private:
    const VulkanDevice& device;
    ImageConfig config;
    vk::Extent2D extent;

    mutable vk::ImageLayout currentLayout = vk::ImageLayout::eUndefined;
    uint64_t generation = 0;

    vk::raii::Image image = nullptr;
    vk::raii::DeviceMemory memory = nullptr;
    vk::raii::ImageView imageView = nullptr;

    void build();




};

    inline ImageConfig makeDepthConfig(const VulkanDevice& device, ImageConfig config = {}){
    if(config.format == vk::Format::eUndefined){
        config.format = device.findSupportedFormat(
            {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint , vk::Format::eD24UnormS8Uint},
            vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    }
    config.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    config.aspect = vk::ImageAspectFlagBits::eDepth;
    return config;
}



