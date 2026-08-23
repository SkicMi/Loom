#include "Texture.h"
#include "VulkanBuffer.h"


static ImageConfig makeTextureImageConfig(const TextureConfig& config) {
    ImageConfig imageConfig;
    imageConfig.format = config.format;
    imageConfig.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | config.extraUsage;
    imageConfig.aspect = vk::ImageAspectFlagBits::eColor;
    imageConfig.memoryUsage = MemoryUsage::GPU_ONLY;
    return imageConfig;
}

Texture::Texture(const VulkanDevice& device,
    const VulkanCommand& command,
    const void* pixels,
    vk::Extent2D extent,
    const TextureConfig& config) : image(device,extent,makeTextureImageConfig(config)){
        if(pixels == nullptr){
            throw std::runtime_error("Texture: pixels is nullptr");
        }
        if(extent.width == 0 || extent.height == 0){
            throw std::runtime_error("Texture: extent is 0");
        }

        vk::DeviceSize bytes = vk::DeviceSize(extent.width) * extent.height * 4;

        {
            VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);

            staging.upload(pixels,bytes);

            command.transitionImageLayout(image, vk::ImageLayout::eTransferDstOptimal);

            command.copyBufferToImage(staging.getBuffer(), image.getImage(), extent);

            command.transitionImageLayout(image, vk::ImageLayout::eShaderReadOnlyOptimal);
        }

        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = config.filter;
        samplerInfo.minFilter = config.filter;
        samplerInfo.addressModeU = config.addressMode;
        samplerInfo.addressModeV = config.addressMode;
        samplerInfo.addressModeW = config.addressMode;
        samplerInfo.anisotropyEnable = config.anisotropyEnable;
        samplerInfo.maxAnisotropy = config.maxAnisotropy;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        sampler = vk::raii::Sampler(device.getDevice(), samplerInfo);


    }

