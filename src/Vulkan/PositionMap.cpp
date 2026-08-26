#include "PositionMap.h"
#include "VulkanBuffer.h"
#include <stdexcept>

namespace{

ImageConfig makePositionImageConfig(){
    ImageConfig config;
    //Cetiri puna float-a. Pozicija na deset metara u pola preciznosti ima korak veci od
    //centimetra, a ovo se usporeduje s poznatim brojevima
    config.format = vk::Format::eR32G32B32A32Sfloat;
    config.usage = vk::ImageUsageFlagBits::eStorage      //compute je pise
                 | vk::ImageUsageFlagBits::eSampled      //sljedeci prolaz je cita
                 | vk::ImageUsageFlagBits::eTransferSrc; //i moze se procitati natrag
    config.aspect = vk::ImageAspectFlagBits::eColor;
    config.memoryUsage = MemoryUsage::GPU_ONLY;
    return config;
}

}

PositionMap::PositionMap(const VulkanDevice& device, vk::Extent2D extent, const PositionMapConfig& config) :
device(device),
extent(extent),
config(config),
image(device, extent, makePositionImageConfig()){

    if(extent.width == 0 || extent.height == 0){
        throw std::runtime_error("PositionMap: an image with no size has no points in it");
    }

    //Najblizi susjed i bez ponavljanja: tocke se ne interpoliraju. Prosjek dviju tocaka s
    //dvije strane ruba je tocka koja lebdi u zraku izmedu njih
    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    sampler = vk::raii::Sampler(device.getDevice(), samplerInfo);
}

void PositionMap::setDepthSource(const vk::raii::DescriptorPool& pool, const RenderTarget& depthTarget){
    if(!depthTarget.hasDepth() || !depthTarget.keepsDepth()){
        throw std::runtime_error("PositionMap: the depth source has to keep its depth (RenderTargetConfig::keepDepth), or there is nothing there to read");
    }
    if(depthTarget.usesDepthCompare()){
        throw std::runtime_error("PositionMap: this target's depth sampler compares instead of reading (RenderTargetConfig::depthCompare) - unprojecting needs the depth value itself, not the answer to a comparison");
    }

    setDepthSource(pool, depthTarget.getDepthSampled(), depthTarget.getExtent());
}

void PositionMap::setDepthSource(const vk::raii::DescriptorPool& pool,
                                 const SampledImage& depth, vk::Extent2D size){
    if(!depth.isValid()){
        throw std::runtime_error("PositionMap: the depth source has no view or no sampler");
    }
    if(size != extent){
        throw std::runtime_error("PositionMap: the depth is " + std::to_string(size.width) + "x" + std::to_string(size.height) +
            " but the position map is " + std::to_string(extent.width) + "x" + std::to_string(extent.height) +
            " - one point per pixel means one pixel per point");
    }

    depthExtent = size;

    if(!computePipeline){
        //Binding 0 cita dubinu, binding 1 pise tocke. Oba na dispatchevu vlastitom setu,
        //jer compute nema per-frame set
        vk::DescriptorSetLayoutBinding depthBinding;
        depthBinding.binding = 0;
        depthBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        depthBinding.descriptorCount = 1;
        depthBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutBinding positionBinding;
        positionBinding.binding = 1;
        positionBinding.descriptorType = vk::DescriptorType::eStorageImage;
        positionBinding.descriptorCount = 1;
        positionBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

        ComputePipelineConfig computeConfig;
        computeConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/unproject.comp.spv";
        computeConfig.descriptorBindings = {depthBinding, positionBinding};
        computeConfig.pushConstantSize = sizeof(UnprojectPush);

        computePipeline = std::make_unique<VulkanComputePipeline>(device, computeConfig);
        computeMaterial = std::make_unique<ComputeMaterial>(device, pool, *computePipeline);
    }

    computeMaterial->setSampledImage(0, depth);
    computeMaterial->setStorageImage(1, image, config.finalLayout);
}

UnprojectPush PositionMap::makePush() const{
    if(!intrinsics.isValid()){
        throw std::runtime_error("PositionMap: no intrinsics were set, so there is no focal length to unproject through (setIntrinsics)");
    }

    UnprojectPush push;
    push.size[0] = extent.width;
    push.size[1] = extent.height;
    push.fx = intrinsics.fx;
    push.fy = intrinsics.fy;
    push.cx = intrinsics.cx;
    push.cy = intrinsics.cy;
    push.nearPlane = intrinsics.nearPlane;
    push.farPlane = intrinsics.farPlane;
    return push;
}

const ComputeMaterial& PositionMap::getComputeMaterial() const{
    if(!computeMaterial){
        throw std::runtime_error("PositionMap: no depth source was bound, so there is nothing to dispatch (setDepthSource)");
    }
    return *computeMaterial;
}

SampledImage PositionMap::getSampled() const{
    return SampledImage{*image.getImageView(), *sampler, &image, image.getGeneration()};
}

ImageData PositionMap::readPixels(const VulkanCommand& command) const{
    if(config.finalLayout != vk::ImageLayout::eTransferSrcOptimal){
        throw std::runtime_error("PositionMap: readPixels needs the map to end its dispatch in eTransferSrcOptimal (PositionMapConfig::finalLayout)");
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
