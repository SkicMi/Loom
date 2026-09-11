#pragma once
#include "VulkanBuffer.h"
#include "VulkanComputePipeline.h"
#include "ComputeMaterial.h"

class VulkanRenderer;

//=============================================================================================
// Sortiranje para kljuc/vrijednost na kartici, stabilno.
//
// ZASTO 32 BITA, KAD SPLATANJE TRAZI VISE. Splatovi se moraju poredati po pločici pa unutar
// pločice po dubini, a to je 64-bitni kljuc. Ali radix sort je STABILAN, pa se isti posao
// dobije s dva prolaza po 32 bita: prvo po dubini, pa po pločici. Drugi sort tada premjesta
// cijele skupine a unutar njih ne dira nista, jer stabilan sort ne dira. Dva jeftina prolaza
// umjesto jednog dvostruko skupljeg, i pola manje koda.
//
// ZASTO STABILNOST NIJE UKRAS. Bez nje gornji trik ne radi uopce. I jos jedno: nestabilan sort
// smije dva jednaka kljuca vratiti u bilo kojem poretku, pa ista scena iz iste kamere daje dvije
// razlicite slike. Ovaj projekt mjeri bajt po bajt, i takva bi provjera postala neprovjerljiva.
//
// KLJUCEVI SU UINT, NE FLOAT, i to nije samo pakiranje - vidi keyFromFloat.
//
// Sve sto sort treba veze se JEDNOM, pri gradnji. Sortiranje po kadru zato ne alocira nista i
// ne dira nijedan descriptor, sto je pravilo koje test budzeta brani.
//=============================================================================================
class RadixSort{
    public:
    //Kljucevi i vrijednosti su tudji i ostaju tudji - sort ih samo presloži na mjestu.
    //capacity je najveci broj elemenata koji ce se ikad sortirati; oba buffera moraju biti
    //barem toliki, jer se medjurezultat vraca natrag u njih
    RadixSort(const VulkanDevice& device,
              const vk::raii::DescriptorPool& pool,
              VulkanBuffer& keys,
              VulkanBuffer& values,
              uint32_t capacity,
              const VulkanBuffer* countSource = nullptr,   //treba ga samo sortIndirect
              uint32_t keyBits = 32);                      //sortira se samo toliko nizih bita

    RadixSort(const RadixSort&) = delete;
    RadixSort& operator = (const RadixSort&) = delete;

    //Osam prolaza po cetiri bita. Paran broj prolaza, pa rezultat zavrsava u buffere koje je
    //pozivatelj dao, a ne u radnima. Mora se zvati unutar kadra, izmedju prolaza crtanja
    void sort(VulkanRenderer& renderer, uint32_t count);

    //ISTO, ALI BROJ ELEMENATA ZNA SAMO KARTICA. Cita se iz countSource danog pri gradnji, od
    //elementa base, ovim rasporedom:
    //
    //   [base]       koliko elemenata      [base+1]  koliko blokova (blocksFor)
    //   [base+2..4]  velicina dispatcha:   blokovi, 1, 1
    //
    //Tko pise buffer jamci da broj nije veci od kapaciteta. Sort to ne moze provjeriti bez
    //citanja natrag - a bas citanje natrag je ono sto se ovdje izbjegava
    void sortIndirect(VulkanRenderer& renderer, uint32_t base);

    //FLOAT PRETVOREN U CJELOBROJNI KLJUC KOJI SE SORTIRA ISTIM REDOM.
    //
    //Bitovi pozitivnog floata su vec poredani kao i sam broj - to je namjerno u IEEE 754. Ali
    //negativni su poredani NAOPAKO, jer im veca mantisa znaci manji broj, a bit predznaka bi ih
    //stavio iznad svih pozitivnih. Zato: pozitivnima se okrene bit predznaka, negativnima se
    //okrenu svi. Nakon toga usporedba cijelih brojeva daje isti poredak kao usporedba floatova,
    //kroz cijeli raspon.
    //
    //Dubina je uvijek pozitivna pa bi i sam pomak predznaka bio dovoljan - ali funkcija koja
    //radi samo za pola ulaza je mina koja ceka prvi drugi poziv
    static uint32_t keyFromFloat(float value);

    //Koliko je blokova potrebno za toliko elemenata. Javno jer test racuna ista mjesta
    static uint32_t blocksFor(uint32_t count);

    static constexpr uint32_t threadsPerBlock = 128;
    static constexpr uint32_t elementsPerThread = 8;
    static constexpr uint32_t elementsPerBlock = threadsPerBlock * elementsPerThread;
    //CETIRI BITA PO PROLAZU, dakle osam prolaza. Osam bita bi bilo cetiri prolaza i upola
    //manje prometa prema memoriji - ali trazi 256 pretinaca u dijeljenoj memoriji po dretvi,
    //sto je drukcije lokalno rangiranje nego ovo ovdje. Mjereno na Intel UHD, 741883 elementa:
    //25.9 ms, od cega 1.95 ms fiksno na 24 dispatcha. Prije nego se to prepise, isto se mjeri
    //na pravoj kartici - integrirana dijeli memoriju s procesorom i ovdje je promet usko grlo
    //(50k i 200k su gotovo jednako brzi, a 741k je cetiri puta sporiji: tu podaci prestanu
    //stati u cache)
    static constexpr uint32_t digits = 16;         //cetiri bita po prolazu

    //Koliko prolaza treba kljuc od toliko bita: po cetiri bita, zaokruzeno na PARAN broj - jer
    //neparan bi rezultat ostavio u radnim bufferima umjesto u pozivateljevim. 32 bita je osam
    //prolaza, kljuc pločice od 12 bita cetiri
    static uint32_t passesFor(uint32_t keyBits);
    uint32_t getPasses() const {return passes;}

    private:
    //countBase koji kaze "broj je u push konstanti, ne u bufferu"
    static constexpr uint32_t direct = 0xFFFFFFFFu;

    struct Params{
        uint32_t count = 0;
        uint32_t shift = 0;
        uint32_t blockCount = 0;
        uint32_t countBase = direct;
    };

    const VulkanDevice& device;

    VulkanBuffer* keys;
    VulkanBuffer* values;
    const VulkanBuffer* countSource;
    uint32_t passes;

    //Radni par, jer se svaki prolaz cita iz jednog a pise u drugi
    VulkanBuffer scratchKeys;
    VulkanBuffer scratchValues;
    VulkanBuffer blockHistogram;
    VulkanBuffer ownCountSource;   //stoji na mjestu countSource kad ga nema

    VulkanComputePipeline histogramPipeline;
    VulkanComputePipeline scanPipeline;
    VulkanComputePipeline scatterPipeline;

    //Po dva za prolaze koji citaju iz jednog para a pisu u drugi: jedan za svaki smjer.
    //Descriptor set se ne prepisuje dok kadar leti, pa se smjer bira materijalom
    ComputeMaterial histogramForward;   //cita korisnikove
    ComputeMaterial histogramBackward;  //cita radne
    ComputeMaterial scan;
    ComputeMaterial scatterForward;     //korisnikovi -> radni
    ComputeMaterial scatterBackward;    //radni -> korisnikovi
};
