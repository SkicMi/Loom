// factories: the four things a Material needed that it could only ever have got from one
//            place are not arguments any more.
//
// Material took device, command, pool, pipeline, image and data. Four of those six come out
// of the same LoomInitializer, the caller copied them across every time, and had no choice
// about any of them - which is the definition of an argument that should not exist.
//
// What has to be proved is that nothing else changed: the short way and the long way have to
// build the same object, and the only honest way to compare two materials is to draw with them.
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

const uint32_t size = 256;

glm::mat4 model(){
    return glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0.3f,1.0f,0.15f));
}

}

int main(){
    TestReport report("factories");

    LoomConfig config;
    config.width = size; config.height = size;
    config.appName = "factories"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = true;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    CameraConfig cameraConfig;
    cameraConfig.position = {1.8f, 1.4f, 2.4f};
    cameraConfig.target = {0.0f, 0.0f, 0.0f};
    Camera camera(cameraConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {0.2f,0.2f,0.2f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig lightConfig;
    lightConfig.type = LightType::Directional;
    lightConfig.direction = {-0.3f,-1.0f,-0.4f};
    Light light(lightConfig);
    loom.renderer.addLight(light);

    PipelineConfig texturedConfig = config.pipelineConfig;
    texturedConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    texturedConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.vert.spv";
    texturedConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/textured.frag.spv";
    VulkanGraphicsPipeline texturedPipeline = loom.createPipeline(texturedConfig);

    const std::vector<uint8_t> checker = makeCheckerboard(64, 8);

    // -------------------------------------------------------------------------------
    // Kratki i dugi put moraju dati isti objekt
    // -------------------------------------------------------------------------------

    Texture theLongWay(loom.device, loom.command, checker.data(), vk::Extent2D{64,64});
    Texture theShortWay = loom.createTexture(checker.data(), vk::Extent2D{64,64});

    report.check("createTexture", theShortWay.getMipLevels() == theLongWay.getMipLevels() &&
                                  theShortWay.getExtent() == theLongWay.getExtent(),
        fmt("%u razina, %ux%u", theShortWay.getMipLevels(),
            theShortWay.getExtent().width, theShortWay.getExtent().height));

    Mesh meshLongWay(loom.device, loom.command, cubeVertices(), cubeIndices());
    Mesh meshShortWay = loom.createMesh(cubeVertices(), cubeIndices());

    report.check("createMesh", meshShortWay.getIndexCount() == meshLongWay.getIndexCount() &&
                               meshShortWay.getVertexCount() == meshLongWay.getVertexCount(),
        fmt("%u vrhova, %u indeksa", meshShortWay.getVertexCount(), meshShortWay.getIndexCount()));

    MaterialData data;
    data.baseColor = {0.9f, 0.6f, 0.3f, 1.0f};
    data.shininess = 48.0f;

    Material materialLongWay(loom.device, loom.command, loom.getDescriptorPool(),
        texturedPipeline, theLongWay.getSampled(), data);
    Material materialShortWay = loom.createMaterial(texturedPipeline, theShortWay.getSampled(), data);

    // -------------------------------------------------------------------------------
    // A jedini posten nacin da se dva materijala usporede je nacrtati s njima
    // -------------------------------------------------------------------------------

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;

    auto render = [&](const Mesh& mesh, const Material& material){
        RenderTarget out(loom.device, vk::Extent2D{size,size}, readConfig);
        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(out);
            loom.renderer.draw(mesh, model(), material);
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return out.readPixels(loom.command).pixels;
    };

    const std::vector<uint8_t> longWay = render(meshLongWay, materialLongWay);
    const std::vector<uint8_t> shortWay = render(meshShortWay, materialShortWay);

    report.check("nesto je nacrtano", countNonBlack(longWay) > 3000,
        fmt("%zu ne-crnih piksela", countNonBlack(longWay)));

    const ByteDiff difference = diffBytes(longWay, shortWay);
    report.check("kratki put = dugi put", difference.different == 0,
        fmt("%zu razlicitih od %zu bajtova", difference.different, longWay.size()));

    // -------------------------------------------------------------------------------
    // Kontrola: da usporedba iznad moze pasti
    // -------------------------------------------------------------------------------

    MaterialData other = data;
    other.baseColor = {0.2f, 0.9f, 0.4f, 1.0f};
    Material different = loom.createMaterial(texturedPipeline, theShortWay.getSampled(), other);

    report.check("kontrola", diffBytes(longWay, render(meshShortWay, different)).different > 0,
        "drugi baseColor daje drugu sliku, dakle usporedba nije trivijalna");

    // -------------------------------------------------------------------------------
    // I preopterecenja koja ne primaju teksturu
    // -------------------------------------------------------------------------------

    //A pipeline that declares only the data binding: the renderer's own has no descriptor
    //set at all, and a material without one is what the pipeline-only constructor is for
    PipelineConfig dataOnlyConfig = config.pipelineConfig;
    dataOnlyConfig.descriptorBindings = {Material::getDataLayoutBinding()};
    VulkanGraphicsPipeline dataOnlyPipeline = loom.createPipeline(dataOnlyConfig);

    Material noTexture = loom.createMaterial(dataOnlyPipeline, data);
    report.check("materijal bez teksture", noTexture.hasDescriptorSet(),
        "createMaterial bez slike gradi materijal s deskriptorskim setom");

    const std::vector<uint8_t> payloadBytes(sizeof(MaterialData), 0x7f);
    Material rawPayload = loom.createMaterial(texturedPipeline, theShortWay.getSampled(),
        payloadBytes.data(), payloadBytes.size());
    report.check("proizvoljan payload", rawPayload.getDataSize() == payloadBytes.size(),
        fmt("%zu bajtova payloada", rawPayload.getDataSize()));

    report.checkNoValidationMessages();
    return report.result();
}
