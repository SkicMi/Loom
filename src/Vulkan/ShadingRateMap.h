#pragma once
#include "VulkanImage.h"
#include "VulkanCommand.h"
#include "Core/ShadingRate.h"

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
};
