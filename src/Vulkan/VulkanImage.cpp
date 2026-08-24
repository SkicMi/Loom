#include "VulkanImage.h"
#include <vk_mem_alloc.h>

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

    layers = config.cube ? 6u : 1u;

    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = config.format;
    imageInfo.extent = vk::Extent3D{extent.width, extent.height, 1};
    imageInfo.mipLevels = config.mipLevels;
    imageInfo.arrayLayers = layers;

    //A cube is six 2D layers plus the promise that they may be viewed as one cube. Without
    //the flag the view below is rejected, and a cube image the driver cannot lay out as a
    //cube is a validation error rather than a slightly different picture
    if(config.cube){
        imageInfo.flags |= vk::ImageCreateFlagBits::eCubeCompatible;

        if(extent.width != extent.height){
            throw std::runtime_error("VulkanImage: a cube image has to be square");
        }
    }
    imageInfo.samples = config.samples;
    imageInfo.tiling = config.tiling;
    imageInfo.usage = config.usage;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;

    //VMA sees the whole VkImageCreateInfo, so AUTO can tell an attachment from a staged
    //texture by its usage flags and place it accordingly. It also honours the driver saying
    //"this image would rather have its own block" (VK_KHR_dedicated_allocation, core since
    //1.1), which the old vkAllocateMemory path had no way of even asking about
    VmaAllocationCreateInfo allocationCreateInfo{};

    if(config.memoryUsage == MemoryUsage::GPU_ONLY){
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }
    else{
        //A linear tiled image the CPU writes into directly. Rare - textures go through a
        //staging buffer - but the path stays open
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    if(config.dedicated){
        allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    allocationCreateInfo.priority = config.priority;

    const VkImageCreateInfo& rawImageInfo = imageInfo;

    VkImage rawImage = VK_NULL_HANDLE;
    VmaAllocation rawAllocation = nullptr;

    if(vmaCreateImage(device.getAllocator().get(), &rawImageInfo, &allocationCreateInfo,
                      &rawImage, &rawAllocation, nullptr) != VK_SUCCESS){
        throw std::runtime_error("VulkanImage: vmaCreateImage failed");
    }

    allocation = MemoryAllocation(device.getAllocator().get(), rawAllocation);
    image = vk::raii::Image(device.getDevice(), rawImage);

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = *image;
    viewInfo.viewType = config.cube ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
    viewInfo.format = config.format;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.aspectMask = config.aspect;
    viewInfo.subresourceRange.levelCount = config.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layers;

    imageView = vk::raii::ImageView(device.getDevice(), viewInfo);

    //One 2D view per layer. The cube view above is what a shader samples with a direction;
    //rendering happens one face at a time, and a pass can only attach a single layer
    layerViews.clear();
    if(layers > 1){
        layerViews.reserve(layers);
        for(uint32_t layer = 0; layer < layers; ++layer){
            vk::ImageViewCreateInfo faceInfo = viewInfo;
            faceInfo.viewType = vk::ImageViewType::e2D;
            faceInfo.subresourceRange.baseArrayLayer = layer;
            faceInfo.subresourceRange.layerCount = 1;

            layerViews.emplace_back(device.getDevice(), faceInfo);
        }
    }
}

void VulkanImage::recreate(vk::Extent2D newExtent){
    currentLayout = vk::ImageLayout::eUndefined; //a fresh image is in no layout at all
    ++generation;                                //and every view of the old one is dead
    extent = newExtent;

    //View first, then the handle, then the memory under it. build() overwrites all three
    //anyway, but releasing them here keeps the old and the new image from coexisting -
    //a full screen colour target is measured in megabytes
    layerViews.clear();
    imageView = nullptr;
    image = nullptr;
    allocation.reset();

    build();
}