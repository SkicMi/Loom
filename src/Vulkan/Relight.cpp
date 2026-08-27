#include "Relight.h"
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
                 const RelightConfig& config){

    if(positions.getExtent() != normals.getExtent()){
        throw std::runtime_error("Relight: the position and normal maps are different sizes, so they do not describe the same surface");
    }

    data.baseColor = config.surface.baseColor;
    data.shininess = config.surface.shininess;
    data.specularStrength = config.surface.specularStrength;

    //Fullscreen: nema vrhova, nema atributa, nema dubine. Trokut koji pokriva ekran dolazi
    //iz SV_VertexID, kao i u svakom drugom fullscreen prolazu
    PipelineConfig pipelineConfig;
    pipelineConfig.vertexBindings.clear();
    pipelineConfig.vertexAttributes.clear();
    pipelineConfig.descriptorBindings = {
        imageBinding(0),                        //pozicije
        imageBinding(1),                        //normale
        Material::getDataLayoutBinding(2)       //payload iza njih
    };
    pipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/relight.vert.spv";
    pipelineConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/relight.frag.spv";
    pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    pipelineConfig.colorFormat = config.colorFormat;
    pipelineConfig.depthTestEnable = false;
    pipelineConfig.depthWriteEnable = false;

    pipeline.emplace(device, pipelineConfig, config.colorFormat, config.depthFormat);

    material.emplace(device, command, pool, *pipeline,
                     std::vector<SampledImage>{positions.getSampled(), normals.getSampled()},
                     &data, sizeof(data));
}

void Relight::setCamera(const Camera& camera){
    //Pogled je kruta transformacija, pa je njegov obrat samo obrat - bez ikakve skale koju
    //bi trebalo posebno paziti
    data.inverseView = glm::inverse(camera.getView());
    material->setData(&data, sizeof(data));
}

void Relight::setSurface(const MaterialData& surface){
    data.baseColor = surface.baseColor;
    data.shininess = surface.shininess;
    data.specularStrength = surface.specularStrength;
    material->setData(&data, sizeof(data));
}
