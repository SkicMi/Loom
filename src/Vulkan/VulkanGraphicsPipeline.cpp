#include "VulkanGraphicsPipeline.h"
#include "Core/FrameData.h"
#include <fstream>
#include <glm/glm.hpp>

VulkanGraphicsPipeline::VulkanGraphicsPipeline(const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const PipelineConfig& config,
    vk::Format depthFormat) : device(device), swapchain(swapchain), config(config), depthFormat(depthFormat){
        createPipeline();
}

std::vector<char> VulkanGraphicsPipeline::readFile(const std::string& path) {
    //ate - at the end - opens file with position on end, binary stops that bytes from being interpreted as text
    std::ifstream file(path, std::ios::ate | std::ios::binary); 

    if(!file.is_open()){
        throw std::runtime_error("Failed to open file: " + path);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(),fileSize);

    return buffer;
}

vk::raii::ShaderModule VulkanGraphicsPipeline::createShaderModule(const std::vector<char>& code){
    
    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = code.size(); //code size in bytes
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data()); //Vulkan expects array of uint32_t, so reinterpret cast is needed to convert from char to uint32_t

    return vk::raii::ShaderModule(device.getDevice(),createInfo);
}

void VulkanGraphicsPipeline::createPipeline(){

    //depth guard
    if(config.depthTestEnable && depthFormat == vk::Format::eUndefined){
        throw std::runtime_error("Pipeline: depth test enabled but depth buffer doesnt exist (LoomConfig::enableDepth)");
    }

    auto vertShaderCode = readFile(config.vertShaderPath);
    auto fragShaderCode = readFile(config.fragShaderPath);

    vk::raii::ShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    vk::raii::ShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    //Shader stage creation info
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = *vertShaderModule;
    vertShaderStageInfo.pName = "main"; //entry point of shader

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = *fragShaderModule;
    fragShaderStageInfo.pName = "main"; //entry point of shader

    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

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
    rasterizer.depthBiasEnable = VK_FALSE;
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
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    //Dynamic state
    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();


    vk::PushConstantRange pushRange;
    pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushRange.offset = 0;
    pushRange.size = sizeof(ObjectData);


    //Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setPushConstantRanges(pushRange);

    std::vector<vk::DescriptorSetLayoutBinding> frameBindings;
    if(config.useFrameData){
        vk::DescriptorSetLayoutBinding frameBinding;
        frameBinding.binding = 0;
        frameBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        frameBinding.descriptorCount = 1;
        frameBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        frameBindings.push_back(frameBinding);

        vk::DescriptorSetLayoutBinding lightBinding;
        lightBinding.binding = 1;
        lightBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        lightBinding.descriptorCount = 1;
        lightBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        frameBindings.push_back(lightBinding);
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
    vk::Format colorFormat = swapchain.getSurfaceFormat().format;

    vk::PipelineRenderingCreateInfo renderingInfo;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;

    //Assemble everything into the graphics pipeline create info
    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
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