// Kada svjetlo pocinje vrijediti.
//
// Renderer skuplja svjetla i salje ih na karticu UNUTAR beginFrame. Pomak nakon toga zato ne
// dira kadar koji je vec zapocet nego tek sljedeci. To je posve razumno ponasanje - frame se
// mora negdje "zamrznuti" - ali je do ovog testa bilo zapisano samo u komentaru, i kostalo je:
// LoomApp je svjetlo postavljao nakon beginFrame, pa je --save, koji snima PRVI i jedini
// kadar, davao bit-identicnu sliku za svaki kut svjetla. Tri kuta, tri ista md5.
//
// U prozoru se to ne vidi jer animacija samo kasni jedan frame. Zato pravilo mora stajati kao
// mjerenje, a ne kao recenica koju netko procita ako bas naide na nju.
#include "TestHarness.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Camera.h"
#include "Core/Light.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/RenderTarget.h"

#include <cstring>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace{

const uint32_t sceneWidth = 320;
const uint32_t sceneHeight = 240;
const vk::Format sceneFormat = vk::Format::eR32G32B32A32Sfloat;

//Dvije strane iste scene. Tockasto svjetlo blizu plohe pada s kvadratom udaljenosti, pa se
//lijevo i desno moraju bitno razlikovati - inace test ne bi mjerio nista
const glm::vec3 leftSide{-0.9f, 0.35f, -0.9f};
const glm::vec3 rightSide{0.9f, 0.35f, -0.9f};

std::vector<Vertex> quad(float half){
    const glm::vec3 white{1.0f, 1.0f, 1.0f};
    return {
        {{-half, -half, 0.0f}, white, {0,0}, {0,0,1}},
        {{ half, -half, 0.0f}, white, {1,0}, {0,0,1}},
        {{ half,  half, 0.0f}, white, {1,1}, {0,0,1}},
        {{-half,  half, 0.0f}, white, {0,1}, {0,0,1}},
    };
}

size_t differing(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b){
    size_t count = 0;
    for(size_t i = 0; i < a.size(); i += 4 * sizeof(float)){
        if(std::memcmp(a.data() + i, b.data() + i, 4 * sizeof(float)) != 0) ++count;
    }
    return count;
}

}

int main(){
    TestReport report("svjetlo i beginFrame");

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 0.0f};
    cameraConfig.target = {0.0f, 0.0f, -1.0f};
    cameraConfig.fovY = glm::radians(50.0f);
    cameraConfig.nearPlane = 0.05f;
    cameraConfig.farPlane = 50.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "light timing"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = false;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.colorFormat = sceneFormat;
    config.headlessColorFormat = sceneFormat;

    LoomInitializer loom(config);
    loom.renderer.setCamera(camera);

    //Bez ambijenta: sve sto se vidi dolazi od svjetla, pa svaka razlika u slici govori o
    //svjetlu i ni o cemu drugom
    EnvironmentConfig dark;
    dark.ambientColor = {0.0f, 0.0f, 0.0f};
    Environment environment(dark);
    loom.renderer.setEnvironment(environment);

    LightConfig bulbConfig;
    bulbConfig.type = LightType::Point;
    bulbConfig.position = rightSide;
    bulbConfig.color = {1.0f, 1.0f, 1.0f};
    bulbConfig.intensity = 6.0f;
    bulbConfig.range = 12.0f;
    Light bulb(bulbConfig);
    loom.renderer.addLight(bulb);

    Mesh panel(loom.device, loom.command, quad(1.2f), std::vector<uint16_t>{0,1,2, 2,3,0});
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -1.6f});

    RenderTargetConfig targetConfig;
    targetConfig.colorFormat = sceneFormat;
    targetConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    targetConfig.enableDepth = false;
    RenderTarget target(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, targetConfig);

    //Jedan kadar. afterBegin se izvrsi TEK kad je frame vec zapocet - u tome je cijeli test
    auto render = [&](const std::function<void()>& afterBegin = {}){
        if(loom.renderer.beginFrame()){
            if(afterBegin) afterBegin();

            loom.renderer.beginPass(target);
            loom.renderer.draw(panel, model);
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return target.readPixels(loom.command).pixels;
    };

    // -------------------------------------------------------------------------------
    // Dvije strane, obje postavljene kako treba - referenca protiv koje se mjeri
    // -------------------------------------------------------------------------------

    bulb.setPosition(leftSide);
    const std::vector<uint8_t> fromLeft = render();

    bulb.setPosition(rightSide);
    const std::vector<uint8_t> fromRight = render();

    const size_t sides = differing(fromLeft, fromRight);

    report.check("strana svjetla uopce mijenja sliku",
        sides > 10000,
        fmt("%zu od %u piksela se razlikuje kad svjetlo prijede s lijeva na desno",
            sides, sceneWidth * sceneHeight));

    // -------------------------------------------------------------------------------
    // Pomak NAKON beginFrame ne dira taj kadar
    // -------------------------------------------------------------------------------

    //Svjetlo je desno. Zapocinjemo kadar, pa ga tek onda micemo lijevo - i kadar mora ispasti
    //bit-identican onome s desnim svjetlom, jer je ono vec poslano na karticu
    const std::vector<uint8_t> moved = render([&](){ bulb.setPosition(leftSide); });

    report.check("pomak nakon beginFrame ne dira taj kadar",
        differing(moved, fromRight) == 0,
        fmt("%zu piksela razlike prema kadru sa starim polozajem", differing(moved, fromRight)));

    // -------------------------------------------------------------------------------
    // ...ali vrijedi od sljedeceg
    // -------------------------------------------------------------------------------

    //Bez ove polovice bi gornja provjera prolazila i da setPosition ne radi uopce nista
    const std::vector<uint8_t> next = render();

    report.check("isti pomak vrijedi od sljedeceg kadra",
        differing(next, fromLeft) == 0 && differing(next, fromRight) == sides,
        fmt("sljedeci kadar je identican lijevom (%zu razlike) i razlicit od desnog (%zu)",
            differing(next, fromLeft), differing(next, fromRight)));

    report.checkNoValidationMessages();
    return report.result();
}
