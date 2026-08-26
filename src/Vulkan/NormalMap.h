#pragma once
#include "VulkanImage.h"
#include "VulkanCommand.h"
#include "VulkanComputePipeline.h"
#include "ComputeMaterial.h"
#include "PositionMap.h"
#include "SampledImage.h"
#include "ImageData.h"
#include <memory>

//Slaze se s Push strukturom u normals.slang
struct NormalPush{
    uint32_t size[2];
};

struct NormalMapConfig{
    //Gdje dispatch ostavlja sliku. eShaderReadOnlyOptimal da je relight prolaz semplira,
    //eTransferSrcOptimal da je se procita natrag
    vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
};

//Normale plohe, izvedene iz slike tocaka.
//
//Zaseban objekt, a ne jos jedan kanal PositionMape, jer normale nece uvijek biti izvedene:
//model za procjenu normala daje ih tocnije nego bilo koja razlika susjednih tocaka, i tada
//ova slika dolazi iz filea umjesto iz dispatcha. Sve sto normale koristi gleda u istu sliku
//i ne zna koja je od te dvije stvari.
//
//Puni float po kanalu kao i pozicije - jedinicni vektor bi stao i u pola toga, i to je prvo
//sto treba stisnuti ako memorija na 4K materijalu postane tijesna
class NormalMap{
    public:
    NormalMap(const VulkanDevice& device, vk::Extent2D extent, const NormalMapConfig& config = {});

    NormalMap(const NormalMap&) = delete;
    NormalMap& operator=(const NormalMap&) = delete;

    //Iz cega se racunaju. Tocke moraju biti u eShaderReadOnlyOptimal, a tamo ih ostavlja
    //dispatch koji ih je napisao
    void setPositionSource(const vk::raii::DescriptorPool& pool,
                           const SampledImage& positions, vk::Extent2D positionExtent);

    void setPositionSource(const vk::raii::DescriptorPool& pool, const PositionMap& positions);

    NormalPush makePush() const;
    const ComputeMaterial& getComputeMaterial() const;

    uint32_t groupsX() const {return (extent.width + 7) / 8;}
    uint32_t groupsY() const {return (extent.height + 7) / 8;}

    ImageData readPixels(const VulkanCommand& command) const;

    //getters
    const VulkanImage& getImage() const {return image;}
    vk::Extent2D getExtent() const {return extent;}
    SampledImage getSampled() const;
    bool hasPositionSource() const {return positionExtent.width > 0;}

    private:
    const VulkanDevice& device;
    vk::Extent2D extent;
    NormalMapConfig config;
    VulkanImage image;

    vk::Extent2D positionExtent{0,0};
    vk::raii::Sampler sampler = nullptr;

    std::unique_ptr<VulkanComputePipeline> computePipeline;
    std::unique_ptr<ComputeMaterial> computeMaterial;
};
