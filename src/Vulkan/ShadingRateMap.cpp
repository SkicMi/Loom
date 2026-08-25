#include "ShadingRateMap.h"
#include "VulkanBuffer.h"
#include <stdexcept>

namespace{

ImageConfig makeRateImageConfig(){
    ImageConfig config;
    //One byte per texel, and it is a number rather than a colour - R8Uint is the format the
    //spec names for this attachment
    config.format = vk::Format::eR8Uint;
    config.usage = vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR
                 | vk::ImageUsageFlagBits::eTransferDst
                 | vk::ImageUsageFlagBits::eStorage;
    config.aspect = vk::ImageAspectFlagBits::eColor;
    config.memoryUsage = MemoryUsage::GPU_ONLY;
    return config;
}

vk::Extent2D rateExtentFor(vk::Extent2D renderExtent, vk::Extent2D texelSize){
    //Rounded up: a render area that does not divide evenly still needs a texel for its edge
    return vk::Extent2D{
        (renderExtent.width + texelSize.width - 1) / texelSize.width,
        (renderExtent.height + texelSize.height - 1) / texelSize.height
    };
}

}

uint8_t ShadingRateMap::pack(ShadingRate rate){
    const ShadingRateExtent size = shadingRateExtent(rate);

    //log2 of 1, 2 or 4 without a loop or a library call
    auto logTwo = [](uint32_t value) -> uint8_t {
        return value >= 4 ? 2 : (value >= 2 ? 1 : 0);
    };

    return static_cast<uint8_t>((logTwo(size.width) << 2) | logTwo(size.height));
}

ShadingRateMap::ShadingRateMap(const VulkanDevice& device, vk::Extent2D renderExtent) :
device(device),
texelSize(device.getShadingRateTexelSize()),
extent(rateExtentFor(renderExtent, device.getShadingRateTexelSize())),
image(device, rateExtentFor(renderExtent, device.getShadingRateTexelSize()), makeRateImageConfig()){

    if(!device.hasShadingRateImage()){
        throw std::runtime_error("ShadingRateMap: this device cannot take the shading rate from an image (attachmentFragmentShadingRate)");
    }
    if(renderExtent.width == 0 || renderExtent.height == 0){
        throw std::runtime_error("ShadingRateMap: the render area it covers has no size");
    }
}

void ShadingRateMap::upload(const VulkanCommand& command, const std::vector<uint8_t>& packed){
    if(packed.size() != texelCount()){
        throw std::runtime_error("ShadingRateMap: expected " + std::to_string(texelCount()) +
            " texels for a " + std::to_string(extent.width) + "x" + std::to_string(extent.height) +
            " map, got " + std::to_string(packed.size()));
    }

    VulkanBuffer staging(device, packed.size(), vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
    staging.upload(packed.data(), packed.size());

    command.transitionImageLayout(image, vk::ImageLayout::eTransferDstOptimal);
    command.copyBufferToImage(staging.getBuffer(), image.getImage(), extent);

    //Where the rasteriser expects to find it. Doing this here rather than at the pass means
    //a map that never changes is transitioned once in its life
    command.transitionImageLayout(image, vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR);
}

void ShadingRateMap::fill(const VulkanCommand& command, ShadingRate rate){
    upload(command, std::vector<uint8_t>(texelCount(), pack(rate)));
}
