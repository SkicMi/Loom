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
    //Set 1 - material bindings ( texture ) . Set 0 is owned by Loom
    std::vector<vk::VertexInputBindingDescription> vertexBindings =  {Vertex::getBindingDescription()};
    std::vector<vk::VertexInputAttributeDescription> vertexAttributes = Vertex::getAttributeDescriptions();
    std::vector<vk::DescriptorSetLayoutBinding> descriptorBindings;

    //set 0 - per-frame data provided by Loom(view and projection)
    bool useFrameData = true;


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
    static constexpr uint32_t frameSet = 0;
    static constexpr uint32_t materialSet = 1;


    //getters
    const vk::raii::Pipeline& getPipeline() const {return pipeline;}
    const vk::raii::PipelineLayout& getPipelineLayout() const {return pipelineLayout;}
    const vk::raii::DescriptorSetLayout& getMaterialSetLayout() const {return setLayouts[materialSet];}
    const vk::raii::DescriptorSetLayout& getFrameSetLayout() const {return setLayouts[frameSet];}
    bool hasDescriptors() const {return !config.descriptorBindings.empty();}
   
    

    private:
    

    const VulkanDevice& device;
    const VulkanSwapchain& swapchain;

    PipelineConfig config;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;   
    std::vector<vk::raii::DescriptorSetLayout> setLayouts;
    vk::raii::Pipeline pipeline = nullptr;

    

    static std::vector<char> readFile(const std::string& path);
    vk::raii::ShaderModule createShaderModule(const std::vector<char>& code);
    void createPipeline();


};