#include "NormalMap.h"
#include "VulkanBuffer.h"
#include <stdexcept>

namespace{

ImageConfig makeNormalImageConfig(){
    ImageConfig config;
    config.format = vk::Format::eR32G32B32A32Sfloat;
    config.usage = vk::ImageUsageFlagBits::eStorage
                 | vk::ImageUsageFlagBits::eSampled
                 | vk::ImageUsageFlagBits::eTransferSrc;
    config.aspect = vk::ImageAspectFlagBits::eColor;
    config.memoryUsage = MemoryUsage::GPU_ONLY;
    return config;
}

}

NormalMap::NormalMap(const VulkanDevice& device, vk::Extent2D extent, const NormalMapConfig& config) :
device(device),
extent(extent),
config(config),
image(device, extent, makeNormalImageConfig()){

    if(extent.width == 0 || extent.height == 0){
        throw std::runtime_error("NormalMap: an image with no size has no normals in it");
    }
    if(config.radius == 0){
        throw std::runtime_error("NormalMap: a radius of zero has no neighbour to take a slope from");
    }

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    sampler = vk::raii::Sampler(device.getDevice(), samplerInfo);
}

void NormalMap::setPositionSource(const vk::raii::DescriptorPool& pool, const PositionMap& positions){
    setPositionSource(pool, positions.getSampled(), positions.getExtent());
}

void NormalMap::setPositionSource(const vk::raii::DescriptorPool& pool,
                                  const SampledImage& positions, vk::Extent2D size){
    if(!positions.isValid()){
        throw std::runtime_error("NormalMap: the position source has no view or no sampler");
    }
    if(size != extent){
        throw std::runtime_error("NormalMap: the positions are " + std::to_string(size.width) + "x" + std::to_string(size.height) +
            " but the normal map is " + std::to_string(extent.width) + "x" + std::to_string(extent.height) +
            " - one normal per point means one point per normal");
    }

    positionExtent = size;

    if(!computePipeline){
        vk::DescriptorSetLayoutBinding positionBinding;
        positionBinding.binding = 0;
        positionBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        positionBinding.descriptorCount = 1;
        positionBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutBinding normalBinding;
        normalBinding.binding = 1;
        normalBinding.descriptorType = vk::DescriptorType::eStorageImage;
        normalBinding.descriptorCount = 1;
        normalBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

        ComputePipelineConfig computeConfig;
        computeConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/normals.comp.spv";
        computeConfig.descriptorBindings = {positionBinding, normalBinding};
        computeConfig.pushConstantSize = sizeof(NormalPush);

        computePipeline = std::make_unique<VulkanComputePipeline>(device, computeConfig);
        computeMaterial = std::make_unique<ComputeMaterial>(device, pool, *computePipeline);
    }

    computeMaterial->setSampledImage(0, positions);
    computeMaterial->setStorageImage(1, image, config.finalLayout);
}

NormalPush NormalMap::makePush() const{
    NormalPush push;
    push.size[0] = extent.width;
    push.size[1] = extent.height;
    push.radius = int32_t(config.radius);
    push.noiseSlope = config.noiseSlope;
    push.slopeLimit = config.slopeLimit;
    push.padding0[0] = push.padding0[1] = push.padding0[2] = 0.0f;
    return push;
}

void NormalMap::setRadius(uint32_t radius){
    if(radius == 0){
        throw std::runtime_error("NormalMap::setRadius: a radius of zero has no neighbour to take a slope from");
    }
    config.radius = radius;
}

const ComputeMaterial& NormalMap::getComputeMaterial() const{
    if(!computeMaterial){
        throw std::runtime_error("NormalMap: no position source was bound, so there is nothing to dispatch (setPositionSource)");
    }
    return *computeMaterial;
}

SampledImage NormalMap::getSampled() const{
    return SampledImage{*image.getImageView(), *sampler, &image, image.getGeneration()};
}

ImageData NormalMap::readPixels(const VulkanCommand& command) const{
    if(config.finalLayout != vk::ImageLayout::eTransferSrcOptimal){
        throw std::runtime_error("NormalMap: readPixels needs the map to end its dispatch in eTransferSrcOptimal (NormalMapConfig::finalLayout)");
    }

    const vk::DeviceSize bytes = vk::DeviceSize(extent.width) * extent.height * bytesPerPixel(image.getFormat());
    device.getDevice().waitIdle();

    VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
    command.copyImageToBuffer(image.getImage(), staging.getBuffer(), extent);

    ImageData out;
    out.extent = extent;
    out.format = image.getFormat();
    out.pixels.resize(static_cast<size_t>(bytes));
    staging.download(out.pixels.data(), bytes);

    return out;
}
