// mipmaps: a texture that shrinks into the distance stops being noise.
//
// Aliasing is not a filtering problem - by the time a pixel is sampled from a full
// resolution checkerboard covering a tenth of a texel, the information is already gone and
// no filter can put it back. The chain has to exist before the sample happens, which is why
// this is a property of the texture rather than of the sampler.
//
// It is measured as high contrast between neighbouring pixels: a minified checkerboard with
// no chain flickers between its two colours, and with one it settles on their average.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Vulkan/Material.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"

#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 400;
const uint32_t sceneHeight = 300;

//A big flat quad whose texture coordinates run 0 to 32, so a repeating texture tiles across
//it. Scaling a plane whose uvs stop at 1 would only stretch one copy, and a stretched
//texture is magnified rather than minified - the opposite of what is being tested
std::vector<Vertex> tiledFloor(float half, float tiles){
    return {
        {{-half,0.0f,-half},{1,1,1},{0.0f, 0.0f},{0,1,0}},
        {{ half,0.0f,-half},{1,1,1},{tiles,0.0f},{0,1,0}},
        {{ half,0.0f, half},{1,1,1},{tiles,tiles},{0,1,0}},
        {{-half,0.0f, half},{1,1,1},{0.0f, tiles},{0,1,0}},
    };
}

//How many side by side pixels disagree sharply. On a checkerboard that has shrunk below one
//texel per pixel this is the whole picture; on one that has been mipped it is the few real
//edges that are left
size_t harshNeighbours(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, int threshold = 60){
    size_t count = 0;
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x + 1 < width; ++x){
            const int left = pixels[(size_t(y) * width + x) * 4];
            const int right = pixels[(size_t(y) * width + x + 1) * 4];
            if(std::abs(left - right) > threshold) ++count;
        }
    }
    return count;
}

std::vector<uint8_t> renderFloor(bool withMipmaps, uint32_t* levelsOut){
    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "mipmaps"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = true;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;

    LoomInitializer loom(config);

    //Low and looking along the floor, so it runs away to the horizon. That is where a texel
    //stops covering a pixel and the chain starts earning its keep
    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.55f, 3.0f};
    cameraConfig.target = {0.0f, 0.35f, -30.0f};
    Camera camera(cameraConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {1.0f, 1.0f, 1.0f};   //flat, so what shows is the texture
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    PipelineConfig texturedConfig = config.pipelineConfig;
    texturedConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    texturedConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.vert.spv";
    texturedConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.frag.spv";
    VulkanGraphicsPipeline texturedPipeline = loom.createPipeline(texturedConfig);

    const std::vector<uint8_t> checker = makeCheckerboard(64, 8);

    TextureConfig textureConfig;
    textureConfig.generateMipmaps = withMipmaps;
    Texture texture(loom.device, loom.command, checker.data(), vk::Extent2D{64,64}, textureConfig);

    if(levelsOut) *levelsOut = texture.getMipLevels();

    Material material(loom.device, loom.command, loom.getDescriptorPool(), texturedPipeline, texture.getSampled());
    Mesh floor(loom.device, loom.command, tiledFloor(60.0f, 32.0f), {0,1,2, 2,3,0});

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    RenderTarget out(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, readConfig);

    if(loom.renderer.beginFrame()){
        loom.renderer.beginPass(out);
        loom.renderer.draw(floor, glm::mat4(1.0f), material);
        loom.renderer.endPass();
        loom.renderer.endFrame();
    }
    loom.waitIdle();

    return out.readPixels(loom.command).pixels;
}

}

int main(){
    TestReport report("mipmaps");

    // -------------------------------------------------------------------------------
    // Koliko razina, i zasto bas toliko
    // -------------------------------------------------------------------------------

    report.check("broj razina",
        mipLevelsFor({64,64}) == 7 && mipLevelsFor({1,1}) == 1 &&
        mipLevelsFor({256,64}) == 9 && mipLevelsFor({5,3}) == 3,
        fmt("64x64 -> %u, 256x64 -> %u, 5x3 -> %u, 1x1 -> %u",
            mipLevelsFor({64,64}), mipLevelsFor({256,64}), mipLevelsFor({5,3}), mipLevelsFor({1,1})));

    uint32_t withLevels = 0;
    uint32_t withoutLevels = 0;

    const std::vector<uint8_t> mipped = renderFloor(true, &withLevels);
    const std::vector<uint8_t> raw = renderFloor(false, &withoutLevels);

    report.check("tekstura ih ima", withLevels == 7 && withoutLevels == 1,
        fmt("s mipmapama %u razina, bez njih %u", withLevels, withoutLevels));

    // -------------------------------------------------------------------------------
    // I da stvarno rade
    // -------------------------------------------------------------------------------

    //And the two pictures really are different pictures, so the counts above are not two
    //measurements of the same render
    const ByteDiff difference = diffBytes(mipped, raw);
    report.check("dvije razlicite slike", difference.different > 0,
        fmt("%zu razlicitih bajtova", difference.different));

    // -------------------------------------------------------------------------------
    // Ono sto se NE smije promijeniti: blizina
    // -------------------------------------------------------------------------------

    //A mip chain must not blur what is close to the camera. The bottom rows of this scene are
    //the floor a metre away, where one texel still covers several pixels and the top level is
    //the one that gets sampled either way
    auto harshInRows = [&](const std::vector<uint8_t>& pixels, uint32_t fromRow, uint32_t toRow){
        size_t count = 0;
        for(uint32_t y = fromRow; y < toRow; ++y){
            for(uint32_t x = 0; x + 1 < sceneWidth; ++x){
                const int left = pixels[(size_t(y) * sceneWidth + x) * 4];
                const int right = pixels[(size_t(y) * sceneWidth + x + 1) * 4];
                if(std::abs(left - right) > 60) ++count;
            }
        }
        return count;
    };

    const size_t harshWithout = harshNeighbours(raw, sceneWidth, sceneHeight);
    const size_t harshWith = harshNeighbours(mipped, sceneWidth, sceneHeight);

    report.check("mipmape miču aliasing", harshWith * 2 < harshWithout,
        fmt("%zu ostrih susjeda s lancem vs %zu bez njega, %.0f%% manje",
            harshWith, harshWithout, 100.0 * (1.0 - double(harshWith) / double(harshWithout))));

    const uint32_t nearFrom = sceneHeight - sceneHeight / 5;
    const size_t nearWith = harshInRows(mipped, nearFrom, sceneHeight);
    const size_t nearWithout = harshInRows(raw, nearFrom, sceneHeight);

    report.check("blizina ostaje ostra", nearWith > nearWithout / 2,
        fmt("u zadnjih %u redova: %zu ostrih s lancem, %zu bez njega",
            sceneHeight - nearFrom, nearWith, nearWithout));

    //And the control, without inventing a number to clear: without a chain most of the hard
    //edges are NOT where hard edges belong. Real detail lives near the camera; everything
    //beyond that is a texel too small to be sampled once and standing in for many
    report.check("aliasing je u daljini", harshWithout > nearWithout * 5,
        fmt("bez lanca %zu ostrih ukupno, a samo %zu u blizini gdje im je mjesto",
            harshWithout, nearWithout));

    report.checkNoValidationMessages();
    return report.result();
}
