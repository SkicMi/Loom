// Plate: snimka ili slika u prozoru, i svjetlo ubaceno u nju.
//
// Ovo je STEPENICA 2, i to namjerno.
//
// Stepenica 1 danas nema gdje drzati snimku. Scene crta oblike iz svog reda i sama vodi svoje
// prolaze; plate nije oblik nego cijeli kadar ispod svega, pa se u taj red ne uklapa. Ta je
// rupa poznata i zatvorit ce se kad se pokaze kakav oblik snimci stvarno treba - do tada je
// ovo posteniji zapis od API-ja izmisljenog unaprijed.
//
// Prijasnja scena s cetiri oblika i sjenama zivi dalje u examples/lit3d.
//
//   ./LoomApp snimka.mp4
//   ./LoomApp slika.png dubina.pfm            <- svjetlo se ubacuje u sliku
//   ./LoomApp slika.png dubina.pfm 1.5 20     <- najblize 1.5 m, najdalje 20 m
#include "Core/LoomInitializer.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Core/Light.h"
#include "Vulkan/Material.h"
#include "Vulkan/NormalMap.h"
#include "Vulkan/PositionMap.h"
#include "Vulkan/Relight.h"
#include "Vulkan/StreamingTexture.h"
#include "Vulkan/Texture.h"

#include <Spool/DepthFile.h>
#include <Spool/ImageFile.h>
#include <Spool/VideoFile.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <thread>

namespace{

//Snimka od 4K ne stane na ekran, a fullscreen prolaz je ionako semplira po uv-u - pa se
//prozor smanji, a ne slika
vk::Extent2D windowSizeFor(uint32_t width, uint32_t height){
    const uint32_t maxWidth = 1600;
    const uint32_t maxHeight = 900;

    double scale = 1.0;
    if(width > maxWidth) scale = std::min(scale, double(maxWidth) / double(width));
    if(height > maxHeight) scale = std::min(scale, double(maxHeight) / double(height));

    return vk::Extent2D{
        std::max(1u, uint32_t(double(width) * scale)),
        std::max(1u, uint32_t(double(height) * scale))
    };
}

bool looksLikeVideo(const std::string& path){
    static const char* endings[] = {".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".mts"};
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return char(std::tolower(c)); });

    for(const char* ending : endings){
        if(lower.size() > std::strlen(ending) &&
           lower.compare(lower.size() - std::strlen(ending), std::strlen(ending), ending) == 0){
            return true;
        }
    }
    return false;
}

}

int main(int argc, char** argv){
    if(argc < 2){
        printf("Loom - snimka u prozoru, i svjetlo ubaceno u nju\n\n"
               "  %s <snimka|slika> [dubina.pfm] [najblize_m] [najdalje_m]\n\n"
               "Bez karte dubine snimka se samo prikazuje.\n"
               "S njom se u nju ubacuje svjetlo koje kruzi oko scene.\n", argv[0]);
        return 1;
    }

    try{
        const std::string platePath = argv[1];
        const std::string depthPath = argc > 2 ? argv[2] : std::string();

        //Raspon je jedina stvar koju model ne moze znati: karta kaze sto je blize a sto dalje,
        //ne koliko je to u metrima. Bez metara inverzni kvadrat nema smisla
        const float nearDistance = argc > 3 ? std::stof(argv[3]) : 1.5f;
        const float farDistance = argc > 4 ? std::stof(argv[4]) : 20.0f;

        // -- sto crtamo --------------------------------------------------------------------

        std::unique_ptr<Spool::VideoReader> video;
        Spool::Image still;
        uint32_t plateWidth = 0, plateHeight = 0;
        double frameRate = 25.0;

        if(looksLikeVideo(platePath)){
            video = std::make_unique<Spool::VideoReader>(platePath);
            plateWidth = video->info().width;
            plateHeight = video->info().height;
            frameRate = video->info().frameRate() > 0.0 ? video->info().frameRate() : 25.0;

            printf("%s\n  %ux%u, %.3f slika/s, %s\n", platePath.c_str(),
                   plateWidth, plateHeight, frameRate, video->info().codec.c_str());
        }
        else{
            still = Spool::loadImage(platePath);
            plateWidth = still.width;
            plateHeight = still.height;
            printf("%s\n  %ux%u\n", platePath.c_str(), plateWidth, plateHeight);
        }

        Spool::DepthImage depth;
        if(!depthPath.empty()){
            depth = Spool::loadDepthImage(depthPath);
            printf("%s\n  %ux%u, %u bita, vrijednosti od %.4f do %.4f\n",
                   depthPath.c_str(), depth.width, depth.height, depth.sourceBits,
                   depth.minValue, depth.maxValue);

            if(depth.width != plateWidth || depth.height != plateHeight){
                printf("\nKarta dubine je %ux%u, a slika %ux%u - moraju biti iste.\n",
                       depth.width, depth.height, plateWidth, plateHeight);
                return 1;
            }
            printf("  raspon: %.2f m do %.2f m\n", nearDistance, farDistance);
        }

        // -- Loom --------------------------------------------------------------------------

        const vk::Extent2D windowSize = windowSizeFor(plateWidth, plateHeight);
        const vk::Extent2D plateSize{plateWidth, plateHeight};

        LoomConfig config;
        config.width = windowSize.width;
        config.height = windowSize.height;
        config.appName = "Loom";
        config.engineName = "Loom";
        config.enableDepth = false;   //plate je jedan crtez preko cijelog kadra
        config.swapchainConfig.preferredPresentMode = vk::PresentModeKHR::eMailbox;

        LoomInitializer loom(config);

        //Kamera kroz koju je slika snimljena. Bez prave zarisne duljine ovo je procjena, i
        //ravno je onome sto model za dubinu ionako ne zna - ali mora biti ISTA kamera kroz
        //koju se poslije odprojicira, inace scena ne odgovara slici
        CameraConfig cameraConfig;
        cameraConfig.position = {0.0f, 0.0f, 0.0f};
        cameraConfig.target = {0.0f, 0.0f, -1.0f};
        cameraConfig.fovY = glm::radians(50.0f);
        cameraConfig.nearPlane = 0.05f;
        cameraConfig.farPlane = std::max(200.0f, farDistance * 4.0f);
        Camera camera(cameraConfig);
        loom.renderer.setCamera(camera);

        //Snimka vec nosi svoje osvjetljenje; ambijent bi ga zbrojio jos jednom
        EnvironmentConfig environmentConfig;
        environmentConfig.ambientColor = {0.0f, 0.0f, 0.0f};
        Environment environment(environmentConfig);
        loom.renderer.setEnvironment(environment);

        StreamingTextureConfig plateConfig;
        plateConfig.format = vk::Format::eR8G8B8A8Srgb;
        plateConfig.filter = vk::Filter::eLinear;
        StreamingTexture plate(loom.device, loom.command, plateSize, plateConfig);

        // -- bez dubine: samo prikaz -------------------------------------------------------

        PipelineConfig showConfig;
        showConfig.vertexBindings.clear();
        showConfig.vertexAttributes.clear();
        showConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
        showConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.vert.spv";
        showConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.frag.spv";
        showConfig.cullMode = vk::CullModeFlagBits::eNone;
        VulkanGraphicsPipeline showPipeline = loom.createPipeline(showConfig);
        Material screen(loom.device, loom.command, loom.getDescriptorPool(),
                        showPipeline, plate.getSampled());

        // -- s dubinom: G-buffer i svjetlo -------------------------------------------------

        std::unique_ptr<Texture> depthTexture;
        std::unique_ptr<PositionMap> positions;
        std::unique_ptr<NormalMap> normals;
        std::unique_ptr<Relight> relight;
        std::unique_ptr<Light> bulb;

        if(depth.isValid()){
            TextureConfig depthConfig;
            depthConfig.format = vk::Format::eR32Sfloat;
            depthConfig.filter = vk::Filter::eNearest;   //dubina se ne prosjecuje
            depthConfig.addressMode = vk::SamplerAddressMode::eClampToEdge;
            depthConfig.generateMipmaps = false;
            depthTexture = std::make_unique<Texture>(loom.device, loom.command,
                                                     depth.values.data(), plateSize, depthConfig);

            positions = std::make_unique<PositionMap>(loom.device, plateSize);
            positions->setPlateDepth(loom.getDescriptorPool(), depthTexture->getSampled(),
                                     plateSize, DepthMapping::fromRange(nearDistance, farDistance));
            positions->setIntrinsics(CameraIntrinsics::fromProjection(
                camera.getProjection(plateWidth, plateHeight), plateWidth, plateHeight));

            normals = std::make_unique<NormalMap>(loom.device, plateSize);
            normals->setPositionSource(loom.getDescriptorPool(), *positions);

            RelightConfig relightConfig;
            relightConfig.colorFormat = loom.getColorFormat();
            relightConfig.surface.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
            relightConfig.surface.shininess = 24.0f;
            relightConfig.surface.specularStrength = 0.35f;

            relight = std::make_unique<Relight>(loom.device, loom.command, loom.getDescriptorPool(),
                                                *positions, *normals, plate.getSampled(), relightConfig);
            relight->setCamera(camera);

            //Sjena se trazi kroz samu sliku - baca je sve sto je u kadru, i nista sto nije
            relight->setIntrinsics(CameraIntrinsics::fromProjection(
                camera.getProjection(plateWidth, plateHeight), plateWidth, plateHeight),
                plateSize);

            ScreenShadowConfig shadowConfig;
            shadowConfig.enabled = true;
            shadowConfig.steps = 32;
            shadowConfig.maxDistance = 0.5f * (nearDistance + farDistance);
            shadowConfig.thickness = 0.15f * (nearDistance + farDistance);
            relight->setShadow(shadowConfig);

            LightConfig bulbConfig;
            bulbConfig.type = LightType::Point;
            bulbConfig.color = {1.0f, 0.82f, 0.55f};
            bulbConfig.intensity = 40.0f;
            bulbConfig.range = farDistance;
            bulb = std::make_unique<Light>(bulbConfig);
            loom.renderer.addLight(*bulb);

            printf("\nSvjetlo kruzi oko scene i baca sjene od svega sto je u kadru.\n"
                   "Zatvori prozor za izlaz.\n");
        }

        // -- petlja ------------------------------------------------------------------------

        const double secondsPerFrame = 1.0 / frameRate;
        auto nextFrameDue = std::chrono::steady_clock::now();
        const auto started = std::chrono::steady_clock::now();

        while(!loom.shouldClose()){
            loom.pollEvents();

            const Spool::Image* frame = &still;
            Spool::Image decoded;

            if(video){
                decoded = video->readNext();
                if(!decoded.isValid()){
                    //Kraj snimke: natrag na pocetak. Snimka koja stane na zadnjem frameu
                    //izgleda isto kao snimka koja se zaglavila
                    video->rewind();
                    decoded = video->readNext();
                    if(!decoded.isValid()) break;
                }
                frame = &decoded;
            }

            if(!loom.renderer.beginFrame()) continue;

            //Tek NAKON beginFrame: prsten se oslanja na to da je renderer vec pricekao frame
            //od prije onoliko frameova koliko prsten ima slotova
            plate.update(frame->pixels.data(), frame->pixels.size());

            if(relight){
                //Svjetlo kruzi ispred scene, na visini sredine raspona. Kruzi zato sto je to
                //jedini nacin da se OKOM vidi da je stvarno u prostoru: da je nalijepljeno na
                //sliku, sjencanje se ne bi mijenjalo dok putuje
                const double time = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started).count();

                const float radius = 0.45f * (nearDistance + farDistance) * 0.5f;
                const float middle = 0.5f * (nearDistance + farDistance);

                bulb->setPosition({
                    radius * float(std::sin(time * 0.6)),
                    0.35f * middle,
                    -middle + radius * float(std::cos(time * 0.6))
                });

                relight->setPlate(plate.getSampled());

                const UnprojectPush unproject = positions->makePush();
                loom.renderer.dispatch(positions->getComputeMaterial(),
                                       positions->groupsX(), positions->groupsY(), 1,
                                       &unproject, sizeof(unproject));

                const NormalPush normalPush = normals->makePush();
                loom.renderer.dispatch(normals->getComputeMaterial(),
                                       normals->groupsX(), normals->groupsY(), 1,
                                       &normalPush, sizeof(normalPush));

                loom.renderer.beginPass();
                loom.renderer.drawFullscreen(relight->getMaterial());
                loom.renderer.endPass();
            }
            else{
                screen.setSampledImage(plate.getSampled());

                loom.renderer.beginPass();
                loom.renderer.drawFullscreen(screen);
                loom.renderer.endPass();
            }

            loom.renderer.endFrame();

            //Snimku treba gledati njenom brzinom, a ne brzinom kojom je karta stigne nacrtati
            nextFrameDue += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(secondsPerFrame));

            const auto now = std::chrono::steady_clock::now();
            if(nextFrameDue > now) std::this_thread::sleep_until(nextFrameDue);
            else nextFrameDue = now;   //zaostali smo; ne skupljamo dug
        }

        loom.waitIdle();
    }
    catch(const std::exception& error){
        printf("\n%s\n", error.what());
        return 1;
    }

    return 0;
}
