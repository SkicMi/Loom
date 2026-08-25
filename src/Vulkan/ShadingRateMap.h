#pragma once
#include "VulkanImage.h"
#include "VulkanCommand.h"
#include "Core/ShadingRate.h"
#include "VulkanComputePipeline.h"
#include "ComputeMaterial.h"
#include "RenderTarget.h"
#include <memory>

//Od koje udaljenosti stopa pada. Dva praga umjesto formule, jer se prag da namjestiti okom
//a formula ne - i jer stopa ionako ima tri vrijednosti, ne kontinuum
struct ShadingRateDistances{
    float quarter = 25.0f;     //dalje od ovoga se sjenca 2x2
    float sixteenth = 70.0f;   //dalje od ovoga 4x4
};

//Ono sto compute shader treba znati. Renderer ga sastavi iz kamere i karte
struct ShadingRatePush{
    uint32_t rateExtent[2];
    uint32_t texelSize[2];
    float depthSize[2];
    float nearPlane;
    float farPlane;
    float quarterDistance;
    float sixteenthDistance;
};

//Stopa sjencanja zadana SLIKOM, a ne po draw pozivu.
//
//Razlika je u tome cije je svojstvo. Materijal zna koliko je vazan - to je svojstvo objekta.
//Udaljenost je svojstvo PIKSELA, i nijedan draw poziv je ne moze izraziti jer se unutar
//jednog objekta mijenja. Zato za nju treba slika: jedan teksel po bloku piksela, svaki sa
//svojom stopom.
//
//Rezolucija nije nas izbor. Drajver zadaje velicinu teksela (na Turingu 16x16, min = max),
//pa slika za 1280x720 ima 80x45 teksela - dovoljno malo da je punjenje computeom besplatno.
class ShadingRateMap{
    public:
    //renderExtent je velicina slike koja se crta, ne slike stope. Ova se izracuna iz nje
    ShadingRateMap(const VulkanDevice& device, vk::Extent2D renderExtent);

    ShadingRateMap(const ShadingRateMap&) = delete;
    ShadingRateMap& operator=(const ShadingRateMap&) = delete;

    //Jedna stopa za cijelu sliku. Grubo, ali je to sto dokazuje da prilog uopce djeluje
    void fill(const VulkanCommand& command, ShadingRate rate);

    //Jedan bajt po tekselu, vec zapakiran. Toliko ih mora biti koliko ih slika ima
    void upload(const VulkanCommand& command, const std::vector<uint8_t>& packed);

    //Odakle se cita dubina. Meta mora cuvati dubinu i NE smije imati comparison sampler -
    //ovdje se cita prava vrijednost, ne odgovor na usporedbu
    void setDepthSource(const vk::raii::DescriptorPool& pool, const RenderTarget& depthTarget);

    void setDistances(const ShadingRateDistances& value) {distances = value;}
    const ShadingRateDistances& getDistances() const {return distances;}

    bool hasDepthSource() const {return depthSource != nullptr;}
    const ComputeMaterial& getComputeMaterial() const;
    const RenderTarget& getDepthSource() const;

    //8x8 niti po grupi, zaokruzeno gore
    uint32_t groupsX() const {return (extent.width + 7) / 8;}
    uint32_t groupsY() const {return (extent.height + 7) / 8;}

    //Kako Vulkan pakira stopu u jedan bajt: log2 sirine u gornja dva bita, log2 visine u
    //donja. 1x1 je 0, 2x2 je 5, 4x4 je 10
    static uint8_t pack(ShadingRate rate);

    //getters
    const VulkanImage& getImage() const {return image;}
    vk::Extent2D getExtent() const {return extent;}
    vk::Extent2D getTexelSize() const {return texelSize;}
    size_t texelCount() const {return size_t(extent.width) * extent.height;}

    private:
    const VulkanDevice& device;
    vk::Extent2D texelSize;
    vk::Extent2D extent;
    VulkanImage image;

    ShadingRateDistances distances;
    const RenderTarget* depthSource = nullptr;

    //Sagradeni tek kad se veze izvor dubine: karta koja se puni rukom ne treba pipeline
    std::unique_ptr<VulkanComputePipeline> computePipeline;
    std::unique_ptr<ComputeMaterial> computeMaterial;
};
