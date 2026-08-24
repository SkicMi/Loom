#pragma once

#include "vulkan/vulkan_raii.hpp"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "Vertex.h"
#include <glm/glm.hpp>

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

    //Colour attachment format. eUndefined means "whatever the swapchain uses", which is
    //what a pipeline drawing to the window wants. A float target for accumulation, or a
    //unorm target a compute pass will read, sets it explicitly
    vk::Format colorFormat = vk::Format::eUndefined;

    //Push constant range. Loom pushes ObjectData in draw, so a pipeline that draws meshes
    //needs at least that much. A fullscreen pipeline can set 0 and carry no range at all
    uint32_t pushConstantSize = sizeof(glm::mat4) * 2;
    vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlagBits::eVertex;

    //A pipeline that writes depth and nothing else - the shadow map pass. With no colour
    //attachment there is nothing for a fragment shader to return, so fragShaderPath may be
    //left empty and the pipeline is built with the vertex stage alone
    bool enableColor = true;

    //Depth
    bool depthTestEnable = false;
    bool depthWriteEnable = false;
    vk::CompareOp depthCompare = vk::CompareOp::eLess;

    //Depth bias, the cure for shadow acne. A surface lit at a grazing angle covers a whole
    //range of depths inside one shadow map texel, so half of it compares as farther than
    //itself and shadows itself. The constant term pushes every fragment back by a fixed
    //number of depth units; the slope term pushes steep surfaces back further, because they
    //are the ones with the most depth inside a texel.
    //Both are paid for in Peter Panning: push too far and the shadow separates from the
    //object casting it
    bool depthBiasEnable = false;
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;


    

};


class VulkanGraphicsPipeline{
    public:
    VulkanGraphicsPipeline(const VulkanDevice& device,
        const VulkanSwapchain& swapchain,
        const PipelineConfig& config = {},
        vk::Format depthFormat = vk::Format::eUndefined);


    vk::Format depthFormat = vk::Format::eUndefined;
    vk::Format colorFormat = vk::Format::eUndefined;
    static constexpr uint32_t frameSet = 0;
    static constexpr uint32_t materialSet = 1;


    //getters
    const vk::raii::Pipeline& getPipeline() const {return pipeline;}
    const vk::raii::PipelineLayout& getPipelineLayout() const {return pipelineLayout;}
    const vk::raii::DescriptorSetLayout& getMaterialSetLayout() const {return setLayouts[materialSet];}
    const vk::raii::DescriptorSetLayout& getFrameSetLayout() const {return setLayouts[frameSet];}
    bool hasDescriptors() const {return !config.descriptorBindings.empty();}
    uint32_t getPushConstantSize() const {return config.pushConstantSize;}
    vk::Format getColorFormat() const {return colorFormat;}
   
    

    private:
    

    const VulkanDevice& device;
    const VulkanSwapchain& swapchain;

    PipelineConfig config;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    std::vector<vk::raii::DescriptorSetLayout> setLayouts;
    vk::raii::Pipeline pipeline = nullptr;

    

    void createPipeline();


};