// shading rate: a material can say how many pixels share one run of the fragment shader.
//
// Rasterisation, depth and coverage stay per pixel - only the shading is shared. So the
// measurement is not a stopwatch, which would be noise, but the picture itself: if a 2x2
// block of pixels came out of one shader run, its four pixels are the same colour, and no
// amount of texture detail underneath can make them differ.
//
// The scene is deliberately built so that at 1x1 almost no block is uniform: a noise texture
// the same size as the target, sampled with nearest filtering through a fullscreen pass, so
// every pixel gets its own texel. Any uniform block after that is the rate and nothing else.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/ShadingRate.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"

namespace{

const uint32_t size = 256;

//Every texel different from its neighbours, and no two the same for a long way. A gradient
//would leave neighbouring pixels close enough to pass for uniform on eight bits
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

//How many blocks of the given size have all their pixels identical
size_t uniformBlocks(const std::vector<uint8_t>& pixels, uint32_t block){
    size_t count = 0;
    for(uint32_t y = 0; y + block <= size; y += block){
        for(uint32_t x = 0; x + block <= size; x += block){
            const size_t first = (size_t(y) * size + x) * 4;
            bool same = true;

            for(uint32_t by = 0; by < block && same; ++by){
                for(uint32_t bx = 0; bx < block && same; ++bx){
                    const size_t here = (size_t(y + by) * size + (x + bx)) * 4;
                    for(int channel = 0; channel < 3; ++channel){
                        if(pixels[here + channel] != pixels[first + channel]){ same = false; break; }
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
    TestReport report("shading rate");

    LoomConfig config;
    config.width = size; config.height = size;
    config.appName = "shading rate"; config.engineName = "Loom tests";
    config.headless = true;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;

    LoomInitializer loom(config);

    const bool supported = loom.device.hasFragmentShadingRate();

    report.check("uredaj podrzava", true,
        supported ? fmt("da: 2x2 %s, 4x4 %s",
                        loom.device.supportsShadingRate(ShadingRate::Quarter) ? "da" : "ne",
                        loom.device.supportsShadingRate(ShadingRate::Sixteenth) ? "da" : "ne")
                  : "ne - ova kartica nema VK_KHR_fragment_shading_rate, crta se punom stopom");

    //Arithmetic, and true whatever the hardware does with it
    report.check("aritmetika stope",
        shadingRateSavings(ShadingRate::Full) == 1 &&
        shadingRateSavings(ShadingRate::Quarter) == 4 &&
        shadingRateSavings(ShadingRate::Sixteenth) == 16 &&
        shadingRateExtent(ShadingRate::Wide).width == 2 &&
        shadingRateExtent(ShadingRate::Tall).height == 2,
        "1x1 -> 1, 2x2 -> 4, 4x4 -> 16");

    if(!supported){
        //Nothing below can be measured on a card that has no rates. Saying so is better than
        //passing checks that never ran
        report.check("mjerenje", false, "preskoceno - bez ekstenzije nema sto mjeriti");
        report.checkNoValidationMessages();
        return report.result();
    }

    // -------------------------------------------------------------------------------
    // Ista scena, tri stope
    // -------------------------------------------------------------------------------

    PipelineConfig postConfig;
    postConfig.vertexBindings.clear();
    postConfig.vertexAttributes.clear();
    postConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    postConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.vert.spv";
    postConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.frag.spv";
    postConfig.cullMode = vk::CullModeFlagBits::eNone;
    VulkanGraphicsPipeline postPipeline = loom.createPipeline(postConfig);

    //Nearest, no mip chain, and the same size as the target: one texel per pixel, so at full
    //rate every pixel is its own colour
    TextureConfig noiseConfig;
    noiseConfig.filter = vk::Filter::eNearest;
    noiseConfig.addressMode = vk::SamplerAddressMode::eClampToEdge;
    noiseConfig.generateMipmaps = false;

    const std::vector<uint8_t> pixels = noise(size, size);
    Texture noiseTexture(loom.device, loom.command, pixels.data(), vk::Extent2D{size,size}, noiseConfig);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;

    //A fullscreen pass has nothing to depth test against, and this Loom was built without
    //depth - so a target that had some would not match the pipeline drawing into it
    readConfig.enableDepth = false;

    auto renderAt = [&](ShadingRate rate){
        RenderTarget out(loom.device, vk::Extent2D{size,size}, readConfig);
        Material material(loom.device, loom.command, loom.getDescriptorPool(), postPipeline, noiseTexture.getSampled());
        material.setShadingRate(rate);

        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(out);
            loom.renderer.drawFullscreen(material);
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();

        return out.readPixels(loom.command).pixels;
    };

    const std::vector<uint8_t> atFull = renderAt(ShadingRate::Full);
    const std::vector<uint8_t> atQuarter = renderAt(ShadingRate::Quarter);

    const size_t blocksTotal = size_t(size / 2) * (size / 2);
    const size_t uniformAtFull = uniformBlocks(atFull, 2);
    const size_t uniformAtQuarter = uniformBlocks(atQuarter, 2);

    //The control first: at full rate the noise really is per pixel, so almost nothing is
    //uniform. Without this the number below would prove nothing about the rate
    report.check("puna stopa nije uniformna", uniformAtFull * 20 < blocksTotal,
        fmt("%zu od %zu blokova 2x2 uniformno pri 1x1", uniformAtFull, blocksTotal));

    //And the claim: at 2x2, every block came out of one shader run
    report.check("2x2 dijeli sjencanje", uniformAtQuarter == blocksTotal,
        fmt("%zu od %zu blokova 2x2 uniformno pri 2x2", uniformAtQuarter, blocksTotal));

    report.check("slika se promijenila", diffBytes(atFull, atQuarter).different > 0,
        fmt("%zu razlicitih bajtova", diffBytes(atFull, atQuarter).different));

    // -------------------------------------------------------------------------------
    // I grublje, ako drajver to nudi
    // -------------------------------------------------------------------------------

    if(loom.device.supportsShadingRate(ShadingRate::Sixteenth)){
        const std::vector<uint8_t> atSixteenth = renderAt(ShadingRate::Sixteenth);

        const size_t coarseTotal = size_t(size / 4) * (size / 4);
        const size_t uniformCoarse = uniformBlocks(atSixteenth, 4);

        report.check("4x4 dijeli sjencanje", uniformCoarse == coarseTotal,
            fmt("%zu od %zu blokova 4x4 uniformno", uniformCoarse, coarseTotal));

        //Coarser than 2x2 has to be coarser, not merely different
        report.check("grublje je grublje", uniformBlocks(atSixteenth, 2) >= uniformAtQuarter,
            fmt("pri 4x4 je %zu blokova 2x2 uniformno, pri 2x2 njih %zu",
                uniformBlocks(atSixteenth, 2), uniformAtQuarter));
    }
    else{
        report.check("4x4", true, "drajver ga ne nudi, preskoceno");
    }

    // -------------------------------------------------------------------------------
    // Stopa se ne prelijeva
    // -------------------------------------------------------------------------------

    //A coarse material must not leave the rate behind for whoever draws next. Two draws in
    //one pass, the first coarse and the second full, and the second has to come out sharp
    {
        RenderTarget out(loom.device, vk::Extent2D{size,size}, readConfig);

        Material coarse(loom.device, loom.command, loom.getDescriptorPool(), postPipeline, noiseTexture.getSampled());
        coarse.setShadingRate(ShadingRate::Quarter);

        Material sharp(loom.device, loom.command, loom.getDescriptorPool(), postPipeline, noiseTexture.getSampled());

        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(out);
            loom.renderer.drawFullscreen(coarse);
            loom.renderer.drawFullscreen(sharp);   //covers the first one entirely
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();

        const std::vector<uint8_t> after = out.readPixels(loom.command).pixels;
        const ByteDiff sameAsSharp = diffBytes(after, atFull);

        report.check("stopa se ne prelijeva", sameAsSharp.different == 0,
            fmt("nakon grubog crteza, ostri je bajt za bajt jednak punoj stopi: %zu razlika",
                sameAsSharp.different));
    }

    report.checkNoValidationMessages();
    return report.result();
}
