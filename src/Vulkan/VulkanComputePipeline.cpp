#include "VulkanComputePipeline.h"
#include "ShaderModule.h"

VulkanComputePipeline::VulkanComputePipeline(const VulkanDevice& device, const ComputePipelineConfig& config) :
    device(device), config(config){
        createPipeline();
}

void VulkanComputePipeline::createPipeline(){

    if(config.shaderPath.empty()){
        throw std::runtime_error("ComputePipeline: no shader path");
    }

    //Slang names the entry point computeMain, but writes it into SPIR-V as "main"
    vk::raii::ShaderModule shaderModule = loadShaderModule(device, config.shaderPath);

    vk::PipelineShaderStageCreateInfo stageInfo;
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = *shaderModule;
    stageInfo.pName = "main";

    vk::DescriptorSetLayoutCreateInfo setLayoutInfo;
    setLayoutInfo.setBindings(config.descriptorBindings);
    setLayout = vk::raii::DescriptorSetLayout(device.getDevice(), setLayoutInfo);

    //named, not a temporary: setSetLayouts only stores a pointer to it
    vk::DescriptorSetLayout rawLayout = *setLayout;

    vk::PipelineLayoutCreateInfo layoutInfo;
    layoutInfo.setSetLayouts(rawLayout);

    vk::PushConstantRange pushRange;
    if(config.pushConstantSize > 0){
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = config.pushConstantSize;
        layoutInfo.setPushConstantRanges(pushRange);
    }

    pipelineLayout = vk::raii::PipelineLayout(device.getDevice(), layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = *pipelineLayout;

    pipeline = vk::raii::Pipeline(device.getDevice(), nullptr, pipelineInfo);
}
