// Plate: snimka u prozoru.
//
// Ovo je STEPENICA 2, i to namjerno.
//
// Stepenica 1 danas nema gdje drzati snimku. Scene crta oblike iz svog reda i sama vodi svoje
// prolaze; plate nije oblik nego cijeli kadar ispod svega, pa se u taj red ne uklapa. Ta je
// rupa poznata i zatvorit ce se kad korak 2 pokaze kakav oblik snimci stvarno treba - do tada
// je ovo posteniji zapis od API-ja izmisljenog unaprijed.
//
// Prijasnja scena s cetiri oblika i sjenama zivi dalje u examples/lit3d.
//
//   ./LoomApp snimka.mp4
#include "Core/LoomInitializer.h"
#include "Vulkan/Material.h"
#include "Vulkan/StreamingTexture.h"
#include "Vulkan/Texture.h"

#include <Spool/VideoFile.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
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

}

int main(int argc, char** argv){
    if(argc < 2){
        printf("Loom - snimka u prozoru\n\n"
               "  %s <snimka>\n\n"
               "Sto god ffmpeg zna procitati: mp4, mov, mkv, avi...\n", argv[0]);
        return 1;
    }

    try{
        // -- snimka prva, jer o njoj ovisi kakav prozor treba ------------------------------

        Spool::VideoReader reader(argv[1]);
        const Spool::VideoInfo& info = reader.info();

        printf("%s\n", argv[1]);
        printf("  %ux%u, %.3f slika/s, %lld frameova%s\n",
               info.width, info.height, info.frameRate(),
               (long long)info.frameCount,
               info.frameCountIsExact ? "" : " (procijenjeno)");
        printf("  %s, %s%s\n", info.codec.c_str(), info.pixelFormat.c_str(),
               info.rotation ? (", okrenuto " + std::to_string(info.rotation) + " stupnjeva").c_str() : "");

        // -- Loom --------------------------------------------------------------------------

        const vk::Extent2D windowSize = windowSizeFor(info.width, info.height);

        LoomConfig config;
        config.width = windowSize.width;
        config.height = windowSize.height;
        config.appName = "Loom";
        config.engineName = "Loom";
        config.enableDepth = false;   //plate je jedan crtez preko cijelog kadra
        config.swapchainConfig.preferredPresentMode = vk::PresentModeKHR::eMailbox;

        LoomInitializer loom(config);

        //Prsten slika, dubok koliko i letenje. Nista se ne alocira po frameu
        StreamingTextureConfig plateConfig;
        plateConfig.format = vk::Format::eR8G8B8A8Srgb;
        plateConfig.filter = vk::Filter::eLinear;
        StreamingTexture plate(loom.device, loom.command,
                               vk::Extent2D{info.width, info.height}, plateConfig);

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

        // -- petlja ------------------------------------------------------------------------

        const double secondsPerFrame = info.frameRate() > 0.0 ? 1.0 / info.frameRate() : 1.0 / 25.0;
        auto nextFrameDue = std::chrono::steady_clock::now();

        while(!loom.shouldClose()){
            loom.pollEvents();

            Spool::Image frame = reader.readNext();
            if(!frame.isValid()){
                //Kraj snimke: natrag na pocetak. Snimka koja stane na zadnjem frameu izgleda
                //isto kao snimka koja se zaglavila
                reader.rewind();
                frame = reader.readNext();
                if(!frame.isValid()) break;
            }

            if(!loom.renderer.beginFrame()) continue;

            //Tek NAKON beginFrame: prsten se oslanja na to da je renderer vec pricekao frame
            //od prije onoliko frameova koliko prsten ima slotova
            plate.update(frame.pixels.data(), frame.pixels.size());

            //Slot se promijenio, pa materijal mora saznati koji je sad na redu
            screen.setSampledImage(plate.getSampled());

            loom.renderer.beginPass();
            loom.renderer.drawFullscreen(screen);
            loom.renderer.endPass();
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
