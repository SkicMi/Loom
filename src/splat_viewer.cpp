// Preglednik gaussian splattinga: .ply s diska, scena u prozoru.
//
// Ovo je STEPENICA 2 i zasad mora biti: Scene iz stepenice 1 crta oblike iz svog reda, a oblak
// od tri cetvrt milijuna gaussiana nije oblik. Kad se pokaze kakav mu oblik stvarno treba, uzet
// ce svoje mjesto gore - isto kako je zapisano za plate u main.cpp.
//
//   ./SplatViewer scena.ply
//   ./SplatViewer scena.ply 4          <- svaki cetvrti splat, kad procesor ne stize
//   ./SplatViewer scena.ply 1 32       <- i velicina pločice
//   ./SplatViewer scena.ply 8 16 60    <- odvrti 60 kadrova, spremi splatview.png i izadji
//
// STO OVDJE JOS NIJE: boja ovisi samo o stupnju 0 sfernih harmonika, dakle ista je iz svih
// smjerova. Scena ce izgledati ispravno ali plosnato - bez odsjaja i bez toga da se povrsina
// mijenja dok kruzis oko nje. To je G4, i namjerno dolazi poslije ovoga.
//
// I PRIPREMA JE NA PROCESORU. Svaki kadar racuna kovarijancu i conic za svaki splat iznova, jer
// oboje ovisi o kameri. Za tri cetvrt milijuna splatova to je vecina vremena kadra; mjeri se i
// ispisuje, pa se vidi tocno koliko. Kad predje na karticu, i broj parova prelazi s njom.
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/SplatRenderer.h"

#include <Spool/GaussianPly.h>
#include <Spool/ImageFile.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace{

//Sredina i polumjer scene, ali po postotcima a ne po krajnostima. Prava scena ima nekoliko
//gaussiana koji su odletjeli daleko od svega ostalog - u train/7000 se x proteze od -62 do 172
//dok je prosjek 1.5 - pa bi kamera postavljena po najvecem i najmanjem gledala prazninu
struct Bounds{
    glm::vec3 centre{0.0f};
    float radius = 1.0f;
};

Bounds robustBounds(const std::vector<Splat>& splats){
    Bounds bounds;
    if(splats.empty()) return bounds;

    std::vector<float> values(splats.size());
    glm::vec3 low(0.0f), high(0.0f);

    for(int axis = 0; axis < 3; ++axis){
        for(size_t i = 0; i < splats.size(); ++i) values[i] = splats[i].position[axis];
        std::sort(values.begin(), values.end());
        //Cetvrtine, ne krajevi. Scena snimljena vani ima pola splatova u nebu i u daljini, pa
        //bi i peti postotak jos uvijek opisivao pozadinu umjesto onoga sto se snimalo
        low[axis]  = values[size_t(0.25 * double(values.size()))];
        high[axis] = values[size_t(0.75 * double(values.size()))];
    }

    bounds.centre = 0.5f * (low + high);
    bounds.radius = 0.5f * glm::length(high - low);
    if(bounds.radius <= 0.0f) bounds.radius = 1.0f;
    return bounds;
}

}

int main(int argc, char** argv){
    if(argc < 2){
        printf("Upotreba: SplatViewer <scena.ply> [korak] [velicina pločice]\n");
        return 1;
    }

    const std::string path = argv[1];
    const uint32_t stride = argc > 2 ? uint32_t(std::atoi(argv[2])) : 1;
    const uint32_t tileSize = argc > 3 ? uint32_t(std::atoi(argv[3])) : 16;

    //Koliko kadrova pa snimka i kraj. Nula znaci "vrti dok ne zatvorim" - snimka postoji da se
    //slika moze pogledati bez gledanja, jer "vrti se" i "tocno je" nisu ista tvrdnja
    const uint32_t framesThenShot = argc > 4 ? uint32_t(std::atoi(argv[4])) : 0;

    // -------------------------------------------------------------------------------
    // S diska u splatove
    // -------------------------------------------------------------------------------

    printf("Citam %s ...\n", path.c_str());
    auto started = std::chrono::steady_clock::now();

    const Spool::GaussianCloud cloud = Spool::loadGaussianPly(path);

    auto elapsed = [&started]{
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        started = std::chrono::steady_clock::now();
        return seconds;
    };

    printf("  %zu gaussiana, stupanj %u, za %.2f s\n", cloud.count(), cloud.shDegree, elapsed());

    //AKTIVACIJA: iz onoga sto u fileu pise u ono sto se crta. Vidi Core/Splat.h zasto to nije
    //napravio Spool
    std::vector<Splat> splats;
    splats.reserve(cloud.count() / stride + 1);

    for(size_t i = 0; i < cloud.count(); i += stride){
        const Spool::Gaussian& source = cloud.gaussians[i];

        Splat splat;
        splat.position = glm::vec3(source.position[0], source.position[1], source.position[2]);
        splat.scale    = SplatMath::activateScale(glm::vec3(source.scale[0], source.scale[1], source.scale[2]));
        splat.rotation = SplatMath::activateRotation(glm::vec4(source.rotation[0], source.rotation[1],
                                                              source.rotation[2], source.rotation[3]));
        splat.opacity  = SplatMath::activateOpacity(source.opacity);
        splat.color    = SplatMath::colorFromSH0(glm::vec3(source.dc[0], source.dc[1], source.dc[2]));
        splats.push_back(splat);
    }

    const Bounds bounds = robustBounds(splats);
    printf("  %zu splatova nakon koraka %u; sredina %.2f %.2f %.2f, polumjer %.2f\n",
           splats.size(), stride, double(bounds.centre.x), double(bounds.centre.y),
           double(bounds.centre.z), double(bounds.radius));

    // -------------------------------------------------------------------------------
    // Prozor, meta i rasterizator
    // -------------------------------------------------------------------------------

    LoomConfig config;
    config.width = 1280; config.height = 720;
    config.appName = "Loom splat viewer"; config.engineName = "Loom";
    config.headless = false;
    config.enableDepth = false;

    //Fullscreen prolaz koji sliku prenosi na ekran nema vertex buffer ni dubinu
    config.pipelineConfig.vertexBindings.clear();
    config.pipelineConfig.vertexAttributes.clear();
    config.pipelineConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    config.pipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/splat_present.vert.spv";
    config.pipelineConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/splat_present.frag.spv";
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.depthTestEnable = false;
    config.pipelineConfig.depthWriteEnable = false;

    LoomInitializer loom(config);
    const vk::Extent2D size = loom.getExtent();

    //Meta u koju rasterizator pise, i s koje fullscreen prolaz cita. Float, jer je to ono sto
    //kompozicija stvarno racuna - pretvorbu u osam bita napravi tek swapchain
    RenderTargetConfig targetConfig;
    targetConfig.colorFormat = vk::Format::eR32G32B32A32Sfloat;
    targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
    targetConfig.enableDepth = false;
    targetConfig.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    RenderTarget splatTarget(loom.device, size, targetConfig);

    Material present(loom.device, loom.command, loom.getDescriptorPool(),
                     loom.vulkanGraphicsPipeline, splatTarget.getSampled());

    SplatRendererConfig rendererConfig;
    rendererConfig.tileSize = tileSize;
    rendererConfig.maxSplats = uint32_t(splats.size());
    rendererConfig.maxPairs = 16u << 20;

    SplatRenderer splatRenderer(loom.device, loom.getDescriptorPool(),
                                splatTarget.getColorImage(), size, rendererConfig);

    printf("  pločica %ux%u, mreza %ux%u\n", tileSize, tileSize,
           splatRenderer.getGrid().width, splatRenderer.getGrid().height);

    // -------------------------------------------------------------------------------
    // Kamera: kruzi oko scene
    // -------------------------------------------------------------------------------

    CameraConfig cameraConfig;
    cameraConfig.fovY = glm::radians(60.0f);
    cameraConfig.nearPlane = 0.01f;
    cameraConfig.farPlane = 1000.0f;
    cameraConfig.target = bounds.centre;
    cameraConfig.up = glm::vec3(0.0f, -1.0f, 0.0f);   //3DGS scene dolaze s Y prema dolje
    Camera camera(cameraConfig);

    float angle = 0.0f;
    float distance = 1.3f * bounds.radius;
    float height = 0.2f * bounds.radius;

    std::vector<SplatMath::PreparedSplat> prepared;
    prepared.reserve(splats.size());

    printf("\nStrelice: kruzenje i visina.  W/S: blize i dalje.  ESC: kraj.\n\n");

    GLFWwindow* window = loom.window->getWindow();
    double lastReport = loom.getTime();
    int framesSinceReport = 0;
    uint32_t totalFrames = 0;
    double prepareSeconds = 0.0;

    while(!loom.shouldClose()){
        loom.pollEvents();

        if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        if(glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) angle -= 0.02f;
        if(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) angle += 0.02f;
        if(glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) height += 0.03f * bounds.radius;
        if(glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) height -= 0.03f * bounds.radius;
        if(glfwGetKey(window, GLFW_KEY_W)     == GLFW_PRESS) distance *= 0.97f;
        if(glfwGetKey(window, GLFW_KEY_S)     == GLFW_PRESS) distance *= 1.03f;

        //Bez tipke se i dalje polako okrece, da se odmah vidi da je scena prostorna
        angle += 0.002f;

        cameraConfig.position = bounds.centre + glm::vec3(distance * std::sin(angle), height,
                                                          distance * std::cos(angle));
        cameraConfig.target = bounds.centre;
        camera = Camera(cameraConfig);

        const glm::mat4 view = camera.getView();
        const glm::mat4 projection = camera.getProjection(size.width, size.height);
        const CameraIntrinsics intrinsics = CameraIntrinsics::fromProjection(projection, size.width, size.height);

        //PRIPREMA, i ovo je vecina kadra. Kovarijanca i conic ovise o kameri, pa se racunaju
        //iznova cim se ona pomakne
        const auto prepareStart = std::chrono::steady_clock::now();

        prepared.clear();
        for(const Splat& splat : splats){
            SplatMath::PreparedSplat one;
            if(SplatMath::prepare(splat, view, intrinsics.fx, intrinsics.fy,
                                  intrinsics.cx, intrinsics.cy, 0.3f, one)){
                prepared.push_back(one);
            }
        }

        //Sprijeda natrag. Sort na kartici to radi po pločici, ali pomaci u polju parova moraju
        //postojati prije nego se ista sortira - a broj parova racuna procesor
        std::sort(prepared.begin(), prepared.end(),
            [](const SplatMath::PreparedSplat& a, const SplatMath::PreparedSplat& b){
                return a.conicOpacityDepth.z < b.conicOpacityDepth.z;
            });

        prepareSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - prepareStart).count();

        splatRenderer.upload(prepared);
        const uint32_t pairCount = splatRenderer.countPairs(prepared);

        if(!loom.renderer.beginFrame()) continue;

        splatRenderer.draw(loom.renderer, uint32_t(prepared.size()), pairCount);

        loom.renderer.beginPass();
        loom.renderer.drawFullscreen(present);
        loom.renderer.endPass();

        loom.renderer.endFrame();

        ++framesSinceReport;
        ++totalFrames;

        if(framesThenShot > 0 && totalFrames >= framesThenShot){
            loom.waitIdle();
            const ImageData shot = loom.renderer.readLastFrame();

            //readLastFrame vraca RGBA; imageFromPixels to samo omota
            const Spool::Image image = Spool::imageFromPixels(shot.pixels.data(),
                shot.extent.width, shot.extent.height, Spool::ChannelOrder::RGBA);
            Spool::saveImage("splatview.png", image);

            printf("\nSnimljeno splatview.png (%ux%u)\n", image.width, image.height);
            break;
        }

        const double now = loom.getTime();
        if(now - lastReport > 1.0){
            printf("  %.1f kadrova/s   %zu splatova vidljivo, %u parova   priprema %.0f %% kadra\n",
                   framesSinceReport / (now - lastReport), prepared.size(), pairCount,
                   100.0 * prepareSeconds / (now - lastReport));
            fflush(stdout);
            lastReport = now;
            framesSinceReport = 0;
            prepareSeconds = 0.0;
        }
    }

    loom.waitIdle();
    return 0;
}
