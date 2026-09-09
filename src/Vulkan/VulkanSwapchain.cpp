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

    //Vidi appDecidesExtent. 0xFFFFFFFF je jedina vrijednost koja znaci "nema broja"
    appExtent = capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max();

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

vk::Extent2D VulkanSwapchain::clampToSurface(vk::Extent2D wanted) const{
    wanted.width  = std::clamp(wanted.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    wanted.height = std::clamp(wanted.height,capabilities.minImageExtent.height,capabilities.maxImageExtent.height);
    return wanted;
}

vk::Extent2D VulkanSwapchain::chooseExtent(){
    //Kompozitor je vec odlucio, i njegov broj nije prijedlog
    if(!appExtent){
        return capabilities.currentExtent;
    }

    return clampToSurface(desiredExtent());
}

vk::Extent2D VulkanSwapchain::desiredExtent() const{
    //Zahtjev vrijedi samo gdje se smije traziti. Inace je istina ono sto prozor kaze da jest,
    //a to je na takvoj povrsini isti broj koji bi dao i currentExtent
    if(appExtent && requested){
        return clampToSurface(*requested);
    }
    return getWindowExtent();
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
    vk::ImageUsageFlags usage = config.imageUsage;
    readbackAvailable = false;
    if(config.allowReadback){
        const vk::SurfaceCapabilitiesKHR capabilities = device.getPhysicalDevice().getSurfaceCapabilitiesKHR(*instance.getSurface());
        if(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc){
            usage |= vk::ImageUsageFlagBits::eTransferSrc;
            readbackAvailable = true;
        }
    }
    createInfo.imageUsage = usage;

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
    //STARI SWAPCHAIN SE PREDAJE, ne baca. Dosad se prvo unistavao pa se novi gradio ni iz
    //cega, a to na Waylandu odmapira samu povrsinu: prozor tada gubi velicinu koju je imao i
    //kompozitor mu vrati onu s kojom je stvoren. Predajom driver zna da je ovo ista povrsina
    //koja se samo mijenja, i smije preuzeti sto se preuzeti da
    createInfo.oldSwapchain = (*swapchain != VK_NULL_HANDLE) ? *swapchain : vk::SwapchainKHR{};

    //Novi nastaje PRIJE nego stari ode: pridruzivanje unisti prethodnog, i to je jedini
    //ispravan redoslijed - dok createInfo drzi njegovu rucku, on mora biti ziv
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

vk::Extent2D VulkanSwapchain::getWindowExtent() const{
    int width = 0, height = 0;
    glfwGetFramebufferSize(window.getWindow(), &width, &height);
    return vk::Extent2D{uint32_t(width), uint32_t(height)};
}

bool VulkanSwapchain::matchesTarget() const{
    const vk::Extent2D target = desiredExtent();
    if(target.width == 0 || target.height == 0) return true;
    return target.width == extent.width && target.height == extent.height;
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

    //Swapchain se OVDJE ne nulira: build() ga treba zivog da bi ga predao kao oldSwapchain
    build();
}