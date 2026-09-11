// Neizravni dispatch: broj parova koji zna samo kartica, i velicine koje ona iz njega izvodi.
//
// test_splat_tiles vec dokazuje da neizravni put daje istu sliku kao gruba sila. Ono sto on NE
// MOZE uhvatiti, jer crta jedan kadar po rendereru, brani se ovdje:
//
//   zastarjele velicine   isti SplatRenderer, kadrovi s vrlo razlicitim brojem parova zaredom.
//                         Da se velicine ne racunaju iznova, drugi kadar bi crtao s brojem
//                         prvoga. Usporedjuje se BAJT ZA BAJT s kadrom koji nista nije prethodilo
//                         - ista matematika na istoj kartici, pa nema izlike za ijedan bit
//   oba smjera            vise pa manje parova (zastarjeli broj bi citao smece iza kraja) i manje
//                         pa vise (zastarjeli broj bi izostavio dio parova). To su dvije
//                         razlicite greske i svaka ima svoju provjeru
//   cijeli put            priprema na kartici + neizravni dispatch, protiv pripreme na procesoru
//                         kroz isti rasterizator. Tu razlika smije postojati, u zadnjim bitovima -
//                         i dokazuje se da je SVA iz pripreme: splatovi pripremljeni na kartici,
//                         procitani i provuceni kroz upload, daju iste bajtove kao put na kartici
//   prekoracenje          scena koja trazi vise parova nego sto ima mjesta mora to reci, i
//                         sljedeci kadar koji stane mora opet biti tocan
//
// STO OVAJ TEST NE DOKAZUJE: da sirenje ne pise izvan polja kad se prekoraci. Pisanje po tudjoj
// memoriji na kartici nitko ne javlja, a buffer koji bi se pokvario prepise se ionako sljedeci
// kadar. Granica u splat_expand stoji zato sto je ocito potrebna, ne zato sto ju je test vidio.
#include "TestHarness.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/SplatRenderer.h"
#include "Vulkan/VulkanImage.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace{

struct Pixel{
    float r, g, b, a;
};

size_t differentPixels(const std::vector<Pixel>& first, const std::vector<Pixel>& second){
    size_t different = 0;
    for(size_t i = 0; i < first.size(); ++i){
        if(std::memcmp(&first[i], &second[i], sizeof(Pixel)) != 0) ++different;
    }
    return different;
}

}

int main(){
    TestReport report("neizravni dispatch");

    const vk::Extent2D size{400, 300};

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "indirect"; config.engineName = "Loom tests";
    config.headless = true;
    //Jedan SplatRenderer trazi 21 set i 73 storage buffera - vise nego sto default od 64 po tipu
    //daje. Test ih zato i gasi cim ne trebaju: u jednom trenutku zivi najvise jedan
    config.maxDescriptorSets = 128;
    LoomInitializer loom(config);

    ImageConfig imageConfig;
    imageConfig.format = vk::Format::eR32G32B32A32Sfloat;
    imageConfig.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
    VulkanImage target(loom.device, size, imageConfig);

    const vk::DeviceSize pixelBytes = vk::DeviceSize(size.width) * size.height * sizeof(Pixel);
    VulkanBuffer readback(loom.device, pixelBytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);

    auto readImage = [&]{
        loom.command.copyImageToBuffer(target.getImage(), readback.getBuffer(), size);
        std::vector<Pixel> pixels(size_t(size.width) * size.height);
        readback.download(pixels.data(), pixelBytes);
        return pixels;
    };

    // -------------------------------------------------------------------------------
    // Scena: stupanj 3, kao pravi file
    // -------------------------------------------------------------------------------

    const uint32_t splatCount = 3000;
    const uint32_t coeffsPerChannel = 15;
    const uint32_t degree = 3;

    std::mt19937 random(20260911);
    auto uniform = [&](float low, float high){
        return low + (high - low) * float(random() % 1000000) / 1000000.0f;
    };

    std::vector<Splat> splats;
    std::vector<SplatMath::RawSplat> raw;
    std::vector<glm::vec3> dcValues;
    std::vector<float> rest;

    for(uint32_t i = 0; i < splatCount; ++i){
        Splat splat;
        splat.position = glm::vec3(uniform(-3.0f, 3.0f), uniform(-2.0f, 2.0f), uniform(-3.0f, 3.0f));
        const float base = uniform(0.02f, 0.25f);
        splat.scale = glm::vec3(base, base * uniform(0.2f, 2.5f), base * uniform(0.2f, 2.5f));
        splat.rotation = glm::normalize(glm::quat(uniform(-1,1), uniform(-1,1), uniform(-1,1), uniform(-1,1)));
        splat.opacity = uniform(0.05f, 0.95f);

        const glm::vec3 dc(uniform(-1.5f, 1.5f), uniform(-1.5f, 1.5f), uniform(-1.5f, 1.5f));
        splat.color = SplatMath::colorFromSH0(dc);
        splats.push_back(splat);
        dcValues.push_back(dc);

        SplatMath::RawSplat one;
        one.positionOpacity = glm::vec4(splat.position, splat.opacity);
        one.scale = glm::vec4(splat.scale, 0.0f);
        one.rotation = glm::vec4(splat.rotation.w, splat.rotation.x, splat.rotation.y, splat.rotation.z);
        one.dc = glm::vec4(dc, 0.0f);
        raw.push_back(one);

        for(uint32_t k = 0; k < coeffsPerChannel * 3; ++k){
            rest.push_back(uniform(-0.3f, 0.3f));
        }
    }

    const float focalX = 360.0f;
    const float focalY = -360.0f;
    const float principalX = 0.5f * size.width;
    const float principalY = 0.5f * size.height;
    const float blur = 0.3f;

    //Blizu i daleko: blizu su mrlje vece i svaka zahvaca vise pločica, pa parova ima puno vise
    const glm::vec3 eyeNear(1.0f, 0.8f, 5.5f);
    const glm::vec3 eyeFar(-3.0f, 2.0f, 16.0f);
    const glm::vec3 lookAt(0.0f);

    auto viewFrom = [&](const glm::vec3& eye){
        return glm::lookAt(eye, lookAt, glm::vec3(0, 1, 0));
    };

    auto makeRenderer = [&](uint32_t maxPairs){
        SplatRendererConfig rendererConfig;
        rendererConfig.maxSplats = splatCount;
        rendererConfig.maxPairs = maxPairs;
        rendererConfig.maxShCoefficients = coeffsPerChannel * 3;
        auto renderer = std::make_unique<SplatRenderer>(loom.device, loom.getDescriptorPool(), target, size, rendererConfig);
        renderer->uploadRaw(raw, rest, degree, coeffsPerChannel);
        return renderer;
    };

    //Cijeli put na kartici: priprema, pa sve ostalo, bez ijednog broja s procesora
    auto drawOnCard = [&](SplatRenderer& splatRenderer, const glm::vec3& eye){
        splatRenderer.setCamera(viewFrom(eye), eye, focalX, focalY, principalX, principalY, blur);
        loom.renderer.beginFrame();
        splatRenderer.prepare(loom.renderer, splatCount);
        splatRenderer.draw(loom.renderer, splatCount);
        loom.renderer.endFrame();
        loom.waitIdle();
        return readImage();
    };

    // -------------------------------------------------------------------------------
    // Zastarjele velicine: blizu, daleko, blizu - na istom rendereru
    // -------------------------------------------------------------------------------

    auto sequence = makeRenderer(1u << 20);

    const std::vector<Pixel> nearFirst = drawOnCard(*sequence, eyeNear);
    const uint32_t pairsNear = sequence->requestedPairs();

    const std::vector<Pixel> farSecond = drawOnCard(*sequence, eyeFar);
    const uint32_t pairsFar = sequence->requestedPairs();

    const std::vector<Pixel> nearThird = drawOnCard(*sequence, eyeNear);
    const uint32_t pairsNearAgain = sequence->requestedPairs();
    sequence.reset();

    auto fresh = makeRenderer(1u << 20);
    const std::vector<Pixel> farFresh = drawOnCard(*fresh, eyeFar);
    const uint32_t pairsFarFresh = fresh->requestedPairs();
    fresh.reset();

    //Ovo mora biti istina da bi sljedece dvije provjere ista znacile: s istim brojem parova
    //zastarjele velicine bile bi slucajno tocne
    report.check("kadrovi traze vrlo razlicit broj parova", pairsFar > 0 && pairsNear > 2 * pairsFar,
        fmt("blizu %u, daleko %u", pairsNear, pairsFar));

    report.check("vise pa manje parova: kao da prvog kadra nije bilo",
        differentPixels(farSecond, farFresh) == 0 && pairsFar == pairsFarFresh,
        fmt("%zu od %zu piksela razlike, parova %u/%u", differentPixels(farSecond, farFresh),
            farFresh.size(), pairsFar, pairsFarFresh));

    report.check("manje pa vise parova: isto kao prvi kadar",
        differentPixels(nearThird, nearFirst) == 0 && pairsNearAgain == pairsNear,
        fmt("%zu od %zu piksela razlike, parova %u/%u", differentPixels(nearThird, nearFirst),
            nearFirst.size(), pairsNearAgain, pairsNear));

    report.check("usporedba grize", differentPixels(nearFirst, farSecond) > nearFirst.size() / 4,
        fmt("blizu i daleko razlikuju se u %zu od %zu piksela", differentPixels(nearFirst, farSecond),
            nearFirst.size()));

    // -------------------------------------------------------------------------------
    // Cijeli put na kartici protiv pripreme na procesoru
    // -------------------------------------------------------------------------------

    {
        //Pripremljeni splatovi s kartice, procitani natrag. Provuceni kroz upload moraju dati
        //ISTE BAJTOVE kao put na kartici - i time se odvaja priprema od svega sto je iza nje
        auto onCard = makeRenderer(1u << 20);
        const std::vector<Pixel> cardImage = drawOnCard(*onCard, eyeNear);

        const vk::DeviceSize preparedBytes = vk::DeviceSize(splatCount) * sizeof(SplatMath::PreparedSplat);
        VulkanBuffer preparedBack(loom.device, preparedBytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
        loom.command.copyBuffer(onCard->getPrepared().getBuffer(), preparedBack.getBuffer(), preparedBytes);
        std::vector<SplatMath::PreparedSplat> cardPrepared(splatCount);
        preparedBack.download(cardPrepared.data(), preparedBytes);
        onCard.reset();

        auto drawUploaded = [&](const std::vector<SplatMath::PreparedSplat>& prepared, uint32_t& cpuPairs){
            auto uploaded = makeRenderer(1u << 20);
            uploaded->upload(prepared);
            cpuPairs = uploaded->countPairs(prepared);
            loom.renderer.beginFrame();
            uploaded->draw(loom.renderer, uint32_t(prepared.size()));
            loom.renderer.endFrame();
            loom.waitIdle();
            return readImage();
        };

        auto prepareOnCpu = [&](float withBlur){
            const glm::mat4 view = viewFrom(eyeNear);
            std::vector<SplatMath::PreparedSplat> prepared;
            for(uint32_t i = 0; i < splatCount; ++i){
                SplatMath::PreparedSplat one;
                if(!SplatMath::prepare(splats[i], view, focalX, focalY, principalX, principalY, withBlur, one)) continue;
                const glm::vec3 colour = SplatMath::colorFromSH(dcValues[i], rest.data() + size_t(i) * coeffsPerChannel * 3,
                                                                coeffsPerChannel, degree, splats[i].position - eyeNear);
                one.color = glm::vec4(glm::max(colour, glm::vec3(0.0f)), 0.0f);
                prepared.push_back(one);
            }
            return prepared;
        };

        struct Difference{
            double worst = 0.0;
            size_t overTenthCode = 0;
            size_t overCode = 0;
        };
        auto compare = [](const std::vector<Pixel>& first, const std::vector<Pixel>& second){
            Difference difference;
            for(size_t i = 0; i < first.size(); ++i){
                const double d = std::max({std::fabs(double(first[i].r) - second[i].r), std::fabs(double(first[i].g) - second[i].g),
                                           std::fabs(double(first[i].b) - second[i].b), std::fabs(double(first[i].a) - second[i].a)});
                difference.worst = std::max(difference.worst, d);
                if(d > 0.1 / 255.0) ++difference.overTenthCode;
                if(d > 1.0 / 255.0) ++difference.overCode;
            }
            return difference;
        };

        uint32_t uploadedPairs = 0, cpuPairs = 0, blurredPairs = 0;
        const std::vector<Pixel> uploadedImage = drawUploaded(cardPrepared, uploadedPairs);
        const std::vector<Pixel> cpuImage = drawUploaded(prepareOnCpu(blur), cpuPairs);
        const std::vector<Pixel> blurredImage = drawUploaded(prepareOnCpu(blur + 0.05f), blurredPairs);

        const Difference cpu = compare(cpuImage, cardImage);
        const Difference blurred = compare(blurredImage, cardImage);

        report.check("priprema kroz dispatch = isti bajtovi kroz upload",
            differentPixels(uploadedImage, cardImage) == 0 && uploadedPairs == pairsNear,
            fmt("%zu od %zu piksela razlike, parova %u/%u", differentPixels(uploadedImage, cardImage),
                cardImage.size(), uploadedPairs, pairsNear));

        report.check("isti broj parova kao procesor", cpuPairs == pairsNear,
            fmt("procesor %u, kartica %u", cpuPairs, pairsNear));

        //MJERI SE BROJ PIKSELA, NE NAJVECA RAZLIKA - izmjereno, i prva verzija je pala bas na tome.
        //Zadnji bitovi pripreme prolaze kroz desetke slojeva i kroz pragove (alfa ispod 1/255,
        //rani prekid), pa pojedini piksel skoci na 1.16e-3. Mala prava greska (blur 0.35) dala
        //je najvise 4.30e-3 - samo 3.7 puta vise, i prag izmedju bio bi slucajan. Broj piksela
        //razdvaja jasno:
        //
        //                        iznad 0.1 koda    iznad koda
        //   zaokruzivanje              2               0
        //   blur 0.35                160              14
        //
        //Granica 16 je osam puta iznad prvog i deset puta ispod drugog
        const size_t tenthCodeLimit = 16;

        report.check("ista slika kao priprema na procesoru",
            cpu.overCode == 0 && cpu.overTenthCode <= tenthCodeLimit,
            fmt("iznad 0.1 koda %zu, iznad koda %zu piksela (najvise %.2e)", cpu.overTenthCode, cpu.overCode, cpu.worst));

        report.check("kontrola: blur 0.35 umjesto 0.30 ne bi prosao",
            blurred.overCode > 0 || blurred.overTenthCode > tenthCodeLimit,
            fmt("iznad 0.1 koda %zu, iznad koda %zu piksela (najvise %.2e)", blurred.overTenthCode, blurred.overCode, blurred.worst));
    }

    // -------------------------------------------------------------------------------
    // Prekoracenje
    // -------------------------------------------------------------------------------

    {
        const uint32_t room = pairsNear / 2;
        auto cramped = makeRenderer(room);

        const std::vector<Pixel> cut = drawOnCard(*cramped, eyeNear);
        const uint32_t asked = cramped->requestedPairs();
        const uint32_t drawn = cramped->lastPairCount();

        size_t broken = 0;
        for(const Pixel& p : cut){
            const bool finite = std::isfinite(p.r) && std::isfinite(p.g) && std::isfinite(p.b) && std::isfinite(p.a);
            if(!finite || p.a < 0.0f || p.a > 1.0f) ++broken;
        }

        report.check("prekoracenje se javlja", asked == pairsNear && drawn == room,
            fmt("scena trazi %u, mjesta ima %u, nacrtano %u", asked, room, drawn));

        report.check("slika bez dijela parova je nepotpuna ali ispravna",
            differentPixels(cut, nearFirst) > 0 && broken == 0,
            fmt("%zu piksela drukcije od pune slike, %zu neispravnih", differentPixels(cut, nearFirst), broken));

        const std::vector<Pixel> after = drawOnCard(*cramped, eyeFar);
        report.check("sljedeci kadar koji stane opet je tocan",
            pairsFar <= room && differentPixels(after, farFresh) == 0 && cramped->requestedPairs() == pairsFar,
            fmt("daleko %u parova u %u mjesta, %zu piksela razlike od svjezeg", pairsFar, room,
                differentPixels(after, farFresh)));
    }

    report.checkNoValidationMessages();
    return report.result();
}
