#include "Relight.h"
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>

namespace{

vk::DescriptorSetLayoutBinding imageBinding(uint32_t binding){
    vk::DescriptorSetLayoutBinding out;
    out.binding = binding;
    out.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    out.descriptorCount = 1;
    out.stageFlags = vk::ShaderStageFlagBits::eFragment;
    return out;
}

}

Relight::Relight(const VulkanDevice& device,
                 const VulkanCommand& command,
                 const vk::raii::DescriptorPool& pool,
                 const PositionMap& positions,
                 const NormalMap& normals,
                 const RelightConfig& config)
: Relight(device, command, pool, positions, normals, SampledImage{}, config){
}

Relight::Relight(const VulkanDevice& device,
                 const VulkanCommand& command,
                 const vk::raii::DescriptorPool& pool,
                 const PositionMap& positions,
                 const NormalMap& normals,
                 SampledImage plate,
                 const RelightConfig& config){

    if(positions.getExtent() != normals.getExtent()){
        throw std::runtime_error("Relight: the position and normal maps are different sizes, so they do not describe the same surface");
    }

    hasPlate = plate.isValid();

    data.baseColor = config.surface.baseColor;
    data.surface = {config.surface.shininess, config.surface.specularStrength,
                    config.surface.diffuseWrap, 0.0f};
    data.shadow = {config.shadow.enabled ? float(config.shadow.steps) : 0.0f,
                   config.shadow.maxDistance, config.shadow.thickness, config.shadow.bias};
    data.shadowSlope = {config.shadow.slopeBias, 0.0f,
                        config.shadow.thicknessGrowth, config.shadow.frontFade};
    data.occlusion = {config.occlusion.strength, config.occlusion.nearRadius,
                      config.occlusion.farRadius, config.occlusion.scale};
    data.occluder = {config.shadow.maskOnly ? 1.0f : 0.0f, config.shadow.occluderBlur,
                     float(config.shadow.rays), config.shadow.lightRadius};
    data.imageSize = {float(positions.getExtent().width), float(positions.getExtent().height), 0.0f, 0.0f};

    //Fullscreen: nema vrhova, nema atributa, nema dubine. Trokut koji pokriva ekran dolazi
    //iz SV_VertexID, kao i u svakom drugom fullscreen prolazu
    PipelineConfig pipelineConfig;
    pipelineConfig.vertexBindings.clear();
    pipelineConfig.vertexAttributes.clear();
    //Bijeli piksel na mjestu maske. Slot postoji uvijek, pa je raspored deskriptora isti bez
    //obzira postavi li se prava maska - inace bi ista dva shadera trebala dva pipelinea
    const uint8_t white[4] = {255, 255, 255, 255};
    TextureConfig maskConfig;
    maskConfig.format = vk::Format::eR8G8B8A8Unorm;
    maskConfig.filter = vk::Filter::eNearest;
    maskConfig.generateMipmaps = false;
    whiteMask.emplace(device, command, white, vk::Extent2D{1, 1}, maskConfig);

    std::vector<SampledImage> images{positions.getSampled(), normals.getSampled(),
                                     whiteMask->getSampled()};
    if(hasPlate){
        images.push_back(plate);
    }

    pipelineConfig.descriptorBindings.clear();
    for(uint32_t slot = 0; slot < images.size(); ++slot){
        pipelineConfig.descriptorBindings.push_back(imageBinding(slot));
    }
    //Payload uvijek iza slika - isto pravilo po kojem ih i Material rasporeduje
    pipelineConfig.descriptorBindings.push_back(Material::getDataLayoutBinding(uint32_t(images.size())));

    //Trokut preko ekrana je isti trokut u oba slucaja, pa je i vertex stage isti file
    pipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/relight.vert.spv";
    pipelineConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) +
        (hasPlate ? "/relight_composite.frag.spv" : "/relight.frag.spv");
    pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    pipelineConfig.colorFormat = config.colorFormat;
    pipelineConfig.depthTestEnable = false;
    pipelineConfig.depthWriteEnable = false;

    pipeline.emplace(device, pipelineConfig, config.colorFormat, config.depthFormat);

    material.emplace(device, command, pool, *pipeline, std::move(images), &data, sizeof(data));
}

void Relight::setCamera(const Camera& camera){
    //Pogled je kruta transformacija, pa je njegov obrat samo obrat - bez ikakve skale koju
    //bi trebalo posebno paziti
    data.view = camera.getView();
    data.inverseView = glm::inverse(data.view);
    material->setData(&data, sizeof(data));
}

void Relight::setIntrinsics(const CameraIntrinsics& intrinsics, vk::Extent2D imageSize){
    data.intrinsics = {intrinsics.fx, intrinsics.fy, intrinsics.cx, intrinsics.cy};
    data.imageSize = {float(imageSize.width), float(imageSize.height), 0.0f, 0.0f};
    material->setData(&data, sizeof(data));
}

void Relight::setShadow(const ScreenShadowConfig& shadow){
    if(shadow.enabled && data.intrinsics.x == 0.0f){
        throw std::runtime_error("Relight::setShadow: the trace turns a point back into a pixel, so it needs the intrinsics the points came from (setIntrinsics)");
    }

    data.shadow = {shadow.enabled ? float(shadow.steps) : 0.0f,
                   shadow.maxDistance, shadow.thickness, shadow.bias};
    data.shadowSlope = {shadow.slopeBias, 0.0f,
                        shadow.thicknessGrowth, shadow.frontFade};
    data.occluder = {shadow.maskOnly ? 1.0f : 0.0f, shadow.occluderBlur,
                     float(shadow.rays), shadow.lightRadius};
    material->setData(&data, sizeof(data));
}

void Relight::setOcclusion(const ScreenOcclusionConfig& occlusion){
    data.occlusion = {occlusion.strength, occlusion.nearRadius,
                      occlusion.farRadius, occlusion.scale};
    material->setData(&data, sizeof(data));
}

void Relight::setSurface(const MaterialData& surface){
    data.baseColor = surface.baseColor;
    data.surface = {surface.shininess, surface.specularStrength, surface.diffuseWrap, 0.0f};
    material->setData(&data, sizeof(data));
}

void Relight::setOccluderMask(const SampledImage& mask){
    if(!mask.isValid()){
        throw std::runtime_error("Relight::setOccluderMask: the mask has no view or no sampler");
    }
    material->setSampledImage(2, mask);
}

void Relight::setPlate(const SampledImage& plate){
    if(!hasPlate){
        throw std::runtime_error("Relight: this pass was built without a plate, so there is no footage to composite over - use the constructor that takes one");
    }
    material->setSampledImage(3, plate);
}
