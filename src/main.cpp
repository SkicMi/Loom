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
#include <algorithm>
#include <vector>
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
               "  %s <snimka|slika> [dubina.pfm] [najblize_m] [najdalje_m] [opcije]\n\n"
               "Bez karte dubine snimka se samo prikazuje.\n"
               "S njom se u nju ubacuje svjetlo koje kruzi oko scene.\n\n"
               "  --save <file.png>   jedan kadar na disk umjesto prozora\n"
               "  --angle <stupnjeva> gdje je svjetlo na kruznici (uz --save)\n"
               "  --fov <stupnjeva>   okomiti kut kamere kojom je snimljeno (default 50)\n"
               "  --mask <maska.png>  silueta subjekta: debljina zaklona, i sjenu baca samo ona\n"
               "  --occluder-blur <px> polumjer medijana kojim trag cita dubinu (default 3)\n"
               "  --thickness <m>     debljina zaklona rukom, umjesto izvedene\n"
               "  --radius <n>        razmak iz kojeg se racunaju normale (default 3)\n"
               "  --noise <nagib>     koliko nagiba model izmislja na ravnom (default 0.30)\n"
               "  --limit <nagib>     gornja granica nagiba, 0 iskljucuje (default 6)\n"
               "  --specular <0..1>   koliko ploha odsjaji (default 0.10)\n"
               "  --ao <0..1>         ambijentna okluzija iz dubine (default 0.6)\n"
               "  --no-shadow         bez trazenja zaklona\n", argv[0]);
        return 1;
    }

    try{
        const std::string platePath = argv[1];
        std::string depthPath;
        std::string savePath;
        std::string maskPath;
        float thicknessOverride = 0.0f;   //0 = izvedi je

        //Raspon je jedina stvar koju model ne moze znati: karta kaze sto je blize a sto dalje,
        //ne koliko je to u metrima. Bez metara inverzni kvadrat nema smisla
        float nearDistance = 1.5f;
        float farDistance = 20.0f;

        bool wantShadow = true;
        float lightAngle = 0.0f;       //stupnjeva na kruznici, samo za --save
        uint32_t normalRadius = 3;

        //Izmjereno, ne pogodeno: 95. postotak nagiba koji Depth Anything V2 small izmislja
        //nad ravnim zidom, gdje je istina tocno nula. Na tom broju greska normale na SUBJEKTU
        //ima minimum (9.26 stupnjeva naspram 10.51 bez oduzimanja), a zid padne s 10.46 na
        //0.68. Za dubinu koju je nacrtao rasterizator je nula, i zato je nula default u samoj
        //biblioteci a ovdje nije - ovaj program postoji zbog PROCIJENJENE dubine
        float noiseSlope = 0.30f;

        //Sest je oko 80 stupnjeva. Iznad toga na ovoj sceni nema prave plohe, ima siluete
        float slopeLimit = 6.0f;

        //Ambijentna okluzija. U kompoziciji ona mnozi same piksele snimke - jedino mjesto na
        //kojem se dira ono sto je vec bilo na slici - pa se moze i iskljuciti s --ao 0
        float aoStrength = 0.6f;
        float aoScale = 0.0f;   //0 = izvedi iz udaljenosti subjekta

        //Iz kolikog polumjera trag uzima medijan dubine. Ima smisla samo uz masku
        //Medijan iz kojeg trag cita dubinu. Nula jer je izmjereno da skoro nista ne donosi:
        //3489 mrlja na nuli, 3386 na sesnaest piksela. Mrlje nisu izolirani siljci nego
        //subjekt koji sjenca sam sebe, a to rjesava odmak. Ostaje dostupno za bucniju dubinu
        float occluderBlur = 0.0f;
        //Odmak = ovo puta udaljenost subjekta. Izmjereno na pravoj fotki, pri kutu 225:
        //    bias   sjena na zidu   mrlje na subjektu
        //    0.02      49956             3485
        //    0.04      42215             1265
        //    0.05      38512              612     <- koljeno
        //    0.07      31780              141
        //Mrlje su subjekt koji sjenca sam sebe iz vlastite bucne dubine - nabori majice. Na
        //0.05 ih nestane 82%, a sjena zadrzi 77% povrsine
        float biasScale = 0.05f;
        float slopeBias = 0.0f;

        //Kut lece kojom je snimka nastala. Model dubine ga ne zna i ne moze znati, a o njemu
        //ovisi CIJELA geometrija: iz njega su intrinsici, iz intrinsika odprojekcija, iz nje
        //normale i mjesto sjene. Kriv kut ne izgleda kao greska nego kao scena razvucena po
        //dubini - i onda se sve mjeri u prostoru kojeg nema
        float fovDegrees = 50.0f;
        float specular = 0.10f;

        int positional = 0;
        for(int i = 2; i < argc; ++i){
            const std::string arg = argv[i];

            //--save izvuce jedan kadar i izade. Postoji zato da se rezultat da IZMJERITI, a ne
            //samo pogledati - "sjena se ne vidi" i "sjene nema" su dvije razlicite stvari
            if(arg == "--save" && i + 1 < argc)        savePath = argv[++i];
            else if(arg == "--angle" && i + 1 < argc)  lightAngle = std::stof(argv[++i]);
            else if(arg == "--fov" && i + 1 < argc)    fovDegrees = std::stof(argv[++i]);
            else if(arg == "--mask" && i + 1 < argc)   maskPath = argv[++i];
            else if(arg == "--thickness" && i + 1 < argc) thicknessOverride = std::stof(argv[++i]);
            else if(arg == "--radius" && i + 1 < argc) normalRadius = uint32_t(std::stoi(argv[++i]));
            else if(arg == "--noise" && i + 1 < argc)  noiseSlope = std::stof(argv[++i]);
            else if(arg == "--limit" && i + 1 < argc)  slopeLimit = std::stof(argv[++i]);
            else if(arg == "--specular" && i + 1 < argc) specular = std::stof(argv[++i]);
            else if(arg == "--ao" && i + 1 < argc)     aoStrength = std::stof(argv[++i]);
            else if(arg == "--ao-scale" && i + 1 < argc) aoScale = std::stof(argv[++i]);
            else if(arg == "--occluder-blur" && i + 1 < argc) occluderBlur = std::stof(argv[++i]);
            else if(arg == "--bias" && i + 1 < argc)   biasScale = std::stof(argv[++i]);
            else if(arg == "--slope-bias" && i + 1 < argc) slopeBias = std::stof(argv[++i]);
            else if(arg == "--no-shadow")              wantShadow = false;
            else if(positional == 0){ depthPath = arg; ++positional; }
            else if(positional == 1){ nearDistance = std::stof(arg); ++positional; }
            else if(positional == 2){ farDistance = std::stof(arg); ++positional; }
        }

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
        config.swapchainConfig.allowReadback = !savePath.empty();

        LoomInitializer loom(config);

        //Kamera kroz koju je slika snimljena. Bez prave zarisne duljine ovo je procjena, i
        //ravno je onome sto model za dubinu ionako ne zna - ali mora biti ISTA kamera kroz
        //koju se poslije odprojicira, inace scena ne odgovara slici
        CameraConfig cameraConfig;
        cameraConfig.position = {0.0f, 0.0f, 0.0f};
        cameraConfig.target = {0.0f, 0.0f, -1.0f};
        cameraConfig.fovY = glm::radians(fovDegrees);
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
        std::unique_ptr<Texture> maskTexture;
        float subjectDistance = 2.0f;
        float backdropDistance = 5.0f;
        float orbitRadius = 0.5f;
        float orbitCentre = 1.0f;

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

            //Karta iz modela je na skali jednog piksela sum, a racun nagiba ga pojaca. Siri
            //razmak to smiruje i gubi tocno onoliko detalja koliko ga u procjeni nije ni bilo
            NormalMapConfig normalConfig;
            normalConfig.radius = normalRadius;
            normalConfig.noiseSlope = noiseSlope;
            normalConfig.slopeLimit = slopeLimit;
            normals = std::make_unique<NormalMap>(loom.device, plateSize, normalConfig);
            normals->setPositionSource(loom.getDescriptorPool(), *positions);

            RelightConfig relightConfig;
            relightConfig.colorFormat = loom.getColorFormat();
            relightConfig.surface.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
            relightConfig.surface.shininess = 24.0f;
            //Zrcalni clan pojacava svaku gresku u normali za oko tri puta, pa je na
            //procijenjenoj dubini prvo sto pretvori plohu u zrnje. Koza ionako nije zrcalo
            relightConfig.surface.specularStrength = specular;

            //Lambertov terminator je ostar rub, a normale iz procijenjene dubine su pune
            //gresaka - pa se svaka od njih na tom rubu vidi kao mrlja. Omatanje ga smeksa
            relightConfig.surface.diffuseWrap = 0.25f;

            relight = std::make_unique<Relight>(loom.device, loom.command, loom.getDescriptorPool(),
                                                *positions, *normals, plate.getSampled(), relightConfig);
            relight->setCamera(camera);

            //Sjena se trazi kroz samu sliku - baca je sve sto je u kadru, i nista sto nije
            relight->setIntrinsics(CameraIntrinsics::fromProjection(
                camera.getProjection(plateWidth, plateHeight), plateWidth, plateHeight),
                plateSize);


            //Gdje je subjekt, doslovno iz karte: udaljenost desetine slike koja je najbliza.
            //Bez toga bi se svjetlo vrtjelo oko sredine RASPONA, a to je kod portreta iza
            //osobe - pa bi pola kruga osvjetljavalo zid, a osobu ostavljalo u mraku
            {
                std::vector<float> sorted = depth.values;
                std::sort(sorted.begin(), sorted.end());
                const DepthMapping calibration = DepthMapping::fromRange(nearDistance, farDistance);

                subjectDistance = calibration.distanceAt(sorted[sorted.size() * 9 / 10]);
                backdropDistance = calibration.distanceAt(sorted[sorted.size() / 10]);

                //Gdje svjetlo mora stajati da bi se sjena VIDJELA.
                //
                //Sjena subjekta pada na pozadinu uvecana za s = (Zp - Zs) / (Zs - Zl), pa je
                //njen pomak s puta pomak svjetla. Svjetlo blizu osi baca sjenu ravno IZA
                //osobe, gdje je ona sama zaklanja; svjetlo predaleko u stranu je izbaci iz
                //kadra. Zato se ne bira pomak svjetla nego mjesto sjene, pa se pomak izvede.
                //
                //Ovo je bilo i cijelo objasnjenje zasto se sjene "nije vidjelo": bila je tamo,
                //samo dva i pol metra izvan slike
                const float lightDistance = 0.5f * subjectDistance;
                const float spread = std::max(backdropDistance - subjectDistance, 0.05f) /
                                     std::max(subjectDistance - lightDistance, 0.05f);

                //Pola polusirine kadra na pozadini: dovoljno u stranu da se vidi, dovoljno
                //unutra da ne izade
                const float halfWidth = backdropDistance * std::tan(cameraConfig.fovY * 0.5f)
                                      * float(plateWidth) / float(plateHeight);

                orbitRadius = 0.5f * halfWidth / std::max(spread, 0.05f);
                orbitCentre = lightDistance;

                printf("  subjekt %.2f m, pozadina %.2f m; svjetlo na %.2f m, pomak %.2f m "
                       "-> sjena pada %.2f m u stranu\n",
                       subjectDistance, backdropDistance, orbitCentre, orbitRadius,
                       orbitRadius * spread);
            }

            LightConfig bulbConfig;
            bulbConfig.type = LightType::Point;
            bulbConfig.color = {1.0f, 0.82f, 0.55f};
            //Jacina raste s kvadratom udaljenosti, jer svjetlo po njemu i pada - inace bi
            //ista brojka bila zasljepljujuca u portretu i nevidljiva u sobi
            bulbConfig.intensity = 0.8f * subjectDistance * subjectDistance;
            bulbConfig.range = std::max(4.0f * backdropDistance, farDistance);
            bulb = std::make_unique<Light>(bulbConfig);
            loom.renderer.addLight(*bulb);

            ScreenShadowConfig shadowConfig;
            shadowConfig.enabled = wantShadow;
            shadowConfig.steps = 40;
            //Doseg mora prijeci put od pozadine do svjetla, inace trag stane prije zaklona
            shadowConfig.maxDistance = 1.5f * (backdropDistance + orbitCentre);

            //KOLIKO JE DUBOK ZAKLON.
            //
            //Dubina daje ljusku: vidi se prednja ploha i nista iza nje, pa trag mora
            //pretpostaviti koliko je ono sto vidi debelo. Bez maske se to nema odakle znati,
            //pa se uzimalo poldrug razmaka subjekta i pozadine - jedina duljina koja je bila
            //pri ruci, ali duljina o NECEM DRUGOM: na portretu je glavi davala dva metra.
            //
            //POSTENO O UCINKU, jer je izmjeren: na portretu se slika time nije promijenila.
            //Debljina zagrize tek ispod pola metra (na 0.05 m sjena padne s 54572 na 21573
            //piksela), a iznad toga je mrtva - jer svjetlo stoji ISPRED subjekta, pa zrake s
            //pozadine prelaze preko njega blizu njegove vlastite dubine i gornja granica se
            //nikad ne dosegne. Vrijedit ce tamo gdje svjetlo ide u stranu, i vrijedi kao
            //brojka koja ima razlog umjesto pretpostavke koja ga nema
            //
            //Maska zna dokle subjekt see, pa se debljina moze izvesti iz njegovog vlastitog
            //raspona dubine. Za kuglu je racun tocan: gledana sprijeda, njene vidljive dubine
            //se protezu tocno polumjer (najbliza tocka pa silueta), a puna debljina je dva
            //polumjera. Za sve pljosnatije od kugle je to donja granica, i to je posteno
            //stanje stvari - ljuska o vlastitim ledima ne zna nista
            shadowConfig.thickness = 1.5f * std::max(backdropDistance - subjectDistance, 0.1f);

            if(!maskPath.empty()){
                const Spool::Image mask = Spool::loadImage(maskPath);
                if(mask.width != plateWidth || mask.height != plateHeight){
                    throw std::runtime_error("maska je " + std::to_string(mask.width) + "x" +
                        std::to_string(mask.height) + ", a snimka " + std::to_string(plateWidth) +
                        "x" + std::to_string(plateHeight) + " - moraju biti piksel na piksel");
                }

                const DepthMapping calibration = DepthMapping::fromRange(nearDistance, farDistance);

                std::vector<float> inside;
                inside.reserve(depth.values.size() / 4);
                for(size_t i = 0; i < depth.values.size(); ++i){
                    if(mask.pixels[i * 4] > 127) inside.push_back(calibration.distanceAt(depth.values[i]));
                }

                if(inside.size() < 64){
                    throw std::runtime_error("u maski nema dovoljno piksela da se izmjeri dubina subjekta");
                }

                std::sort(inside.begin(), inside.end());

                //Peti i devedeset peti postotak, ne krajnje vrijednosti: jedan piksel ruba u
                //kojem se dubina prelijeva na pozadinu inace odredi debljinu cijelog subjekta
                const float front = inside[inside.size() * 5 / 100];
                const float back = inside[inside.size() * 95 / 100];

                shadowConfig.thickness = std::max(2.0f * (back - front), 0.05f);

                printf("  maska: subjekt je vidljivo dubok %.3f m (%.2f do %.2f m) "
                       "-> debljina zaklona %.3f m\n",
                       back - front, front, back, shadowConfig.thickness);

                //Sjenu tada baca SAMO subjekt. Bez toga je zaklon sve sto se vidi, pa i zid
                //sam sebi - i silueta bacene sjene dolazi od onoga sto je model dubine
                //napravio od cigli, a ne od tijela
                shadowConfig.maskOnly = true;

                //I trag cita medijan umjesto same tocke: nabor na majici je za normale
                //detalj, a za trag mrlja koja baca sjenu koje nema
                shadowConfig.occluderBlur = occluderBlur;

                TextureConfig maskConfig;
                maskConfig.format = vk::Format::eR8G8B8A8Unorm;
                maskConfig.filter = vk::Filter::eNearest;
                maskConfig.generateMipmaps = false;
                maskTexture = std::make_unique<Texture>(loom.device, loom.command,
                                                        mask.pixels.data(), plateSize, maskConfig);
                relight->setOccluderMask(maskTexture->getSampled());
            }
            shadowConfig.bias = biasScale * subjectDistance;

            //Odmak koji raste s putem NIJE ukljucen, i to namjerno. Akne se pojave samo kad je
            //svjetlo dublje od plohe, a ovdje kruzi ispred subjekta; a odmak ne razlikuje lazni
            //zaklon od pravog, pa bi se platio sjenom. Mjereno u test_shadow_slope: on mice 98%
            //akni, ali u toj sceni odnese i cijelu pravu sjenu
            shadowConfig.slopeBias = slopeBias;

            //Sto se dalje odhodalo, to se manje zna o tome sto je iza uzorkovane plohe
            shadowConfig.thicknessGrowth = 2.6f;

            //Ploha blize kameri od svjetla ne moze zaklanjati. Ovdje to NIJE rubni slucaj:
            //svjetlo kruzi na pola udaljenosti subjekta, dakle ispred njega
            shadowConfig.frontFade = 0.25f * subjectDistance;
            //Rukom zadana debljina nadglasa i masku i pretpostavku. Postoji da se moze
            //IZMJERITI koliko debljina uopce mijenja - a odgovor je ispao "u ovoj postavci
            //gotovo nista", sto se drukcije ne bi ni vidjelo
            if(thicknessOverride > 0.0f) shadowConfig.thickness = thicknessOverride;

            relight->setShadow(shadowConfig);

            //Bez nje subjekt lebdi nad pozadinom: nista ne potamni tamo gdje se dodiruju.
            //
            //Mjerilo je vezano uz RAZMAK subjekta i pozadine, jer je "koliko metara razlike
            //jos znaci dodir" pitanje o velicini scene a ne o slici. Izmjereno na portretu,
            //tamnjenje uz rub subjekta naspram daleko od njega:
            //    0.04 m  -1.22 / -0.14      omjer 9
            //    0.10 m  -1.18 / -0.00      dalje od ruba vise nema nista
            //    0.25 m  -1.43 / -0.00
            //    0.50 m  -3.81 / -0.00      silueta pocinje izgledati kao obrub
            //Petnaest posto razmaka daje 0.22 m na ovoj sceni, dakle sredinu tog raspona
            ScreenOcclusionConfig occlusionConfig;
            occlusionConfig.strength = aoStrength;
            occlusionConfig.scale = aoScale > 0.0f ? aoScale
                : 0.15f * std::max(backdropDistance - subjectDistance, 0.1f);
            relight->setOcclusion(occlusionConfig);

            printf("\nSvjetlo kruzi oko scene i baca sjene od svega sto je u kadru.\n"
                   "Zatvori prozor za izlaz.\n");
        }

        // -- petlja ------------------------------------------------------------------------

        //Jedan kadar na disk: fiksni kut svjetla, jedan frame, gotovo. Sve sto se poslije
        //mjeri mjeri se nad ovim, jer se dvije slike daju oduzeti a dva dojma ne
        const bool once = !savePath.empty();

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

            //Svjetlo se postavlja PRIJE beginFrame, i to nije stvar ukusa: renderer skuplja
            //svjetla i salje ih na karticu unutar beginFrame (VulkanRenderer.cpp, lightStaging).
            //Pomak nakon toga vrijedi tek za SLJEDECI kadar - u prozoru se to vidi samo kao
            //kasnjenje od jednog framea, ali --save snima prvi i jedini kadar, pa je svaki kut
            //davao bit-identicnu sliku. Tri md5-a ista, i to je bila prva stvar koju je
            //mjerenje naslo
            if(relight){
                //Svjetlo kruzi ISPRED subjekta, kao kljucno svjetlo koje netko nosi oko njega
                //- ne oko sredine scene, jer je tamo vec zid. Kruzenje je jedini nacin da se
                //OKOM vidi da je svjetlo u prostoru: da je nalijepljeno na sliku, sjencanje se
                //ne bi mijenjalo dok putuje
                const double time = once ? double(glm::radians(lightAngle)) / 0.6
                    : std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - started).count();

                bulb->setPosition({
                    orbitRadius * float(std::sin(time * 0.6)),
                    orbitRadius * float(std::cos(time * 0.6)) + 0.15f * subjectDistance,
                    -orbitCentre
                });
            }

            if(!loom.renderer.beginFrame()) continue;

            //Tek NAKON beginFrame: prsten se oslanja na to da je renderer vec pricekao frame
            //od prije onoliko frameova koliko prsten ima slotova
            plate.update(frame->pixels.data(), frame->pixels.size());

            if(relight){
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

            if(once){
                loom.waitIdle();
                const ImageData shot = loom.renderer.readLastFrame();
                Spool::savePng(savePath, Spool::imageFromPixels(
                    shot.pixels.data(), shot.extent.width, shot.extent.height,
                    Spool::ChannelOrder::BGRA));
                printf("zapisano: %s\n", savePath.c_str());
                break;
            }

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
