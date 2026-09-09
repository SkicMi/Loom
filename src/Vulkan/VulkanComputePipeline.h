#pragma once
#include "VulkanDevice.h"
#include <string>
#include <vector>

struct ComputePipelineConfig{
    std::string shaderPath; //no default, a compute shader has no sensible fallback

    //set 0 - the dispatch's own resources. Compute has no per-frame set
    std::vector<vk::DescriptorSetLayoutBinding> descriptorBindings;

    uint32_t pushConstantSize = 0; //size only, the library never knows what is inside

    //SPECIJALIZACIJSKE KONSTANTE, po redu: mjesto u nizu je constant_id iz shadera.
    //
    //Vrijednost koju shader vidi kao KONSTANTU, ali koja se bira tek pri stvaranju pipelinea.
    //Nije isto sto i push constant: push se cita u petlji kao svaka druga vrijednost, a ovo
    //prevodilac drivera ugradi u kod - petlja se moze odmotati, grana nestati, a radna grupa
    //dobiti velicinu koja u SPIR-V-u nije zapisana kao broj.
    //
    //Zbog tog zadnjeg ovo i postoji: velicina pločice u rasterizatoru splatova je velicina
    //radne grupe, a ona se u Vulkanu ne da poslati push constantom. Jedina druga mogucnost bila
    //bi prevesti isti shader vise puta, po jednom za svaku velicinu
    std::vector<uint32_t> specializationConstants;
};

class VulkanComputePipeline{
    public:
    VulkanComputePipeline(const VulkanDevice& device, const ComputePipelineConfig& config);

    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator = (const VulkanComputePipeline&) = delete;
    VulkanComputePipeline(VulkanComputePipeline&&) = default;

    static constexpr uint32_t resourceSet = 0;

    //getters
    const vk::raii::Pipeline& getPipeline() const {return pipeline;}
    const vk::raii::PipelineLayout& getPipelineLayout() const {return pipelineLayout;}
    const vk::raii::DescriptorSetLayout& getResourceSetLayout() const {return setLayout;}
    uint32_t getPushConstantSize() const {return config.pushConstantSize;}

    private:
    const VulkanDevice& device;
    ComputePipelineConfig config;

    vk::raii::DescriptorSetLayout setLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    void createPipeline();

};
