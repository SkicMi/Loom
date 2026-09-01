// B1: budzet kadra - sto se u kadru smije dogoditi, i koliko smije kostati
//
// Svih 45 testova prije ovoga mjeri PIKSELE. Trostruko sporiji Loom prolazi ih sve bez ijedne
// rijeci, jer nijedan ne pita koliko je posla trebalo. Ovaj pita.
//
// Vrijeme je pritom mjera koju je lako kriviti. Tri stvari su probane, i dvije su odbacene
// mjerenjem prije nego su napisane kao tvrdnja:
//
//   milisekunda po kadru   isti kod je 0.81 ms na GTX 1650 i 6.5 ms na lavapipeu. Prag koji
//                          prolazi tamo ne hvata nista ovdje
//   omjer prema praznom    prazan kadar mjeri SLANJE naredbi, posao drivera a ne GPU-a:
//     kadru                omjer je 5.4 ovdje i 16 tamo
//   mjerna sipka           compute lanac cjelobrojnih operacija, kao jedinica tudjeg posla
//                          koji Loom ne moze promijeniti. Rasap je ispao 30x (275 milijuna
//                          lanaca po kadru ovdje, 9 milijuna tamo) - jer ALU i rasterizacija
//                          ne skaliraju zajedno izmedju kartice i softverskog rasterizatora.
//                          Gore od onoga sto je trebala zamijeniti, pa je bacena
//
// Ostaje ono sto JEST prenosivo, i to su prve dvije provjere: da kadar ne alocira, i da cijena
// po objektu ne raste s brojem objekata. Stropovi su brojevi o STROJU, pa ih drzi klasa
// uredjaja i tako su i zapisani - jedan za cijeli kadar, jedan za sam piksel.
//
// I jedna stvar koju je mjerenje ispravilo usput: prva scena su bile 25 sitnih kugli, i u njoj
// je trostruko skuplji fragment shader prosao kroz sve provjere a da se nijedan broj nije
// pomakao. Zato scena sad ima plohu preko cijelog kadra, i zato se ista scena mjeri i na
// cetiri puta vise piksela: ta razlika je jedino mjesto gdje se sjencanje vidi samo.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/Light.h"
#include "Core/LoomShapes.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Material.h"
#include "Vulkan/VulkanAllocator.h"

#include <algorithm>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const vk::Extent2D size{512, 512};

std::vector<glm::mat4> grid(){
    std::vector<glm::mat4> out;
    for(int y = -2; y <= 2; ++y){
        for(int x = -2; x <= 2; ++x){
            out.push_back(glm::translate(glm::mat4(1.0f), {0.55f * x, 0.55f * y, 0.0f})
                        * glm::scale(glm::mat4(1.0f), glm::vec3(0.42f)));
        }
    }
    return out;
}

double median(std::vector<double> values){
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}

int main(){
    TestReport report("B1 budzet kadra");

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "budget"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 3.0f};
    Camera camera(cameraConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {0.15f, 0.15f, 0.18f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig lightConfig;
    lightConfig.type = LightType::Directional;
    lightConfig.direction = {-0.3f, -1.0f, -0.4f};
    lightConfig.shadowExtent = 2.0f;
    Light light(lightConfig);
    loom.renderer.addLight(light);

    LoomShapes::Primitives primitives(loom);

    RenderTarget shadowMap(loom.device, size, makeShadowMapConfig());

    RenderTargetConfig colorConfig;
    colorConfig.keepDepth = true;
    RenderTarget colorTarget(loom.device, size, colorConfig);

    //ISTA SCENA, CETIRI PUTA VISE PIKSELA.
    //
    //Kadar u cjelini je na kartici ovoga reda tvrdoglav: dodati fragment shaderu 220 sinusa
    //po pikselu digne ga za 16 posto, jer ga drze draw pozivi a ne fragmenti. Razlika izmedju
    //ova dva kadra nema tu manu - draw pozivi su isti, prolaz sjena je isti, razlikuje se samo
    //786432 dodatnih piksela. To je onda cijena SJENCANJA i nicega drugog
    const vk::Extent2D bigSize{size.width * 2, size.height * 2};
    RenderTarget bigTarget(loom.device, bigSize, colorConfig);

    PipelineConfig shadowPipelineConfig;
    shadowPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/shadow.vert.spv";
    shadowPipelineConfig.fragShaderPath = "";
    shadowPipelineConfig.enableColor = false;
    shadowPipelineConfig.vertexAttributes = Vertex::getPositionAttribute();
    shadowPipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    shadowPipelineConfig.depthTestEnable = true;
    shadowPipelineConfig.depthWriteEnable = true;
    VulkanGraphicsPipeline shadowPipeline(loom.device, shadowPipelineConfig,
        loom.getColorFormat(), shadowMap.getDepthFormat());
    Material shadowMaterial(shadowPipeline);

    const std::vector<glm::mat4> models = grid();

    //Ploha stoji iza kugli i ispunjava kadar - fragmentni posao koji scena inace ne bi imala
    const glm::mat4 backdrop = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -1.0f})
                             * glm::scale(glm::mat4(1.0f), glm::vec3(6.0f));

    //Kadar se mjeri ZAJEDNO s cekanjem na GPU. Bez toga bi se mjerilo samo koliko traje slanje
    //naredbi - a to je onaj dio posla koji regresija u sjencanju uopce ne dira
    auto frameInto = [&](size_t count, const RenderTarget& target){
        const auto started = std::chrono::steady_clock::now();

        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(shadowMap, light);
            for(size_t i = 0; i < count; ++i){
                loom.renderer.draw(primitives.cubeMesh(), models[i], shadowMaterial);
            }
            loom.renderer.endPass();

            loom.renderer.beginPass(target);

            //Ploha preko cijelog kadra, i to UVIJEK, i u praznom kadru. Bez nje scena nije
            //fragmentno vezana: 25 sitnih kugli trosi draw pozive, pa je trostruko skuplji
            //fragment shader kroz mjerenje prosao a da se broj nije ni pomakao. Buduci da je
            //u svakom kadru, ispada iz granicne cijene po objektu i ne kvari drugu provjeru
            primitives.plane(backdrop);

            for(size_t i = 0; i < count; ++i) primitives.sphere(models[i]);
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();

        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    };

    auto frame = [&](size_t count){ return frameInto(count, colorTarget); };

    //Zagrijavanje: prvi kadrovi grade mesheve, materijale i cjevovode. Kad bi se i oni mjerili,
    //mjerilo bi se gradjenje a ne crtanje
    for(int i = 0; i < 5; ++i) frame(models.size());

    // -------------------------------------------------------------------------------
    // Kadar ne smije alocirati
    // -------------------------------------------------------------------------------

    const MemoryStats before = loom.device.getAllocator().getStats();
    VulkanAllocator::resetAllocationsMade();

    const int frames = 40;
    std::vector<double> full, few, empty;
    for(int i = 0; i < frames; ++i) full.push_back(frame(models.size()));
    for(int i = 0; i < frames; ++i) few.push_back(frame(5));
    for(int i = 0; i < frames; ++i) empty.push_back(frame(0));

    //NAIZMJENICE, i razlika se uzima po PARU. Dva odvojena niza pa razlika njihovih minimuma
    //je previse nervozna: dovoljno je da veliki kadar uhvati tudji posao na stroju a mali ne,
    //i broj skoci trostruko (vidjeno 0.070 do 0.244 kroz cetiri pokretanja). Par je snimljen
    //u istom trenutku, pa ga opterecenje pomakne cijelog
    for(int i = 0; i < 3; ++i) frameInto(models.size(), bigTarget);

    std::vector<double> pairs;
    for(int i = 0; i < frames; ++i){
        const double small = frameInto(models.size(), colorTarget);
        const double large = frameInto(models.size(), bigTarget);
        pairs.push_back(large - small);
    }

    const MemoryStats after = loom.device.getAllocator().getStats();
    const uint64_t made = VulkanAllocator::getAllocationsMade();

    //Dvije razlicite tvrdnje, i trebaju obje: broj koji stoji kaze da nista nije ostalo za
    //sobom, a broj koji je NAPRAVLJEN kaze da se nista nije ni dogodilo. Alokacija koja se u
    //istom kadru oslobodi je nevidljiva prvome, a skupa jednako kao i svaka druga
    report.check("kadar ne alocira",
        made == 0 &&
        after.allocationCount == before.allocationCount && after.blockCount == before.blockCount,
        fmt("%llu novih alokacija kroz %d kadrova; stanje %u -> %u alokacija, %u -> %u blokova",
            (unsigned long long)made, 3 * frames,
            before.allocationCount, after.allocationCount,
            before.blockCount, after.blockCount));

    // -------------------------------------------------------------------------------
    // Cijena po objektu ne smije rasti s brojem objekata
    // -------------------------------------------------------------------------------

    const double fullMedian = median(full);
    const double emptyMedian = median(empty);

    //Najbolji kadar, ne medijan: medijan hvata i tudji posao na stroju, pa je omjer ispod
    //njega znao skociti s 0.9 na 0.55 bez ijedne izmjene u kodu. Najbolji kadar je ono sto je
    //kod sposoban napraviti kad ga se pusti, i to je jedina brojka koja se smije usporedjivati
    const double fullBest = *std::min_element(full.begin(), full.end());
    const double fewBest = *std::min_element(few.begin(), few.end());
    const double emptyBest = *std::min_element(empty.begin(), empty.end());

    //Prazan kadar se oduzima jer je fiksni trosak prolaza: ostaje sam crtez. Ovo je jedina
    //vremenska tvrdnja koja ne ovisi o kartici. Izmjereno na najboljim kadrovima: 0.84 kroz
    //cetiri pokretanja na GTX 1650, i 0.60 do 1.12 na lavapipeu, gdje je prazan kadar
    //dovoljno malen da ga tudji posao na stroju zaljulja. Prag 2.0 je iznad svega toga, a
    //kvadratna cijena bi ovdje dala peticu
    const double perObjectMany = (fullBest - emptyBest) / double(models.size());
    const double perObjectFew = (fewBest - emptyBest) / 5.0;
    const double growth = perObjectMany / perObjectFew;

    report.check("cijena je linearna u broju objekata",
        perObjectFew > 0.0 && growth < 2.0,
        fmt("%.4f ms po objektu na %zu, %.4f na 5 -> omjer %.2f (mjereno 0.60-1.12 na dvije kartice)",
            perObjectMany, models.size(), perObjectFew, growth));

    // -------------------------------------------------------------------------------
    // I strop, koji je broj o OVOM stroju
    // -------------------------------------------------------------------------------

    const double best = fullBest;

    //Dva broja, jer su dvije klase uredjaja i na obje se ovo vrti. Izmjereno: 0.81 ms na
    //GTX 1650, 6.3 do 7.1 ms na lavapipeu - strop je oko cetiri puta iznad toga. Hvata samo
    //grubu regresiju, i to je posteno reci: sam kadar drze draw pozivi, pa sporiji fragment
    //shader ovdje ne bi ni pisnuo. Njega hvata provjera iznad
    //Cijena sjencanja: sve osim piksela je u oba kadra isto, pa razlika pripada njima
    //MEDIJAN para, ne najbolji: najbolji uzima onaj par u kojem je veliki kadar slucajno bio
    //brz a mali spor, pa je izmjerio nulu (0.001 ns kroz pet pokretanja, uz 3.065 na sljedecem).
    //Sum je oko prave vrijednosti simetrican, pa ga medijan skrati a rub ne
    const double bestPair = median(pairs);
    const double extraPixels = double(bigSize.width) * bigSize.height - double(size.width) * size.height;
    const double perPixel = 1.0e6 * bestPair / extraPixels;   //nanosekunde po pikselu

    //Izmjereno kroz sest pokretanja: 0.100 do 0.109 ns po pikselu na GTX 1650, i 6.15 do 6.61
    //na lavapipeu - pa je i ovaj strop po klasi uredjaja, na dva i pol puta iznad najgoreg. Mutacija
    //koja fragmentu doda 220 sinusa dize ovaj broj sedam puta i obara ovu provjeru, dok
    //cijeli kadar ostaje na 0.96 ms i strop od 3 ms mirno prolazi. Zato su to dva broja
    const bool software = loom.device.getDeviceType() == vk::PhysicalDeviceType::eCpu;
    const double ceiling = software ? 24.0 : 3.0;

    report.check("sjencanje ostaje unutar stropa",
        perPixel > 0.0 && perPixel < (software ? 16.0 : 0.28),
        fmt("%.3f ns po pikselu iz %d parova (%ux%u naspram %ux%u, srednja razlika %.3f ms), strop %.2f",
            perPixel, frames, bigSize.width, bigSize.height, size.width, size.height, bestPair,
            software ? 16.0 : 0.28));

    report.check("kadar ostaje unutar stropa",
        best < ceiling,
        fmt("%s: najbolji %.3f ms, medijan %.3f, prazan %.3f, strop %.1f ms",
            loom.device.getDeviceName().c_str(), best, fullMedian, emptyMedian, ceiling));

    report.checkNoValidationMessages();
    return report.result();
}
