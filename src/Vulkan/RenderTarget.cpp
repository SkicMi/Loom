#include "RenderTarget.h"

RenderTarget::RenderTarget(const VulkanDevice& device, vk::Extent2D extent, const RenderTargetConfig& config) :
device(device),
config(config),
extent(extent),
colorImage(device,extent, makeColorConfig(config)),
depthImage(config.enableDepth ? std::optional<VulkanImage>(std::in_place, device,  extent, makeDepthConfig(device, config.depthConfig)) : std::nullopt){

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = config.filter;
    samplerInfo.minFilter = config.filter;
    samplerInfo.addressModeU = config.addressMode;
    samplerInfo.addressModeV = config.addressMode;
    samplerInfo.addressModeW = config.addressMode;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.maxLod = 0.0f;

    sampler = vk::raii::Sampler(device.getDevice(),samplerInfo);

}

void RenderTarget::resize(vk::Extent2D newExtent){
    extent = newExtent;
    colorImage.recreate(newExtent);
    if(depthImage){
        depthImage->recreate(newExtent);
    }
}

ImageData RenderTarget::readPixels(const VulkanCommand& command) const{
    if(config.finalLayout != vk::ImageLayout::eTransferSrcOptimal){
        throw std::runtime_error("readPixels: target's finalLayout is not eTransferSrcOptimal");
    }
    const vk::Format format = colorImage.getFormat();
    const vk::DeviceSize bytes = vk::DeviceSize(extent.width) * extent.height * bytesPerPixel(format);
    device.getDevice().waitIdle();

    VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::CPU_TO_GPU);
    command.copyImageToBuffer(colorImage.getImage(), staging.getBuffer(), extent);

    ImageData out;
    out.extent = extent;
    out.format = format;
    out.pixels.resize(static_cast<size_t>(bytes));
    staging.download(out.pixels.data(), bytes);

    return out;
}


  