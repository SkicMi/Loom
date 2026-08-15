#pragma once

#include "vulkan/vulkan_raii.hpp"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "Vertex.h"

enum class BlendMode{
    None,
    Alpha,
    Additive
};

struct PipelineConfig{
    //Vertex input(empty = hardcoded in shader)
    std::vector<vk::VertexInputBindingDescription> vertexBindings =  {Vertex::getBindingDescription()};
    std::vector<vk::VertexInputAttributeDescription> vertexAttributes = Vertex::getAttributeDescriptions();
    std::vector<vk::DescriptorSetLayoutBinding> descriptorBindings;

    //Different for every pipeline
    std::string vertShaderPath = std::string(LOOM_SHADER_DIR) + "/triangle.vert.spv";
    std::string fragShaderPath = std::string(LOOM_SHADER_DIR) + "/triangle.frag.spv";

    //Geometry interpretation
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;

    //Face culling
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
    vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;

    //Color blending
    BlendMode blendMode = BlendMode::None;

    //Depth
    bool depthTestEnable = false;
    bool depthWriteEnable = false;
    vk::CompareOp depthCompare = vk::CompareOp::eLess;


    

};


class VulkanGraphicsPipeline{
    public:
    VulkanGraphicsPipeline(const VulkanDevice& device,
        const VulkanSwapchain& swapchain,
        const PipelineConfig& config = {},
        vk::Format depthFormat = vk::Format::eUndefined);


    vk::Format depthFormat = vk::Format::eUndefined;


    //getters
    const vk::raii::Pipeline& getPipeline() const {return pipeline;}
    const vk::raii::PipelineLayout& getPipelineLayout() const {return pipelineLayout;}
    const vk::raii::DescriptorSetLayout& getDescriptorSetLayout() const {return descriptorSetLayout;}
    bool hasDescriptors() const {return !config.descriptorBindings.empty();}
   
    

    private:
    const VulkanDevice& device;
    const VulkanSwapchain& swapchain;

    PipelineConfig config;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;   
    vk::raii::Pipeline pipeline = nullptr;

    static std::vector<char> readFile(const std::string& path);
    vk::raii::ShaderModule createShaderModule(const std::vector<char>& code);
    void createPipeline();


};