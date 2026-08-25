// shading rate map: the rate comes from an image, so it can differ from pixel to pixel.
//
// A draw call cannot express distance. Distance changes within one object, and the rate set
// per draw is one number for the whole of it - so the thing that knows about distance has to
// be an image, one texel per block of pixels.
//
// The map here is filled by hand, in halves, because what is being proved is that an attached
// map decides anything at all: it must coarsen where it says coarse and leave the rest alone.
// Filling it from depth is the next step and a different claim.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/ShadingRate.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/ShadingRateMap.h"
#include "Vulkan/Texture.h"

namespace{

const uint32_t size = 256;

std::vector<uint8_t> noise(uint32_t width, uint32_t height){
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    uint32_t state = 0x9e3779b9u;
    for(size_t i = 0; i + 3 < pixels.size(); i += 4){
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        pixels[i+0] = uint8_t(state);
        pixels[i+1] = uint8_t(state >> 8);
        pixels[i+2] = uint8_t(state >> 16);
        pixels[i+3] = 255;
    }
    return pixels;
}

//Uniform 2x2 blocks, counted only inside the given band of rows
size_t uniformBlocksInRows(const std::vector<uint8_t>& pixels, uint32_t fromRow, uint32_t toRow){
    size_t count = 0;
    for(uint32_t y = fromRow; y + 2 <= toRow; y += 2){
        for(uint32_t x = 0; x + 2 <= size; x += 2){
            const size_t first = (size_t(y) * size + x) * 4;
            bool same = true;
            for(uint32_t by = 0; by < 2 && same; ++by){
                for(uint32_t bx = 0; bx < 2 && same; ++bx){
                    const size_t here = (size_t(y + by) * size + (x + bx)) * 4;
                    for(int c = 0; c < 3; ++c){
                        if(pixels[here + c] != pixels[first + c]){ same = false; break; }
                    }
                }
            }
            if(same) ++count;
        }
    }
    return count;
}

}

int main(){
    TestReport report("shading rate map");

    LoomConfig config;
    config.width = size; config.height = size;
    config.appName = "rate map"; config.engineName = "Loom tests";
    config.headless = true;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;

    LoomInitializer loom(config);

    if(!loom.device.hasShadingRateImage()){
        report.check("slika stope", false, "ova kartica ne podrzava attachmentFragmentShadingRate");
        report.checkNoValidationMessages();
        return report.result();
    }

    // -------------------------------------------------------------------------------
    // Rezolucija nije nas izbor
    // -------------------------------------------------------------------------------

    ShadingRateMap map(loom.device, vk::Extent2D{size,size});

    const vk::Extent2D texel = loom.device.getShadingRateTexelSize();
    const uint32_t expectedWidth = (size + texel.width - 1) / texel.width;

    report.check("velicina karte", map.getExtent().width == expectedWidth &&
                                   map.getTexelSize().width == texel.width,
        fmt("teksel %ux%u, karta %ux%u za sliku %ux%u",
            texel.width, texel.height, map.getExtent().width, map.getExtent().height, size, size));

    //Vulkan's packing, which is what ends up in each byte
    report.check("pakiranje",
        ShadingRateMap::pack(ShadingRate::Full) == 0 &&
        ShadingRateMap::pack(ShadingRate::Quarter) == 5 &&
        ShadingRateMap::pack(ShadingRate::Sixteenth) == 10 &&
        ShadingRateMap::pack(ShadingRate::Wide) == 4 &&
        ShadingRateMap::pack(ShadingRate::Tall) == 1,
        "1x1 -> 0, 2x1 -> 4, 1x2 -> 1, 2x2 -> 5, 4x4 -> 10");

    // -------------------------------------------------------------------------------
    // Gornja polovica gruba, donja ostra
    // -------------------------------------------------------------------------------

    //A map in halves. Nothing about the scene changes between the two halves, so any
    //difference in the picture came from the map and from nowhere else
    std::vector<uint8_t> packed(map.texelCount(), ShadingRateMap::pack(ShadingRate::Full));
    const uint32_t halfRow = map.getExtent().height / 2;
    for(uint32_t y = 0; y < halfRow; ++y){
        for(uint32_t x = 0; x < map.getExtent().width; ++x){
            packed[size_t(y) * map.getExtent().width + x] = ShadingRateMap::pack(ShadingRate::Quarter);
        }
    }
    map.upload(loom.command, packed);

    PipelineConfig postConfig;
    postConfig.vertexBindings.clear();
    postConfig.vertexAttributes.clear();
    postConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    postConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.vert.spv";
    postConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.frag.spv";
    postConfig.cullMode = vk::CullModeFlagBits::eNone;
    VulkanGraphicsPipeline postPipeline = loom.createPipeline(postConfig);

    TextureConfig noiseConfig;
    noiseConfig.filter = vk::Filter::eNearest;
    noiseConfig.addressMode = vk::SamplerAddressMode::eClampToEdge;
    noiseConfig.generateMipmaps = false;

    const std::vector<uint8_t> pixels = noise(size, size);
    Texture noiseTexture(loom.device, loom.command, pixels.data(), vk::Extent2D{size,size}, noiseConfig);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    readConfig.enableDepth = false;

    auto render = [&](bool attachMap, ShadingImportance importance){
        if(attachMap) loom.renderer.setShadingRateMap(map);
        else loom.renderer.clearShadingRateMap();

        RenderTarget out(loom.device, vk::Extent2D{size,size}, readConfig);
        Material material(loom.device, loom.command, loom.getDescriptorPool(), postPipeline, noiseTexture.getSampled());
        material.setImportance(importance);

        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(out);
            loom.renderer.drawFullscreen(material);
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return out.readPixels(loom.command).pixels;
    };

    const std::vector<uint8_t> withoutMap = render(false, ShadingImportance::Normal);
    const std::vector<uint8_t> withMap = render(true, ShadingImportance::Normal);

    //Where the map is 2x2 - the top half of the picture - every block has to be uniform
    const uint32_t coarseRows = halfRow * texel.height;
    const size_t blocksInHalf = size_t(coarseRows / 2) * (size / 2);

    const size_t coarseWith = uniformBlocksInRows(withMap, 0, coarseRows);
    const size_t sharpWith = uniformBlocksInRows(withMap, coarseRows, size);

    report.check("gornja polovica gruba", coarseWith == blocksInHalf,
        fmt("%zu od %zu blokova uniformno u redovima 0-%u", coarseWith, blocksInHalf, coarseRows));

    //And the half the map left alone must be exactly as sharp as if there were no map
    const size_t sharpWithout = uniformBlocksInRows(withoutMap, coarseRows, size);
    report.check("donja polovica netaknuta", sharpWith == sharpWithout,
        fmt("%zu uniformnih blokova s kartom, %zu bez nje", sharpWith, sharpWithout));

    //The control: without a map nothing anywhere is uniform, so the count above is the map
    report.check("bez karte nista nije uniformno",
        uniformBlocksInRows(withoutMap, 0, size) == 0,
        fmt("%zu uniformnih blokova bez karte", uniformBlocksInRows(withoutMap, 0, size)));

    // -------------------------------------------------------------------------------
    // Vazan materijal se ispisuje iz karte
    // -------------------------------------------------------------------------------

    const std::vector<uint8_t> critical = render(true, ShadingImportance::Critical);

    const ByteDiff sameAsNoMap = diffBytes(critical, withoutMap);
    report.check("Critical ignorira kartu", sameAsNoMap.different == 0,
        fmt("s kartom i Critical materijalom, %zu bajtova razlike od slike bez karte",
            sameAsNoMap.different));

    //If Critical and Normal came out the same, the check above would pass on a map that
    //never did anything
    report.check("a Normal je ne ignorira", diffBytes(critical, withMap).different > 0,
        fmt("Critical i Normal se razlikuju u %zu bajtova", diffBytes(critical, withMap).different));

    loom.renderer.clearShadingRateMap();

    report.checkNoValidationMessages();
    return report.result();
}
