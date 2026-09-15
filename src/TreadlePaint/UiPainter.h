#pragma once
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/VulkanCommand.h"
#include "Vulkan/VulkanGraphicsPipeline.h"
#include "Vulkan/VulkanRenderer.h"

#include <Treadle/Draw.h>

#include <vector>

//=============================================================================================
// Treadle nacrtan Loomom. Jedini sloj koji zna za oboje - isti razlog i isto mjesto kao
// LoomPreset: da je ovo unutar Treadlea, Treadle bi ovisio o Vulkanu i vise se ne bi dao
// testirati bez kartice; da je unutar Looma, Loom bi znao za suicelje.
//
// Sve sto radi je: uzmi polje vrhova u pikselima, prepisi ga u buffer, nacrtaj. Nema stanja
// suicelja i ne zna sto je gumb.
//=============================================================================================
class UiPainter{
    public:
    UiPainter(const VulkanDevice& device,
              const VulkanCommand& command,
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

    //JEDAN PAR BUFFERA PO KADRU U LETU. Loom drzi dva kadra u letu, pa bi jedan buffer
    //prepisivao ono sto kartica upravo cita - suicelje bi treperilo izmedju dva rasporeda.
    //Isti razlog zbog kojeg SplatRenderer trazi waitIdle, samo je ovdje rjesenje jeftino
    std::vector<VulkanBuffer> vertexBuffers;
    std::vector<VulkanBuffer> indexBuffers;
    uint32_t nextBuffer = 0;
};
