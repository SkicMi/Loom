// Preglednik gaussian splattinga: .ply s diska, scena u prozoru.
//
// Ovo je STEPENICA 2 i zasad mora biti: Scene iz stepenice 1 crta oblike iz svog reda, a oblak
// od tri cetvrt milijuna gaussiana nije oblik. Kad se pokaze kakav mu oblik stvarno treba, uzet
// ce svoje mjesto gore - isto kako je zapisano za plate u main.cpp.
//
//   ./SplatViewer scena.ply
//   ./SplatViewer scena.ply 4          <- svaki cetvrti splat, kad procesor ne stize
//   ./SplatViewer scena.ply 1 32       <- i velicina pločice
//   ./SplatViewer scena.ply 8 16 60    <- odvrti 60 kadrova, spremi splatview.png i izadji
//   ./SplatViewer soba.ply 1 16 0 0 x.png 1 0 iznutra        <- prostor, pogled iznutra
//   ./SplatViewer soba.ply 1 16 40 0 x.png 1 0.05 model/txt  <- iz pravih poza, s kockom
//
// BOJA OVISI O SMJERU POGLEDA (G4): odsjaj na metalu i nebo koje se mijenja dok kamera kruzi.
// Racuna se svaki kadar, jer smjer od kamere do gaussiana je jedino sto se mijenja.
//
// PRIPREMA JE NA KARTICI, i broj parova s njom. Sirovi splatovi odu na karticu jednom; svaki
// kadar procesor posalje samo kameru. Kovarijancu, conic, boju iz smjera i broj parova racuna
// kartica, a velicine dispatcha iz tog broja izvodi sama - procesor po kadru ne cita nista.
// Ispisuje se koliko je parova scena trazila, i vice kad ih je vise nego sto ima mjesta.
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/SplatRenderer.h"

#include <Engine/ColmapImport.h>

#include <Spool/GaussianPly.h>
#include <Spool/ImageFile.h>

#include <Treadle/Ui.h>
#include <TreadlePaint/UiPainter.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace{

//=============================================================================================
// POGLED KAO U BLENDERU: tocka oko koje se kruzi, dva kuta i udaljenost.
//
// Prije su to bila tri nezavisna broja (kut, visina, udaljenost) i jos prekidac izvana/iznutra,
// pa je "iznutra" bio zaseban nacin s vlastitim racunom. Ovdje je iznutra samo udaljenost blizu
// nule - jedan racun umjesto tri, i nista se ne moze raziici.
//
// Y RASTE PREMA DOLJE, jer 3DGS scene dolaze takve. Zato je u smjeru minus sinus: pozitivan
// nagib podize kameru
//=============================================================================================
struct Orbit{
    glm::vec3 pivot{0.0f};
    float yaw = 0.0f;
    float pitch = 0.2f;
    float distance = 1.0f;

    //Od sredista prema kameri
    glm::vec3 offset() const{
        return glm::vec3(std::cos(pitch) * std::sin(yaw), -std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    }

    glm::vec3 eye() const {return pivot + offset() * distance;}
    glm::vec3 forward() const {return -offset();}

    //Desno i gore NA EKRANU. Trebaju pomicanju: povlacenje misa mora micati scenu onako kako
    //je korisnik uhvatio, a ne po osima svijeta
    glm::vec3 right() const{
        return glm::normalize(glm::cross(forward(), glm::vec3(0.0f, -1.0f, 0.0f)));
    }
    glm::vec3 screenUp() const {return glm::normalize(glm::cross(right(), forward()));}

    //Iz rijesene poze: ista kamera, samo zapisana drugim brojevima. Srediste se stavi ispred
    //nje, jer bi inace kruzenje kruzilo oko tocke u kojoj kamera stoji
    void fromPose(const Engine::Pose& pose, float reach){
        const glm::vec3 ahead = pose.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
        pivot = pose.position + ahead * reach;
        distance = reach;
        pitch = std::asin(std::clamp(ahead.y, -1.0f, 1.0f));
        yaw = std::atan2(-ahead.x, -ahead.z);
    }
};

//Kotacic misa GLFW javlja dogadjajem a ne stanjem, pa se mora skupljati sa strane. Jedan
//preglednik ima jedan prozor, pa je jedan broj dovoljan
float wheelSinceLastFrame = 0.0f;

void onScroll(GLFWwindow*, double, double y){
    wheelSinceLastFrame += float(y);
}

//Broj u niz, sa zadanim brojem decimala. Suicelje trazi tekst, a std::to_string za float daje
//sest decimala - sirinu koju nijedna ploha nema
std::string fmtNumber(double value, int decimals){
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

//Sredina i polumjer scene, ali po postotcima a ne po krajnostima. Prava scena ima nekoliko
//gaussiana koji su odletjeli daleko od svega ostalog - u train/7000 se x proteze od -62 do 172
//dok je prosjek 1.5 - pa bi kamera postavljena po najvecem i najmanjem gledala prazninu
struct Bounds{
    glm::vec3 centre{0.0f};
    float radius = 1.0f;

    float middleShare = 0.0f;   //koliki dio splatova je blizu sredista - podatak, ne odluka
};

Bounds robustBounds(const std::vector<Splat>& splats){
    Bounds bounds;
    if(splats.empty()) return bounds;

    std::vector<float> values(splats.size());
    glm::vec3 low(0.0f), high(0.0f);

    for(int axis = 0; axis < 3; ++axis){
        for(size_t i = 0; i < splats.size(); ++i) values[i] = splats[i].position[axis];
        std::sort(values.begin(), values.end());
        //Cetvrtine, ne krajevi. Scena snimljena vani ima pola splatova u nebu i u daljini, pa
        //bi i peti postotak jos uvijek opisivao pozadinu umjesto onoga sto se snimalo
        low[axis]  = values[size_t(0.25 * double(values.size()))];
        high[axis] = values[size_t(0.75 * double(values.size()))];
    }

    bounds.centre = 0.5f * (low + high);
    bounds.radius = 0.5f * glm::length(high - low);
    if(bounds.radius <= 0.0f) bounds.radius = 1.0f;

    //Koliko gradje ima blizu sredista - samo kao podatak. Pokusao sam po tome razlikovati predmet
    //od prostora i to je bilo krivo: namjestena soba NIJE suplja, u sredini su joj pult, stolice i
    //stol, pa je izasla kao predmet (1.76 posto). Razlika nije u tome ima li gradje u sredini nego
    //gdje je kamera STAJALA dok se snimalo, a to u .ply datoteci ne pise. Zato se ne pogadja nego
    //bira - tipkom I
    size_t inMiddle = 0;
    const float middle = 0.35f * bounds.radius;
    for(const Splat& splat : splats){
        if(glm::length(splat.position - bounds.centre) < middle) ++inMiddle;
    }
    bounds.middleShare = float(double(inMiddle) / double(splats.size()));
    return bounds;
}

}

int main(int argc, char** argv){
    if(argc < 2){
        printf("Upotreba: SplatViewer <scena.ply> [korak] [pločica] [kadrovi] [kut] [ime.png] [sh] [kocka] [iznutra|mapa_modela] [prelet]\n");
        printf("\nImenovani prekidaci, mogu stajati bilo gdje:\n");
        printf("  --tocke           crta rekonstruirane tocke kao sitne svijetle splatove\n");
        printf("  --kocka-na-podu   kocka lezi na podu, na mjestu koje vidi najvise kamera\n");
        printf("  --nova-putanja    kamera ide zagladjenom krivuljom kroz poze, a ne kroz njih\n");
        printf("  --poza N          krece od N-tog polozaja na putanji\n");
        return 1;
    }

    const std::string path = argv[1];
    const uint32_t stride = argc > 2 ? uint32_t(std::atoi(argv[2])) : 1;
    const uint32_t tileSize = argc > 3 ? uint32_t(std::atoi(argv[3])) : 16;

    //Koliko kadrova pa snimka i kraj. Nula znaci "vrti dok ne zatvorim" - snimka postoji da se
    //slika moze pogledati bez gledanja, jer "vrti se" i "tocno je" nisu ista tvrdnja
    const uint32_t framesThenShot = argc > 4 ? uint32_t(std::atoi(argv[4])) : 0;

    //Pocetni kut, u stupnjevima. Postoji da se ista scena moze snimiti iz dva smjera i
    //usporediti - parallaksa i zaklon su jedini dokaz da je ovo prostor a ne slika
    const float startAngle = argc > 5 ? float(std::atof(argv[5])) * 3.14159265f / 180.0f : 0.0f;

    //Ime snimke, da se dvije usporedbe ne prepisu
    const std::string shotName = argc > 6 ? std::string(argv[6]) : std::string("splatview.png");

    //Deveti argument je ili "iznutra" ili MAPA S MODELOM. Model daje ono sto u .ply datoteci ne
    //postoji: gdje je kamera stvarno stajala.
    //
    //ZASTO JE TO VAZNO. Bez njega se krece iz sredista scene, a srediste nije mjesto na kojem je
    //itko stajao - na 68 sekundi obilaska to je ispalo u tamnom hodniku, i slika je bila kasa koja
    //je izgledala kao los trening a nije bila. Prave poze se kroz njih i setaju, tipkama N i P
    //NE NA SEDMOM MJESTU - ono je vec bilo zauzeto za sferne harmonike, i kad sam ga preuzeo
    //atoi("puni/txt") je dao nulu pa su se svi prikazi crtali BEZ boje iz smjera pogleda. Greska
    //koja ne pada nego samo tise izgleda
    const std::string mode = argc > 9 ? std::string(argv[9]) : std::string();

    //PRELET: crta redom kroz sve prave poze i sprema po sliku za svaku. Postoji jer jedna slika ne
    //pokazuje je li scena prostorna - tek se kroz gibanje vidi zaklon i paralaksa, dakle da je ovo
    //prostor a ne razglednica
    const bool flyover = argc > 10 && std::string(argv[10]) == "prelet";

    //IMENOVANI PREKIDACI. Popis polozajnih argumenata je narastao do deset i dalje se ne da citati,
    //pa novo ide pod imenom. Mogu stajati bilo gdje
    auto hasFlag = [&](const char* name){
        for(int i = 1; i < argc; ++i) if(std::string(argv[i]) == name) return true;
        return false;
    };
    const bool showPoints = hasFlag("--tocke");
    const bool boxOnFloor = hasFlag("--kocka-na-podu");
    const bool newPath = hasFlag("--nova-putanja");

    //Suicelje se u snimku NE crta, jer bi dvije slike koje se usporedjuju nosile plohu s
    //gumbima preko sebe. Ovo je jedina iznimka i postoji zbog provjere samog suicelja - inace
    //se ono nikako ne da vidjeti drukcije nego gledanjem u ekran
    const bool uiInShot = hasFlag("--suicelje-na-slici");

    //Kocka za brisanje odmah upaljena. Inace se pali kvacicom u suicelju
    const bool cubeAtStart = hasFlag("--kocka-brisanje");

    //Iz kojeg polozaja na putanji krenuti. Postoji da se odredjeni kadar da pogledati bez crtanja
    //svih prije njega - a bas to je trebalo kad se provjeravalo zasto se kocka ne vidi
    int startPose = -1;
    float lookOffset = 0.0f;
    for(int i = 1; i + 1 < argc; ++i){
        if(std::string(argv[i]) == "--poza") startPose = std::atoi(argv[i + 1]);
        //Zaokret pogleda oko prave poze, u stupnjevima. Postoji jer argument [kut] u ovom nacinu ne
        //radi nista: rotacija se racuna kao angle - startAngle, a angle bas odatle i krece
        if(std::string(argv[i]) == "--zaokret") lookOffset = float(std::atof(argv[i + 1])) * 3.14159265f / 180.0f;
    }
    const bool insideStart = mode == "iznutra";
    const std::string modelPath = (!mode.empty() && mode != "iznutra") ? mode : std::string();

    //Boja iz smjera pogleda se da ugasiti, i to nije udobnost nego mjerenje: razlika izmedju
    //upaljenog i ugasenog je jedini nacin da se vidi koliko G4 stvarno radi
    const bool useSH = argc > 7 ? (std::atoi(argv[7]) != 0) : true;

    //Ubacena kocka: velicina kao udio polumjera scene. Nula znaci bez nje. Ovo je mjerni predmet -
    //ako poze i mjerilo valjaju, stoji gdje treba, prave je velicine i zaklanja ono iza sebe
    const float boxSize = argc > 8 ? float(std::atof(argv[8])) : 0.0f;

    std::vector<Engine::Pose> realPoses;
    std::vector<glm::vec3> modelPoints;
    if(!modelPath.empty()){
        Engine::ColmapModel model;
        if(!Engine::readColmapText(modelPath, model)){
            printf("Ne mogu procitati model iz %s\n", modelPath.c_str());
            return 1;
        }
        for(size_t i = 0; i < model.reconstruction.poses.size(); ++i){
            if(model.reconstruction.posed[i]) realPoses.push_back(model.reconstruction.poses[i]);
        }
        for(size_t i = 0; i < model.reconstruction.points.size(); ++i){
            if(model.reconstruction.solved[i]) modelPoints.push_back(model.reconstruction.points[i]);
        }
        printf("Model: %zu pravih poza i %zu tocaka iz %s\n",
               realPoses.size(), modelPoints.size(), modelPath.c_str());
    }


    // -------------------------------------------------------------------------------
    // S diska u splatove
    // -------------------------------------------------------------------------------

    printf("Citam %s ...\n", path.c_str());
    auto started = std::chrono::steady_clock::now();

    const Spool::GaussianCloud cloud = Spool::loadGaussianPly(path);

    auto elapsed = [&started]{
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        started = std::chrono::steady_clock::now();
        return seconds;
    };

    printf("  %zu gaussiana, stupanj %u, za %.2f s\n", cloud.count(), cloud.shDegree, elapsed());

    //AKTIVACIJA: iz onoga sto u fileu pise u ono sto se crta. Vidi Core/Splat.h zasto to nije
    //napravio Spool
    std::vector<Splat> splats;
    splats.reserve(cloud.count() / stride + 1);

    //Koji je gaussian u oblaku dao koji splat. Treba jer se boja racuna svaki kadar iz njegovih
    //koeficijenata, a korak preskace - pa redni broj u splats vise nije redni broj u oblaku
    std::vector<uint32_t> sourceIndex;
    sourceIndex.reserve(splats.capacity());

    const uint32_t coeffsPerChannel = cloud.restStride / 3;

    //KOJI GAUSSIAN JOS POSTOJI. Brisanje kockom radi NAD OBLAKOM, ne nad splatovima koji se
    //crtaju: korak preskace, pa bi brisanje nad prorijedenim popisom ostavilo u datoteci sve
    //ono sto se nije crtalo - a kutija je volumen i mora odnijeti sve unutar sebe.
    //
    //Maska a ne kopija oblaka: na sceni od cetiri milijuna gaussiana kopija je jos gigabajt
    std::vector<uint8_t> alive(cloud.count(), 1);

    //SAMO POLOZAJI, u vlastitom polju. Brojanje i brisanje citaju iz cijelog oblaka samo tri
    //broja po gaussianu, a zapis je 62 bajta - pa se kroz memoriju vukla 233 MB za 45 MB
    //podatka. Izmjereno na 3.76 milijuna gaussiana, medijan od 15 prolaza: 11.3 ms iz oblaka,
    //3.9 ms odavde. Klizac velicine se prebrojava svaki kadar dok se vuce, pa se to osjeti
    std::vector<glm::vec3> positions;
    positions.reserve(cloud.count());
    for(const Spool::Gaussian& gaussian : cloud.gaussians){
        positions.push_back(glm::vec3(gaussian.position[0], gaussian.position[1], gaussian.position[2]));
    }

    auto buildSplats = [&]{
        splats.clear();
        sourceIndex.clear();

        for(size_t i = 0; i < cloud.count(); i += stride){
            if(!alive[i]) continue;

            const Spool::Gaussian& source = cloud.gaussians[i];

            Splat splat;
            splat.position = glm::vec3(source.position[0], source.position[1], source.position[2]);
            splat.scale    = SplatMath::activateScale(glm::vec3(source.scale[0], source.scale[1], source.scale[2]));
            splat.rotation = SplatMath::activateRotation(glm::vec4(source.rotation[0], source.rotation[1],
                                                                  source.rotation[2], source.rotation[3]));
            splat.opacity  = SplatMath::activateOpacity(source.opacity);
            splat.color    = SplatMath::colorFromSH0(glm::vec3(source.dc[0], source.dc[1], source.dc[2]));
            splats.push_back(splat);
            sourceIndex.push_back(uint32_t(i));
        }

        //TOCKE IZ MODELA KAO SPLATOVI. Ne crtaju se zasebnim prolazom nego se ubace u isti
        //oblak: rasterizator ih time sortira i zaklanja zajedno sa scenom, pa tocka iza zida
        //stvarno zavrsi iza zida. Zasebni prolaz bi ih crtao preko svega.
        //
        //Idu na kraj, poslije brisanja - one nisu dio scene nego mjerni instrument, pa ih
        //kocka ne brise. Sitne su, neprozirne i namjerno svijetle
        if(showPoints && !realPoses.empty() && !modelPoints.empty()){
            for(const glm::vec3& point : modelPoints){
                Splat dot;
                dot.position = point;
                dot.scale = glm::vec3(0.004f);
                dot.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                dot.opacity = 0.95f;
                dot.color = glm::vec3(0.15f, 0.95f, 1.0f);   //ciklama-plava: ne pojavljuje se u sobi
                splats.push_back(dot);
                sourceIndex.push_back(UINT32_MAX);           //nema izvora u oblaku - vidi upload
            }
        }
    };

    buildSplats();

    if(showPoints && !realPoses.empty() && !modelPoints.empty()){
        printf("  + %zu tocaka iz modela nacrtano kao splatovi\n", modelPoints.size());
    }

    const Bounds bounds = robustBounds(splats);
    printf("  %zu splatova nakon koraka %u; sredina %.2f %.2f %.2f, polumjer %.2f\n",
           splats.size(), stride, double(bounds.centre.x), double(bounds.centre.y),
           double(bounds.centre.z), double(bounds.radius));

    if(stride > 1){
        printf("  PAZI: korak %u znaci da se crta svaki %u. gaussian. Scena ce izgledati kao\n"
               "  sum i pruge - to nije greska crtanja nego %u%% podataka koji nedostaju.\n"
               "  Za pravu sliku pokreni bez broja.\n", stride, stride,
               uint32_t(100.0 - 100.0 / double(stride)));
    }

    // -------------------------------------------------------------------------------
    // Prozor, meta i rasterizator
    // -------------------------------------------------------------------------------

    LoomConfig config;
    config.width = 1280; config.height = 720;
    config.appName = "Loom splat viewer"; config.engineName = "Loom";
    config.headless = false;
    config.enableDepth = false;
    //Jedan SplatRenderer trazi 21 set i 71 storage buffer, a default od 64 po tipu to ne
    //daje - tolerantan driver precuti, strog ne
    config.maxDescriptorSets = 128;
    //Vrijeme po koraku s karticinog sata. SplatRenderer stavlja 10 oznaka po kadru
    config.rendererConfig.maxTimestamps = 32;

    //Fullscreen prolaz koji sliku prenosi na ekran nema vertex buffer ni dubinu
    config.pipelineConfig.vertexBindings.clear();
    config.pipelineConfig.vertexAttributes.clear();
    config.pipelineConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    config.pipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/splat_present.vert.spv";
    config.pipelineConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/splat_present.frag.spv";
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.depthTestEnable = false;
    config.pipelineConfig.depthWriteEnable = false;

    LoomInitializer loom(config);
    //VELICINA SLIKE NA POCETKU. Prozor se moze promijeniti, pa ovo NIJE ista stvar kao velicina
    //u kojoj se crta ovog kadra - vidi windowSize u petlji. Ovdje stoji samo zato sto se meta i
    //rasterizator grade prije prvog kadra i moraju od necega poceti
    vk::Extent2D size = loom.getExtent();

    //Meta u koju rasterizator pise, i s koje fullscreen prolaz cita. Float, jer je to ono sto
    //kompozicija stvarno racuna - pretvorbu u osam bita napravi tek swapchain
    RenderTargetConfig targetConfig;
    targetConfig.colorFormat = vk::Format::eR32G32B32A32Sfloat;
    targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
    targetConfig.enableDepth = false;
    targetConfig.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    SplatRendererConfig rendererConfig;
    rendererConfig.tileSize = tileSize;
    rendererConfig.maxSplats = uint32_t(splats.size());
    //Najvise sto stane u najmanju granicu grupa koju Vulkan jamci: 65535 grupa po 256
    rendererConfig.maxPairs = 65535u * 256u;
    rendererConfig.maxShCoefficients = useSH ? coeffsPerChannel * 3 : 0;

    //META, PRIKAZ I RASTERIZATOR SE MOGU SAGRADITI IZNOVA, jer im velicina ovisi o prozoru.
    //Zato stoje u optionalu: nijedan od ta tri tipa se ne da pridruziti (drze vk::raii i
    //reference), pa je unistiti-pa-sagraditi-na-mjestu jedini nacin da promijene velicinu.
    //
    //Bez toga je meta ostajala na velicini s pocetka, a fullscreen prolaz ju je razvlacio preko
    //novog prozora - slika u krivom omjeru koja se ne moze objasniti nicim u sceni
    std::optional<RenderTarget> splatTarget;
    std::optional<Material> present;
    std::optional<SplatRenderer> splatRenderer;

    auto buildForSize = [&](vk::Extent2D extent){
        //Redom obrnutim od gradnje: rasterizator drzi metinu sliku, prikaz je uzorkuje
        splatRenderer.reset();
        present.reset();
        splatTarget.reset();

        splatTarget.emplace(loom.device, extent, targetConfig);
        present.emplace(loom.device, loom.command, loom.getDescriptorPool(),
                        loom.vulkanGraphicsPipeline, splatTarget->getSampled());
        splatRenderer.emplace(loom.device, loom.getDescriptorPool(),
                              splatTarget->getColorImage(), extent, rendererConfig);
        size = extent;
    };

    buildForSize(size);

    //SIROVI SPLATOVI, JEDNOM. Aktivacija je vec napravljena jer ne ovisi o kameri; sve sto ovisi
    //racuna kartica svaki kadar. Koeficijenti se uzimaju preko sourceIndex, jer korak preskace
    uint32_t splatCount = uint32_t(splats.size());

    //Poziva se opet nakon brisanja. Oblak i koeficijenti ostaju na procesoru, pa ponovno
    //slanje kosta samo prolaz kroz ono sto je ostalo
    auto uploadSplats = [&](bool announce){
        splatCount = uint32_t(splats.size());

        std::vector<SplatMath::RawSplat> raw;
        raw.reserve(splats.size());
        std::vector<float> rest;
        if(useSH) rest.reserve(splats.size() * coeffsPerChannel * 3);

        for(size_t i = 0; i < splats.size(); ++i){
            const Splat& splat = splats[i];
            const bool fromCloud = sourceIndex[i] != UINT32_MAX;

            SplatMath::RawSplat one;
            one.positionOpacity = glm::vec4(splat.position, splat.opacity);
            one.scale = glm::vec4(splat.scale, 0.0f);
            one.rotation = glm::vec4(splat.rotation.w, splat.rotation.x, splat.rotation.y, splat.rotation.z);

            if(fromCloud){
                const Spool::Gaussian& source = cloud.gaussians[sourceIndex[i]];
                one.dc = glm::vec4(source.dc[0], source.dc[1], source.dc[2], 0.0f);
            }else{
                //Ubacena tocka nema koeficijente: boja se vraca u nulti clan sfernih harmonika, a
                //visi clanovi su nula - pa joj boja ne ovisi o smjeru, sto je za biljeg i ispravno
                const glm::vec3 sh = (splat.color - 0.5f) / 0.2820948f;
                one.dc = glm::vec4(sh, 0.0f);
            }
            raw.push_back(one);

            if(useSH && coeffsPerChannel > 0){
                if(fromCloud){
                    const float* coefficients = cloud.restFor(sourceIndex[i]);
                    rest.insert(rest.end(), coefficients, coefficients + coeffsPerChannel * 3);
                }else{
                    rest.insert(rest.end(), size_t(coeffsPerChannel) * 3, 0.0f);
                }
            }
        }

        splatRenderer->uploadRaw(raw, rest, useSH ? cloud.shDegree : 0, useSH ? coeffsPerChannel : 0);
        if(announce){
            printf("  na karticu: %.0f MB splatova, %.0f MB koeficijenata\n",
                   double(raw.size() * sizeof(SplatMath::RawSplat)) / 1048576.0,
                   double(rest.size() * sizeof(float)) / 1048576.0);
        }
    };

    uploadSplats(true);

    printf("  pločica %ux%u, mreza %ux%u\n", tileSize, tileSize,
           splatRenderer->getGrid().width, splatRenderer->getGrid().height);

    // -------------------------------------------------------------------------------
    // Kamera: kruzi oko scene
    // -------------------------------------------------------------------------------

    CameraConfig cameraConfig;
    cameraConfig.fovY = glm::radians(60.0f);
    cameraConfig.nearPlane = 0.01f;
    cameraConfig.farPlane = 1000.0f;
    cameraConfig.target = bounds.centre;
    cameraConfig.up = glm::vec3(0.0f, -1.0f, 0.0f);   //3DGS scene dolaze s Y prema dolje
    Camera camera(cameraConfig);

    //NOVA PUTANJA. Rijesene poze su mjesta na kojima je kamera STVARNO bila, pa prelet kroz njih
    //pokazuje scenu ondje gdje je i snimljena - najpovoljniji mogući pogled. Ovdje se umjesto toga
    //napravi glatka krivulja koja kroz ta mjesta samo PROLAZI: jaki tekuci prosjek preko sirokog
    //prozora, pa podignuta.
    //
    //ZASTO JE TO JACA PROVJERA. Kamera zavrsi ondje gdje nije bila nijednom, i gleda u smjeru u
    //kojem nije gledala. Ako geometrija valja, kocka postavljena na pod ostaje na istom mjestu i
    //pod istim kutom; ako ne valja, pluta ili klizi - a to se na pravim pozama ne bi vidjelo
    std::vector<Engine::Pose> pathPoses;
    if(newPath && realPoses.size() > 8){
        const int span = std::max(4, int(realPoses.size()) / 12);
        for(int i = 0; i < int(realPoses.size()); ++i){
            glm::vec3 sum(0.0f);
            int taken = 0;
            for(int d = -span; d <= span; ++d){
                const int j = i + d;
                if(j < 0 || j >= int(realPoses.size())) continue;
                sum += realPoses[size_t(j)].position;
                ++taken;
            }
            Engine::Pose smoothed;
            smoothed.position = sum / float(taken);
            //Y raste prema DOLJE u 3DGS sceni, pa se oduzimanjem kamera podize
            smoothed.position.y -= 0.05f * bounds.radius;

            //ODSTUPANJE SE OGRANICAVA. Zagladjivanje sece zavoje, a na obilasku sobe zavoj ide oko
            //namjestaja i uza zid - pa presjecena putanja zavrsi U zidu ili iznad stropa, gdje nema
            //nicega. Izmjereno: bez ove granice je 40 od 274 kadra (15 posto) bilo posve prazno, i
            //to u jednom neprekinutom rasponu.
            //
            //Putanja time ostaje nova - nijedan polozaj nije nijedna snimljena poza - ali ne izlazi
            //iz prostora kojim se stvarno prolazilo
            const glm::vec3 original = realPoses[size_t(i)].position;
            const float limit = 0.06f * bounds.radius;
            const glm::vec3 away = smoothed.position - original;
            const float distance = glm::length(away);
            if(distance > limit) smoothed.position = original + away * (limit / distance);

            smoothed.orientation = realPoses[size_t(i)].orientation;
            pathPoses.push_back(smoothed);
        }

        //Smjer se uzima iz same krivulje - kamo putanja ide, tamo se i gleda. Orijentacija iz
        //izvorne poze bi vratila stari pogled i ponistila smisao nove putanje
        for(size_t i = 0; i < pathPoses.size(); ++i){
            const size_t ahead = std::min(pathPoses.size() - 1, i + size_t(span));
            const glm::vec3 forward = pathPoses[ahead].position - pathPoses[i].position;
            if(glm::length(forward) < 1e-4f) continue;
            pathPoses[i].orientation = glm::quatLookAt(glm::normalize(forward), glm::vec3(0.0f, -1.0f, 0.0f));
        }
        printf("Nova putanja: %zu polozaja, zagladjeno preko %d susjeda\n", pathPoses.size(), span);
    }

    const std::vector<Engine::Pose>& cameraPath = pathPoses.empty() ? realPoses : pathPoses;

    //IZVANA ILI IZNUTRA. Predmet se obilazi, prostor se gleda iznutra - i to se ne da procitati iz
    //.ply datoteke, jer u njoj ne pise gdje je kamera stajala. Zato je zadano obilazenje, a tipka
    //I prebacuje. Bez toga je soba izgledala kao jednolicna smedja ploha: vanjska strana zidova
    bool inside = insideStart || !realPoses.empty();
    size_t whichPose = realPoses.empty() ? 0 : realPoses.size() / 2;   //sredina snimke, ne rub
    if(startPose >= 0 && !realPoses.empty()) whichPose = size_t(startPose) % realPoses.size();
    bool poseHeld = false;

    //POGLED SE VISE NE OKRECE SAM. Dok se snimalo iz zadanog kuta to je smetalo mjerenju, a dok
    //se gleda rukom smeta gledanju - kamera koja se mice sama odnese pogled s onoga sto se
    //upravo promatra. Sve micanje je sada misem, kao u Blenderu
    Orbit orbit;
    if(!cameraPath.empty()){
        orbit.fromPose(cameraPath[whichPose], 0.35f * bounds.radius);
    }else{
        orbit.pivot = bounds.centre;
        orbit.yaw = startAngle + lookOffset;
        orbit.pitch = inside ? 0.0f : 0.2f;
        orbit.distance = inside ? 0.001f * bounds.radius : 1.3f * bounds.radius;
    }

    //Stanje vucenja: gdje je mis bio prosli kadar i je li gumb tada bio dolje
    double lastCursorX = 0.0, lastCursorY = 0.0;
    bool dragging = false;

    printf("Blizu sredista je %.2f%% splatova.\n", 100.0 * double(bounds.middleShare));

    //Mjerna kocka iz argumenata: puna, na fiksnom mjestu. Zivi izvan grane jer je petlja mora
    //vratiti svaki put kad se kocka za brisanje ugasi
    SplatRenderer::Box measuringBox;

    if(boxSize > 0.0f){
        SplatRenderer::Box& box = measuringBox;
        box.visible = true;
        //Ne u sredistu scene - ondje je obicno sam predmet, pa bi kocka zavrsila zakopana u
        //njemu. Mjesto je FIKSNO U SVIJETU, vezano na kut zadan argumentom a ne na kameru: kocka
        //koja se seli s kamerom ne bi dokazivala nista o prostoru
        //GDJE. Bez modela: pomaknuto od sredista scene, jer je u sredistu obicno sam predmet pa bi
        //kocka zavrsila zakopana u njemu. S modelom: ispred POCETNE prave poze, jer kamera tada
        //gleda u prostor a ne u geometrijsko srediste - i bez toga kocka jednostavno nije u kadru.
        //
        //U oba slucaja mjesto je FIKSNO U SVIJETU i racuna se JEDNOM. Kocka koja se seli s kamerom
        //ne bi dokazivala nista o prostoru; ova stoji, pa se hodanjem kroz poze vidi kako je
        //zaklanja ono ispred nje i kako joj se mijenja velicina
        if(boxOnFloor && !modelPoints.empty()){
            //NA POD, a ne u zrak. U 3DGS sceni Y raste prema DOLJE, pa je pod medju NAJVECIM
            //vrijednostima Y. Uzima se 92. percentil a ne najveca: par odbjeglih tocaka ispod poda
            //inace odredi visinu, i kocka zavrsi zakopana.
            //
            //Vodoravno ide u srediste putanje - ondje je otvoren prostor kojim se prolazilo, pa
            //kocka nije unutar namjestaja. Mjesto se racuna JEDNOM i ostaje fiksno u svijetu
            std::vector<float> heights;
            heights.reserve(modelPoints.size());
            for(const glm::vec3& point : modelPoints) heights.push_back(point.y);
            std::sort(heights.begin(), heights.end());
            const float floorY = heights[size_t(0.92 * double(heights.size()))];

            box.halfExtent = glm::vec3(boxSize * bounds.radius);

            //MJESTO SE BIRA MJERENJEM, ne pogadja. Prvi pokusaj ju je stavio ispred kamere na
            //sredini putanje - i bila je vidljiva u 4 kadra od 634, jer putanja ide dalje a kocka
            //ostaje. Kocka koja se ne vidi ne dokazuje nista.
            //
            //Sada se po podu razapne mreza kandidata i za svakoga prebroji iz koliko ju kamera na
            //putanji vide. Uzima se onaj koji se vidi najduze - to je mjesto na koje se snimatelj
            //stvarno najvise osvrtao, i ondje kocka moze stajati dovoljno dugo da se vidi drzi li se
            std::vector<float> xs, zs;
            xs.reserve(modelPoints.size()); zs.reserve(modelPoints.size());
            for(const glm::vec3& point : modelPoints){ xs.push_back(point.x); zs.push_back(point.z); }
            std::sort(xs.begin(), xs.end());
            std::sort(zs.begin(), zs.end());

            //Cetvrtine a ne krajevi: rub oblaka je obicno par odbjeglih tocaka
            const float x0 = xs[size_t(0.25 * double(xs.size()))], x1 = xs[size_t(0.75 * double(xs.size()))];
            const float z0 = zs[size_t(0.25 * double(zs.size()))], z1 = zs[size_t(0.75 * double(zs.size()))];

            glm::vec3 best(0.0f);
            uint32_t bestSeen = 0;
            const int grid = 14;
            for(int i = 0; i <= grid; ++i){
                for(int j = 0; j <= grid; ++j){
                    const glm::vec3 candidate(
                        x0 + (x1 - x0) * float(i) / float(grid),
                        floorY - box.halfExtent.y,
                        z0 + (z1 - z0) * float(j) / float(grid));

                    uint32_t seen = 0;
                    for(const Engine::Pose& pose : cameraPath){
                        const glm::vec3 inCamera = glm::conjugate(pose.orientation) * (candidate - pose.position);
                        const float depth = -inCamera.z;
                        //Raspon dubine je sirok namjerno. Prva verzija ga je stegnula na 1.2
                        //polumjera i dobila JEDAN vidljiv polozaj od 634 - jer je pod 3.4 jedinice
                        //ispod kamere, pa da udje u kadar od 60 stupnjeva mora biti dalje od toga.
                        //Stegnut raspon je time iskljucivao bas ono sto se trazilo
                        if(depth < 0.05f * bounds.radius || depth > 3.0f * bounds.radius) continue;

                        //Vidno polje preglednika: 60 stupnjeva okomito, slika 16:9
                        const float halfHeight = depth * std::tan(glm::radians(30.0f));
                        const float halfWidth = halfHeight * 16.0f / 9.0f;
                        if(std::fabs(inCamera.x) < halfWidth * 0.9f && std::fabs(inCamera.y) < halfHeight * 0.9f) ++seen;
                    }
                    if(seen > bestSeen){ bestSeen = seen; best = candidate; }
                }
            }

            box.center = best;
            printf("Kocka na podu: srediste %.2f %.2f %.2f, vidljiva iz %u od %zu polozaja\n",
                   double(best.x), double(best.y), double(best.z), bestSeen, cameraPath.size());

            //Koji su to polozaji - da se zna gdje u preletu gledati
            std::string which;
            uint32_t listed = 0;
            for(size_t i = 0; i < cameraPath.size() && listed < 14; ++i){
                const glm::vec3 inCamera = glm::conjugate(cameraPath[i].orientation) * (best - cameraPath[i].position);
                const float depth = -inCamera.z;
                if(depth < 0.05f * bounds.radius || depth > 3.0f * bounds.radius) continue;
                const float halfHeight = depth * std::tan(glm::radians(30.0f));
                if(std::fabs(inCamera.x) < halfHeight * (16.0f / 9.0f) * 0.9f && std::fabs(inCamera.y) < halfHeight * 0.9f){
                    which += " " + std::to_string(i);
                    ++listed;
                }
            }
            printf("  vidi se iz kadrova:%s ...\n", which.c_str());
            printf("Pod je na y = %.3f; kocka lezi na njemu\n", double(floorY));
        }else if(!cameraPath.empty()){
            const Engine::Pose& from = cameraPath[whichPose];
            const glm::vec3 forward = from.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
            box.center = from.position + forward * (0.35f * bounds.radius);
        }else{
            box.center = bounds.centre + 0.55f * bounds.radius *
                         glm::vec3(std::sin(0.436f), 0.15f, std::cos(0.436f));   //25 st, fiksno
        }
        box.halfExtent = glm::vec3(boxSize * bounds.radius);
        box.orientation = glm::angleAxis(0.4f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)));
        splatRenderer->setBox(box);
        printf("Kocka %s, poluosovina %.3f (%.2f polumjera scene)\n",
               boxOnFloor ? "na podu" : (cameraPath.empty() ? "kraj sredista scene" : "ispred pocetne poze"),
               box.halfExtent.x, boxSize);
    }

    printf("\nMis: srednji gumb okrece, Shift+srednji ili desni pomice, kotacic priblizava.\n"
           "Tipkovnica: strelice okrecu, W/S naprijed i natrag, F vraca pogled na scenu.%s  ESC: kraj.\n\n",
           realPoses.empty() ? "" : "  N/P: sljedeca i prethodna prava kamera.");

    GLFWwindow* window = loom.window->getWindow();
    glfwSetScrollCallback(window, onScroll);

    // -------------------------------------------------------------------------------
    // Suicelje i kocka kojom se brise
    // -------------------------------------------------------------------------------

    Treadle::Ui ui;
    UiPainter painter(loom.device, loom.command, loom.getColorFormat(), vk::Format::eUndefined,
                      1u << 17);

    //KOCKA ZA BRISANJE je zasebna od one iz argumenata. Ona je mjerni predmet i puna je; ova je
    //kavez i sluzi odabiru. Rasterizator drzi jednu, pa kad je ova upaljena ona ima prednost -
    //a to i jest ono sto se tada gleda
    bool cubeOn = cubeAtStart;

    //TRI POLUOSOVINE, ne jedna. Kocka jednakih stranica ne moze obrisati pod a ostaviti zid -
    //za to treba ploca. Kvacica "sve jednako" drzi ih zajedno dok se ne zatreba drukcije, pa
    //uobicajeni slucaj i dalje ide jednim klizacem
    glm::vec3 cubeShare{0.15f, 0.15f, 0.15f};   //poluosovine kao udio polumjera scene
    bool cubeUniform = true;
    int deleteMode = 0;                   //0 unutra, 1 izvan
    //Pocetno na tocku oko koje se kruzi, ne na srediste scene: ondje kamera gleda, pa je kocka
    //odmah u kadru. U sredistu scene bi na sobi zavrsila iza ledja
    glm::vec3 cubeCenter = orbit.pivot;
    bool cubeMoved = true;                //treba prebrojati sto je u njoj

    //HVATANJE KOCKE TIPKOM G, kao u Blenderu. Drzi se dubina na kojoj je kocka bila kad je
    //uhvacena, pa se ona mice po ravnini usporednoj s ekranom - kotacic tada mijenja bas tu
    //dubinu umjesto da priblizava kameru
    bool grabbing = false;
    float grabDepth = 0.0f;

    //Sto je zadnje brisanje odnijelo. Cetiri bajta po obrisanom gaussianu, a bez toga jedan
    //promasen klik znaci ponovno ucitavanje cijele scene s diska
    std::vector<uint32_t> lastRemoved;

    size_t insideCount = 0;
    size_t aliveCount = cloud.count();
    double countMilliseconds = 0.0;
    std::string lastMessage;

    //Koliko je zivih gaussiana oblaka unutar kocke. Racuna se SAMO kad se kocka pomakne, jer je
    //to prolaz kroz cijeli oblak - na cetiri milijuna gaussiana desetak milisekundi
    auto countInside = [&]{
        const auto started = std::chrono::steady_clock::now();
        const glm::vec3 half = cubeShare * bounds.radius;

        insideCount = 0;
        aliveCount = 0;
        for(size_t i = 0; i < cloud.count(); ++i){
            if(!alive[i]) continue;
            ++aliveCount;
            if(SplatMath::insideBox(positions[i], cubeCenter, half)) ++insideCount;
        }
        countMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    };

    //Promjena prozora primijecena, ali jos ne izvedena - vidi petlju
    bool resizePending = false;
    double resizeSeenAt = 0.0;

    double lastReport = loom.getTime();
    int framesSinceReport = 0;
    double framesPerSecond = 0.0;
    uint32_t totalFrames = 0;
    uint32_t pairsAsked = 0;

    //Zbroj vremena po koraku kroz sekundu, pa se ispise prosjek. Poredak je onaj kojim su
    //oznake zapisane, i ime koraka je ime oznake kojom zavrsava
    std::vector<std::pair<std::string, double>> stageSums;
    uint32_t timedFrames = 0;

    //PROCESOROVA STRANA KADRA, po dijelovima. Kartica javlja svoje vrijeme sama, pa je dugo
    //izgledalo da je sve receno - a kad se pokazalo 13 ms na kartici uz jedan kadar u sekundi,
    //nije bilo nicega sto bi reklo gdje je ostatak.
    //
    //Odgovor je bio: 999 ms u slanju, i to JEDNAKO na sceni od 3.76 milijuna splatova i na onoj
    //od 18 tisuca. Broj koji ne ovisi o poslu nije cijena posla nego cekanje - Wayland
    //kompozitor gusi povrsinu koju nitko ne gleda na otprilike jedan poziv u sekundi, pa prozor
    //iza drugog prozora crta jednom u sekundi bez obzira sto je u njemu. Cim se prozor izvuce
    //naprijed, vraca se na brzinu ekrana. Ostavljeno jer je to jedini nacin da se ta razlika vidi
    double uiMilliseconds = 0.0, waitMilliseconds = 0.0, submitMilliseconds = 0.0;
    auto sinceNow = [](std::chrono::steady_clock::time_point from){
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
    };

    while(!loom.shouldClose()){
        const auto frameStarted = std::chrono::steady_clock::now();
        loom.pollEvents();

        if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        // ---------------------------------------------------------------------------
        // Ulaz u Treadle. Suicelje ga vidi PRVO, pa tek onda kamera pita smije li i ona -
        // inace bi vucenje klizaca ujedno okretalo pogled
        // ---------------------------------------------------------------------------

        //VELICINA SE PITA SVAKI KADAR. Prije je ovdje stajala ona procitana prije prvog kadra,
        //pa se poslije promjene prozora suicelje crtalo u starom mjerilu dok je mis dolazio u
        //novom: gumbi su bili pomaknuti i moralo se klikati pokraj njih. Nista nije puklo -
        //samo se promasivalo
        const vk::Extent2D windowSize = loom.getExtent();

        //PREGRADNJA TEK KAD SE PROZOR SMIRI. Vucenje ruba salje promjenu svakih par
        //milisekundi, a pregradnja trazi ponovno slanje cijelog oblaka na karticu - na cetiri
        //milijuna gaussiana skoro sekundu. Pregradnja po svakoj promjeni znacila bi da se
        //prozor ne da povuci. Suicelje se u medjuvremenu vec crta u novoj velicini, pa se
        //klika tocno i dok slika jos stoji u staroj
        if(windowSize.width != size.width || windowSize.height != size.height){
            if(!resizePending){
                resizePending = true;
                resizeSeenAt = loom.getTime();
            }else if(loom.getTime() - resizeSeenAt > 0.25 && windowSize.width > 0 && windowSize.height > 0){
                const auto rebuildStarted = std::chrono::steady_clock::now();
                loom.waitIdle();
                buildForSize(windowSize);
                uploadSplats(false);
                resizePending = false;
                printf("Nova velicina %ux%u, pločica %ux%u, za %.2f s\n",
                       size.width, size.height, splatRenderer->getGrid().width,
                       splatRenderer->getGrid().height,
                       std::chrono::duration<double>(std::chrono::steady_clock::now() - rebuildStarted).count());
            }
        }else{
            resizePending = false;
        }

        double cursorX = 0.0, cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);

        //POKAZIVAC DOLAZI U KOORDINATAMA PROZORA, a suicelje se crta u PIKSELIMA SLIKE. To su
        //dvije razlicite mjere cim kompozitor skalira prozor, i tada se promasuje jednako kao
        //s ustajalom velicinom. Omjer se pita, ne pretpostavlja
        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        const float cursorScaleX = windowWidth > 0 ? float(windowSize.width) / float(windowWidth) : 1.0f;
        const float cursorScaleY = windowHeight > 0 ? float(windowSize.height) / float(windowHeight) : 1.0f;

        Treadle::Input input;
        input.mouseX = float(cursorX) * cursorScaleX;
        input.mouseY = float(cursorY) * cursorScaleY;
        input.down[uint32_t(Treadle::MouseButton::Left)]   = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        input.down[uint32_t(Treadle::MouseButton::Right)]  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        input.down[uint32_t(Treadle::MouseButton::Middle)] = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
        input.wheel = wheelSinceLastFrame;
        input.shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                   || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

        if(cubeMoved){
            countInside();
            cubeMoved = false;
        }

        ui.begin(input, float(windowSize.width), float(windowSize.height));
        ui.panel("Loom", 16.0f, 16.0f, 320.0f);

        ui.value("splatova", std::to_string(splatCount));
        ui.value("kadrova/s", fmtNumber(framesPerSecond, 1));
        if(!cameraPath.empty()){
            ui.value("kamera", std::to_string(whichPose + 1) + " / " + std::to_string(cameraPath.size()));
        }

        ui.separator();
        ui.checkbox("kocka za brisanje", &cubeOn);

        if(cubeOn){
            ui.checkbox("sve stranice jednake", &cubeUniform);

            //Klizac koji je pomaknut vodi ostale kad su stranice vezane. Prvo se ne zna koji
            //ce to biti, pa se pita svaki - a mice se najvise jedan po kadru
            float sizeX = cubeShare.x, sizeY = cubeShare.y, sizeZ = cubeShare.z;
            const bool movedX = ui.slider("sirina X", &sizeX, 0.005f, 1.5f);
            const bool movedY = ui.slider("visina Y", &sizeY, 0.005f, 1.5f);
            const bool movedZ = ui.slider("dubina Z", &sizeZ, 0.005f, 1.5f);

            if(movedX || movedY || movedZ){
                const float driver = movedX ? sizeX : (movedY ? sizeY : sizeZ);
                cubeShare = cubeUniform ? glm::vec3(driver) : glm::vec3(sizeX, sizeY, sizeZ);
                cubeMoved = true;
            }else if(cubeUniform && (cubeShare.x != cubeShare.y || cubeShare.y != cubeShare.z)){
                //Kvacica upravo ukljucena nad razlicitim stranicama: izjednacuje se po najvecoj,
                //jer smanjivanje bi tiho odbacilo dio odabira
                cubeShare = glm::vec3(std::max({cubeShare.x, cubeShare.y, cubeShare.z}));
                cubeMoved = true;
            }

            if(ui.button("Kocka ovamo")){
                //Na tocku oko koje se kruzi, a ne na kameru: to je mjesto u koje se gleda, i
                //jedino koje korisnik postavlja namjerno
                cubeCenter = orbit.pivot;
                cubeMoved = true;
            }

            ui.label("G i mis: pomakni kocku");

            ui.choice("brise se", {"unutra", "izvan"}, &deleteMode);
            ui.value("u kocki", std::to_string(insideCount) + " od " + std::to_string(aliveCount));
            ui.value("brojanje", fmtNumber(countMilliseconds, 1) + " ms");

            if(ui.button("Obrisi")){
                const glm::vec3 half = cubeShare * bounds.radius;
                const bool removeInside = deleteMode == 0;

                lastRemoved.clear();
                for(size_t i = 0; i < cloud.count(); ++i){
                    if(!alive[i]) continue;
                    if(SplatMath::insideBox(positions[i], cubeCenter, half) == removeInside){
                        alive[i] = 0;
                        lastRemoved.push_back(uint32_t(i));
                    }
                }
                const size_t removed = lastRemoved.size();

                //CEKA SE PRIJE PISANJA. SplatRenderer ima jedan primjerak radnih polja, pa slanje
                //novog oblaka dok prethodni kadar jos crta prepisuje ono sto kartica upravo cita
                loom.waitIdle();
                buildSplats();
                uploadSplats(false);
                cubeMoved = true;

                aliveCount -= removed;   //tocno i nakon drugog brisanja; countInside ga svejedno prebroji
                lastMessage = "obrisano " + std::to_string(removed);
                printf("Obrisano %zu gaussiana (%s kocke), ostalo %zu\n",
                       removed, removeInside ? "unutar" : "izvan", aliveCount);
            }

            if(!lastRemoved.empty() && ui.button("Vrati zadnje brisanje")){
                for(uint32_t index : lastRemoved) alive[index] = 1;
                aliveCount += lastRemoved.size();

                loom.waitIdle();
                buildSplats();
                uploadSplats(false);
                cubeMoved = true;

                lastMessage = "vraceno " + std::to_string(lastRemoved.size());
                printf("Vraceno %zu gaussiana\n", lastRemoved.size());
                lastRemoved.clear();
            }

            if(ui.button("Spremi .ply")){
                if(aliveCount == 0){
                    //Prazan .ply se ne da procitati natrag, pa bi zapisivanje dalo datoteku
                    //koja izgleda kao rezultat a nije. Do praznog se dodje jednim klikom:
                    //"izvan" male kocke u praznom zraku odnese cijelu scenu
                    lastMessage = "nema sto spremiti";
                    printf("Nista nije ostalo - prazan .ply se ne pise\n");
                }else{
                    //NOVI FILE, nikad preko ulaznog. Brisanje nema korak natrag, pa je jedina
                    //obrana to da izvorna datoteka ostane netaknuta dok se ne provjeri sto je ispalo
                    const size_t dot = path.rfind('.');
                    const std::string out = (dot == std::string::npos ? path : path.substr(0, dot))
                                          + "_rezano.ply";
                    try{
                        Spool::saveGaussianPly(out, cloud, alive);
                        lastMessage = "spremljeno";
                        printf("Spremljeno %s (%zu gaussiana)\n", out.c_str(), aliveCount);
                    }catch(const std::exception& error){
                        lastMessage = "greska pri spremanju";
                        printf("%s\n", error.what());
                    }
                }
            }

            if(!lastMessage.empty()) ui.label(lastMessage);
        }

        ui.end();

        // ---------------------------------------------------------------------------
        // Pogled. Sve micanje je misem; tipkovnica je samo rezerva za mis bez srednjeg gumba
        // ---------------------------------------------------------------------------

        const bool orbitDrag = input.isDown(Treadle::MouseButton::Middle) && !input.shift;
        const bool panDrag = (input.isDown(Treadle::MouseButton::Middle) && input.shift)
                          || input.isDown(Treadle::MouseButton::Right);

        //Dok se kocka drzi, mis pripada njoj. Bez ovoga bi se scena okretala ispod kocke koja
        //se upravo postavlja, pa bi se oboje micalo i nista se ne bi dalo pogoditi
        const bool holdingCube = cubeOn && glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
        const bool wantsDrag = (orbitDrag || panDrag) && !ui.wantsMouse() && !holdingCube;

        if(wantsDrag && dragging){
            const float dx = float(cursorX - lastCursorX);
            const float dy = float(cursorY - lastCursorY);

            if(orbitDrag){
                orbit.yaw -= dx * 0.006f;
                orbit.pitch -= dy * 0.006f;

                //Preko zenita bi se gore prevrnulo i pogled bi se vrtio na mjestu. Granica je tik
                //ispod pravog kuta, jer je na samom zenitu smjer pogleda i "gore" isti vektor
                orbit.pitch = std::clamp(orbit.pitch, -1.55f, 1.55f);
            }else{
                //Pomicanje PRATI POKAZIVAC: koliko svijeta stane u jedan piksel na udaljenosti
                //sredista. Bez toga se na velikoj sceni mice nevidljivo malo, a na maloj odleti
                const float perPixel = 2.0f * std::max(orbit.distance, 1e-4f)
                                     * std::tan(0.5f * cameraConfig.fovY) / float(windowSize.height);
                orbit.pivot += (-dx * orbit.right() + dy * orbit.screenUp()) * perPixel;
            }
        }
        dragging = wantsDrag;
        lastCursorX = cursorX;
        lastCursorY = cursorY;

        // ---------------------------------------------------------------------------
        // KOCKA SE HVATA TIPKOM G. Klizaci mogu sve, ali postaviti kocku trima brojevima
        // znaci gledati u brojeve umjesto u scenu. Ovako se drzi G i kocka ide za
        // pokazivacem - po ravnini USPOREDNOJ S EKRANOM, na dubini na kojoj je zatecena.
        //
        // Dubina se ne pogadja iz pokazivaca jer je pokazivac dvodimenzionalan: zraka kroz
        // piksel sijece beskonacno mnogo tocaka i sve su jednako dobre. Zato dubinu dok traje
        // hvatanje mijenja kotacic, koji tada ne priblizava kameru
        // ---------------------------------------------------------------------------

        const bool grabNow = cubeOn && glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !ui.wantsMouse();

        if(grabNow && !grabbing){
            grabDepth = glm::dot(cubeCenter - orbit.eye(), orbit.forward());
            printf("  kocka uhvacena; kotacic mijenja dubinu\n");
        }
        grabbing = grabNow;

        if(grabbing){
            grabDepth *= std::exp(wheelSinceLastFrame * 0.1f);
            grabDepth = std::max(grabDepth, 0.02f * bounds.radius);

            //Zraka kroz pokazivac, slozena iz osi samog pogleda - bez obrata matrice i bez
            //jos jednog mjesta na kojem se konvencija moze raziici
            const float tanHalf = std::tan(0.5f * cameraConfig.fovY);
            const float aspect = float(windowSize.width) / float(windowSize.height);
            const float acrossX = (2.0f * input.mouseX / float(windowSize.width) - 1.0f) * tanHalf * aspect;
            const float acrossY = -(2.0f * input.mouseY / float(windowSize.height) - 1.0f) * tanHalf;

            const glm::vec3 ray = orbit.forward() + orbit.right() * acrossX + orbit.screenUp() * acrossY;

            //Dijeli se s komponentom PO SMJERU POGLEDA, ne s duljinom: tako svaka tocka ravnine
            //ostane na istoj dubini, pa kocka ne bjezi prema rubovima kadra
            const float along = glm::dot(ray, orbit.forward());
            if(std::fabs(along) > 1e-6f){
                cubeCenter = orbit.eye() + ray * (grabDepth / along);
                cubeMoved = true;
            }
        }else if(!ui.wantsMouse() && wheelSinceLastFrame != 0.0f){
            //Mnozenje a ne oduzimanje: korak kotacica je uvijek isti udio udaljenosti, pa
            //priblizavanje jednako radi na sobi i na predmetu
            orbit.distance *= std::exp(-wheelSinceLastFrame * 0.15f);
        }
        wheelSinceLastFrame = 0.0f;

        if(glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) orbit.yaw -= 0.02f;
        if(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) orbit.yaw += 0.02f;
        if(glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) orbit.pitch = std::min(1.55f, orbit.pitch + 0.02f);
        if(glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) orbit.pitch = std::max(-1.55f, orbit.pitch - 0.02f);
        if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) orbit.distance *= 0.97f;
        if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) orbit.distance *= 1.03f;

        //Izlaz iz zabludjelog pogleda. Scena je velika i lako je odletjeti van nje, a onda nista
        //ne pomaze jer se kruzi oko tocke koja je ostala iza
        if(glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS){
            orbit.pivot = bounds.centre;
            orbit.distance = 1.3f * bounds.radius;
        }

        orbit.distance = std::clamp(orbit.distance, 1e-4f * bounds.radius, 40.0f * bounds.radius);

        //U preletu poza napreduje sama, jedan kadar jedna poza
        if(flyover && !cameraPath.empty() && totalFrames > 0){
            whichPose = size_t(totalFrames) % cameraPath.size();
        }

        //Setnja kroz prave poze. Korak je jedna kamera, a ne jedan kadar snimke - kamere su vec
        //prorijedjene, pa je jedan korak vidljiv pomak a ne treptaj
        if(!cameraPath.empty()){
            const bool nextNow = glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS;
            const bool backNow = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
            if((nextNow || backNow) && !poseHeld){
                if(nextNow) whichPose = (whichPose + 1) % cameraPath.size();
                else        whichPose = (whichPose + cameraPath.size() - 1) % cameraPath.size();
                orbit.fromPose(cameraPath[whichPose], 0.35f * bounds.radius);
                printf("  kamera %zu od %zu\n", whichPose + 1, cameraPath.size());
            }
            poseHeld = nextNow || backNow;
        }

        //SNIMANJE I PRELET IDU IZ POZE, ne iz pogleda koji se rukom pomice. Slika koja se
        //usporedjuje s drugom mora doci iz istog mjesta oba puta, a rucni pogled to nije
        if((flyover || framesThenShot > 0) && !cameraPath.empty()){
            const Engine::Pose& pose = cameraPath[whichPose];
            const glm::vec3 ahead = glm::angleAxis(lookOffset, glm::vec3(0.0f, 1.0f, 0.0f))
                                  * (pose.orientation * glm::vec3(0.0f, 0.0f, -1.0f));
            cameraConfig.position = pose.position;
            cameraConfig.target = pose.position + ahead * bounds.radius;
            cameraConfig.up = pose.orientation * glm::vec3(0.0f, 1.0f, 0.0f);
        }else{
            cameraConfig.position = orbit.eye();
            cameraConfig.target = orbit.pivot;
            cameraConfig.up = glm::vec3(0.0f, -1.0f, 0.0f);   //3DGS scene dolaze s Y prema dolje
        }
        camera = Camera(cameraConfig);

        //Kocka za brisanje ima prednost nad mjernom: rasterizator drzi jednu, a kad je ova
        //upaljena ona je ono sto se gleda
        if(cubeOn){
            SplatRenderer::Box cage;
            cage.visible = true;
            cage.center = cubeCenter;
            cage.halfExtent = cubeShare * bounds.radius;
            cage.orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            cage.color = glm::vec3(1.0f, 0.75f, 0.2f);
            cage.edgeShare = 0.02f;
            splatRenderer->setBox(cage);
        }else{
            splatRenderer->setBox(measuringBox);
        }

        const glm::mat4 view = camera.getView();
        const glm::mat4 projection = camera.getProjection(size.width, size.height);
        const CameraIntrinsics intrinsics = CameraIntrinsics::fromProjection(projection, size.width, size.height);

        uiMilliseconds += sinceNow(frameStarted);
        const auto waitStarted = std::chrono::steady_clock::now();

        //ČEKA SE PRIJE PISANJA, i to nije opreznost nego nužnost. Loom drži dva kadra u letu, a
        //SplatRenderer ima JEDAN primjerak svakog radnog polja - kameru, splatove, ključeve,
        //poretke, raspone. Kad bi se sljedeći kadar poslao dok prethodni još crta, GPU bi čitao
        //pola jedne a pola druge scene: slika se raspadne u šum koji izgleda kao greška
        //rasterizatora a nije. Dok polja ne postanu po kadru, ovo košta paralelizam kartice i
        //procesora - i to je sad, kad je priprema na kartici, prava cijena
        loom.waitIdle();

        waitMilliseconds += sinceNow(waitStarted);
        const auto submitStarted = std::chrono::steady_clock::now();

        //Prosli kadar je gotov, pa je broj koji je trazio sad tocan
        pairsAsked = splatRenderer->requestedPairs();

        const std::vector<GpuTimestamp> marks = loom.renderer.readFrameTimes();
        if(marks.size() > 1){
            if(stageSums.size() != marks.size() - 1){
                stageSums.clear();
                for(size_t i = 1; i < marks.size(); ++i) stageSums.push_back({marks[i].label, 0.0});
            }
            for(size_t i = 1; i < marks.size(); ++i){
                stageSums[i - 1].second += marks[i].milliseconds - marks[i - 1].milliseconds;
            }
            ++timedFrames;
        }

        splatRenderer->setCamera(view, cameraConfig.position, intrinsics.fx, intrinsics.fy,
                                intrinsics.cx, intrinsics.cy);

        if(!loom.renderer.beginFrame()) continue;

        splatRenderer->prepare(loom.renderer, splatCount);
        splatRenderer->draw(loom.renderer, splatCount);

        loom.renderer.beginPass();
        loom.renderer.drawFullscreen(*present);

        //SUICELJE SE NE SNIMA. Slika koja se usporedjuje s drugom ne smije nositi plohu s
        //gumbima preko sebe - a to bi se primijetilo tek kad bi netko usporedio dvije slike
        //i vidio da se razlikuju bas ondje gdje je ploha
        if(uiInShot || (!flyover && framesThenShot == 0)){
            painter.draw(loom.renderer, ui.drawn(), windowSize.width, windowSize.height);
        }

        loom.renderer.endPass();

        loom.renderer.endFrame();
        submitMilliseconds += sinceNow(submitStarted);

        ++framesSinceReport;
        ++totalFrames;

        //U preletu se sprema SVAKI kadar, pa se od njih sklopi snimka. Inace se ceka zadani broj
        //kadrova i sprema jedan - da se scena stigne slegnuti prije nego se usporedjuje
        const bool saveNow = flyover ? (totalFrames > 0 && totalFrames <= cameraPath.size())
                                     : (framesThenShot > 0 && totalFrames >= framesThenShot);

        if(saveNow){
            loom.waitIdle();
            const ImageData shot = loom.renderer.readLastFrame();

            //Poredak se PITA FORMATU. Ovdje je dugo pisalo da readLastFrame vraca RGBA i to je
            //bilo krivo: swapchain je pregovorio eB8G8R8A8Srgb, pa je svaka spremljena slika
            //imala zamijenjeno crveno i plavo. U prozoru se to ne vidi jer prozor cita isti
            //format kojim je pisano - greska je postojala samo u datoteci
            const Spool::Image image = Spool::imageFromPixels(shot.pixels.data(),
                shot.extent.width, shot.extent.height,
                isBgraFormat(shot.format) ? Spool::ChannelOrder::BGRA : Spool::ChannelOrder::RGBA);
            std::string name = shotName;
            if(flyover){
                char numbered[64];
                std::snprintf(numbered, sizeof(numbered), "_%05u.png", uint32_t(totalFrames - 1));
                const size_t dot = shotName.rfind('.');
                name = (dot == std::string::npos ? shotName : shotName.substr(0, dot)) + numbered;
            }
            Spool::saveImage(name, image);

            if(flyover){
                if(totalFrames % 25 == 0) printf("\r  prelet %u / %zu  ", uint32_t(totalFrames), cameraPath.size());
                if(totalFrames >= cameraPath.size()){
                    printf("\nPrelet gotov: %zu kadrova\n", cameraPath.size());
                    break;
                }
            }else{
                printf("\nSnimljeno %s (%ux%u)\n", name.c_str(), image.width, image.height);
                break;
            }
        }

        const double now = loom.getTime();
        if(now - lastReport > 1.0){
            framesPerSecond = framesSinceReport / (now - lastReport);
            printf("  %.1f kadrova/s   %u splatova, %u parova%s\n",
                   framesSinceReport / (now - lastReport), splatCount, pairsAsked,
                   pairsAsked > rendererConfig.maxPairs ? "   PREMALO MJESTA - dio slike nedostaje (maxPairs)" : "");
            if(timedFrames > 0){
                double total = 0.0;
                for(const auto& stage : stageSums) total += stage.second;
                printf("    kartica %.0f ms:", total / timedFrames);
                for(const auto& stage : stageSums){
                    printf("  %s %.1f", stage.first.c_str(), stage.second / timedFrames);
                }
                printf("\n");
                for(auto& stage : stageSums) stage.second = 0.0;
                timedFrames = 0;
            }
            if(framesSinceReport > 0){
                printf("    procesor: suicelje %.1f ms, cekanje %.1f ms, slanje %.1f ms\n",
                       uiMilliseconds / framesSinceReport, waitMilliseconds / framesSinceReport,
                       submitMilliseconds / framesSinceReport);
            }
            uiMilliseconds = waitMilliseconds = submitMilliseconds = 0.0;
            fflush(stdout);
            lastReport = now;
            framesSinceReport = 0;
        }
    }

    loom.waitIdle();
    return 0;
}
