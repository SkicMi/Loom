#pragma once
#include "VulkanBuffer.h"
#include "VulkanComputePipeline.h"
#include "ComputeMaterial.h"

class VulkanRenderer;

//=============================================================================================
// Ekskluzivni prefiksni zbroj nad poljem uinta, na mjestu.
//
// Sto to znaci: polje [3, 1, 4, 1] postane [0, 3, 4, 8], a ukupan zbroj (9) zavrsi u zasebnom
// bufferu. Rezultat na mjestu i je "koliko ih ide prije mene" - a to je tocno ono sto binning
// splatova treba: svaki splat mora znati gdje u zajednickom polju parova pocinje njegov dio.
//
// DVORAZINSKI, ne jedna dretva kroz cijelo polje. To je nauceno mjerenjem na radix scanu, gdje
// je jednodretveni zbroj ispao 18 od 39 ms - jedna dretva ceka memoriju na svakom koraku, jer
// svaki sljedeci zbroj ovisi o prethodnom. Ovdje su tri prolaza: blok zbroji sam sebe, zbroje
// se zbrojevi blokova, pa se svakom bloku doda sto ide prije njega.
//
// Kao i RadixSort: sve se veze jednom, pri gradnji, pa zbrajanje po kadru ne alocira nista.
//=============================================================================================
class PrefixSum{
    public:
    //Polje je tudje i ostaje tudje - mijenja mu se sadrzaj, ne vlasnistvo
    PrefixSum(const VulkanDevice& device,
              const vk::raii::DescriptorPool& pool,
              VulkanBuffer& values,
              uint32_t capacity);

    PrefixSum(const PrefixSum&) = delete;
    PrefixSum& operator = (const PrefixSum&) = delete;

    //Mora se zvati unutar kadra. Poslije njega polje nosi pomake, a getTotal() ukupan zbroj
    void scan(VulkanRenderer& renderer, uint32_t count);

    //Jedan uint: koliko ih je ukupno bilo. Cita se s kartice kao i svaki drugi buffer, ali
    //obicno ga ne treba citati procesor - sljedeci dispatch ga uzima izravno
    const VulkanBuffer& getTotal() const {return total;}

    static uint32_t blocksFor(uint32_t count);

    static constexpr uint32_t threadsPerBlock = 256;
    static constexpr uint32_t elementsPerThread = 4;
    static constexpr uint32_t elementsPerBlock = threadsPerBlock * elementsPerThread;

    private:
    struct Params{
        uint32_t count = 0;
        uint32_t blockCount = 0;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
    };

    const VulkanDevice& device;

    VulkanBuffer blockTotals;
    VulkanBuffer total;

    VulkanComputePipeline blocksPipeline;
    VulkanComputePipeline totalsPipeline;
    VulkanComputePipeline addPipeline;

    ComputeMaterial blocksMaterial;
    ComputeMaterial totalsMaterial;
    ComputeMaterial addMaterial;
};
