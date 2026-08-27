#pragma once
#include "VulkanGraphicsPipeline.h"
#include "SampledImage.h"
#include "VulkanCommand.h"
#include "VulkanBuffer.h"
#include "Core/MaterialData.h"
#include "Core/ShadingRate.h"
#include <vector>

class Material{
    public:
    Material(const VulkanGraphicsPipeline& pipeline) : pipeline(&pipeline) {};
    Material(const VulkanDevice& device, 
            const VulkanCommand& command,
            const vk::raii::DescriptorPool& pool,
            const VulkanGraphicsPipeline& pipeline, 
            SampledImage image, 
            const MaterialData& data = {});
            
    Material(const VulkanDevice& device, 
        const VulkanCommand& command, 
        const vk::raii::DescriptorPool& pool,
        const VulkanGraphicsPipeline& pipeline, 
        const MaterialData& data);

    //Any payload the shader declares at set 1 binding 1. MaterialData is what Loom offers
    //by default, not what the library is able to carry
    Material(const VulkanDevice& device,
        const VulkanCommand& command,
        const vk::raii::DescriptorPool& pool,
        const VulkanGraphicsPipeline& pipeline,
        SampledImage image,
        const void* data,
        size_t size);

    //Vise slika odjednom, vezanih redom na bindinge 0, 1, 2... a payload na onaj iza njih.
    //
    //Postoji zbog G-buffera: pozicije i normale su dvije slike koje opisuju istu plohu i
    //nijedna od njih nije "tekstura ovog objekta". Materijal s jednom slikom je i dalje isti
    //materijal - ovo je nadogradnja, ne zamjena
    Material(const VulkanDevice& device,
        const VulkanCommand& command,
        const vk::raii::DescriptorPool& pool,
        const VulkanGraphicsPipeline& pipeline,
        std::vector<SampledImage> images,
        const void* data,
        size_t size);

    

    Material(const Material&) = delete;
    Material& operator = (const Material&) = delete; 
    Material(Material&&) = default;
    

    //Koliko piksela dijeli jedno sjencanje ovog materijala.
    //
    //Stoji na materijalu, a ne na pipelineu, jer je to odluka o VAZNOSTI: pod u daljini i
    //zrcalo u prvom planu mogu dijeliti pipeline i ne bi trebali dijeliti stopu. Uredaj koji
    //to ne podrzava crta jednako, samo bez ustede
    void setShadingRate(ShadingRate rate) {shadingRate = rate;}
    ShadingRate getShadingRate() const {return shadingRate;}

    //Smije li slika stope pogrubiti ovaj materijal. Critical je nacin da refleksija ostane
    //ostra i kad je daleko - jer je udaljenost dobra procjena vaznosti, ali nije savrsena
    void setImportance(ShadingImportance value) {importance = value;}
    ShadingImportance getImportance() const {return importance;}

    static vk::DescriptorSetLayoutBinding getDataLayoutBinding(uint32_t binding = 1){
        vk::DescriptorSetLayoutBinding layoutBinding;
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        return layoutBinding;
    }

    //Runtime setters, take effect the next frame this material is drawn in. Withing one frame : Last value set wins for every draw using this material
    void setData(const MaterialData& newData);
    void setData(const void* newData, size_t size);
    void setSampledImage(const SampledImage& newImage);
    void setSampledImage(size_t index, const SampledImage& newImage);
    void setBaseColor(const glm::vec4& newBaseColor);
    void setShininess(float newShininess);
    void setSpecularStrength(float newSpecularStrength);

    //getters
    const MaterialData& getData() const;
    size_t getDataSize() const {return payload.size();}
    const VulkanGraphicsPipeline& getPipeline() const{return *pipeline;}
    bool hasDescriptorSet() const {return !descriptorSets.empty();}
    const vk::raii::DescriptorSet& getDescriptorSet(size_t frame) const {return descriptorSets[frame];}
    void uploadIfDirty(size_t frame) const;


    private:
    const VulkanGraphicsPipeline* pipeline;
    const VulkanDevice* device = nullptr;
    std::vector<uint8_t> payload;
    ShadingRate shadingRate = ShadingRate::Full;
    ShadingImportance importance = ShadingImportance::Normal;
    mutable std::vector<SampledImage> images;

    //Na koji binding ide payload. Jedna slika ostavlja ga na 1, kako je oduvijek bilo; N
    //slika ga gura na N, jer bindingi ispod pripadaju slikama
    uint32_t dataBinding = 1;
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    mutable std::vector<VulkanBuffer> dataBuffers;
    mutable std::vector<uint8_t> dirty;
    mutable std::vector<uint8_t> imageDirty;

    void build(const VulkanDevice& device,
                const VulkanCommand& command,
                const vk::raii::DescriptorPool& pool,
                std::vector<SampledImage> images);

    void refreshImageIfStale() const;

    void writeImages(size_t frame) const;

};