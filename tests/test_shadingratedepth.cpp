// shading rate from depth: the map is filled by a compute pass reading a depth prepass, so
//                          what decides the rate is how far away each block of pixels is.
//
// This is the whole point of the map. A draw call knows how important an object is; only the
// depth buffer knows how far each of its pixels ended up. A floor running to the horizon is
// one draw, one material, one rate - and half of it should be shaded coarsely.
//
// The prepass is not a cost here, it is the thing that pays for itself: the main pass loads
// the depth it wrote instead of computing it again, so every pixel is shaded exactly once
// however many triangles overlap it.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/ShadingRate.h"
#include "Vulkan/Material.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/ShadingRateMap.h"
#include "Vulkan/Texture.h"

#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 512;
const uint32_t sceneHeight = 384;

//A floor stretching away from the camera, tiled so the texture is never flat colour - a
//uniform block has to mean the rate and not the texture
std::vector<Vertex> tiledFloor(float half, float tiles){
    return {
        {{-half,0.0f,-half},{1,1,1},{0.0f, 0.0f},{0,1,0}},
        {{ half,0.0f,-half},{1,1,1},{tiles,0.0f},{0,1,0}},
        {{ half,0.0f, half},{1,1,1},{tiles,tiles},{0,1,0}},
        {{-half,0.0f, half},{1,1,1},{0.0f, tiles},{0,1,0}},
    };
}

//Whether one 2x2 block is all the same colour
bool blockIsUniform(const std::vector<uint8_t>& pixels, uint32_t x, uint32_t y){
    const size_t first = (size_t(y) * sceneWidth + x) * 4;
    for(uint32_t by = 0; by < 2; ++by){
        for(uint32_t bx = 0; bx < 2; ++bx){
            const size_t here = (size_t(y + by) * sceneWidth + (x + bx)) * 4;
            for(int c = 0; c < 3; ++c){
                if(pixels[here + c] != pixels[first + c]) return false;
            }
        }
    }
    return true;
}

//Blocks that had detail without the map and lost it with one. Counting uniform blocks
//outright would count the sky, which is one colour whatever the rate says - and the sky is
//exactly where an eyeballed band of rows lands
size_t coarsenedBlocks(const std::vector<uint8_t>& without, const std::vector<uint8_t>& with,
                       uint32_t fromRow, uint32_t toRow){
    size_t count = 0;
    for(uint32_t y = fromRow; y + 2 <= toRow; y += 2){
        for(uint32_t x = 0; x + 2 <= sceneWidth; x += 2){
            if(!blockIsUniform(without, x, y) && blockIsUniform(with, x, y)) ++count;
        }
    }
    return count;
}

//Uniform 2x2 blocks in a band of rows
size_t uniformBlocksInRows(const std::vector<uint8_t>& pixels, uint32_t fromRow, uint32_t toRow){
    size_t count = 0;
    for(uint32_t y = fromRow; y + 2 <= toRow; y += 2){
        for(uint32_t x = 0; x + 2 <= sceneWidth; x += 2){
            const size_t first = (size_t(y) * sceneWidth + x) * 4;
            bool same = true;
            for(uint32_t by = 0; by < 2 && same; ++by){
                for(uint32_t bx = 0; bx < 2 && same; ++bx){
                    const size_t here = (size_t(y + by) * sceneWidth + (x + bx)) * 4;
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
    TestReport report("shading rate from depth");

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "rate depth"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = true;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;

    LoomInitializer loom(config);

    if(!loom.device.hasShadingRateImage()){
        report.check("slika stope", false, "ova kartica ne podrzava attachmentFragmentShadingRate");
        report.checkNoValidationMessages();
        return report.result();
    }

    //Low and looking along the floor: near rows are a metre away, far rows are at the horizon
    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.7f, 4.0f};
    cameraConfig.target = {0.0f, 0.45f, -40.0f};
    cameraConfig.nearPlane = 0.1f;
    cameraConfig.farPlane = 200.0f;
    Camera camera(cameraConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {1.0f,1.0f,1.0f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    //The target keeps its depth, so the prepass can write it and the colour pass can load it
    RenderTargetConfig sceneConfig;
    sceneConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    sceneConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    sceneConfig.keepDepth = true;
    sceneConfig.loadDepth = true;
    //No comparison sampler: the compute pass reads the depth value itself, not an answer to
    //a comparison
    sceneConfig.depthCompare = false;

    RenderTarget scene(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, sceneConfig);

    PipelineConfig texturedConfig = config.pipelineConfig;
    texturedConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    texturedConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.vert.spv";
    texturedConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.frag.spv";

    //Derived from the colour config, not written beside it. Same vertex shader, same
    //attributes, same push constants - which is the whole reason eEqual can be used below
    VulkanGraphicsPipeline prepassPipeline(loom.device, makeDepthPrepassConfig(texturedConfig),
        loom.getColorFormat(), scene.getDepthFormat());
    Material prepassMaterial(prepassPipeline);

    //eEqual: the prepass already decided what is visible, so the colour pass shades exactly
    //the fragments that survived it and not one more
    texturedConfig.depthCompare = vk::CompareOp::eEqual;
    texturedConfig.depthWriteEnable = false;
    VulkanGraphicsPipeline texturedPipeline = loom.createPipeline(texturedConfig);

    //The same colour pass with the looser test, to measure what eEqual costs in coverage
    PipelineConfig looseConfig = texturedConfig;
    looseConfig.depthCompare = vk::CompareOp::eLessOrEqual;
    VulkanGraphicsPipeline loosePipeline = loom.createPipeline(looseConfig);

    const std::vector<uint8_t> checker = makeCheckerboard(64, 4);
    TextureConfig textureConfig;
    textureConfig.generateMipmaps = false;   //mipmaps would smooth the distance by themselves
    textureConfig.filter = vk::Filter::eNearest;
    Texture texture(loom.device, loom.command, checker.data(), vk::Extent2D{64,64}, textureConfig);

    Material floorMaterial(loom.device, loom.command, loom.getDescriptorPool(), texturedPipeline, texture.getSampled());
    Mesh floor(loom.device, loom.command, tiledFloor(80.0f, 48.0f), {0,1,2, 2,3,0});

    ShadingRateMap map(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    map.setDepthSource(loom.getDescriptorPool(), scene);

    ShadingRateDistances distances;
    distances.quarter = 12.0f;     //beyond twelve units, 2x2
    distances.sixteenth = 45.0f;   //beyond forty five, 4x4
    map.setDistances(distances);

    auto renderWith = [&](const VulkanGraphicsPipeline& colourPipeline){
        loom.renderer.clearShadingRateMap();
        Material other(loom.device, loom.command, loom.getDescriptorPool(), colourPipeline, texture.getSampled());

        if(loom.renderer.beginFrame()){
            loom.renderer.beginDepthPass(scene);
            loom.renderer.draw(floor, glm::mat4(1.0f), prepassMaterial);
            loom.renderer.endPass();

            loom.renderer.beginPass(scene);
            loom.renderer.draw(floor, glm::mat4(1.0f), other);
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return scene.readPixels(loom.command).pixels;
    };

    auto render = [&](bool useMap){
        if(useMap) loom.renderer.setShadingRateMap(map);
        else loom.renderer.clearShadingRateMap();

        if(loom.renderer.beginFrame()){
            //1. depth only, into the target the colour pass will use
            loom.renderer.beginDepthPass(scene);
            loom.renderer.draw(floor, glm::mat4(1.0f), prepassMaterial);
            loom.renderer.endPass();

            //2. that depth, read by a compute pass, becomes the rate map
            if(useMap){
                loom.renderer.updateShadingRateMap(map);
            }

            //3. and the colour pass loads the depth rather than clearing it
            loom.renderer.beginPass(scene);
            loom.renderer.draw(floor, glm::mat4(1.0f), floorMaterial);
            loom.renderer.endPass();

            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return scene.readPixels(loom.command).pixels;
    };

    const std::vector<uint8_t> withoutMap = render(false);
    const std::vector<uint8_t> withMap = render(true);

    // -------------------------------------------------------------------------------
    // Prepass i ucitavanje dubine: slika mora biti ista kao da ga nema
    // -------------------------------------------------------------------------------

    report.check("nesto je nacrtano", countNonBlack(withoutMap) > 20000,
        fmt("%zu ne-crnih piksela uz eEqual i izvedeni prepass", countNonBlack(withoutMap)));

    //eEqual is the strict test: it shades only fragments whose depth is exactly what the
    //prepass wrote. If the derived prepass agreed only approximately, this picture would be
    //missing pixels that the looser test keeps - so comparing the two is the measurement of
    //whether the derivation actually holds
    const std::vector<uint8_t> loose = renderWith(loosePipeline);
    const ByteDiff strictVsLoose = diffBytes(withoutMap, loose);

    report.check("eEqual ne gubi nista", strictVsLoose.different == 0,
        fmt("%zu razlicitih bajtova izmedu eEqual i eLessOrEqual, %zu ne-crnih u oba",
            strictVsLoose.different, countNonBlack(loose)));

    //With eEqual and depth writes off, the colour pass draws only where the prepass agreed.
    //If loadDepth were not working, the cleared depth would reject every fragment and the
    //picture would be empty - so the check above is also the check that the load happened
    //With depth writes off, the colour pass can only draw where the depth it loaded lets it.
    //Had the load not happened, the cleared buffer would still admit everything - so this is
    //checked the other way: the picture has to be the same one the prepass decided on
    report.check("dubina se ucitala", countNonBlack(withMap) == countNonBlack(withoutMap),
        fmt("%zu ne-crnih s kartom, %zu bez nje - stopa mijenja boje, ne pokrivenost",
            countNonBlack(withMap), countNonBlack(withoutMap)));

    // -------------------------------------------------------------------------------
    // I ono glavno: daljina je gruba, blizina nije
    // -------------------------------------------------------------------------------

    //The near band is the bottom of the picture: floor a metre or two from the camera
    const uint32_t nearFrom = sceneHeight - sceneHeight / 5;

    const size_t coarsenedEverywhere = coarsenedBlocks(withoutMap, withMap, 0, sceneHeight);
    const size_t coarsenedNear = coarsenedBlocks(withoutMap, withMap, nearFrom, sceneHeight);

    report.check("daljina je pogrubljena", coarsenedEverywhere > 500,
        fmt("%zu blokova je imalo detalj bez karte i izgubilo ga s njom", coarsenedEverywhere));

    //And the near band is untouched. Same draw, same material, same texture - the only thing
    //that differs between those pixels and the coarsened ones is how far away they are
    report.check("blizina netaknuta", coarsenedNear == 0,
        fmt("%zu blokova pogrubljeno u zadnjih %u redova", coarsenedNear, sceneHeight - nearFrom));

    //If everything had gone coarse the first check would pass and mean nothing
    const size_t stillSharp = uniformBlocksInRows(withMap, nearFrom, sceneHeight);
    const size_t sharpWithout = uniformBlocksInRows(withoutMap, nearFrom, sceneHeight);
    report.check("nije sve pogrubljeno", stillSharp == sharpWithout,
        fmt("u blizini %zu uniformnih s kartom, %zu bez nje", stillSharp, sharpWithout));

    // -------------------------------------------------------------------------------
    // Pragovi stvarno odlucuju
    // -------------------------------------------------------------------------------

    ShadingRateDistances everything;
    everything.quarter = 0.5f;      //everything past half a unit is coarse
    everything.sixteenth = 1000.0f;
    map.setDistances(everything);

    const std::vector<uint8_t> allCoarse = render(true);
    const size_t nearAllCoarse = coarsenedBlocks(withoutMap, allCoarse, nearFrom, sceneHeight);

    report.check("prag pomice granicu", nearAllCoarse > 0,
        fmt("uz prag na 0.5 je i blizina pogrubljena: %zu blokova, a na pragu 12 njih %zu",
            nearAllCoarse, coarsenedNear));

    // -------------------------------------------------------------------------------
    // Granice
    // -------------------------------------------------------------------------------

    bool noDepthSourceThrew = false;
    try{
        ShadingRateMap bare(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
        loom.renderer.updateShadingRateMap(bare);
    }
    catch(const std::exception&){ noDepthSourceThrew = true; }
    report.check("karta bez dubine", noDepthSourceThrew, "baca iznimku");

    bool scratchDepthThrew = false;
    try{
        RenderTarget scratch(loom.device, vk::Extent2D{64,64});
        ShadingRateMap other(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
        other.setDepthSource(loom.getDescriptorPool(), scratch);
    }
    catch(const std::exception&){ scratchDepthThrew = true; }
    report.check("dubina koja se ne cuva", scratchDepthThrew, "baca iznimku");

    bool loadWithoutKeepThrew = false;
    try{
        RenderTargetConfig broken;
        broken.loadDepth = true;   //but keepDepth is false
        RenderTarget refused(loom.device, vk::Extent2D{64,64}, broken);
    }
    catch(const std::exception&){ loadWithoutKeepThrew = true; }
    report.check("ucitaj bez cuvanja", loadWithoutKeepThrew, "baca iznimku");

    loom.renderer.clearShadingRateMap();

    report.checkNoValidationMessages();
    return report.result();
}
