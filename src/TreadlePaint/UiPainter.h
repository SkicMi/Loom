#pragma once
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/VulkanCommand.h"
#include "Vulkan/VulkanGraphicsPipeline.h"
#include "Vulkan/VulkanRenderer.h"
#include "Vulkan/Texture.h"

#include <Treadle/Draw.h>

#include <vector>

//=============================================================================================
// Treadle nacrtan Loomom. Jedini sloj koji zna za oboje - isti razlog i isto mjesto kao
// LoomPreset: da je ovo unutar Treadlea, Treadle bi ovisio o Vulkanu i vise se ne bi dao
// testirati bez kartice; da je unutar Looma, Loom bi znao za suicelje.
//
// Sve sto radi je: uzmi polje vrhova u pikselima, prepisi ga u buffer, nacrtaj. Nema stanja
// suicelja i ne zna sto je gumb.
//
// Slova su kvadratici tinte iz ATLASA (pravi font), pa ovaj sloj nosi jos dvije stvari koje
// pravi crtac ne treba: atlas teksturu i njen descriptor set. Atlas je stalan - generira se
// iz FontData.h i nikad se ne mijenja - pa je i descriptor set jedan za sve kadrove
//=============================================================================================
class UiPainter{
    public:
    UiPainter(const VulkanDevice& device,
              const VulkanCommand& command,
              const vk::raii::DescriptorPool& descriptorPool,
              vk::Format colorFormat,
              vk::Format depthFormat = vk::Format::eUndefined,
              uint32_t maxVertices = 1u << 16);

    UiPainter(const UiPainter&) = delete;
    UiPainter& operator = (const UiPainter&) = delete;

    //Unutar prolaza, poslije svega ostalog - suicelje je iznad slike. Velicina je ona u koju
    //se crta, i mora se slagati s onom koju je Treadle dobio, inace UI nije ondje gdje se
    //mislilo da klikne
    void draw(VulkanRenderer& renderer, const Treadle::DrawList& list,
              uint32_t width, uint32_t height);

    //Koliko vrhova stane. Tko preko toga, taj se odsijeca - i to se javi jednom, ne svaki
    //kadar, jer bi inace ispis progutao sve ostalo
    uint32_t capacity() const {return maxVertices;}

    private:
    struct Push{
        float screenWidth = 0.0f;
        float screenHeight = 0.0f;
        float encodeSrgb = 0.0f;
        float padding = 0.0f;
    };

    static PipelineConfig makeConfig(vk::Format colorFormat);

    const VulkanDevice& device;
    uint32_t maxVertices;
    bool srgbTarget = false;
    bool warned = false;

    VulkanGraphicsPipeline pipeline;

    //Atlas slova iz FontData.h: R8 pokrivenost, obican linearni filter, rub zarobljen na
    //prazno da filtriranje uz rub tinte ne gleda u susjedno slovo. Bez mip lanaca - tekst se
    //nikad ne smanjuje, samo priblizava na njegovu pravu velicinu
    Texture atlas;

    //Set 0 je prazan (cjevovod bez okvira), set 1 je atlas. Zajednicki bazen descriptora iz
    //LoomInitializer-a daje oba; atlas set se napise jednom jer se atlas nikad ne mijenja
    const vk::raii::DescriptorPool& descriptorPool;
    vk::raii::DescriptorSet emptyFrameSet = nullptr;
    vk::raii::DescriptorSet atlasSet = nullptr;

    //JEDAN PAR BUFFERA PO KADRU U LETU. Loom drzi dva kadra u letu, pa bi jedan buffer
    //prepisivao ono sto kartica upravo cita - suicelje bi treperilo izmedju dva rasporeda.
    //Isti razlog zbog kojeg SplatRenderer trazi waitIdle, samo je ovdje rjesenje jeftino
    std::vector<VulkanBuffer> vertexBuffers;
    std::vector<VulkanBuffer> indexBuffers;
    uint32_t nextBuffer = 0;
};