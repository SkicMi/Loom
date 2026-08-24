#include "VulkanGraphicsPipeline.h"
#include "ShaderModule.h"
#include "Core/FrameData.h"
#include <glm/glm.hpp>

VulkanGraphicsPipeline::VulkanGraphicsPipeline(const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const PipelineConfig& config,
    vk::Format depthFormat) : depthFormat(depthFormat), device(device), swapchain(swapchain), config(config){
        createPipeline();
}

void VulkanGraphicsPipeline::createPipeline(){

    //depth guard
    if(config.depthTestEnable && depthFormat == vk::Format::eUndefined){
        throw std::runtime_error("Pipeline: depth test enabled but depth buffer doesnt exist (LoomConfig::enableDepth)");
    }

    //A colour attachment with nobody to write it is a pipeline that draws nothing, and an
    //empty fragment path with a colour attachment is the same mistake the other way round
    if(!config.enableColor && !config.fragShaderPath.empty()){
        throw std::runtime_error("Pipeline: enableColor is off but a fragment shader was given - a depth only pass has nothing for it to return");
    }
    if(config.enableColor && config.fragShaderPath.empty()){
        throw std::runtime_error("Pipeline: no fragment shader, but there is a colour attachment to write (PipelineConfig::enableColor)");
    }

    const bool hasFragment = !config.fragShaderPath.empty();

    vk::raii::ShaderModule vertShaderModule = loadShaderModule(device, config.vertShaderPath);
    vk::raii::ShaderModule fragShaderModule = nullptr;

    //Shader stage creation info
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = *vertShaderModule;
    vertShaderStageInfo.pName = "main"; //entry point of shader

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    shaderStages.push_back(vertShaderStageInfo);

    if(hasFragment){
        fragShaderModule = loadShaderModule(device, config.fragShaderPath);

        vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
        fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
        fragShaderStageInfo.module = *fragShaderModule;
        fragShaderStageInfo.pName = "main"; //entry point of shader
        shaderStages.push_back(fragShaderStageInfo);
    }

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.setVertexBindingDescriptions(config.vertexBindings);
    vertexInputInfo.setVertexAttributeDescriptions(config.vertexAttributes);

    //Input Assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
    inputAssembly.topology = config.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    //Viewport and Scissor
    vk::PipelineViewportStateCreateInfo viewportState;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    //Rasterizer
    vk::PipelineRasterizationStateCreateInfo rasterizer;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.depthBiasEnable = config.depthBiasEnable;
    rasterizer.depthBiasConstantFactor = config.depthBiasConstant;
    rasterizer.depthBiasSlopeFactor = config.depthBiasSlope;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.lineWidth = 1.0f;

    //Multisampling - off
    vk::PipelineMultisampleStateCreateInfo multisampling;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    //Depth and stencil
    vk::PipelineDepthStencilStateCreateInfo depthStencil;
    depthStencil.depthTestEnable = config.depthTestEnable;
    depthStencil.depthWriteEnable = config.depthWriteEnable;
    depthStencil.depthCompareOp = config.depthCompare;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;


    //Color blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
        vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB |
        vk::ColorComponentFlagBits::eA;

    switch(config.blendMode){
        case BlendMode::None:
            colorBlendAttachment.blendEnable = VK_FALSE;
            break;
        case BlendMode::Alpha:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
            break;
        case BlendMode::Additive:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
            break;

    }

    vk::PipelineColorBlendStateCreateInfo colorBlending;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = config.enableColor ? 1 : 0;
    colorBlending.pAttachments = config.enableColor ? &colorBlendAttachment : nullptr;

    //Dynamic state
    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();


    vk::PushConstantRange pushRange;
    pushRange.stageFlags = config.pushConstantStages;
    pushRange.offset = 0;
    pushRange.size = config.pushConstantSize;

    //Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    if(config.pushConstantSize > 0){
        pipelineLayoutInfo.setPushConstantRanges(pushRange);
    }

    std::vector<vk::DescriptorSetLayoutBinding> frameBindings;
    if(config.useFrameData){
        vk::DescriptorSetLayoutBinding frameBinding;
        frameBinding.binding = 0;
        frameBinding.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        frameBinding.descriptorCount = 1;
        frameBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        frameBindings.push_back(frameBinding);

        vk::DescriptorSetLayoutBinding lightBinding;
        lightBinding.binding = 1;
        lightBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        lightBinding.descriptorCount = 1;
        lightBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        frameBindings.push_back(lightBinding);

        //The shadow map, as a comparison sampler. It sits on set 0 because it belongs to the
        //frame and its lights, not to any one material - the same map shadows every object
        //drawn in the pass. The renderer always writes this binding, with a one pixel
        //placeholder when no shadow map is set, so the set is never partially bound
        vk::DescriptorSetLayoutBinding shadowBinding;
        shadowBinding.binding = 2;
        shadowBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        shadowBinding.descriptorCount = 1;
        shadowBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        frameBindings.push_back(shadowBinding);
    }

    vk::DescriptorSetLayoutCreateInfo frameLayoutInfo;
    frameLayoutInfo.setBindings(frameBindings);
    setLayouts.emplace_back(device.getDevice(), frameLayoutInfo);

    if(!config.descriptorBindings.empty()){
        vk::DescriptorSetLayoutCreateInfo materialLayoutInfo;
        materialLayoutInfo.setBindings(config.descriptorBindings);
        setLayouts.emplace_back(device.getDevice(), materialLayoutInfo);
    }

    std::vector<vk::DescriptorSetLayout> rawLayouts;
    rawLayouts.reserve(setLayouts.size());
    for(const auto& layout : setLayouts){
        rawLayouts.push_back(*layout);
    }

    pipelineLayoutInfo.setSetLayouts(rawLayouts);

    pipelineLayout = vk::raii::PipelineLayout(device.getDevice(),pipelineLayoutInfo);

    //Dynamic rendering - rendering info
    colorFormat = config.colorFormat == vk::Format::eUndefined
                ? swapchain.getSurfaceFormat().format
                : config.colorFormat;

    vk::PipelineRenderingCreateInfo renderingInfo;
    renderingInfo.colorAttachmentCount = config.enableColor ? 1 : 0;
    renderingInfo.pColorAttachmentFormats = config.enableColor ? &colorFormat : nullptr;
    renderingInfo.depthAttachmentFormat = depthFormat;

    //Assemble everything into the graphics pipeline create info
    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.setStages(shaderStages);
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = *pipelineLayout;
    pipelineInfo.renderPass = nullptr; //not using render pass, using dynamic rendering instead

    pipeline = vk::raii::Pipeline(device.getDevice(), nullptr, pipelineInfo);


}