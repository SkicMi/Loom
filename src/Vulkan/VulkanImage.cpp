#include "VulkanImage.h"

VulkanImage::VulkanImage(const VulkanDevice& device, vk::Extent2D extent, const ImageConfig& config) : 
device(device) , config(config) , extent(extent){
    if(config.format == vk::Format::eUndefined){
        throw std::runtime_error("VulkanImage: format not set");
    }
    build();
}

void VulkanImage::build(){

    //A sentence now instead of a VK_ERROR_FORMAT_NOT_SUPPORTED and two VUIDs later.
    //sRGB formats never support storage images, so the usage and the format must agree
    //before the image is created, not when a descriptor is written
    if(config.usage & vk::ImageUsageFlagBits::eStorage){
        vk::FormatProperties formatProperties = device.getPhysicalDevice().getFormatProperties(config.format);
        if(!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage)){
            throw std::runtime_error("VulkanImage: eStorage usage requested but the format does not support storage images");
        }
    }

    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = config.format;
    imageInfo.extent = vk::Extent3D{extent.width, extent.height, 1};
    imageInfo.mipLevels = config.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = config.samples;
    imageInfo.tiling = config.tiling;
    imageInfo.usage = config.usage;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;

    image = vk::raii::Image(device.getDevice(), imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();

    vk::MemoryPropertyFlags properties;
    if(config.memoryUsage == MemoryUsage::GPU_ONLY){
        properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    }
    else{
        properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    }

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, properties);

    memory = vk::raii::DeviceMemory(device.getDevice(), allocInfo);
    image.bindMemory(*memory,0);

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = *image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = config.format;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.aspectMask = config.aspect;
    viewInfo.subresourceRange.levelCount = config.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    imageView = vk::raii::ImageView(device.getDevice(), viewInfo);
}

void VulkanImage::recreate(vk::Extent2D newExtent){
    currentLayout = vk::ImageLayout::eUndefined; //a fresh image is in no layout at all
    extent = newExtent;

    imageView = nullptr;
    image = nullptr;
    memory = nullptr;

    build();
}