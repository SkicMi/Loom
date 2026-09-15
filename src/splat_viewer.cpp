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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace{

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

    for(size_t i = 0; i < cloud.count(); i += stride){
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

    //TOCKE IZ MODELA KAO SPLATOVI. Ne crta ih se zasebnim prolazom nego se ubace u isti oblak:
    //rasterizator ih time sortira i zaklanja zajedno sa scenom, pa tocka iza zida stvarno zavrsi
    //iza zida. Zasebni prolaz bi ih crtao preko svega i pokazivao krivu sliku.
    //
    //Sitne su i neprozirne, i namjerno svijetle - nisu dio prizora nego mjerni instrument
    size_t pointSplats = 0;
    if(showPoints && !realPoses.empty() && !modelPoints.empty()){
        for(const glm::vec3& point : modelPoints){
            Splat dot;
            dot.position = point;
            dot.scale = glm::vec3(0.004f);
            dot.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            dot.opacity = 0.95f;
            dot.color = glm::vec3(0.15f, 0.95f, 1.0f);      //ciklama-plava: ne pojavljuje se u sobi
            splats.push_back(dot);
            sourceIndex.push_back(UINT32_MAX);              //nema izvora u oblaku - vidi upload
            ++pointSplats;
        }
        printf("  + %zu tocaka iz modela nacrtano kao splatovi\n", pointSplats);
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
    const vk::Extent2D size = loom.getExtent();

    //Meta u koju rasterizator pise, i s koje fullscreen prolaz cita. Float, jer je to ono sto
    //kompozicija stvarno racuna - pretvorbu u osam bita napravi tek swapchain
    RenderTargetConfig targetConfig;
    targetConfig.colorFormat = vk::Format::eR32G32B32A32Sfloat;
    targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
    targetConfig.enableDepth = false;
    targetConfig.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    RenderTarget splatTarget(loom.device, size, targetConfig);

    Material present(loom.device, loom.command, loom.getDescriptorPool(),
                     loom.vulkanGraphicsPipeline, splatTarget.getSampled());

    SplatRendererConfig rendererConfig;
    rendererConfig.tileSize = tileSize;
    rendererConfig.maxSplats = uint32_t(splats.size());
    //Najvise sto stane u najmanju granicu grupa koju Vulkan jamci: 65535 grupa po 256
    rendererConfig.maxPairs = 65535u * 256u;
    rendererConfig.maxShCoefficients = useSH ? coeffsPerChannel * 3 : 0;

    SplatRenderer splatRenderer(loom.device, loom.getDescriptorPool(),
                                splatTarget.getColorImage(), size, rendererConfig);

    //SIROVI SPLATOVI, JEDNOM. Aktivacija je vec napravljena jer ne ovisi o kameri; sve sto ovisi
    //racuna kartica svaki kadar. Koeficijenti se uzimaju preko sourceIndex, jer korak preskace
    const uint32_t splatCount = uint32_t(splats.size());
    {
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

        splatRenderer.uploadRaw(raw, rest, useSH ? cloud.shDegree : 0, useSH ? coeffsPerChannel : 0);
        printf("  na karticu: %.0f MB splatova, %.0f MB koeficijenata\n",
               double(raw.size() * sizeof(SplatMath::RawSplat)) / 1048576.0,
               double(rest.size() * sizeof(float)) / 1048576.0);
    }

    printf("  pločica %ux%u, mreza %ux%u\n", tileSize, tileSize,
           splatRenderer.getGrid().width, splatRenderer.getGrid().height);

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
    float angle = startAngle;
    float distance = inside ? 0.0f : 1.3f * bounds.radius;
    float height = inside ? 0.0f : 0.2f * bounds.radius;
    bool insideHeld = false;

    printf("Blizu sredista je %.2f%% splatova.\n", 100.0 * double(bounds.middleShare));

    if(boxSize > 0.0f){
        SplatRenderer::Box box;
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
        splatRenderer.setBox(box);
        printf("Kocka %s, poluosovina %.3f (%.2f polumjera scene)\n",
               boxOnFloor ? "na podu" : (cameraPath.empty() ? "kraj sredista scene" : "ispred pocetne poze"),
               box.halfExtent.x, boxSize);
    }

    printf("\nStrelice: kruzenje i visina.  W/S: naprijed i natrag.  I: izvana/iznutra.%s  ESC: kraj.\n\n",
           realPoses.empty() ? "" : "  N/P: sljedeca i prethodna prava kamera.");

    GLFWwindow* window = loom.window->getWindow();
    double lastReport = loom.getTime();
    int framesSinceReport = 0;
    uint32_t totalFrames = 0;
    uint32_t pairsAsked = 0;

    //Zbroj vremena po koraku kroz sekundu, pa se ispise prosjek. Poredak je onaj kojim su
    //oznake zapisane, i ime koraka je ime oznake kojom zavrsava
    std::vector<std::pair<std::string, double>> stageSums;
    uint32_t timedFrames = 0;

    while(!loom.shouldClose()){
        loom.pollEvents();

        if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        if(glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) angle -= 0.02f;
        if(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) angle += 0.02f;
        if(glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) height += 0.03f * bounds.radius;
        if(glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) height -= 0.03f * bounds.radius;
        //U prostoru W/S hoda naprijed i natrag jer mnozenje udaljenosti oko nule ne mice nista;
        //oko predmeta ostaje mnozenje, da se prilaz jednako ponasa na maloj i velikoj sceni
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
                printf("  kamera %zu od %zu\n", whichPose + 1, cameraPath.size());
            }
            poseHeld = nextNow || backNow;
        }

        //Prebacivanje na pritisak, ne na drzanje
        const bool insideNow = glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS;
        if(insideNow && !insideHeld){
            inside = !inside;
            distance = inside ? 0.0f : 1.3f * bounds.radius;
            height = inside ? 0.0f : 0.2f * bounds.radius;
            printf("  pogled: %s\n", inside ? "iznutra" : "izvana");
        }
        insideHeld = insideNow;

        if(inside){
            if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) distance += 0.02f * bounds.radius;
            if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) distance -= 0.02f * bounds.radius;
        }else{
            if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) distance *= 0.97f;
            if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) distance *= 1.03f;
        }

        //Bez tipke se polako okrece, da se odmah vidi da je scena prostorna. Kad se snima iz
        //zadanog kuta to bi pomaknulo bas ono sto se htjelo usporediti, pa tada miruje
        if(framesThenShot == 0) angle += 0.002f;

        //PREDMET: kamera kruzi oko njega i gleda u njega. PROSTOR: kamera stoji u sredini i gleda
        //VAN - kut vise ne okrece polozaj nego pogled, jer je to jedini nacin da se prostor obidje
        //iznutra. Gledanje u srediste bi iz sobe znacilo zuriti u suprotni zid kroz zrak
        const glm::vec3 direction(std::sin(angle), 0.0f, std::cos(angle));
        if(!cameraPath.empty()){
            //PRAVA POZA. Engineova Pose je kamera u svijetu, -Z naprijed i +Y gore - ista
            //konvencija koju Loomova Camera ocekuje, pa se smjer i gore uzimaju iz nje umjesto da
            //se pretpostavljaju. Strelice lijevo/desno okrecu pogled oko te poze, da se moze
            //pogledati uokolo bez skakanja na drugu kameru
            const Engine::Pose& pose = cameraPath[whichPose];
            const glm::quat turn = glm::angleAxis(angle - startAngle + lookOffset, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 forward = turn * (pose.orientation * glm::vec3(0.0f, 0.0f, -1.0f));

            cameraConfig.position = pose.position + glm::vec3(0.0f, height, 0.0f) + forward * distance;
            cameraConfig.target = cameraConfig.position + forward * bounds.radius;
            cameraConfig.up = pose.orientation * glm::vec3(0.0f, 1.0f, 0.0f);
        }else if(inside){
            cameraConfig.position = bounds.centre + direction * distance + glm::vec3(0.0f, height, 0.0f);
            cameraConfig.target = cameraConfig.position + direction * bounds.radius
                                + glm::vec3(0.0f, -0.15f * height, 0.0f);
        }else{
            cameraConfig.position = bounds.centre + glm::vec3(distance * std::sin(angle), height,
                                                              distance * std::cos(angle));
            cameraConfig.target = bounds.centre;
        }
        camera = Camera(cameraConfig);

        const glm::mat4 view = camera.getView();
        const glm::mat4 projection = camera.getProjection(size.width, size.height);
        const CameraIntrinsics intrinsics = CameraIntrinsics::fromProjection(projection, size.width, size.height);

        //ČEKA SE PRIJE PISANJA, i to nije opreznost nego nužnost. Loom drži dva kadra u letu, a
        //SplatRenderer ima JEDAN primjerak svakog radnog polja - kameru, splatove, ključeve,
        //poretke, raspone. Kad bi se sljedeći kadar poslao dok prethodni još crta, GPU bi čitao
        //pola jedne a pola druge scene: slika se raspadne u šum koji izgleda kao greška
        //rasterizatora a nije. Dok polja ne postanu po kadru, ovo košta paralelizam kartice i
        //procesora - i to je sad, kad je priprema na kartici, prava cijena
        loom.waitIdle();

        //Prosli kadar je gotov, pa je broj koji je trazio sad tocan
        pairsAsked = splatRenderer.requestedPairs();

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

        splatRenderer.setCamera(view, cameraConfig.position, intrinsics.fx, intrinsics.fy,
                                intrinsics.cx, intrinsics.cy);

        if(!loom.renderer.beginFrame()) continue;

        splatRenderer.prepare(loom.renderer, splatCount);
        splatRenderer.draw(loom.renderer, splatCount);

        loom.renderer.beginPass();
        loom.renderer.drawFullscreen(present);
        loom.renderer.endPass();

        loom.renderer.endFrame();

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
            fflush(stdout);
            lastReport = now;
            framesSinceReport = 0;
        }
    }

    loom.waitIdle();
    return 0;
}
