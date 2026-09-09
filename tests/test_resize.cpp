// B2: velicina slike se mijenja u letu.
//
// Resize je dosad bio jedina stvar u Loomu potvrdjena samo OKOM: promijeni se prozor, slika
// izgleda dobro, dalje se radi. Testovi su pokrivali resize METE pod materijalom
// (test_api_contracts), sto je druga stvar - tamo se mijenja slika u koju se crta, ovdje se
// mijenja swapchain, njegove slike, njihovi layouti i sve sto na njima visi.
//
// TKO OVDJE ODLUCUJE O VELICINI - izmjereno prije nego napisano kao tvrdnja. Prva verzija ovog
// testa je promjenu vozila kroz glfwSetWindowSize i pala, a pala je s tudje strane:
//
//   currentExtent povrsine       0xFFFFFFFF, dakle driver doslovno kaze "ti odluci"
//   glfwSetWindowSize(480x360)   GLFW odmah javi 480x360 ... i prvi pollEvents poslije toga
//                                (30-60 ms) vrati 320x240. Kompozitorov configure, koji GLFW
//                                primijeni bezuvjetno
//   isto bez libdecora,          tri puta identicno, dakle nije stvar ukrasa
//     isto bez ukrasa
//   swapchain koji USTRAJE       480x360 kroz dvadeset kadrova, bez ijedne validation poruke,
//                                dok prozor cijelo vrijeme stoji na 320x240
//
// Znaci: velicinu SLIKE Loom bira, velicinu PROZORA ne. Zato promjenu vozi onaj tko je smije
// voziti - renderer.resize() gdje povrsina prepusta odluku aplikaciji, a sam prozor tamo gdje
// je diktira kompozitor.
//
// Pala je usput i kontrola koju je prva verzija imala: gol GLFW prozor je "zadrzao" 480x360 i
// time izgledao kao dokaz da se prozor da promijeniti. Nije - taj prozor nikad ne dobije sliku,
// pa se nikad ne mapira, pa mu kompozitor nikad nista ne odgovori. Provjera koja prolazi bas
// zato sto se nista nije dogodilo.
//
// Cetiri tvrdnje, i svaka pada na drugu vrstu greske:
//
//   nova velicina je stvarno nova   swapchain javlja drugi extent, i slika ga ima
//   slika je ista kao da je ta      ono sto se vidi u prozoru mora biti bajt-identicno meti te
//     velicina od pocetka           velicine crtanoj u istom kadru
//   ta usporedba grize              ista meta, drugi kut -> mora se razlikovati. Bez ovoga bi
//                                   dvije crne slike prosle kao dokaz
//   povratak vraca isto             natrag na staru velicinu daje bajt-identicnu prvu sliku
//
// Zadnja je ona koja hvata stanje zaostalo iza promjene: ako nakon nje ista scena na istoj
// velicini vise ne daje iste bajtove, nesto se u putu do slike promijenilo, a ne bi smjelo.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/Light.h"
#include "Core/LoomShapes.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/VulkanAllocator.h"

#include <chrono>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const vk::Extent2D small{320, 240};
const vk::Extent2D large{480, 360};

}

int main(){
    TestReport report("B2 resize");

    LoomConfig config;
    config.width = small.width; config.height = small.height;
    config.appName = "resize"; config.engineName = "Loom tests";
    config.headless = false;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    CameraConfig cameraConfig;
    cameraConfig.position = {1.8f, 1.4f, 2.4f};
    cameraConfig.target = {0.0f, 0.0f, 0.0f};
    Camera camera(cameraConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {0.15f, 0.15f, 0.15f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig lightConfig;
    lightConfig.type = LightType::Directional;
    lightConfig.direction = {-0.3f, -1.0f, -0.4f};
    Light light(lightConfig);
    loom.renderer.addLight(light);

    LoomShapes::Primitives primitives(loom);

    //Nepomicna scena: kut je zakucan, a ne izveden iz sata. Slika koja ovisi o vremenu se ne
    //da usporediti sa sobom
    const glm::mat4 model  = glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0.3f, 1.0f, 0.1f));
    const glm::mat4 tilted = glm::rotate(glm::mat4(1.0f), 0.9f, glm::vec3(0.3f, 1.0f, 0.1f));

    //Crta u prozor, i usput u metu ako je dana. Prozor uvijek vidi isti kut; meta smije vidjeti
    //drugi, i to je jedini nacin da se provjeri grize li sama usporedba
    auto drawFrame = [&](const RenderTarget* target, const glm::mat4& targetModel){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) return false;

        if(target){
            loom.renderer.beginPass(*target);
            primitives.cube(targetModel);
            loom.renderer.endPass();
        }

        loom.renderer.beginPass();
        primitives.cube(model);
        loom.renderer.endPass();

        loom.renderer.endFrame();
        return true;
    };

    auto settle = [&](int frames){
        for(int i = 0; i < frames; ++i) drawFrame(nullptr, model);
        loom.waitIdle();
    };

    //Promjenu vozi onaj tko je smije voziti. Gdje povrsina prepusta odluku aplikaciji, trazi je
    //Loom; gdje je diktira kompozitor, jedini nacin je zamoliti prozor i cekati da se to javi
    //natrag kroz framebuffer
    const bool oursToChoose = loom.swapchain->appDecidesExtent();

    auto changeTo = [&](vk::Extent2D wanted){
        if(oursToChoose){
            loom.renderer.resize(wanted);
            settle(4);
        }
        else{
            glfwSetWindowSize(loom.window->getWindow(), int(wanted.width), int(wanted.height));

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while(std::chrono::steady_clock::now() < deadline){
                int width = 0, height = 0;
                glfwGetFramebufferSize(loom.window->getWindow(), &width, &height);
                if(uint32_t(width) == wanted.width && uint32_t(height) == wanted.height) break;
                drawFrame(nullptr, model);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            settle(4);
        }

        return loom.getExtent().width == wanted.width && loom.getExtent().height == wanted.height;
    };

    settle(4);

    const ImageData first = loom.renderer.readLastFrame();
    report.check("prozor se da procitati",
        first.extent.width == small.width && first.extent.height == small.height &&
        countNonBlack(first.pixels) > 200,
        fmt("%ux%u, %zu ne-crnih piksela, velicinu bira %s", first.extent.width,
            first.extent.height, countNonBlack(first.pixels),
            oursToChoose ? "aplikacija" : "kompozitor"));

    // -------------------------------------------------------------------------------
    // Vece
    // -------------------------------------------------------------------------------

    const bool grew = changeTo(large);
    report.check("slika ima novu velicinu", grew,
        fmt("%ux%u nakon promjene", loom.getExtent().width, loom.getExtent().height));

    //Meta se stvara TEK SAD, na novoj velicini, i crta u istom kadru kao i prozor
    RenderTargetConfig readable;
    readable.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readable.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    readable.keepDepth = true;
    RenderTarget reference(loom.device, large, readable);

    drawFrame(&reference, model);
    loom.waitIdle();

    const std::vector<uint8_t> windowPixels = loom.renderer.readLastFrame().pixels;
    const std::vector<uint8_t> targetPixels = reference.readPixels(loom.command).pixels;

    const ByteDiff against = diffBytes(windowPixels, targetPixels);
    report.check("prozor je isti kao meta te velicine",
        against.different == 0 && !windowPixels.empty(),
        fmt("%zu od %zu bajtova razlike prema meti %ux%u",
            against.different, windowPixels.size(), large.width, large.height));

    //Ista meta, drugi kut. Usporedba koja gore daje nulu mora ovdje dati nesto, inace ne mjeri
    //sliku nego samo to da su obje jednako prazne
    RenderTarget twin(loom.device, large, readable);
    drawFrame(&twin, tilted);
    loom.waitIdle();

    const ByteDiff mustDiffer = diffBytes(loom.renderer.readLastFrame().pixels,
                                          twin.readPixels(loom.command).pixels);
    report.check("ta usporedba grize",
        mustDiffer.different > 0,
        fmt("%zu bajtova razlike prema meti crtanoj pod drugim kutom", mustDiffer.different));

    // -------------------------------------------------------------------------------
    // I natrag
    // -------------------------------------------------------------------------------

    const bool shrank = changeTo(small);
    report.check("slika se vratila", shrank,
        fmt("%ux%u nakon povratka", loom.getExtent().width, loom.getExtent().height));

    const ImageData back = loom.renderer.readLastFrame();
    const ByteDiff roundTrip = diffBytes(back.pixels, first.pixels);

    report.check("povratak daje istu sliku",
        roundTrip.different == 0 && back.extent.width == small.width,
        fmt("%zu od %zu bajtova razlike prema prvoj slici", roundTrip.different, first.pixels.size()));

    // -------------------------------------------------------------------------------
    // I da se pritom nista ne nakuplja
    // -------------------------------------------------------------------------------

    const MemoryStats before = loom.device.getAllocator().getStats();
    const uint64_t madeBefore = VulkanAllocator::getAllocationsMade();

    for(int i = 0; i < 5; ++i){
        changeTo(large);
        changeTo(small);
    }

    const MemoryStats after = loom.device.getAllocator().getStats();
    const uint64_t perChange = (VulkanAllocator::getAllocationsMade() - madeBefore) / 10;

    report.check("deset promjena ne ostavlja nista za sobom",
        after.allocationCount <= before.allocationCount && after.blockCount <= before.blockCount,
        fmt("%u -> %u alokacija, %u -> %u blokova kroz deset promjena",
            before.allocationCount, after.allocationCount, before.blockCount, after.blockCount));

    //Stanje gore je isto i kad se svaka promjena naplati iznova, jer se sve i oslobodi. Ovaj
    //broj je jedini koji vidi koliko je posla promjena STVARNO trazila
    report.check("promjena kosta jednu sliku dubine",
        perChange <= 1,
        fmt("%llu alokacija po promjeni", (unsigned long long)perChange));

    // -------------------------------------------------------------------------------
    // I da se volan da vratiti
    // -------------------------------------------------------------------------------

    //Tko trazi velicinu, drzi je - pa promjena prozora vise ne bi mijenjala sliku. Zato postoji
    //put natrag, i zato se provjerava da vodi tamo gdje kaze da vodi
    if(oursToChoose){
        loom.renderer.resize(large);
        settle(2);
        const bool held = loom.getExtent().width == large.width;

        loom.renderer.followWindow();
        settle(2);

        int windowWidth = 0, windowHeight = 0;
        glfwGetFramebufferSize(loom.window->getWindow(), &windowWidth, &windowHeight);

        report.check("volan se vraca prozoru",
            held && loom.getExtent().width == uint32_t(windowWidth) &&
            loom.getExtent().height == uint32_t(windowHeight),
            fmt("drzana %ux%u, pa opet prati prozor %dx%d", large.width, large.height,
                windowWidth, windowHeight));
    }

    // -------------------------------------------------------------------------------
    // I da se ne da napraviti u krivom trenutku
    // -------------------------------------------------------------------------------

    bool refused = false;
    if(oursToChoose){
        loom.pollEvents();
        if(loom.renderer.beginFrame()){
            try{
                loom.renderer.resize(large);
            }
            catch(const std::runtime_error&){
                refused = true;
            }
            loom.renderer.beginPass();
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
    }
    else{
        refused = true;   //bez prava na promjenu nema ni tog trenutka
    }

    report.check("usred kadra se odbija", refused,
        refused ? "slike u koje se crta ne nestaju ispod zapisanih naredbi"
                : "resize je prosao dok je kadar bio otvoren");

    report.checkNoValidationMessages();
    return report.result();
}
