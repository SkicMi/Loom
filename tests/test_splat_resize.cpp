// Rasterizator sagradjen iznova u novoj velicini - sto se dogodi kad se prozor povuce.
//
// Meta i rasterizator drze velicinu slike u sebi: mreza pločica, polje raspona, vezana slika.
// Prozor se mijenja, pa se moraju moci sagraditi iznova. Ovdje se provjerava da to stvarno ide
// vise puta, jer se prozor ne povuce jednom.
//
// Sto se brani:
//
//   deset pregradnji     descriptor setovi se MORAJU osloboditi kad materijal umre. Ako ne,
//                        bazen od 128 setova nestane oko seste pregradnje - a pad tada dodje
//                        usred vucenja ruba prozora, dakle najdalje od uzroka
//   mreza prati velicinu broj pločica se mora promijeniti s njom, inace se crta u stari raspored
//   crta se i poslije    nakon pregradnje kadar mora dati istu sliku kao da je tako sagradjen
//                        od pocetka. Pregradnja koja preživi a ne crta nije pregradnja
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/SplatRenderer.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>
#include <vector>

namespace{

struct Pixel{
    float r = 0, g = 0, b = 0, a = 0;
};

//Jedan neproziran splat ispred kamere, dovoljno velik da pokrije srediste slike
std::vector<SplatMath::PreparedSplat> oneSplat(const glm::mat4& view, vk::Extent2D extent, float focal){
    Splat splat;
    splat.position = glm::vec3(0.0f, 0.0f, -4.0f);
    splat.scale = glm::vec3(0.5f);
    splat.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    splat.opacity = 0.99f;
    splat.color = glm::vec3(0.9f, 0.4f, 0.1f);

    std::vector<SplatMath::PreparedSplat> prepared;
    SplatMath::PreparedSplat one;
    if(SplatMath::prepare(splat, view, focal, -focal,
                          0.5f * float(extent.width), 0.5f * float(extent.height), 0.3f, one)){
        prepared.push_back(one);
    }
    return prepared;
}

}

int main(){
    TestReport report("pregradnja rasterizatora");

    LoomConfig config;
    config.width = 320; config.height = 180;
    config.appName = "splat resize"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = false;
    config.maxDescriptorSets = 128;
    LoomInitializer loom(config);

    RenderTargetConfig targetConfig;
    targetConfig.colorFormat = vk::Format::eR32G32B32A32Sfloat;
    targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eSampled |
                                   vk::ImageUsageFlagBits::eStorage |
                                   vk::ImageUsageFlagBits::eTransferSrc;
    targetConfig.enableDepth = false;
    targetConfig.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    SplatRendererConfig rendererConfig;
    rendererConfig.tileSize = 16;
    rendererConfig.maxSplats = 16;
    rendererConfig.maxPairs = 1u << 16;
    rendererConfig.maxShCoefficients = 0;

    const glm::mat4 view = glm::lookAt(glm::vec3(0,0,0), glm::vec3(0,0,-1), glm::vec3(0,1,0));
    const float focal = 300.0f;

    std::optional<RenderTarget> target;
    std::optional<SplatRenderer> renderer;

    //-- deset pregradnji ---------------------------------------------------------------------
    bool everyRebuildHeld = true;
    bool gridFollowed = true;
    std::string lastGrid;
    uint32_t previousTiles = 0;

    for(int step = 0; step < 10; ++step){
        const vk::Extent2D extent{uint32_t(320 + step * 32), uint32_t(180 + step * 18)};
        try{
            renderer.reset();
            target.reset();
            target.emplace(loom.device, extent, targetConfig);
            renderer.emplace(loom.device, loom.getDescriptorPool(), target->getColorImage(),
                             extent, rendererConfig);
        }catch(const std::exception&){
            everyRebuildHeld = false;
            break;
        }

        const uint32_t tiles = renderer->getTileCount();
        if(step > 0 && tiles <= previousTiles) gridFollowed = false;
        previousTiles = tiles;
        lastGrid = fmt("%ux%u", renderer->getGrid().width, renderer->getGrid().height);
    }

    report.check("deset pregradnji", everyRebuildHeld,
        everyRebuildHeld ? "bazen descriptora izdrzao" : "pao prije desete - setovi se ne oslobadjaju");
    report.check("mreza prati velicinu", gridFollowed,
        fmt("zadnja mreza %s, %u pločica", lastGrid.c_str(), previousTiles));

    //-- i dalje crta -------------------------------------------------------------------------
    if(everyRebuildHeld){
        const vk::Extent2D extent{uint32_t(320 + 9 * 32), uint32_t(180 + 9 * 18)};
        const std::vector<SplatMath::PreparedSplat> prepared = oneSplat(view, extent, focal);

        renderer->upload(prepared);
        renderer->setCamera(view, glm::vec3(0.0f), focal, -focal,
                            0.5f * float(extent.width), 0.5f * float(extent.height));

        loom.renderer.beginFrame();
        renderer->draw(loom.renderer, uint32_t(prepared.size()));
        loom.renderer.endFrame();
        loom.waitIdle();

        const vk::DeviceSize pixelBytes = vk::DeviceSize(extent.width) * extent.height * sizeof(Pixel);
        VulkanBuffer readback(loom.device, pixelBytes, vk::BufferUsageFlagBits::eTransferDst,
                              MemoryUsage::GPU_TO_CPU);
        loom.command.copyImageToBuffer(target->getColorImage().getImage(), readback.getBuffer(), extent);

        std::vector<Pixel> pixels(size_t(extent.width) * extent.height);
        readback.download(pixels.data(), pixelBytes);

        const Pixel& middle = pixels[size_t(extent.height / 2) * extent.width + extent.width / 2];

        //Srediste nosi boju splata, a rub slike ostaje prazan - dakle mreza pločica i rasponi
        //pokrivaju bas novu velicinu, ne staru
        const Pixel& corner = pixels[0];

        report.check("crta i poslije pregradnje",
            middle.a > 0.9f && middle.r > middle.b && corner.a < 0.01f,
            fmt("srediste (%.3f %.3f %.3f) a %.3f, kut a %.3f",
                middle.r, middle.g, middle.b, middle.a, corner.a));
    }

    report.checkNoValidationMessages();
    return report.result();
}
