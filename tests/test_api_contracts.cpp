// the promises the library makes outside of any one feature: a pipeline is not tied to the
// swapchain, a material carries whatever the shader declares, a push constant range is the
// pipeline's business, a material survives its source being resized, and an image knows
// which layout it is in
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Vulkan/Material.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"

struct PostData{          //deliberately not MaterialData
    float tint[4];
    float unused[4];
};

int main(){
    TestReport report("api contracts");

    LoomConfig config;
    config.width = 256; config.height = 256;
    config.appName = "contracts"; config.engineName = "Loom tests";
    config.enableDepth = true;
    config.maxDescriptorSets = 32;
    config.descriptorsPerType = 64;
    config.rendererConfig.clearColor = {0.0f, 0.25f, 0.75f, 1.0f};

    LoomInitializer loom(config);

    const vk::Extent2D extent{64,64};
    std::vector<uint8_t> pixels(size_t(64) * 64 * 4);
    for(uint32_t y = 0; y < 64; ++y){
        for(uint32_t x = 0; x < 64; ++x){
            size_t i = (size_t(y) * 64 + x) * 4;
            pixels[i+0] = uint8_t(x * 4); pixels[i+1] = uint8_t(y * 4);
            pixels[i+2] = uint8_t((x ^ y) * 4); pixels[i+3] = 255;
        }
    }

    TextureConfig textureConfig;
    textureConfig.format = vk::Format::eR8G8B8A8Unorm;   //no sRGB anywhere in this path
    textureConfig.filter = vk::Filter::eNearest;
    textureConfig.addressMode = vk::SamplerAddressMode::eClampToEdge;
    Texture texture(loom.device, loom.command, pixels.data(), extent, textureConfig);

    report.check("tekstura pamti layout",
        texture.getImage().getCurrentLayout() == vk::ImageLayout::eShaderReadOnlyOptimal,
        "eShaderReadOnlyOptimal nakon uploada");

    PipelineConfig unormPost;
    unormPost.vertexBindings.clear();
    unormPost.vertexAttributes.clear();
    unormPost.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    unormPost.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.vert.spv";
    unormPost.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.frag.spv";
    unormPost.cullMode = vk::CullModeFlagBits::eNone;
    unormPost.colorFormat = vk::Format::eR8G8B8A8Unorm;  //not the swapchain's format
    unormPost.pushConstantSize = 0;                      //and no push constant range at all
    VulkanGraphicsPipeline unormPipeline = loom.createPipeline(unormPost);

    PostData neutral{{1.0f,1.0f,1.0f,1.0f},{0,0,0,0}};
    Material payload(loom.device, loom.command, loom.getDescriptorPool(),
                     unormPipeline, texture.getSampled(), &neutral, sizeof(neutral));

    RenderTargetConfig unormTarget;
    unormTarget.colorFormat = vk::Format::eR8G8B8A8Unorm;
    unormTarget.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    unormTarget.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    RenderTarget target(loom.device, extent, unormTarget);

    RenderTarget source(loom.device, vk::Extent2D{128,128});
    Material fromTarget(loom.device, loom.command, loom.getDescriptorPool(), unormPipeline, source.getSampled());
    RenderTarget resizeOut(loom.device, extent, unormTarget);

    Mesh triangle(loom.device, loom.command,
        std::vector<Vertex>{{{0,0,0},{1,1,1},{0,0},{0,0,1}},
                            {{1,0,0},{1,1,1},{1,0},{0,0,1}},
                            {{0,1,0},{1,1,1},{0,1},{0,0,1}}},
        std::vector<uint16_t>{0,1,2});

    bool drawRefused = false;
    int drawn = 0;
    while(drawn < 1 && !loom.shouldClose()){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.beginPass(target);
        loom.renderer.drawFullscreen(payload);
        try{
            //this pipeline has no room for ObjectData, and says so instead of corrupting a push
            loom.renderer.draw(triangle, glm::mat4(1.0f), payload);
        }
        catch(const std::exception&){ drawRefused = true; }
        loom.renderer.endPass();

        loom.renderer.beginPass();
        loom.renderer.endPass();
        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const std::vector<uint8_t> copied = target.readPixels(loom.command).pixels;
    const ByteDiff unormPath = diffBytes(pixels, copied);
    report.check("unorm put je tocan", unormPath.different == 0,
        fmt("unorm tekstura -> unorm cilj, %zu razlicitih od %zu bajtova", unormPath.different, pixels.size()));

    report.check("push range", drawRefused, "draw s meshom na pipelineu bez rangea baca");

    PostData tinted{{1.0f, 0.5f, 0.0f, 1.0f},{0,0,0,0}};
    payload.setData(&tinted, sizeof(tinted));

    bool sizeRefused = false;
    try{
        float tooSmall = 1.0f;
        payload.setData(&tooSmall, sizeof(tooSmall));
    }
    catch(const std::exception&){ sizeRefused = true; }

    //resize the source while a material is pointing at it
    source.resize(extent);

    drawn = 0;
    while(drawn < 2 && !loom.shouldClose()){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.beginPass(target);
        loom.renderer.drawFullscreen(payload);
        loom.renderer.endPass();

        loom.renderer.beginPass(source);
        loom.renderer.endPass();

        loom.renderer.beginPass(resizeOut);
        loom.renderer.drawFullscreen(fromTarget);
        loom.renderer.endPass();

        loom.renderer.beginPass();
        loom.renderer.endPass();
        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const std::vector<uint8_t> tintedBack = target.readPixels(loom.command).pixels;
    size_t tintWrong = 0;
    for(size_t p = 0; p + 3 < tintedBack.size(); p += 4){
        const uint8_t wantR = pixels[p+0];
        const uint8_t wantG = uint8_t(pixels[p+1] * 0.5f + 0.5f);
        const uint8_t wantB = 0;
        if(abs(int(tintedBack[p+0]) - int(wantR)) > 1) ++tintWrong;
        if(abs(int(tintedBack[p+1]) - int(wantG)) > 1) ++tintWrong;
        if(abs(int(tintedBack[p+2]) - int(wantB)) > 1) ++tintWrong;
    }
    report.check("vlastiti payload", tintWrong == 0 && sizeRefused,
        fmt("struktura od %zu B mnozi boju, kriva velicina %s", sizeof(PostData), sizeRefused ? "baca" : "NE BACA"));

    //the source was cleared to (0, 0.25, 0.75) linear, stored sRGB, sampled back to linear
    //and written raw into a unorm image
    const std::vector<uint8_t> resized = resizeOut.readPixels(loom.command).pixels;
    const uint8_t wantR = 0, wantG = uint8_t(0.25 * 255.0 + 0.5), wantB = uint8_t(0.75 * 255.0 + 0.5);
    size_t resizeWrong = 0;
    for(size_t i = 0; i + 3 < resized.size(); i += 4){
        if(abs(int(resized[i+0]) - int(wantR)) > 2) ++resizeWrong;
        if(abs(int(resized[i+1]) - int(wantG)) > 2) ++resizeWrong;
        if(abs(int(resized[i+2]) - int(wantB)) > 2) ++resizeWrong;
    }
    report.check("resize pod materijalom", resizeWrong == 0,
        fmt("cilj 128->64, ocekivano RGB %u,%u,%u, dobiveno %u,%u,%u",
            wantR, wantG, wantB, resized[0], resized[1], resized[2]));

    report.checkNoValidationMessages();
    return report.result();
}
