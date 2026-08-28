#include "StreamingTexture.h"
#include "Barriers.h"
#include <stdexcept>

namespace{

ImageConfig makeStreamingImageConfig(const StreamingTextureConfig& config){
    ImageConfig out;
    out.format = config.format;
    out.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    out.aspect = vk::ImageAspectFlagBits::eColor;
    out.memoryUsage = MemoryUsage::GPU_ONLY;

    //Bez mip lanca: on se gradi blitanjem po razinama, sto je posao po frameu koji snimka ne
    //treba - plate se crta jedan na jedan preko ekrana i nikad se ne smanjuje
    out.mipLevels = 1;
    return out;
}

}

StreamingTexture::Slot::Slot(const VulkanDevice& device, vk::Extent2D extent,
                             const ImageConfig& imageConfig, vk::DeviceSize bytes,
                             const VulkanCommand& command)
: image(device, extent, imageConfig),
  staging(device, bytes, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU),
  transfer(command.createTransferBuffer()){

    vk::FenceCreateInfo fenceInfo;
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
    done = vk::raii::Fence(device.getDevice(), fenceInfo);
}

StreamingTexture::StreamingTexture(const VulkanDevice& device,
                                   const VulkanCommand& command,
                                   vk::Extent2D extent,
                                   const StreamingTextureConfig& config)
: device(device), command(command), extent(extent), config(config){

    if(extent.width == 0 || extent.height == 0){
        throw std::runtime_error("StreamingTexture: an image with no size has no pixels to stream");
    }

    uint32_t wanted = config.slots;
    if(wanted == 0) wanted = uint32_t(command.getCommandBuffers().size());
    if(wanted < 2) wanted = 2;

    bytesPerFrame = vk::DeviceSize(extent.width) * extent.height * 4;

    const ImageConfig imageConfig = makeStreamingImageConfig(config);

    slots.reserve(wanted);
    for(uint32_t i = 0; i < wanted; ++i){
        slots.emplace_back(device, extent, imageConfig, bytesPerFrame, command);

        //Svaki slot krece iz stanja u kojem ga shader smije citati. Bez toga bi prvi frame
        //koji jos nije uploadan bio vezan u nedefiniranom rasporedu
        command.transitionImageLayout(slots.back().image, vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = config.filter;
    samplerInfo.minFilter = config.filter;
    samplerInfo.addressModeU = config.addressMode;
    samplerInfo.addressModeV = config.addressMode;
    samplerInfo.addressModeW = config.addressMode;
    samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueBlack;
    sampler = vk::raii::Sampler(device.getDevice(), samplerInfo);
}

void StreamingTexture::update(const void* pixels, size_t size){
    if(pixels == nullptr){
        throw std::runtime_error("StreamingTexture::update: pixels is nullptr");
    }
    if(size != size_t(bytesPerFrame)){
        throw std::runtime_error("StreamingTexture::update: expected " + std::to_string(bytesPerFrame) +
            " bytes for a " + std::to_string(extent.width) + "x" + std::to_string(extent.height) +
            " frame, got " + std::to_string(size));
    }

    current = uint32_t(updates % slots.size());
    Slot& slot = slots[current];

    //Prijenos u ovaj slot od prije slots frameova. Gotovo nikad ne ceka - ako ceka, znaci da
    //upload ne stize za crtanjem, a to je nesto sto se hoce znati a ne sakriti
    const vk::Result waited = device.getDevice().waitForFences(*slot.done, VK_TRUE, UINT64_MAX);
    if(waited != vk::Result::eSuccess){
        throw std::runtime_error("StreamingTexture::update: waiting for the previous upload failed");
    }
    device.getDevice().resetFences(*slot.done);

    slot.staging.upload(pixels, bytesPerFrame);

    command.submitWithoutWaiting(slot.transfer, slot.done, [&](const vk::raii::CommandBuffer& buffer){
        recordBarrier(buffer, imageBarrier(*slot.image.getImage(),
                                           vk::ImageLayout::eShaderReadOnlyOptimal,
                                           vk::ImageLayout::eTransferDstOptimal,
                                           vk::ImageAspectFlagBits::eColor));

        vk::BufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0,0,0};
        region.imageExtent = vk::Extent3D{extent.width, extent.height, 1};

        buffer.copyBufferToImage(*slot.staging.getBuffer(), *slot.image.getImage(),
                                 vk::ImageLayout::eTransferDstOptimal, region);

        recordBarrier(buffer, imageBarrier(*slot.image.getImage(),
                                           vk::ImageLayout::eTransferDstOptimal,
                                           vk::ImageLayout::eShaderReadOnlyOptimal,
                                           vk::ImageAspectFlagBits::eColor));
    });

    //Slika sama pamti gdje je ostala, pa je nitko poslije ne mora pogadati
    slot.image.setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

    ++updates;
}

SampledImage StreamingTexture::getSampled() const{
    const Slot& slot = slots[current];
    return SampledImage{*slot.image.getImageView(), *sampler, &slot.image, slot.image.getGeneration()};
}
