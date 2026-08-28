#pragma once
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanCommand.h"
#include "SampledImage.h"
#include <vector>

struct StreamingTextureConfig{
    vk::Format format = vk::Format::eR8G8B8A8Srgb;
    vk::Filter filter = vk::Filter::eLinear;
    vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eClampToEdge;

    //Koliko slika u prstenu. 0 znaci "koliko frameova ima u letu", sto je i jedini broj koji
    //je ovdje tocan: manje bi znacilo pisati u sliku koju karta jos cita, vise bi bila
    //memorija koja nikad ne dode na red
    uint32_t slots = 0;
};

//Tekstura koja se mijenja svaki frame.
//
//Obicna Texture se napise jednom pri stvaranju i vise se ne dira. Snimka nije takva: svaki
//frame donosi nove piksele, a slika u koju bi se pisali je ista ona iz koje karta jos crta
//prethodni frame. Zato prsten: pise se u slot koji vise nitko ne gleda.
//
//I zato prijenos koji se ne ceka. Jednokratni upload smije zaustaviti red - poslije njega se
//ionako odmah crta. Snimka koja bi to radila svaki frame zaustavljala bi crtanje radi
//kopiranja koje se moglo obaviti usput.
//
//ZAMKA: update se smije zvati NAJVISE JEDNOM po frameu, i tek nakon beginFrame. Prsten se
//oslanja na to da je renderer vec pricekao frame od prije slots frameova - dva poziva u
//istom frameu pojedu dva slota i jedan od njih je jos u letu.
class StreamingTexture{
    public:
    StreamingTexture(const VulkanDevice& device,
                     const VulkanCommand& command,
                     vk::Extent2D extent,
                     const StreamingTextureConfig& config = {});

    StreamingTexture(const StreamingTexture&) = delete;
    StreamingTexture& operator=(const StreamingTexture&) = delete;

    //Sljedeci frame snimke. Cetiri bajta po pikselu, gusto slozeno, prvi red prvi.
    //Ne alocira nista - ni sliku, ni medjuspremnik, ni bafer naredbi
    void update(const void* pixels, size_t size);

    //Slot u koji je zadnji update pisao. Ovo je ono sto se veze prije crtanja, i mijenja se
    //svaki frame - materijal to mora saznati iznova
    SampledImage getSampled() const;

    //getters
    vk::Extent2D getExtent() const {return extent;}
    uint32_t getSlotCount() const {return uint32_t(slots.size());}
    uint32_t getCurrentSlot() const {return current;}
    uint64_t getUpdateCount() const {return updates;}
    vk::Format getFormat() const {return config.format;}

    private:
    const VulkanDevice& device;
    const VulkanCommand& command;
    vk::Extent2D extent;
    StreamingTextureConfig config;

    struct Slot{
        VulkanImage image;
        VulkanBuffer staging;
        vk::raii::CommandBuffer transfer = nullptr;
        vk::raii::Fence done = nullptr;

        //Prva uporaba nema sto cekati; ograda se stvara vec signalizirana pa se ovo ne mora
        //nigdje posebno provjeravati
        Slot(const VulkanDevice& device, vk::Extent2D extent, const ImageConfig& imageConfig,
             vk::DeviceSize bytes, const VulkanCommand& command);
    };

    std::vector<Slot> slots;
    vk::raii::Sampler sampler = nullptr;

    uint32_t current = 0;
    uint64_t updates = 0;
    vk::DeviceSize bytesPerFrame = 0;
};
