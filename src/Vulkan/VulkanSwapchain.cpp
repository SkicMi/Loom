#include "VulkanSwapchain.h"


VulkanSwapchain::VulkanSwapchain(const VulkanInstance& instance,
const Window& window,
const VulkanDevice& device,
const SwapchainConfig& config) : 
instance(instance),
window(window),
device(device),
config(config){
    build();

   
}


void VulkanSwapchain::queryCapabilities(){
    const auto& physical = device.getPhysicalDevice();
    const auto& surface = *instance.getSurface();

    capabilities = physical.getSurfaceCapabilitiesKHR(surface);
    formats = physical.getSurfaceFormatsKHR(surface);
    presentModes = physical.getSurfacePresentModesKHR(surface);

}

vk::SurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(){
    for(const auto& available : formats){
        if(available.format == config.preferredSurfaceFormat.format &&
        available.colorSpace == config.preferredSurfaceFormat.colorSpace){
            return available;
        }
    }
    return formats[0];
}


vk::PresentModeKHR VulkanSwapchain::choosePresentMode(){
    for(const auto& available : presentModes){
        if(available == config.preferredPresentMode){
            return available;
        }
    }
    return vk::PresentModeKHR::eFifo; //Fallback to guaranteed one
}

vk::Extent2D VulkanSwapchain::chooseExtent(){
    if(capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()){
        return capabilities.currentExtent;
    }

    int width,height;
    glfwGetFramebufferSize(window.getWindow(),&width,&height);

    vk::Extent2D actual = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };

    actual.width = std::clamp(actual.width, capabilities.minImageExtent.width,capabilities.maxImageExtent.width);
    actual.height = std::clamp(actual.height,capabilities.minImageExtent.height,capabilities.maxImageExtent.height);


    return actual;
}

uint32_t VulkanSwapchain::chooseImageCount(){
    uint32_t count = config.preferredImageCount;

    if(count == 0){
        count = capabilities.minImageCount + 1;
    }

    if(count < capabilities.minImageCount){
        count = capabilities.minImageCount;
    }

    if(capabilities.maxImageCount != 0 && count > capabilities.maxImageCount){
        count = capabilities.maxImageCount;
    }

    return count;
}

void VulkanSwapchain::createSwapchain(){
    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.surface = *instance.getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = config.imageUsage;

    const QueueFamilyIndices& indices = device.getQueueIndices();
    uint32_t families[] = {
        indices.graphicsFamilies.value(),
        indices.presentFamilies.value()
    };

    if(indices.graphicsFamilies != indices.presentFamilies){
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = families;
    }
    else{
        createInfo.imageSharingMode  = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = vk::True;
    createInfo.oldSwapchain = nullptr;

    swapchain = vk::raii::SwapchainKHR(device.getDevice(),createInfo);

}

void VulkanSwapchain::createImageViews(){
    images = swapchain.getImages();

    imageViews.clear();
    imageViews.reserve(images.size());

    for(const auto& image : images){
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = image;
        viewInfo.viewType = vk::ImageViewType::e2D; //Swapchain image is always a grid of 2D pixels, this doesn't mean that the image is 2D, it can be 3D, but the view is always 2D
        viewInfo.format = surfaceFormat.format;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        imageViews.emplace_back(device.getDevice(),viewInfo);
    }
}   

void VulkanSwapchain::build(){
    queryCapabilities();
    surfaceFormat = chooseSurfaceFormat();
    presentMode = choosePresentMode();
    extent = chooseExtent();
    imageCount = chooseImageCount();
    createSwapchain();
    createImageViews();
}

void VulkanSwapchain::recreateSwapchain(){
    //Block while minimized, vulkan doesn't allow 0x0 swapchain
    int width = 0, height = 0;
    glfwGetFramebufferSize(window.getWindow(), &width, &height);
    while(width == 0 || height == 0){
        glfwGetFramebufferSize(window.getWindow(), &width, &height);
        glfwWaitEvents();
    }

    //Destroy old swapchain and image views
    device.getDevice().waitIdle();
    imageViews.clear();
    images.clear();
    swapchain = nullptr;

    //rebuild
    build();
}