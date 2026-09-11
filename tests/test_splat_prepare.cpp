// Priprema splatova na kartici, mjerena protiv iste matematike na procesoru.
//
// Priprema je bila 83 % kadra dok je bila na procesoru, i sve sto racuna ovisi o kameri - pa se
// ne da izracunati jednom i spremiti. Zato ide na karticu.
//
// TIME NASTAJU DVA PRIMJERKA ISTE MATEMATIKE: SplatMath::prepare u C++ i splat_prepare.slang na
// kartici. To je opasno na nacin na koji jedan primjerak nikad nije - mogu se s vremenom
// raziici, a nista u kodu nece izgledati krivo. Jedina obrana je da ih test stalno drzi jedno
// uz drugo, na podacima koji pokrivaju sve sto se u sceni pojavljuje.
//
// SVE SE USPOREDJUJE, ne samo boja: sredina, sva tri clana conica, neprozirnost, dubina i
// polumjer. Svaki od njih moze biti krivo prenesen a da slika i dalje izgleda uvjerljivo.
//
// TOLERANCIJA JE RELATIVNA I MJERENA, ne izmisljena. Dvije strane racunaju istim redoslijedom
// ali ne istim instrukcijama, pa se zadnji bitovi smiju razlikovati - a red velicine razlike
// mora ostati onaj koji pokretni zarez dopusta, i test ga ispisuje da se vidi kad naraste.
//
// RELATIVNO PREMA CEMU - to je bila prva greska ovog testa, ne shadera. Sredina 320 + (-240.07)
// = -0.0677 dala je relativnu razliku 2e-5, a u double se pokazalo da je PROCESOR bio dalje od
// istine (1.8e-5) nego kartica (3e-6). Razlika je bila jedan korak zaokruzivanja na 240, mjeren
// prema 0.07. Zato se svaka velicina mjeri prema najvecem clanu koji u nju ulazi.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/SplatRenderer.h"
#include "Vulkan/VulkanImage.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <string>
#include <vector>

namespace{

//Najveca relativna razlika, uz apsolutni pod za brojeve blizu nule
double worstRelative(float a, float b, float floorValue){
    const double scale = std::max(double(floorValue), std::max(std::fabs(double(a)), std::fabs(double(b))));
    return std::fabs(double(a) - double(b)) / scale;
}

}

int main(){
    TestReport report("priprema na kartici");

    const vk::Extent2D size{640, 480};

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "prepare"; config.engineName = "Loom tests";
    config.headless = true;
    //Jedan SplatRenderer trazi 21 set i 71 storage buffer, a default od 64 po tipu to ne
    //daje - tolerantan driver precuti, strog ne
    config.maxDescriptorSets = 128;
    LoomInitializer loom(config);

    ImageConfig imageConfig;
    imageConfig.format = vk::Format::eR32G32B32A32Sfloat;
    imageConfig.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
    VulkanImage target(loom.device, size, imageConfig);

    // -------------------------------------------------------------------------------
    // Scena koja pokriva sve sto se u pravoj sceni pojavljuje
    // -------------------------------------------------------------------------------

    const uint32_t splatCount = 4000;
    const uint32_t coeffsPerChannel = 15;   //stupanj 3, kao pravi 3DGS file
    const uint32_t degree = 3;

    std::mt19937 random(20260910);
    auto uniform = [&](float low, float high){
        return low + (high - low) * float(random() % 1000000) / 1000000.0f;
    };

    std::vector<Splat> splats;
    std::vector<SplatMath::RawSplat> raw;
    std::vector<glm::vec3> dcValues;
    std::vector<float> rest;

    splats.reserve(splatCount);
    raw.reserve(splatCount);
    rest.reserve(splatCount * coeffsPerChannel * 3);

    for(uint32_t i = 0; i < splatCount; ++i){
        Splat splat;
        //Ukljucujuci one iza kamere, koje priprema mora odbaciti
        splat.position = glm::vec3(uniform(-4.0f, 4.0f), uniform(-3.0f, 3.0f), uniform(-14.0f, 2.0f));

        //Od manjih od piksela do onih koje pokrivaju pola kadra, i jako izduzenih
        const float base = uniform(0.005f, 0.30f);
        splat.scale = glm::vec3(base, base * uniform(0.1f, 3.0f), base * uniform(0.1f, 3.0f));
        splat.rotation = glm::normalize(glm::quat(uniform(-1,1), uniform(-1,1), uniform(-1,1), uniform(-1,1)));
        splat.opacity = uniform(0.02f, 0.99f);

        const glm::vec3 dc(uniform(-2.0f, 2.0f), uniform(-2.0f, 2.0f), uniform(-2.0f, 2.0f));
        splat.color = SplatMath::colorFromSH0(dc);
        dcValues.push_back(dc);
        splats.push_back(splat);

        SplatMath::RawSplat one;
        one.positionOpacity = glm::vec4(splat.position, splat.opacity);
        one.scale = glm::vec4(splat.scale, 0.0f);
        one.rotation = glm::vec4(splat.rotation.w, splat.rotation.x, splat.rotation.y, splat.rotation.z);
        one.dc = glm::vec4(dc, 0.0f);
        raw.push_back(one);

        for(uint32_t k = 0; k < coeffsPerChannel * 3; ++k){
            rest.push_back(uniform(-0.6f, 0.6f));
        }
    }

    // -------------------------------------------------------------------------------
    // Kamera koja nije ni u ishodistu ni okrenuta niz os
    // -------------------------------------------------------------------------------

    const glm::vec3 cameraPosition(1.7f, -0.9f, 2.3f);
    const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.2f, 0.1f, -4.0f), glm::vec3(0,1,0));

    const float focalX = 520.0f;
    const float focalY = -520.0f;              //negativan, jer Vulkan okrece Y
    const float principalX = 0.5f * size.width;
    const float principalY = 0.5f * size.height;
    const float blur = 0.3f;

    SplatRendererConfig rendererConfig;
    rendererConfig.maxSplats = splatCount;
    rendererConfig.maxPairs = 1u << 21;
    rendererConfig.maxShCoefficients = coeffsPerChannel * 3;

    SplatRenderer splatRenderer(loom.device, loom.getDescriptorPool(), target, size, rendererConfig);
    splatRenderer.uploadRaw(raw, rest, degree, coeffsPerChannel);
    splatRenderer.setCamera(view, cameraPosition, focalX, focalY, principalX, principalY, blur);

    loom.renderer.beginFrame();
    splatRenderer.prepare(loom.renderer, splatCount);
    loom.renderer.endFrame();
    loom.waitIdle();

    VulkanBuffer readback(loom.device, vk::DeviceSize(splatCount) * sizeof(SplatMath::PreparedSplat),
                          vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
    loom.command.copyBuffer(splatRenderer.getPrepared().getBuffer(), readback.getBuffer(),
                            vk::DeviceSize(splatCount) * sizeof(SplatMath::PreparedSplat));

    std::vector<SplatMath::PreparedSplat> onCard(splatCount);
    readback.download(onCard.data(), splatCount * sizeof(SplatMath::PreparedSplat));

    // -------------------------------------------------------------------------------
    // Ista matematika na procesoru
    // -------------------------------------------------------------------------------

    double worstCentre = 0.0, worstConic = 0.0, worstDepth = 0.0, worstColour = 0.0;
    double worstRadius = 0.0;
    double worstFlat = 0.0;     //koliko se boja s karte razlikuje od boje BEZ smjera pogleda
    size_t visibleMismatch = 0;
    size_t visibleCount = 0;

    for(uint32_t i = 0; i < splatCount; ++i){
        SplatMath::PreparedSplat wanted;
        const bool visible = SplatMath::prepare(splats[i], view, focalX, focalY,
                                                principalX, principalY, blur, wanted);

        const SplatMath::PreparedSplat& got = onCard[i];
        const bool cardSaysVisible = got.conicOpacityDepth.w > 0.0f;

        //Vidljivost se mora slagati prije nego se bilo sto usporedjuje: splat koji je jedna
        //strana odbacila a druga nije nema smisla usporedjivati po vrijednostima
        if(visible != cardSaysVisible){
            ++visibleMismatch;
            continue;
        }
        if(!visible) continue;
        ++visibleCount;

        //Boja iz smjera pogleda, na procesoru
        const glm::vec3 colour = SplatMath::colorFromSH(dcValues[i], rest.data() + size_t(i) * coeffsPerChannel * 3,
                                                        coeffsPerChannel, degree,
                                                        splats[i].position - cameraPosition);
        const glm::vec3 clamped = glm::max(colour, glm::vec3(0.0f));

        //Sredina je glavna tocka plus pomak, i ta dva se cesto skoro ponište (320 - 319.9). Tada
        //je rezultat malen a greska zaokruzivanja velika koliko je velik POMAK - pa se mjeri
        //prema vecem od njih, ne prema rezultatu
        const float pixelTermX = std::max(principalX, std::fabs(wanted.centerConic.x - principalX));
        const float pixelTermY = std::max(principalY, std::fabs(wanted.centerConic.y - principalY));
        worstCentre = std::max(worstCentre, worstRelative(got.centerConic.x, wanted.centerConic.x, pixelTermX));
        worstCentre = std::max(worstCentre, worstRelative(got.centerConic.y, wanted.centerConic.y, pixelTermY));

        worstConic = std::max(worstConic, worstRelative(got.centerConic.z, wanted.centerConic.z, 1e-4f));
        worstConic = std::max(worstConic, worstRelative(got.centerConic.w, wanted.centerConic.w, 1e-4f));
        worstConic = std::max(worstConic, worstRelative(got.conicOpacityDepth.x, wanted.conicOpacityDepth.x, 1e-4f));

        worstDepth = std::max(worstDepth, worstRelative(got.conicOpacityDepth.z, wanted.conicOpacityDepth.z, 1e-3f));
        worstRadius = std::max(worstRadius, std::fabs(double(got.conicOpacityDepth.w) - double(wanted.conicOpacityDepth.w)));

        //Boja je zbroj sesnaest clanova velicine do jedan, pa zaokruzivanje ostavlja apsolutni
        //trag reda 1e-7 i kad je sam zbroj 0.001 - zato je pod 1.0, a ne velicina rezultata
        for(int c = 0; c < 3; ++c){
            worstColour = std::max(worstColour, worstRelative(got.color[c], clamped[c], 1.0f));
            worstFlat = std::max(worstFlat, std::fabs(double(got.color[c]) - double(glm::max(splats[i].color[c], 0.0f))));
        }
    }

    report.check("vidljivost se slaze", visibleMismatch == 0 && visibleCount > splatCount / 3,
        fmt("%zu splatova se ne slaze, %zu ih je vidljivo od %u", visibleMismatch, visibleCount, splatCount));

    report.check("sredina u pikselima", worstCentre < 1e-5,
        fmt("najveca relativna razlika %.2e", worstCentre));

    report.check("conic", worstConic < 1e-4,
        fmt("najveca relativna razlika %.2e kroz sva tri clana", worstConic));

    report.check("dubina", worstDepth < 1e-6,
        fmt("najveca relativna razlika %.2e", worstDepth));

    //Polumjer prolazi kroz ceil, pa je ili isti ili se razlikuje za cijeli piksel - relativna
    //razlika bi ovdje bila kriva mjera
    report.check("polumjer u pikselima", worstRadius < 1.5,
        fmt("najveca apsolutna razlika %.1f piksela", worstRadius));

    report.check("boja iz smjera pogleda", worstColour < 1e-4,
        fmt("najveca relativna razlika %.2e", worstColour));

    // -------------------------------------------------------------------------------
    // Kontrola: usporedba mora gristi
    // -------------------------------------------------------------------------------

    {
        //Ista scena, druga kamera. Ako bi usporedba prosla i s ovim, ne bi mjerila nista
        const glm::mat4 other = glm::lookAt(cameraPosition + glm::vec3(2.0f, 0.5f, 1.0f),
                                            glm::vec3(0.2f, 0.1f, -4.0f), glm::vec3(0,1,0));
        double worst = 0.0;
        size_t compared = 0;
        for(uint32_t i = 0; i < splatCount; ++i){
            SplatMath::PreparedSplat wanted;
            if(!SplatMath::prepare(splats[i], other, focalX, focalY, principalX, principalY, blur, wanted)) continue;
            if(onCard[i].conicOpacityDepth.w <= 0.0f) continue;
            worst = std::max(worst, worstRelative(onCard[i].centerConic.x, wanted.centerConic.x, 1.0f));
            ++compared;
        }
        report.check("usporedba grize", worst > 0.01 && compared > 100,
            fmt("s pomaknutom kamerom razlika naraste na %.2e kroz %zu splatova", worst, compared));
    }

    //Kontrola za boju: gornja kontrola mice kameru, pa ne bi primijetila shader koji sfernih
    //harmonika uopce nema - sredina bi mu i dalje bila tocna. Boja s kartice mora se od ravne
    //boje stupnja 0 razlikovati za puno vise nego sto je dopustena greska
    report.check("boja stvarno ovisi o smjeru", worstFlat > 0.1,
        fmt("od boje bez smjera pogleda razlikuje se do %.2e", worstFlat));

    report.checkNoValidationMessages();
    return report.result();
}
