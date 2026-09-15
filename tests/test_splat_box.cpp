// Ubacena kocka u splat sceni: predmet kojim se VIDI kako je solvalo.
//
// Splat scena sama po sebi nema mjerilo ni tlo na koje bi se oko oslonilo. Kocka postavljena na
// poznato mjesto to daje: ako poze i mjerilo valjaju, ona stoji gdje treba, prave je velicine, i
// zaklanja ono sto je iza nje.
//
// ZASTO ZRAKOM A NE LOOMOM. Splat se stapa sprijeda natrag i nema jednu dubinu po pikselu, pa bi
// spajanje s tudjim depth bufferom trazilo citanje natrag i pretvorbu jedinica. Presjek zrake i
// kutije daje dubinu u ISTIM jedinicama koje splat vec nosi (-viewZ), pa se usporedba svede na
// jedan if - i bas taj if se ovdje mjeri.
//
// Sto se brani:
//
//   ispred zaklanja      unutar siluete se ne smije vidjeti nista od zida iza: petlja mora STATI,
//                        ne samo preskociti. Piksel mora biti tocno sjencanje kocke
//   velicina je tocna    silueta mora biti siroka 2*h*f/d piksela, koliko projekcija nalaze
//   iza se ne vidi       kocka iza neprozirnog zida ne smije promijeniti sliku
//   sa strane nista      kocka izvan kadra mora dati BIT-IDENTICNU sliku onoj bez kocke. Bez ove
//                        provjere bi i shader koji uvijek nesto doda prosao sve gornje
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/SplatRenderer.h"
#include "Vulkan/VulkanImage.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

namespace{

struct Pixel{
    float r = 0, g = 0, b = 0, a = 0;
};

}

int main(){
    TestReport report("ubacena kocka u splat sceni");

    const vk::Extent2D size{200, 150};
    const float focal = 300.0f;
    const glm::vec3 boxColor(0.85f, 0.35f, 0.15f);

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "splat box"; config.engineName = "Loom tests";
    config.headless = true;
    config.maxDescriptorSets = 128;   //jedan SplatRenderer trazi 21 set
    LoomInitializer loom(config);

    ImageConfig imageConfig;
    imageConfig.format = vk::Format::eR32G32B32A32Sfloat;
    imageConfig.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
    VulkanImage target(loom.device, size, imageConfig);

    const vk::DeviceSize pixelBytes = vk::DeviceSize(size.width) * size.height * sizeof(Pixel);
    VulkanBuffer readback(loom.device, pixelBytes, vk::BufferUsageFlagBits::eTransferDst,
                          MemoryUsage::GPU_TO_CPU);

    // -------------------------------------------------------------------------------
    // Zid splatova na dubini 10: gust i neproziran, da "iza se ne vidi" nesto znaci
    // -------------------------------------------------------------------------------

    const glm::mat4 view = glm::lookAt(glm::vec3(0,0,0), glm::vec3(0,0,-1), glm::vec3(0,1,0));
    const float wallDepth = 10.0f;

    //TRI SLOJA, ne jedan. Jedan sloj neprozirnosti 0.99 propusta oko 1.5 posto svjetla - izmjereno
    //ovdje, kad je provjera "iza se ne vidi" pala uz razliku 1.5e-02. To nije bila greska nego
    //tocan racun: kroz poluproziran zid se kocka i treba nazrijeti. Da provjera nesto znaci, zid
    //mora biti stvarno neproziran, a tri sloja ostave 3e-06
    std::vector<SplatMath::PreparedSplat> prepared;
    for(int layer = 0; layer < 3; ++layer){
    for(int gx = -20; gx <= 20; ++gx){
        for(int gy = -16; gy <= 16; ++gy){
            Splat splat;
            splat.position = glm::vec3(0.2f * float(gx), 0.2f * float(gy),
                                       -wallDepth + 0.05f * float(layer));
            splat.scale = glm::vec3(0.13f);
            splat.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            splat.opacity = 0.99f;
            splat.color = glm::vec3(0.20f, 0.45f, 0.75f);

            SplatMath::PreparedSplat one;
            if(SplatMath::prepare(splat, view, focal, -focal,
                                  0.5f * size.width, 0.5f * size.height, 0.3f, one)){
                prepared.push_back(one);
            }
        }
    }
    }

    SplatRendererConfig rendererConfig;
    rendererConfig.tileSize = 16;
    rendererConfig.maxSplats = uint32_t(prepared.size());
    rendererConfig.maxPairs = 1u << 20;

    SplatRenderer splatRenderer(loom.device, loom.getDescriptorPool(), target, size, rendererConfig);
    splatRenderer.upload(prepared);
    splatRenderer.setCamera(view, glm::vec3(0.0f), focal, -focal,
                            0.5f * float(size.width), 0.5f * float(size.height));

    auto drawWith = [&](const SplatRenderer::Box& box){
        splatRenderer.setBox(box);
        loom.renderer.beginFrame();
        splatRenderer.draw(loom.renderer, uint32_t(prepared.size()));
        loom.renderer.endFrame();
        loom.waitIdle();

        loom.command.copyImageToBuffer(target.getImage(), readback.getBuffer(), size);
        std::vector<Pixel> pixels(size_t(size.width) * size.height);
        readback.download(pixels.data(), pixelBytes);
        return pixels;
    };

    const std::vector<Pixel> reference = drawWith(SplatRenderer::Box{});

    //Zid mora stvarno biti neproziran, inace "iza se ne vidi" ne mjeri nista
    {
        const Pixel& middle = reference[size_t(size.height / 2) * size.width + size.width / 2];
        report.check("zid je neproziran i vidi se",
            middle.a > 0.99f && middle.b > middle.r,
            fmt("srediste: pokrivenost %.4f, boja (%.3f %.3f %.3f)", middle.a, middle.r, middle.g, middle.b));
    }

    // -------------------------------------------------------------------------------
    // Kocka ispred zida
    // -------------------------------------------------------------------------------

    const float boxDepth = 5.0f;
    const float half = 0.5f;

    SplatRenderer::Box front;
    front.visible = true;
    front.center = glm::vec3(0.0f, 0.0f, -boxDepth);
    front.halfExtent = glm::vec3(half);
    front.color = boxColor;

    const std::vector<Pixel> withFront = drawWith(front);

    {
        //Sjencanje je isto kao u shaderu: prednja ploha gleda u +Z, svjetlo je stalno
        const glm::vec3 normal(0.0f, 0.0f, 1.0f);
        const glm::vec3 light = glm::normalize(glm::vec3(0.4f, 0.7f, 0.6f));
        const float lit = 0.35f + 0.65f * std::max(0.0f, glm::dot(normal, light));

        const Pixel& middle = withFront[size_t(size.height / 2) * size.width + size.width / 2];
        const double worst = std::max({std::fabs(double(middle.r) - double(lit * boxColor.r)),
                                       std::fabs(double(middle.g) - double(lit * boxColor.g)),
                                       std::fabs(double(middle.b) - double(lit * boxColor.b))});

        report.check("ispred zida se vidi samo kocka", worst < 1e-5,
            fmt("srediste (%.4f %.4f %.4f), ocekivano (%.4f %.4f %.4f), razlika %.2e",
                middle.r, middle.g, middle.b, lit * boxColor.r, lit * boxColor.g, lit * boxColor.b, worst));
    }

    {
        //Silueta se mjeri po sredisnjem retku: koliko je piksela poprimilo boju kocke
        const size_t row = size_t(size.height / 2) * size.width;
        uint32_t wide = 0;
        for(uint32_t x = 0; x < size.width; ++x){
            const Pixel& pixel = withFront[row + x];
            if(pixel.r > pixel.b) ++wide;   //kocka je narancasta, zid plav
        }

        //Projekcija: silueta je PREDNJA PLOHA, a ona je na dubini d - h, ne d. Prva verzija ovog
        //testa je racunala sa sredistem i ocekivala 60 px umjesto 66.7 - kocka nije ploca
        const double expected = 2.0 * double(half) * double(focal) / double(boxDepth - half);
        report.check("kocka je tocne velicine u slici",
            std::fabs(double(wide) - expected) <= 2.0,
            fmt("izmjereno %u px, projekcija nalaze %.1f px", wide, expected));
    }

    // -------------------------------------------------------------------------------
    // Kocka iza zida, pa kocka izvan kadra
    // -------------------------------------------------------------------------------

    {
        SplatRenderer::Box behind = front;
        behind.center = glm::vec3(0.0f, 0.0f, -20.0f);
        const std::vector<Pixel> withBehind = drawWith(behind);

        double worst = 0.0;
        for(size_t i = 0; i < reference.size(); ++i){
            worst = std::max({worst, std::fabs(double(reference[i].r) - double(withBehind[i].r)),
                                     std::fabs(double(reference[i].g) - double(withBehind[i].g)),
                                     std::fabs(double(reference[i].b) - double(withBehind[i].b))});
        }

        //Ne trazi se bit-identicnost: zid ima neprozirnost 0.99, pa kroz njega teorijski prolazi
        //trunka svjetla. Trazi se da to bude ispod jednog koda boje, dakle nevidljivo
        report.check("kocka iza neprozirnog zida se ne vidi", worst < 1.0 / 255.0,
            fmt("najveca razlika %.2e (prag %.2e)", worst, 1.0 / 255.0));
    }

    {
        SplatRenderer::Box aside = front;
        aside.center = glm::vec3(40.0f, 0.0f, -boxDepth);   //daleko izvan kadra
        const std::vector<Pixel> withAside = drawWith(aside);

        size_t different = 0;
        for(size_t i = 0; i < reference.size(); ++i){
            if(reference[i].r != withAside[i].r || reference[i].g != withAside[i].g ||
               reference[i].b != withAside[i].b || reference[i].a != withAside[i].a) ++different;
        }

        report.check("kocka izvan kadra ne mijenja nijedan piksel", different == 0,
            fmt("%zu od %zu piksela razlicito", different, reference.size()));
    }

    // -------------------------------------------------------------------------------
    // KAVEZ. Ista kutija, ali crtana samo po bridovima - to je ono sto treba kad se kockom
    // BIRA sto obrisati, jer puni blok zaklanja bas one splatove o kojima se odlucuje.
    //
    // Dvije tvrdnje, i jedna bez druge ne vrijedi: kroz plohu se mora vidjeti zid, a brid
    // mora ostati. Kavez koji se samo ne vidi prosao bi prvu
    // -------------------------------------------------------------------------------

    {
        SplatRenderer::Box cage = front;
        cage.edgeShare = 0.12f;
        const std::vector<Pixel> withCage = drawWith(cage);

        const size_t middle = size_t(size.height / 2) * size.width + size.width / 2;

        //Silueta je 2*h*f/d piksela siroka, a d je PREDNJA PLOHA - dakle boxDepth - half, ne
        //boxDepth. S razmakom do sredista ispalo bi 30 px umjesto 33, pa bi uzorak na 28 px
        //bio na 0.84 poluosovine - unutar plohe, izvan ruba. Kavez bi izgledao pokvaren a
        //bio bi tocan
        const int halfWidth = int(half * focal / (boxDepth - half));
        const size_t edge = size_t(size.height / 2) * size.width + size_t(int(size.width / 2) + halfWidth - 2);

        const bool seeThrough = std::fabs(double(withCage[middle].b) - double(reference[middle].b)) < 1e-4
                             && std::fabs(double(withCage[middle].r) - double(reference[middle].r)) < 1e-4;

        //Na bridu je kutija, a ona je narancasta - dakle crveno mora nadvladati plavo, sto je
        //kod zida obrnuto
        const bool edgeDrawn = withCage[edge].r > withCage[edge].b;

        report.check("kroz kavez se vidi", seeThrough,
            fmt("srediste (%.3f %.3f %.3f), zid (%.3f %.3f %.3f)",
                withCage[middle].r, withCage[middle].g, withCage[middle].b,
                reference[middle].r, reference[middle].g, reference[middle].b));

        report.check("brid kaveza stoji", edgeDrawn,
            fmt("na %d px od sredine: (%.3f %.3f %.3f)", halfWidth - 2,
                withCage[edge].r, withCage[edge].g, withCage[edge].b));
    }

    // -------------------------------------------------------------------------------
    // IZNUTRA. Kamera unutar kutije: prije se tada nije crtalo nista, jer je ulazna ploha
    // iza ledja. Za mjerni predmet je to bilo ispravno, za kavez nije - u njega se ulazi da
    // se vidi sto je unutra, a kavez koji tada nestane ne govori vise gdje mu je rub
    // -------------------------------------------------------------------------------

    {
        //Poluosovine su nejednake namjerno: kutija mora biti dovoljno uska da joj bridovi
        //udju u kadar. S kockom 3x3x3 oko kamere bridovi zavrsavaju na 211 px od sredine, a
        //slika je siroka 200 - kavez bi bio tocan a ne bi se vidio nista
        SplatRenderer::Box around;
        around.visible = true;
        around.center = glm::vec3(0.0f, 0.0f, -1.0f);
        around.halfExtent = glm::vec3(0.6f, 0.45f, 3.0f);   //kamera je u nuli, dakle unutra
        around.edgeShare = 0.06f;
        const std::vector<Pixel> fromInside = drawWith(around);

        size_t painted = 0;
        for(size_t i = 0; i < reference.size(); ++i){
            if(fromInside[i].r > reference[i].r + 0.02f) ++painted;
        }

        report.check("kavez se vidi iznutra", painted > 100,
            fmt("%zu piksela nosi boju kutije", painted));
    }

    return report.result();
}
