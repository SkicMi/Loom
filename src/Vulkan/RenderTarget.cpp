#include "RenderTarget.h"

RenderTarget::RenderTarget(const VulkanDevice& device, vk::Extent2D extent, const RenderTargetConfig& config) :
device(device),
config(config),
extent(extent),
colorImage(config.enableColor ? std::optional<VulkanImage>(std::in_place, device, extent, makeColorConfig(config)) : std::nullopt),
depthImage(config.enableDepth ? std::optional<VulkanImage>(std::in_place, device,  extent, makeTargetDepthConfig(device, config)) : std::nullopt){

    //A target that is neither colour nor depth is nothing at all, and a pass into it would
    //fail deep inside beginRendering instead of here
    if(!config.enableColor && !config.enableDepth){
        throw std::runtime_error("RenderTarget: neither colour nor depth is enabled, so there is nothing to render into");
    }
    if(config.keepDepth && !config.enableDepth){
        throw std::runtime_error("RenderTarget: keepDepth was asked for but there is no depth attachment (enableDepth)");
    }

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = config.filter;
    samplerInfo.minFilter = config.filter;
    samplerInfo.addressModeU = config.addressMode;
    samplerInfo.addressModeV = config.addressMode;
    samplerInfo.addressModeW = config.addressMode;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.maxLod = 0.0f;

    sampler = vk::raii::Sampler(device.getDevice(),samplerInfo);

    //A second sampler, because a depth buffer read as a shadow map wants nothing the colour
    //sampler offers. Clamping to a white border means a fragment that falls outside the
    //light's box compares against "farther than anything" and comes back lit, rather than
    //wrapping around and picking up a shadow from the far side of the map
    if(config.keepDepth){
        vk::SamplerCreateInfo depthSamplerInfo;
        depthSamplerInfo.magFilter = vk::Filter::eLinear;
        depthSamplerInfo.minFilter = vk::Filter::eLinear;
        depthSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        depthSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        depthSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        depthSamplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        depthSamplerInfo.compareEnable = config.depthCompare;
        depthSamplerInfo.compareOp = config.depthCompareOp;
        depthSamplerInfo.maxLod = 0.0f;

        depthSampler = vk::raii::Sampler(device.getDevice(), depthSamplerInfo);
    }

}

void RenderTarget::resize(vk::Extent2D newExtent){
    extent = newExtent;
    if(colorImage){
        colorImage->recreate(newExtent);
    }
    if(depthImage){
        depthImage->recreate(newExtent);
    }
}

ImageData RenderTarget::readPixels(const VulkanCommand& command) const{
    if(!colorImage){
        throw std::runtime_error("readPixels: this target has no colour attachment (RenderTargetConfig::enableColor)");
    }
    if(config.finalLayout != vk::ImageLayout::eTransferSrcOptimal){
        throw std::runtime_error("readPixels: target's finalLayout is not eTransferSrcOptimal");
    }
    const vk::Format format = colorImage->getFormat();
    const vk::DeviceSize bytes = vk::DeviceSize(extent.width) * extent.height * bytesPerPixel(format);
    device.getDevice().waitIdle();

    VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
    command.copyImageToBuffer(colorImage->getImage(), staging.getBuffer(), extent);

    ImageData out;
    out.extent = extent;
    out.format = format;
    out.pixels.resize(static_cast<size_t>(bytes));
    staging.download(out.pixels.data(), bytes);

    return out;
}

ImageData RenderTarget::readDepthPixels(const VulkanCommand& command) const{
    if(!depthImage){
        throw std::runtime_error("readDepthPixels: this target has no depth attachment (RenderTargetConfig::enableDepth)");
    }
    //Without keepDepth the pass ended with storeOp eDontCare, so whatever is in the image now
    //is whatever the driver felt like leaving there. Reading it would be reading noise
    if(!config.keepDepth){
        throw std::runtime_error("readDepthPixels: the target was not created with keepDepth, so its depth was discarded at endPass");
    }

    const vk::Format format = depthImage->getFormat();
    const vk::DeviceSize bytes = vk::DeviceSize(extent.width) * extent.height * bytesPerPixel(format);
    device.getDevice().waitIdle();

    //The depth is borrowed out of whatever layout the pass left it in and put back, the same
    //way readLastFrame borrows a swapchain image out of ePresentSrcKHR
    const vk::ImageLayout was = depthImage->getCurrentLayout();
    command.transitionImageLayout(*depthImage, vk::ImageLayout::eTransferSrcOptimal);

    VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
    command.copyImageToBuffer(depthImage->getImage(), staging.getBuffer(), extent, vk::ImageAspectFlagBits::eDepth);

    command.transitionImageLayout(*depthImage, was);

    ImageData out;
    out.extent = extent;
    out.format = format;
    out.pixels.resize(static_cast<size_t>(bytes));
    staging.download(out.pixels.data(), bytes);

    return out;
}
