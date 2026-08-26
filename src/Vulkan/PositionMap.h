#pragma once
#include "VulkanImage.h"
#include "VulkanCommand.h"
#include "VulkanComputePipeline.h"
#include "ComputeMaterial.h"
#include "RenderTarget.h"
#include "SampledImage.h"
#include "Core/CameraIntrinsics.h"
#include "ImageData.h"
#include <memory>

//Ono sto compute shader treba znati. Slaze se s Push strukturom u unproject.slang, i ako se
//to dvoje raziđe, prevodi se i tiho cita krive bajtove
struct UnprojectPush{
    uint32_t size[2];
    float fx;
    float fy;
    float cx;
    float cy;
    float nearPlane;
    float farPlane;
};

struct PositionMapConfig{
    //Gdje dispatch ostavlja sliku pozicija. eShaderReadOnlyOptimal da je sljedeci prolaz
    //semplira, eTransferSrcOptimal da je se procita natrag na CPU
    vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
};

//Slika tocaka u prostoru, napravljena od slike dubine.
//
//Jedan teksel po pikselu, cetiri float-a: xyz je tocka u view prostoru, w je 1 gdje ploha
//postoji i 0 gdje je pozadina. Ta nula nije ukras - svjetlo koje obasja nebo je prvo sto se
//vidi da ne valja.
//
//Puni float po kanalu, ne pola. Pozicije se poslije usporeduju s poznatim brojevima, a
//polovicna preciznost na deset metara promasi vise nego cijela ova racunica smije.
class PositionMap{
    public:
    PositionMap(const VulkanDevice& device, vk::Extent2D extent, const PositionMapConfig& config = {});

    PositionMap(const PositionMap&) = delete;
    PositionMap& operator=(const PositionMap&) = delete;

    //Odakle se cita dubina. Semplirljiva slika i njena velicina - ne nuzno RenderTarget, jer
    //prozorska dubina nije meta, a tuda dubina iz filea nije ni jedno ni drugo.
    //
    //Sampler NE smije biti comparison: ovdje se cita sama vrijednost dubine, a ne odgovor na
    //usporedbu. Meta s depthCompare = true daje upravo takav sampler
    void setDepthSource(const vk::raii::DescriptorPool& pool,
                        const SampledImage& depth, vk::Extent2D depthExtent);

    //Isto, kad dubina jest meta
    void setDepthSource(const vk::raii::DescriptorPool& pool, const RenderTarget& depthTarget);

    //Kroz sto se odprojicira. Bez ovoga dispatch nema kroz sto racunati i baca
    void setIntrinsics(const CameraIntrinsics& value) {intrinsics = value;}
    const CameraIntrinsics& getIntrinsics() const {return intrinsics;}

    //Sve sto treba za dispatch:
    //    renderer.dispatch(map.getComputeMaterial(), map.groupsX(), map.groupsY(),
    //                      &push, sizeof(push));
    //gdje je push = map.makePush()
    UnprojectPush makePush() const;
    const ComputeMaterial& getComputeMaterial() const;

    //8x8 niti po grupi, zaokruzeno gore
    uint32_t groupsX() const {return (extent.width + 7) / 8;}
    uint32_t groupsY() const {return (extent.height + 7) / 8;}

    //Tocke natrag na CPU. Trazi da je config.finalLayout eTransferSrcOptimal, jer inace
    //slika nije tamo gdje kopiranje ocekuje da jest
    ImageData readPixels(const VulkanCommand& command) const;

    //getters
    const VulkanImage& getImage() const {return image;}
    vk::Extent2D getExtent() const {return extent;}
    SampledImage getSampled() const;
    bool hasDepthSource() const {return depthExtent.width > 0;}

    private:
    const VulkanDevice& device;
    vk::Extent2D extent;
    PositionMapConfig config;
    VulkanImage image;

    CameraIntrinsics intrinsics;
    vk::Extent2D depthExtent{0,0};

    vk::raii::Sampler sampler = nullptr;

    std::unique_ptr<VulkanComputePipeline> computePipeline;
    std::unique_ptr<ComputeMaterial> computeMaterial;
};
