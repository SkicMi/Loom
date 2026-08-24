#pragma once
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanAllocator.h"

struct ImageConfig{
    vk::Format format = vk::Format::eUndefined; //must be set
    vk::ImageUsageFlags usage = {};
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
    vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
    MemoryUsage memoryUsage = MemoryUsage::GPU_ONLY;
    uint32_t mipLevels = 1;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;

    //A whole VkDeviceMemory block of its own. Attachments want this: they are big, they live
    //as long as the window does, and a dedicated block is the one a driver can page out or
    //resize on its own without dragging half a dozen unrelated buffers along
    bool dedicated = false;

    //0..1, only listened to when VK_EXT_memory_priority is present. Attachments go to 1.0,
    //because a frame without its depth buffer cannot be drawn at all
    float priority = 0.5f;

    //Six layers, flagged cube compatible, and sampled through a cube view. A point light
    //casts in every direction at once, so its shadow map is not one image but six faces of
    //one - and the shader looks it up with a direction rather than a coordinate
    bool cube = false;
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

    //One face of a layered image, as something that can be rendered into. The view above
    //covers all six at once and is what a shader samples; these are what a pass attaches
    const vk::raii::ImageView& getLayerView(uint32_t layer) const {
        if(layer >= layerViews.size()){
            throw std::runtime_error("VulkanImage: no such layer view - this image is not layered");
        }
        return layerViews[layer];
    }
    uint32_t getLayerCount() const {return layers;}
    bool isCube() const {return config.cube;}
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

    //Same destruction contract as VulkanBuffer: the allocation is declared first so it is
    //freed last, after the view and the image handle above it are already gone
    MemoryAllocation allocation;
    vk::raii::Image image = nullptr;
    vk::raii::ImageView imageView = nullptr;
    std::vector<vk::raii::ImageView> layerViews;
    uint32_t layers = 1;

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

    //A depth buffer is a full screen attachment that lives as long as the swapchain: exactly
    //the resource that earns its own block and the highest eviction priority
    config.dedicated = true;
    config.priority = 1.0f;
    return config;
}



