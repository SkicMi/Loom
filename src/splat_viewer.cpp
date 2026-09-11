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
// BOJA OVISI O SMJERU POGLEDA (G4): odsjaj na metalu i nebo koje se mijenja dok kamera kruzi.
// Racuna se svaki kadar, jer smjer od kamere do gaussiana je jedino sto se mijenja.
//
// PRIPREMA JE NA KARTICI, i broj parova s njom. Sirovi splatovi odu na karticu jednom; svaki
// kadar procesor posalje samo kameru. Kovarijancu, conic, boju iz smjera i broj parova racuna
// kartica, a velicine dispatcha iz tog broja izvodi sama - procesor po kadru ne cita nista.
// Ispisuje se koliko je parova scena trazila, i vice kad ih je vise nego sto ima mjesta.
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

    //Pocetni kut, u stupnjevima. Postoji da se ista scena moze snimiti iz dva smjera i
    //usporediti - parallaksa i zaklon su jedini dokaz da je ovo prostor a ne slika
    const float startAngle = argc > 5 ? float(std::atof(argv[5])) * 3.14159265f / 180.0f : 0.0f;

    //Ime snimke, da se dvije usporedbe ne prepisu
    const std::string shotName = argc > 6 ? std::string(argv[6]) : std::string("splatview.png");

    //Boja iz smjera pogleda se da ugasiti, i to nije udobnost nego mjerenje: razlika izmedju
    //upaljenog i ugasenog je jedini nacin da se vidi koliko G4 stvarno radi
    const bool useSH = argc > 7 ? (std::atoi(argv[7]) != 0) : true;

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

    //Koji je gaussian u oblaku dao koji splat. Treba jer se boja racuna svaki kadar iz njegovih
    //koeficijenata, a korak preskace - pa redni broj u splats vise nije redni broj u oblaku
    std::vector<uint32_t> sourceIndex;
    sourceIndex.reserve(splats.capacity());

    const uint32_t coeffsPerChannel = cloud.restStride / 3;

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
        sourceIndex.push_back(uint32_t(i));
    }

    const Bounds bounds = robustBounds(splats);
    printf("  %zu splatova nakon koraka %u; sredina %.2f %.2f %.2f, polumjer %.2f\n",
           splats.size(), stride, double(bounds.centre.x), double(bounds.centre.y),
           double(bounds.centre.z), double(bounds.radius));

    if(stride > 1){
        printf("  PAZI: korak %u znaci da se crta svaki %u. gaussian. Scena ce izgledati kao\n"
               "  sum i pruge - to nije greska crtanja nego %u%% podataka koji nedostaju.\n"
               "  Za pravu sliku pokreni bez broja.\n", stride, stride,
               uint32_t(100.0 - 100.0 / double(stride)));
    }

    // -------------------------------------------------------------------------------
    // Prozor, meta i rasterizator
    // -------------------------------------------------------------------------------

    LoomConfig config;
    config.width = 1280; config.height = 720;
    config.appName = "Loom splat viewer"; config.engineName = "Loom";
    config.headless = false;
    config.enableDepth = false;
    //Jedan SplatRenderer trazi 21 set i 71 storage buffer, a default od 64 po tipu to ne
    //daje - tolerantan driver precuti, strog ne
    config.maxDescriptorSets = 128;
    //Vrijeme po koraku s karticinog sata. SplatRenderer stavlja 10 oznaka po kadru
    config.rendererConfig.maxTimestamps = 32;

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
    //Najvise sto stane u najmanju granicu grupa koju Vulkan jamci: 65535 grupa po 256
    rendererConfig.maxPairs = 65535u * 256u;
    rendererConfig.maxShCoefficients = useSH ? coeffsPerChannel * 3 : 0;

    SplatRenderer splatRenderer(loom.device, loom.getDescriptorPool(),
                                splatTarget.getColorImage(), size, rendererConfig);

    //SIROVI SPLATOVI, JEDNOM. Aktivacija je vec napravljena jer ne ovisi o kameri; sve sto ovisi
    //racuna kartica svaki kadar. Koeficijenti se uzimaju preko sourceIndex, jer korak preskace
    const uint32_t splatCount = uint32_t(splats.size());
    {
        std::vector<SplatMath::RawSplat> raw;
        raw.reserve(splats.size());
        std::vector<float> rest;
        if(useSH) rest.reserve(splats.size() * coeffsPerChannel * 3);

        for(size_t i = 0; i < splats.size(); ++i){
            const Splat& splat = splats[i];
            const Spool::Gaussian& source = cloud.gaussians[sourceIndex[i]];

            SplatMath::RawSplat one;
            one.positionOpacity = glm::vec4(splat.position, splat.opacity);
            one.scale = glm::vec4(splat.scale, 0.0f);
            one.rotation = glm::vec4(splat.rotation.w, splat.rotation.x, splat.rotation.y, splat.rotation.z);
            one.dc = glm::vec4(source.dc[0], source.dc[1], source.dc[2], 0.0f);
            raw.push_back(one);

            if(useSH && coeffsPerChannel > 0){
                const float* coefficients = cloud.restFor(sourceIndex[i]);
                rest.insert(rest.end(), coefficients, coefficients + coeffsPerChannel * 3);
            }
        }

        splatRenderer.uploadRaw(raw, rest, useSH ? cloud.shDegree : 0, useSH ? coeffsPerChannel : 0);
        printf("  na karticu: %.0f MB splatova, %.0f MB koeficijenata\n",
               double(raw.size() * sizeof(SplatMath::RawSplat)) / 1048576.0,
               double(rest.size() * sizeof(float)) / 1048576.0);
    }

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

    float angle = startAngle;
    float distance = 1.3f * bounds.radius;
    float height = 0.2f * bounds.radius;

    printf("\nStrelice: kruzenje i visina.  W/S: blize i dalje.  ESC: kraj.\n\n");

    GLFWwindow* window = loom.window->getWindow();
    double lastReport = loom.getTime();
    int framesSinceReport = 0;
    uint32_t totalFrames = 0;
    uint32_t pairsAsked = 0;

    //Zbroj vremena po koraku kroz sekundu, pa se ispise prosjek. Poredak je onaj kojim su
    //oznake zapisane, i ime koraka je ime oznake kojom zavrsava
    std::vector<std::pair<std::string, double>> stageSums;
    uint32_t timedFrames = 0;

    while(!loom.shouldClose()){
        loom.pollEvents();

        if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        if(glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) angle -= 0.02f;
        if(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) angle += 0.02f;
        if(glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) height += 0.03f * bounds.radius;
        if(glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) height -= 0.03f * bounds.radius;
        if(glfwGetKey(window, GLFW_KEY_W)     == GLFW_PRESS) distance *= 0.97f;
        if(glfwGetKey(window, GLFW_KEY_S)     == GLFW_PRESS) distance *= 1.03f;

        //Bez tipke se polako okrece, da se odmah vidi da je scena prostorna. Kad se snima iz
        //zadanog kuta to bi pomaknulo bas ono sto se htjelo usporediti, pa tada miruje
        if(framesThenShot == 0) angle += 0.002f;

        cameraConfig.position = bounds.centre + glm::vec3(distance * std::sin(angle), height,
                                                          distance * std::cos(angle));
        cameraConfig.target = bounds.centre;
        camera = Camera(cameraConfig);

        const glm::mat4 view = camera.getView();
        const glm::mat4 projection = camera.getProjection(size.width, size.height);
        const CameraIntrinsics intrinsics = CameraIntrinsics::fromProjection(projection, size.width, size.height);

        //ČEKA SE PRIJE PISANJA, i to nije opreznost nego nužnost. Loom drži dva kadra u letu, a
        //SplatRenderer ima JEDAN primjerak svakog radnog polja - kameru, splatove, ključeve,
        //poretke, raspone. Kad bi se sljedeći kadar poslao dok prethodni još crta, GPU bi čitao
        //pola jedne a pola druge scene: slika se raspadne u šum koji izgleda kao greška
        //rasterizatora a nije. Dok polja ne postanu po kadru, ovo košta paralelizam kartice i
        //procesora - i to je sad, kad je priprema na kartici, prava cijena
        loom.waitIdle();

        //Prosli kadar je gotov, pa je broj koji je trazio sad tocan
        pairsAsked = splatRenderer.requestedPairs();

        const std::vector<GpuTimestamp> marks = loom.renderer.readFrameTimes();
        if(marks.size() > 1){
            if(stageSums.size() != marks.size() - 1){
                stageSums.clear();
                for(size_t i = 1; i < marks.size(); ++i) stageSums.push_back({marks[i].label, 0.0});
            }
            for(size_t i = 1; i < marks.size(); ++i){
                stageSums[i - 1].second += marks[i].milliseconds - marks[i - 1].milliseconds;
            }
            ++timedFrames;
        }

        splatRenderer.setCamera(view, cameraConfig.position, intrinsics.fx, intrinsics.fy,
                                intrinsics.cx, intrinsics.cy);

        if(!loom.renderer.beginFrame()) continue;

        splatRenderer.prepare(loom.renderer, splatCount);
        splatRenderer.draw(loom.renderer, splatCount);

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
            Spool::saveImage(shotName, image);

            printf("\nSnimljeno %s (%ux%u)\n", shotName.c_str(), image.width, image.height);
            break;
        }

        const double now = loom.getTime();
        if(now - lastReport > 1.0){
            printf("  %.1f kadrova/s   %u splatova, %u parova%s\n",
                   framesSinceReport / (now - lastReport), splatCount, pairsAsked,
                   pairsAsked > rendererConfig.maxPairs ? "   PREMALO MJESTA - dio slike nedostaje (maxPairs)" : "");
            if(timedFrames > 0){
                double total = 0.0;
                for(const auto& stage : stageSums) total += stage.second;
                printf("    kartica %.0f ms:", total / timedFrames);
                for(const auto& stage : stageSums){
                    printf("  %s %.1f", stage.first.c_str(), stage.second / timedFrames);
                }
                printf("\n");
                for(auto& stage : stageSums) stage.second = 0.0;
                timedFrames = 0;
            }
            fflush(stdout);
            lastReport = now;
            framesSinceReport = 0;
        }
    }

    loom.waitIdle();
    return 0;
}
