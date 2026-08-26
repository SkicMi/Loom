#pragma once

#include "vulkan/vulkan_raii.hpp"
#include "VulkanDevice.h"
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
    //Smije li ovaj pipeline crtati u prolaz kojemu je prilozena slika stope. Vulkan to trazi
    //kao zastavicu pri STVARANJU, a slika stope se veze tek kasnije - pa Loom je postavlja
    //svima na uredaju koji to podrzava. Iskljuci za pipeline za koji sigurno znas da nikad
    //nece vidjeti takav prolaz
    bool allowShadingRateAttachment = true;

    bool depthBiasEnable = false;
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;


    

};


//Depth prepass koji se s ovim pipelineom slaze DO ZADNJEG BITA.
//
//To nije sitnica nego uvjet: glavni prolaz smije testirati eEqual samo ako je dubinu koju
//usporeduje izracunao isti vertex shader s istim ulazima. Dva razlicita shadera koja "rade
//isto" daju brojeve koji se razlikuju u zadnjem bitu, i eEqual tada odbije svaki fragment -
//slika ostane prazna, a nista u kodu ne izgleda krivo.
//
//Zato se prepass IZVODI iz configa za boju umjesto da se pise pokraj njega: ono sto se
//izvede ne moze se razici. Mijenja se samo ono sto prepass ne treba - fragment stage i boja.
inline PipelineConfig makeDepthPrepassConfig(PipelineConfig config){
    //Nema sto vratiti kad nema boje, pa nema ni fragment stagea. Pola posla koje se ne radi
    config.fragShaderPath.clear();
    config.enableColor = false;

    //Vertex shader, atributi, push constants i set 0 ostaju netaknuti. Oni su ono zbog cega
    //ce se dubine poklopiti
    config.depthTestEnable = true;
    config.depthWriteEnable = true;
    config.depthCompare = vk::CompareOp::eLess;

    return config;
}

class VulkanGraphicsPipeline{
    public:
    //A pipeline needs two formats, not a swapchain. defaultColorFormat is only the fallback
    //for a config that left colorFormat undefined - a pipeline drawing to the window wants
    //the window's format, and one drawing to a target names its own. Taking the format
    //rather than the swapchain is what lets a pipeline exist in a process with no window
    VulkanGraphicsPipeline(const VulkanDevice& device,
        const PipelineConfig& config = {},
        vk::Format defaultColorFormat = vk::Format::eUndefined,
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

    //Config kojim je sagraden. Postoji zato da se depth prepass moze IZVESTI iz cjevovoda
    //kojim se stvarno crta boja, a ne prepisati pokraj njega - vidi makeDepthPrepassConfig
    const PipelineConfig& getConfig() const {return config;}
    uint32_t getPushConstantSize() const {return config.pushConstantSize;}
    vk::Format getColorFormat() const {return colorFormat;}
   
    

    private:
    

    const VulkanDevice& device;
    vk::Format defaultColorFormat = vk::Format::eUndefined;

    PipelineConfig config;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    std::vector<vk::raii::DescriptorSetLayout> setLayouts;
    vk::raii::Pipeline pipeline = nullptr;

    

    void createPipeline();


};