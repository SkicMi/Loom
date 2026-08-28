// 0c: snimka se vrti u nasem prozoru.
//
// Tekstura koja se mijenja svaki frame nije ista stvar kao tekstura. Obicna se napise jednom
// i vise se ne dira; snimka svaki frame donosi nove piksele u sliku iz koje karta jos crta
// prethodni frame. Dvije stvari koje se pritom lako izgube:
//
//   1. da se prikaze BAS taj frame, a ne prethodni koji je jos u letu
//   2. da se pritom nista ne alocira, jer alokacija po frameu je alokacija po sekundi puta
//      trideset
//
// Oboje se mjeri. Snimka je prava - Spool je dekodira - pa je ovo ujedno prvi test u kojem
// Loom crta nesto sto nije sam napravio.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/StreamingTexture.h"
#include "Vulkan/Texture.h"

#include <Spool/ImageFile.h>
#include <Spool/Sequence.h>
#include <Spool/VideoFile.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace{

const uint32_t frameWidth = 64;
const uint32_t frameHeight = 48;
const uint32_t frameTotal = 10;

//Svaki frame jedna puna boja, i to boje koje se ne daju zamijeniti jedna za drugu. Kad se
//trazi "je li prikazan frame 7", odgovor mora biti jednoznacan iz jednog piksela
Spool::Image solidFrame(uint32_t index){
    const uint8_t r = uint8_t(15 + index * 24);
    const uint8_t g = uint8_t(index % 2 ? 210 : 35);
    const uint8_t b = uint8_t(245 - index * 22);

    std::vector<uint8_t> pixels(size_t(frameWidth) * frameHeight * 4);
    for(size_t i = 0; i + 3 < pixels.size(); i += 4){
        pixels[i+0] = r; pixels[i+1] = g; pixels[i+2] = b; pixels[i+3] = 255;
    }
    return Spool::imageFromPixels(pixels.data(), frameWidth, frameHeight);
}

bool haveFfmpeg(){
    return std::system("ffmpeg -version > /dev/null 2>&1") == 0;
}

}

int main(){
    TestReport report("0c playback");

    if(!haveFfmpeg()){
        report.check("ffmpeg", false, "nije na putanji - testna snimka se ne moze napraviti");
        return report.result();
    }

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_playback";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);

    std::vector<Spool::Image> source;
    Spool::SequenceConfig sequenceConfig;
    sequenceConfig.directory = (work / "src").string();
    Spool::SequenceWriter writer(sequenceConfig);

    for(uint32_t i = 0; i < frameTotal; ++i){
        source.push_back(solidFrame(i));
        writer.write(source.back());
    }

    const std::string video = (work / "clip.mkv").string();
    const std::string encode = "ffmpeg -y -framerate 24 -i " + (work / "src" / "frame_%04d.png").string() +
                               " -c:v ffv1 -pix_fmt gbrp " + video + " > /dev/null 2>&1";
    if(std::system(encode.c_str()) != 0){
        report.check("snimka je napravljena", false, "kodiranje nije uspjelo");
        return report.result();
    }

    // -------------------------------------------------------------------------------
    // Loom
    // -------------------------------------------------------------------------------

    LoomConfig config;
    config.width = frameWidth; config.height = frameHeight;
    config.appName = "playback"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = false;
    //Unorm, ne Srgb: usporeduje se bajt koji je usao s bajtom koji je izasao, pa nikakva
    //pretvorba krivulje ne smije stajati izmedu
    config.pipelineConfig.colorFormat = vk::Format::eR8G8B8A8Unorm;
    config.headlessColorFormat = vk::Format::eR8G8B8A8Unorm;

    LoomInitializer loom(config);

    StreamingTextureConfig streamConfig;
    streamConfig.format = vk::Format::eR8G8B8A8Unorm;
    streamConfig.filter = vk::Filter::eNearest;
    StreamingTexture plate(loom.device, loom.command, vk::Extent2D{frameWidth, frameHeight}, streamConfig);

    report.check("prsten je dubok koliko i letenje",
        plate.getSlotCount() == loom.command.getCommandBuffers().size(),
        fmt("%u slotova za %zu frameova u letu", plate.getSlotCount(), loom.command.getCommandBuffers().size()));

    PipelineConfig showConfig;
    showConfig.vertexBindings.clear();
    showConfig.vertexAttributes.clear();
    showConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    showConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.vert.spv";
    showConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.frag.spv";
    showConfig.cullMode = vk::CullModeFlagBits::eNone;
    showConfig.colorFormat = vk::Format::eR8G8B8A8Unorm;
    VulkanGraphicsPipeline showPipeline = loom.createPipeline(showConfig);

    Material screen(loom.device, loom.command, loom.getDescriptorPool(), showPipeline, plate.getSampled());

    RenderTargetConfig targetConfig;
    targetConfig.colorFormat = vk::Format::eR8G8B8A8Unorm;
    targetConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    targetConfig.enableDepth = false;
    RenderTarget out(loom.device, vk::Extent2D{frameWidth, frameHeight}, targetConfig);

    // -------------------------------------------------------------------------------
    // Vrtimo je
    // -------------------------------------------------------------------------------

    const MemoryStats before = loom.device.getAllocator().getStats();

    Spool::VideoReader reader(video);
    std::vector<std::vector<uint8_t>> shown;

    while(!reader.atEnd()){
        const Spool::Image frame = reader.readNext();
        if(!frame.isValid()) break;

        if(!loom.renderer.beginFrame()) break;

        //Redoslijed nije proizvoljan: update tek NAKON beginFrame, jer se prsten oslanja na
        //to da je renderer vec pricekao frame od prije slots frameova
        plate.update(frame.pixels.data(), frame.pixels.size());

        //Slot se promijenio, pa materijal mora saznati koji je sad na redu
        screen.setSampledImage(plate.getSampled());

        loom.renderer.beginPass(out);
        loom.renderer.drawFullscreen(screen);
        loom.renderer.endPass();
        loom.renderer.endFrame();

        loom.waitIdle();
        shown.push_back(out.readPixels(loom.command).pixels);
    }

    const MemoryStats after = loom.device.getAllocator().getStats();

    report.check("prikazano je koliko je i snimljeno", shown.size() == frameTotal,
        fmt("%zu od %u frameova", shown.size(), frameTotal));

    // -------------------------------------------------------------------------------
    // Je li prikazan BAS taj frame
    // -------------------------------------------------------------------------------

    size_t wrong = 0;
    int worst = 0;
    for(size_t i = 0; i < shown.size(); ++i){
        int delta = 0;
        for(size_t b = 0; b < shown[i].size() && b < source[i].pixels.size(); ++b){
            delta = std::max(delta, std::abs(int(shown[i][b]) - int(source[i].pixels[b])));
        }
        worst = std::max(worst, delta);
        if(delta != 0) ++wrong;
    }

    report.check("svaki frame je bas taj frame", wrong == 0 && worst == 0,
        fmt("%zu od %zu frameova odstupa, najveca razlika po kanalu %d", wrong, shown.size(), worst));

    //Kontrola: da se tekstura nije osvjezavala, svi bi frameovi bili isti i gornja provjera
    //bi pala samo ako se izvor mijenja. Provjerava se da se STVARNO mijenja
    size_t distinct = 0;
    for(size_t i = 1; i < shown.size(); ++i){
        if(shown[i] != shown[i-1]) ++distinct;
    }
    report.check("slika se stvarno mijenja svaki frame", distinct + 1 == shown.size(),
        fmt("%zu promjena kroz %zu frameova", distinct, shown.size()));

    // -------------------------------------------------------------------------------
    // I da se pritom nista ne alocira
    // -------------------------------------------------------------------------------

    //Ovo je razlika izmedu streaminga i pravljenja nove teksture svaki frame. Trideset
    //frameova u sekundi puta jedna slika i jedan medjuspremnik je alokacija na koju se
    //drajver prije ili poslije potuzi - a citanje natrag u ovom testu i samo alocira, pa se
    //trazi da ih ne bude vise nego sto ih citanje objasnjava
    const uint32_t newAllocations = after.allocationCount - before.allocationCount;

    report.check("vrtnja ne alocira", newAllocations == 0,
        fmt("%u novih alokacija kroz %zu frameova (%u blokova prije, %u poslije)",
            newAllocations, shown.size(), before.blockCount, after.blockCount));

    report.check("prsten se vrtio", plate.getUpdateCount() == frameTotal,
        fmt("%llu osvjezavanja kroz %u slotova", (unsigned long long)plate.getUpdateCount(), plate.getSlotCount()));

    report.checkNoValidationMessages();
    std::filesystem::remove_all(work);
    return report.result();
}
