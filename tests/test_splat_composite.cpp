// G3a: prva slika. Splatovi se slazu jedan preko drugog, grubom silom.
//
// Rasterizator s pločicama dolazi u G3b i njegov test ce biti da daje ISTU sliku kao ovaj. Zato
// ovaj mora biti ocito tocan, i zato se ne usporedjuje s drugom kopijom sebe nego s brojevima
// koji se znaju unaprijed:
//
//   vrh mrlje         u sredini je alfa tocno jednaka neprozirnosti, jer je exp(0) = 1
//   masa mrlje        zbroj alfe preko svih piksela je opacity * 2*pi*sigma^2 - integral
//                     gaussiana, analiticki. Ovo hvata krivi conic, krivi faktor 0.5, krivu
//                     velicinu piksela: sve tri promijene masu a ostave vrh na mjestu
//   redoslijed        dva splata preko istog piksela: C = a1*c1 + (1-a1)*a2*c2, i zamjena
//                     poretka mora dati drugi broj. Alfa kompozicija nije komutativna i to je
//                     jedini razlog zasto G2 uopce postoji
//   zaklon            neproziran splat sprijeda mora sakriti sve iza sebe, do zadnjeg bita
//   simetrija         kruzna mrlja je simetricna oko svoje sredine
//
// Slika je float, ne osam bita, i namjerno: usporedbe ostaju egzaktne umjesto da se lome o
// zaokruzivanje sRGB-a (vidi commit "prenosivost").
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/ComputeMaterial.h"
#include "Vulkan/VulkanImage.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

namespace{

struct Params{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t count = 0;
    uint32_t padding0 = 0;
};

struct Pixel{
    float r = 0, g = 0, b = 0, a = 0;
};

}

int main(){
    TestReport report("G3a kompozicija splatova");

    const vk::Extent2D size{256, 256};

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "splat"; config.engineName = "Loom tests";
    config.headless = true;
    LoomInitializer loom(config);

    // -------------------------------------------------------------------------------
    // Meta u koju se crta: float, da nista ne zaokruzuje
    // -------------------------------------------------------------------------------

    ImageConfig imageConfig;
    imageConfig.format = vk::Format::eR32G32B32A32Sfloat;
    imageConfig.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
    imageConfig.aspect = vk::ImageAspectFlagBits::eColor;
    VulkanImage target(loom.device, size, imageConfig);

    const vk::DeviceSize pixelBytes = vk::DeviceSize(size.width) * size.height * sizeof(Pixel);
    VulkanBuffer readback(loom.device, pixelBytes, vk::BufferUsageFlagBits::eTransferDst,
                          MemoryUsage::GPU_TO_CPU);

    const uint32_t maxSplats = 64;
    VulkanBuffer splatBuffer(loom.device, maxSplats * sizeof(SplatMath::PreparedSplat),
                             vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::CPU_TO_GPU);

    vk::DescriptorSetLayoutBinding bufferBinding;
    bufferBinding.binding = 0;
    bufferBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    bufferBinding.descriptorCount = 1;
    bufferBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding imageBinding = bufferBinding;
    imageBinding.binding = 1;
    imageBinding.descriptorType = vk::DescriptorType::eStorageImage;

    ComputePipelineConfig pipelineConfig;
    pipelineConfig.shaderPath = std::string(LOOM_SHADER_DIR) + "/splat_composite.comp.spv";
    pipelineConfig.descriptorBindings = {bufferBinding, imageBinding};
    pipelineConfig.pushConstantSize = sizeof(Params);
    VulkanComputePipeline pipeline = loom.createComputePipeline(pipelineConfig);

    ComputeMaterial material(loom.device, loom.getDescriptorPool(), pipeline);
    material.setStorageBuffer(0, splatBuffer);
    material.setStorageImage(1, target, vk::ImageLayout::eTransferSrcOptimal);

    //Jedan crtez: splatovi gore, dispatch, pikseli dolje
    auto draw = [&](const std::vector<SplatMath::PreparedSplat>& splats){
        splatBuffer.upload(splats.data(), splats.size() * sizeof(SplatMath::PreparedSplat));

        Params params;
        params.width = size.width; params.height = size.height;
        params.count = uint32_t(splats.size());

        loom.renderer.beginFrame();
        loom.renderer.dispatch(material, (size.width + 7) / 8, (size.height + 7) / 8, 1,
                               &params, sizeof(params));
        loom.renderer.endFrame();
        loom.waitIdle();

        loom.command.copyImageToBuffer(target.getImage(), readback.getBuffer(), size);

        std::vector<Pixel> pixels(size_t(size.width) * size.height);
        readback.download(pixels.data(), pixelBytes);
        return pixels;
    };

    auto at = [&](const std::vector<Pixel>& pixels, uint32_t x, uint32_t y){
        return pixels[size_t(y) * size.width + x];
    };

    // -------------------------------------------------------------------------------
    // Jedna kruzna mrlja: vrh, masa i simetrija
    // -------------------------------------------------------------------------------

    //Conic slozen rukom, da mrlja bude tocno kruzna sa zadanom sigmom. Tako se analiticki
    //odgovori znaju bez ijedne kamere u igri
    const float sigma = 6.0f;
    const float opacity = 0.7f;
    const glm::vec2 centre(128.5f, 96.5f);

    SplatMath::PreparedSplat round;
    round.centerConic = glm::vec4(centre.x, centre.y, 1.0f / (sigma * sigma), 0.0f);
    round.conicOpacityDepth = glm::vec4(1.0f / (sigma * sigma), opacity, 5.0f, 0.0f);
    round.color = glm::vec4(1.0f, 0.5f, 0.25f, 0.0f);

    {
        const std::vector<Pixel> image = draw({round});

        //Sredina piksela je na .5, pa je sredina mrlje tocno na jednom pikselu
        const Pixel peak = at(image, 128, 96);

        report.check("vrh je neprozirnost",
            std::fabs(peak.a - opacity) < 1e-6f &&
            std::fabs(peak.r - opacity * 1.0f) < 1e-6f &&
            std::fabs(peak.g - opacity * 0.5f) < 1e-6f,
            fmt("alfa %.6f (ocekivano %.6f), boja %.4f %.4f %.4f",
                double(peak.a), double(opacity), double(peak.r), double(peak.g), double(peak.b)));

        //Masa: integral gaussiana je 2*pi*sigma^2. Odsijecanje na 1/255 odnese nesto s ruba,
        //pa se trazi blizina a ne jednakost - ali blizina koja je i dalje tvrdnja
        double mass = 0.0;
        for(const Pixel& p : image) mass += p.a;
        const double analytic = double(opacity) * 2.0 * M_PI * double(sigma) * double(sigma);

        report.check("masa mrlje je integral gaussiana",
            std::fabs(mass - analytic) / analytic < 0.01,
            fmt("zbroj alfe %.2f, analiticki %.2f, razlika %.2f %%",
                mass, analytic, 100.0 * std::fabs(mass - analytic) / analytic));

        //Simetrija: cetiri piksela na istoj udaljenosti od sredine moraju biti jednaka
        const Pixel left  = at(image, 118, 96);
        const Pixel right = at(image, 138, 96);
        const Pixel up    = at(image, 128, 86);
        const Pixel down  = at(image, 128, 106);

        report.check("kruzna mrlja je simetricna",
            std::fabs(left.a - right.a) < 1e-7f && std::fabs(up.a - down.a) < 1e-7f &&
            std::fabs(left.a - up.a) < 1e-7f && left.a > 0.001f,
            fmt("lijevo %.7f desno %.7f gore %.7f dolje %.7f",
                double(left.a), double(right.a), double(up.a), double(down.a)));

        //I da je uopce nesto nacrtano, a ne pola slike
        size_t touched = 0;
        for(const Pixel& p : image) if(p.a > 0.0f) ++touched;
        report.check("mrlja je mrlja, ne pola slike",
            touched > 100 && touched < 2000,
            fmt("%zu piksela od %u ima nesto", touched, size.width * size.height));
    }

    // -------------------------------------------------------------------------------
    // Redoslijed: alfa kompozicija nije komutativna, i to je cijeli razlog za G2
    // -------------------------------------------------------------------------------

    {
        //Dva splata na istom mjestu, razlicitih boja
        SplatMath::PreparedSplat front = round;
        front.conicOpacityDepth.y = 0.6f;
        front.color = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

        SplatMath::PreparedSplat back = round;
        back.conicOpacityDepth.y = 0.8f;
        back.color = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);

        const std::vector<Pixel> redFirst = draw({front, back});
        const std::vector<Pixel> blueFirst = draw({back, front});

        const Pixel a = at(redFirst, 128, 96);
        const Pixel b = at(blueFirst, 128, 96);

        //Analiticki, u sredini gdje je exp(0) = 1
        const float alphaFront = 0.6f, alphaBack = 0.8f;
        const float expectedRed = alphaFront;                       //crveni je sprijeda
        const float expectedBlue = (1.0f - alphaFront) * alphaBack; //plavi dobiva ostatak

        report.check("redoslijed daje analiticku vrijednost",
            std::fabs(a.r - expectedRed) < 1e-6f && std::fabs(a.b - expectedBlue) < 1e-6f,
            fmt("crveno %.6f (ocekivano %.6f), plavo %.6f (ocekivano %.6f)",
                double(a.r), double(expectedRed), double(a.b), double(expectedBlue)));

        report.check("zamjena poretka mijenja sliku",
            std::fabs(a.r - b.r) > 0.1f && std::fabs(a.b - b.b) > 0.1f,
            fmt("crveno %.4f naspram %.4f, plavo %.4f naspram %.4f",
                double(a.r), double(b.r), double(a.b), double(b.b)));

        //I da je ukupna alfa ista bez obzira na poredak - koliko je svjetla zaustavljeno ne
        //ovisi o tome kojim je redom zaustavljeno
        report.check("zaustavljeno svjetlo ne ovisi o poretku",
            std::fabs(a.a - b.a) < 1e-6f,
            fmt("%.7f naspram %.7f", double(a.a), double(b.a)));
    }

    // -------------------------------------------------------------------------------
    // Zaklon: neproziran splat sprijeda sakriva sve iza sebe
    // -------------------------------------------------------------------------------

    {
        SplatMath::PreparedSplat opaque = round;
        opaque.conicOpacityDepth.y = 1.0f;
        opaque.color = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

        SplatMath::PreparedSplat hidden = round;
        hidden.conicOpacityDepth.y = 1.0f;
        hidden.color = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

        const std::vector<Pixel> image = draw({opaque, hidden});
        const Pixel centre2 = at(image, 128, 96);

        //Alfa je odsjecena na 0.99, pa iza prvog splata ostane tocno jedan posto
        report.check("zaklon",
            std::fabs(centre2.g - 0.99f) < 1e-6f &&
            std::fabs(centre2.r - 0.01f * 0.99f) < 1e-5f,
            fmt("zeleno %.6f (odsjeceno na 0.99), crveno %.6f (ostatak 1%% od 0.99)",
                double(centre2.g), double(centre2.r)));
    }

    // -------------------------------------------------------------------------------
    // I sve to kroz pravu kameru, ne kroz rucno slozen conic
    // -------------------------------------------------------------------------------

    {
        //Kugla na osi, na poznatoj udaljenosti: sredina mora pasti na glavnu tocku, a
        //polumjer mora biti f*s/d - isti broj koji je G1 provjerio bez ijednog piksela
        const float focal = 400.0f;
        const float radius = 0.05f;
        const float distance = 3.0f;

        Splat splat;
        splat.position = glm::vec3(0.0f, 0.0f, -distance);
        splat.scale = glm::vec3(radius);
        splat.opacity = 0.9f;
        splat.color = glm::vec3(0.2f, 0.8f, 0.4f);

        const glm::mat4 view = glm::lookAt(glm::vec3(0,0,0), glm::vec3(0,0,-1), glm::vec3(0,1,0));

        SplatMath::PreparedSplat prepared;
        const bool visible = SplatMath::prepare(splat, view, focal, -focal,
                                                0.5f * size.width, 0.5f * size.height,
                                                0.0f, prepared);

        const std::vector<Pixel> image = draw({prepared});

        const float expectedSigma = focal * radius / distance;
        double mass = 0.0;
        for(const Pixel& p : image) mass += p.a;
        const double analytic = double(splat.opacity) * 2.0 * M_PI * double(expectedSigma * expectedSigma);

        report.check("kroz kameru: sredina i masa",
            visible &&
            std::fabs(prepared.centerConic.x - 0.5f * size.width) < 1e-4f &&
            std::fabs(prepared.centerConic.y - 0.5f * size.height) < 1e-4f &&
            std::fabs(mass - analytic) / analytic < 0.02,
            fmt("sredina %.3f,%.3f (ocekivano %.1f,%.1f), sigma %.3f px, masa %.1f naspram %.1f",
                double(prepared.centerConic.x), double(prepared.centerConic.y),
                double(0.5f * size.width), double(0.5f * size.height),
                double(expectedSigma), mass, analytic));

        //Splat iza kamere se ne priprema
        Splat behind = splat;
        behind.position = glm::vec3(0.0f, 0.0f, 1.0f);
        SplatMath::PreparedSplat ignored;
        report.check("iza kamere se ne crta",
            !SplatMath::prepare(behind, view, focal, -focal, 128.0f, 128.0f, 0.0f, ignored),
            "prepare vraca false");
    }

    // -------------------------------------------------------------------------------
    // I da dva ista crteza daju istu sliku
    // -------------------------------------------------------------------------------

    {
        const std::vector<Pixel> first = draw({round});
        const std::vector<Pixel> second = draw({round});

        size_t different = 0;
        for(size_t i = 0; i < first.size(); ++i){
            if(std::memcmp(&first[i], &second[i], sizeof(Pixel)) != 0) ++different;
        }

        report.check("isti crtez daje istu sliku", different == 0,
            fmt("%zu od %zu piksela razlike", different, first.size()));
    }

    report.checkNoValidationMessages();
    return report.result();
}
