#include "Texture.h"
#include "VulkanBuffer.h"
#include "ImageData.h"


static ImageConfig makeTextureImageConfig(const TextureConfig& config, vk::Extent2D extent) {
    ImageConfig imageConfig;
    imageConfig.format = config.format;
    imageConfig.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | config.extraUsage;
    imageConfig.aspect = vk::ImageAspectFlagBits::eColor;
    imageConfig.memoryUsage = MemoryUsage::GPU_ONLY;

    if(config.generateMipmaps){
        imageConfig.mipLevels = mipLevelsFor(extent);

        //A mip chain is built by blitting each level out of the one above it, so the image
        //has to be a transfer source as well as a destination
        imageConfig.usage |= vk::ImageUsageFlagBits::eTransferSrc;
    }

    return imageConfig;
}

Texture::Texture(const VulkanDevice& device,
    const VulkanCommand& command,
    const void* pixels,
    vk::Extent2D extent,
    const TextureConfig& config) : image(device,extent,makeTextureImageConfig(config, extent)){
        if(pixels == nullptr){
            throw std::runtime_error("Texture: pixels is nullptr");
        }
        if(extent.width == 0 || extent.height == 0){
            throw std::runtime_error("Texture: extent is 0");
        }

        //Iz formata, ne cetiri fiksno: karta dubine je jedan float po pikselu i nema sto
        //raditi u cetiri kanala
        vk::DeviceSize bytes = vk::DeviceSize(extent.width) * extent.height * bytesPerPixel(config.format);

        {
            VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);

            staging.upload(pixels,bytes);

            command.transitionImageLayout(image, vk::ImageLayout::eTransferDstOptimal);

            command.copyBufferToImage(staging.getBuffer(), image.getImage(), extent);

            if(image.getMipLevels() > 1){
                //Fills every level below the top and leaves the whole image where a shader
                //can read it, so there is no transition to do afterwards
                command.generateMipmaps(image);
            }
            else{
                command.transitionImageLayout(image, vk::ImageLayout::eShaderReadOnlyOptimal);
            }
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
        //Zero would pin every lookup to the top level and the chain would exist unused
        samplerInfo.maxLod = static_cast<float>(image.getMipLevels());

        sampler = vk::raii::Sampler(device.getDevice(), samplerInfo);


    }

