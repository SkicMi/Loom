#pragma once
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include "VulkanComputePipeline.h"
#include "ComputeMaterial.h"
#include "PrefixSum.h"
#include "RadixSort.h"
#include "Core/Splat.h"

#include <vector>

class VulkanRenderer;

//=============================================================================================
// Rasterizator 3D gaussiana: pločice, rasponi i alfa kompozicija.
//
// Sedam dispatcha po kadru, plus dva sortiranja:
//
//   1 brojanje      koliko pločica zahvaca koji splat
//   2 zbroj         gdje ciji dio pocinje (PrefixSum, tri prolaza)
//   3 sirenje       jedan par (pločica, splat) po zahvacenoj pločici
//   4 sort dubine   parovi poredani sprijeda natrag
//   5 skupljanje    kljucevi pločica preslozeni u taj novi poredak
//   6 sort pločica  stabilan, pa unutar pločice ostaje poredak po dubini
//   7 rasponi       gdje u sortiranom polju pocinje koja pločica
//   8 crtanje       jedna grupa po pločici, jedna dretva po pikselu
//
// GDJE JE OVO SPORIJE NEGO STO MORA BITI, i neka stoji zapisano prije nego se izmjeri: broj
// parova zna se tek nakon zbroja, a to je broj NA KARTICI. Velicine dispatcha ga trebaju na
// procesoru. Rjesenja su dva - procitati ga natrag (stanka usred kadra) ili neizravni dispatch
// (kojeg Loom jos nema). Zasad ga procesor racuna SAM, jer on ionako priprema splatove; GPU ga
// racuna neovisno i test provjerava da se ta dva broja slazu. Kad priprema predje na karticu,
// ovo postaje neizravni dispatch i to je zaseban korak.
//
// JEDAN PRIMJERAK RADNIH POLJA, A LOOM DRZI DVA KADRA U LETU. Splatovi, kljucevi, poreci i
// rasponi postoje po jednom, ne po kadru. Tko pripremi sljedeci kadar dok prethodni jos crta,
// prepisuje ono sto kartica upravo cita - i slika se raspadne u sum koji izgleda kao greska
// rasterizatora a nije. Do dana kad ta polja postanu po kadru, pozivatelj mora cekati (waitIdle)
// prije nego ista upise. SplatViewer to radi i tamo pise zasto.
//
// VELICINA PLOČICE JE PODESIVA jer je ujedno velicina radne grupe - vidi
// ComputePipelineConfig::specializationConstants.
//=============================================================================================
struct SplatRendererConfig{
    //16x16 je ono sto koristi referentna implementacija. 8 i 32 su isto tako valjani, i G5 ce
    //reci koja je na kojoj kartici najbrza - zato ovo uopce i jest broj a ne konstanta
    uint32_t tileSize = 16;

    uint32_t maxSplats = 1u << 20;

    //Koliko parova (pločica, splat) najvise moze nastati. Splat koji pokriva pola ekrana sam
    //napravi stotine parova, pa ovo nije maxSplats nego visekratnik - a kad se prekoraci, bolje
    //je da se cuje nego da se tiho pise izvan polja
    uint32_t maxPairs = 4u << 20;

    //Koliko koeficijenata sfernih harmonika po splatu treba mjesta. 45 je stupanj 3, sto je
    //ono sto pravi 3DGS file nosi; nula znaci da se boja ne mijenja sa smjerom
    uint32_t maxShCoefficients = 45;
};

class SplatRenderer{
    public:
    SplatRenderer(const VulkanDevice& device,
                  const vk::raii::DescriptorPool& pool,
                  const VulkanImage& target,
                  vk::Extent2D extent,
                  const SplatRendererConfig& config = {});

    SplatRenderer(const SplatRenderer&) = delete;
    SplatRenderer& operator = (const SplatRenderer&) = delete;

    //Splatovi vec pripremljeni: sredina u pikselima, conic, neprozirnost, dubina, polumjer.
    //Postoji za testove i za usporedbu s pripremom na kartici
    void upload(const std::vector<SplatMath::PreparedSplat>& splats);

    //SIROVI SPLATOVI, jednom. Sve je vec aktivirano jer aktivacija ne ovisi o kameri; ono sto
    //ovisi (kovarijanca, conic, polumjer, dubina, boja iz smjera) racuna kartica svaki kadar
    void uploadRaw(const std::vector<SplatMath::RawSplat>& splats,
                   const std::vector<float>& shRest,
                   uint32_t shDegree,
                   uint32_t coeffsPerChannel);

    //Kamera ovog kadra. Mora se pozvati prije prepare()
    void setCamera(const glm::mat4& view, const glm::vec3& cameraPosition,
                   float focalX, float focalY, float principalX, float principalY,
                   float blur = 0.3f);

    //Priprema na kartici. Unutar kadra, prije draw()
    void prepare(VulkanRenderer& renderer, uint32_t splatCount);

    //Za test: ono sto je priprema napisala
    const VulkanBuffer& getPrepared() const {return splats;}

    //Koliko parova ce nastati za te splatove. Racuna se istom matematikom koju racuna i shader,
    //i test provjerava da se slazu - jer dvije strane koje broje razlicito znace ili splatove
    //koji nedostaju ili pisanje izvan tudjeg dijela polja
    uint32_t countPairs(const std::vector<SplatMath::PreparedSplat>& splats) const;

    //Isto, ali po splatu. Test iz ovoga slaze pomake i usporedjuje ih s onima koje je
    //izracunala kartica - dva neovisna racuna istog broja
    std::vector<uint32_t> tileCounts(const std::vector<SplatMath::PreparedSplat>& splats) const;

    //Mora se zvati unutar kadra. pairCount je ono sto je vratio countPairs
    void draw(VulkanRenderer& renderer, uint32_t splatCount, uint32_t pairCount);

    vk::Extent2D getGrid() const {return grid;}
    uint32_t getTileCount() const {return grid.width * grid.height;}

    //Za test: pomaci koje je izracunala kartica
    const VulkanBuffer& getOffsets() const {return counts;}

    private:
    struct TileParams{
        uint32_t gridX = 0;
        uint32_t gridY = 0;
        uint32_t tileSize = 0;
        uint32_t splatCount = 0;
    };
    struct PairParams{
        uint32_t pairCount = 0;
        uint32_t padding0 = 0, padding1 = 0, padding2 = 0;
    };
    struct RasterParams{
        uint32_t imageX = 0, imageY = 0;
        uint32_t gridX = 0, gridY = 0;
        uint32_t pairCount = 0;
        uint32_t padding0 = 0, padding1 = 0, padding2 = 0;
    };

    const VulkanDevice& device;
    SplatRendererConfig config;
    vk::Extent2D extent;
    vk::Extent2D grid;

    uint32_t storedDegree = 0;
    uint32_t storedCoeffs = 0;
    SplatMath::PrepareParams pendingParams;

    VulkanBuffer splats;
    VulkanBuffer rawSplats;
    VulkanBuffer shRest;
    VulkanBuffer prepareParams;
    VulkanBuffer counts;        //postane pomaci nakon zbroja
    VulkanBuffer tileKeys;
    VulkanBuffer depthKeys;
    VulkanBuffer splatIndices;
    VulkanBuffer sortValues;
    VulkanBuffer gatheredTiles;
    VulkanBuffer ranges;

    PrefixSum prefixSum;
    RadixSort sortByDepth;
    RadixSort sortByTile;

    VulkanComputePipeline preparePipeline;
    VulkanComputePipeline countPipeline;
    VulkanComputePipeline expandPipeline;
    VulkanComputePipeline gatherPipeline;
    VulkanComputePipeline clearPipeline;
    VulkanComputePipeline rangesPipeline;
    VulkanComputePipeline rasterPipeline;

    ComputeMaterial prepareMaterial;
    ComputeMaterial countMaterial;
    ComputeMaterial expandMaterial;
    ComputeMaterial gatherMaterial;
    ComputeMaterial clearMaterial;
    ComputeMaterial rangesMaterial;
    ComputeMaterial rasterMaterial;
};
