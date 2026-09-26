// loom-render: render projekta iz kamere, bez prozora.
//
//   loom-render projekt.usda [--kamera /Solve/Kamera] [--kadar 42 | --od 1 --do 120]
//               [--uzorci 256] [--odbijanja 12] [--rezolucija 0.5] [--izlaz mapa] [--ime render]
//               [--bez-ploce] [--prozirno] [--bez-catchera] [--bez-neba]
//               [--dubina | --bez-dubine] [--normale] [--albedo] [--bez-filtra]
//               [--sunce elevacija azimut jakost] [--velicina-sunca 0.53] [--zamucenost 3] [--nebo 0.35]
//               [--hdri nebo.hdr] [--hdri-jakost 1] [--hdri-rotacija 0] [--jednoliko]
//               [--agx] [--ekspozicija 0] [--bez-exr] [--bez-png] [--dretve N] [--procesor]
//               [--motion-blur 0.5] [--koraci 16] [--prag-suma 0.01] [--kaustike] [--ostrina 2.8 5] [--holdout]
//               [--post] [--bloom 0.04] [--vinjeta 0.15] [--aberacija 1.5] [--zrno 0.03] ...
//
// Racuna na KARTICI (Vulkan compute, TracerGpu) kad je ima; bez Vulkana, ili s --procesor, na
// procesoru. Oba daju istu sliku (test_tracer_gpu).
//
// Isti most (LoomRender.h) i isti tracer kao gumb Render u editoru - pa se render provjerava i na
// stroju bez kartice i bez zaslona, i pokrece u noci za cijelu sekvencu. Na kraju ispise sto je
// zapisao; greska je izlazni kod 1 s razlogom, nikad tiho prazna slika.
#include "LoomRender.h"
#include "LoomRenderGpu.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <Warp/Project.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace{

void usage(){
    std::printf(
        "loom-render projekt.usda [zastavice]\n"
        "  --kamera PUT          kamera (USD put, npr. /Solve/Kamera); zadano prva u sceni\n"
        "  --kadar N             jedan kadar (zadano: pocetak timelinea)\n"
        "  --od A --do B         sekvenca, datoteke ime_####\n"
        "  --uzorci N            uzoraka po pikselu (128)\n"
        "  --odbijanja N         najvise odbijanja (12)\n"
        "  --rezolucija S        udio rezolucije kamere (1)\n"
        "  --izlaz MAPA --ime IME\n"
        "  --bez-ploce --prozirno --bez-catchera --bez-neba\n"
        "  --dubina --bez-dubine --normale --albedo --bez-filtra\n"
        "  --sunce E A J  --velicina-sunca STUP  --zamucenost T  --nebo L\n"
        "  --hdri DATOTEKA --hdri-jakost J --hdri-rotacija STUP  --jednoliko\n"
        "  --scena               nebo i svjetla samo iz scene (kupola, sunce, lampe)\n"
        "  --motion-blur Z       zatvarac otvoren Z kadra (0.5 = 180 st); --koraci N trenutaka (16)\n"
        "  --prag-suma X         prilagodljivo uzorkovanje (0.01; 0 = svi pikseli sve uzorke)\n"
        "  --kaustike            kaustike putanjama umjesto staklenih sjena (tocno, sumovito)\n"
        "  --ostrina N D         dubinska ostrina: f-broj N, ostro na udaljenosti D (1 jedinica = 1 m)\n"
        "  --senzor MM           sirina senzora za zarisnu u mm (36)\n"
        "  --holdout             splat scene zaklanja CG iza stvarnih ploha (snimka se vidi)\n"
        "  --filtar oidn|atrous  filtar suma (zadano OIDN kad je ucitan, tools/oidn/fetch.sh)\n"
        "  --agx --ekspozicija EV --bez-exr --bez-png --dretve N\n"
        "  --procesor            racunaj na procesoru i kad kartica postoji\n"
        "  --post                post s zadanim (bloom 0.04, vinjeta 0.15); ili pojedinacno:\n"
        "  --bloom X --bloom-radijus X --bloom-prag X --vinjeta X --aberacija PX\n"
        "  --temperatura K --kontrast X --zasicenje X --zrno X   (samo PNG; EXR ostaje sirov)\n");
}

}

int main(int argc, char** argv){
    namespace fs = std::filesystem;
    if(argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"){ usage(); return argc < 2 ? 1 : 0; }
    const std::string projectPath = argv[1];
    Loom::RenderOptions options;
    std::string cameraPath;
    double single = -1.0;
    bool haveRange = false;
    auto number = [&](int& i){ if(i + 1 >= argc){ std::fprintf(stderr, "%s treba broj\n", argv[i]); std::exit(1); } return std::atof(argv[++i]); };
    for(int i = 2; i < argc; ++i){
        const std::string a = argv[i];
        if(a == "--kamera" && i + 1 < argc) cameraPath = argv[++i];
        else if(a == "--kadar") single = number(i);
        else if(a == "--od"){ options.firstFrame = number(i); haveRange = true; }
        else if(a == "--do"){ options.lastFrame = number(i); haveRange = true; }
        else if(a == "--uzorci") options.samples = uint32_t(std::max(1.0, number(i)));
        else if(a == "--odbijanja") options.maxBounces = uint32_t(std::max(0.0, number(i)));
        else if(a == "--rezolucija") options.resolutionScale = float(number(i));
        else if(a == "--izlaz" && i + 1 < argc) options.outputFolder = argv[++i];
        else if(a == "--ime" && i + 1 < argc) options.name = argv[++i];
        else if(a == "--bez-ploce") options.plate = false;
        else if(a == "--prozirno") options.transparent = true;
        else if(a == "--bez-catchera") options.shadowCatcher = false;
        else if(a == "--bez-neba") options.skyVisible = false;
        else if(a == "--dubina") options.depth = true;
        else if(a == "--bez-dubine") options.depth = false;
        else if(a == "--normale") options.normal = true;
        else if(a == "--albedo") options.albedo = true;
        else if(a == "--bez-filtra") options.denoise = false;
        else if(a == "--sunce"){
            options.sky = Loom::RenderOptions::Sky::Physical;
            options.sunElevation = float(number(i));
            options.sunAzimuth = float(number(i));
            options.sunIntensity = float(number(i));
        }
        else if(a == "--velicina-sunca") options.sunSize = float(number(i));
        else if(a == "--zamucenost") options.turbidity = float(number(i));
        else if(a == "--nebo") options.skyIntensity = float(number(i));
        else if(a == "--hdri" && i + 1 < argc){ options.sky = Loom::RenderOptions::Sky::Hdri; options.hdri = argv[++i]; }
        else if(a == "--hdri-jakost") options.hdriIntensity = float(number(i));
        else if(a == "--hdri-rotacija") options.hdriRotation = float(number(i));
        else if(a == "--jednoliko") options.sky = Loom::RenderOptions::Sky::Uniform;
        else if(a == "--scena") options.sky = Loom::RenderOptions::Sky::Scene;
        else if(a == "--agx") options.view = Tracer::ViewTransform::AgX;
        else if(a == "--ekspozicija") options.exposure = float(number(i));
        else if(a == "--bez-exr") options.writeExr = false;
        else if(a == "--bez-png") options.writePng = false;
        else if(a == "--dretve") options.threads = uint32_t(std::max(0.0, number(i)));
        else if(a == "--procesor") options.gpu = false;
        else if(a == "--bloom"){ options.post.enabled = true; options.post.bloom = float(number(i)); }
        else if(a == "--bloom-radijus"){ options.post.enabled = true; options.post.bloomRadius = float(number(i)); }
        else if(a == "--bloom-prag"){ options.post.enabled = true; options.post.bloomThreshold = float(number(i)); }
        else if(a == "--vinjeta"){ options.post.enabled = true; options.post.vignette = float(number(i)); }
        else if(a == "--aberacija"){ options.post.enabled = true; options.post.chromaticAberration = float(number(i)); }
        else if(a == "--temperatura"){ options.post.enabled = true; options.post.temperature = float(number(i)); }
        else if(a == "--kontrast"){ options.post.enabled = true; options.post.contrast = float(number(i)); }
        else if(a == "--zasicenje"){ options.post.enabled = true; options.post.saturation = float(number(i)); }
        else if(a == "--zrno"){ options.post.enabled = true; options.post.grain = float(number(i)); }
        else if(a == "--post"){ options.post.enabled = true; }
        else if(a == "--motion-blur"){ options.motionBlur = true; options.shutter = float(number(i)); }
        else if(a == "--koraci") options.motionSteps = uint32_t(std::max(2.0, number(i)));
        else if(a == "--prag-suma") options.noiseThreshold = float(std::max(0.0, number(i)));
        else if(a == "--kaustike") options.caustics = true;
        else if(a == "--ostrina"){ options.depthOfField = true; options.fStop = float(number(i)); options.focusDistance = float(number(i)); }
        else if(a == "--senzor") options.sensorWidth = float(number(i));
        else if(a == "--holdout") options.splatHoldout = true;
        else if(a == "--filtar" && i + 1 < argc){
            const std::string which = argv[++i];
            options.denoiser = which == "atrous" ? Tracer::Denoiser::ATrous : Tracer::Denoiser::Auto;
        }
        else{ std::fprintf(stderr, "Nepoznata zastavica: %s\n", a.c_str()); usage(); return 1; }
    }
    Warp::Stage stage;
    std::string error;
    if(!Warp::loadProject(projectPath, stage, error)){
        std::fprintf(stderr, "Ne mogu otvoriti %s: %s\n", projectPath.c_str(), error.c_str());
        return 1;
    }
    if(!cameraPath.empty()){
        options.camera = stage.find(cameraPath);
        if(options.camera == Warp::None || !stage.get(options.camera)->camera){
            std::fprintf(stderr, "Nema kamere %s u projektu\n", cameraPath.c_str());
            return 1;
        }
    }
    if(haveRange){
        options.sequence = true;
        if(options.lastFrame < options.firstFrame) std::swap(options.firstFrame, options.lastFrame);
    }else{
        options.firstFrame = single >= 0.0 ? single : stage.startFrame;
    }
    std::string folder = options.outputFolder;
    if(folder.empty()) folder = (fs::path(projectPath).parent_path() / "render").string();

    //Kartica bez prozora. Ako Vulkana nema (ili nema uredjaja), render ide na procesor
    std::unique_ptr<LoomInitializer> loom;
    std::unique_ptr<Loom::GpuRenderDriver> driver;
    if(options.gpu){
        try{
            LoomConfig config;
            config.width = 64;
            config.height = 64;
            config.headless = true;
            config.maxDescriptorSets = 256;        //tracer: trace, resolve i filtar/post na kartici
            config.appName = "loom-render";
            config.engineName = "Loom";
            loom = std::make_unique<LoomInitializer>(config);
            driver = std::make_unique<Loom::GpuRenderDriver>(*loom, 0.25);
            driver->previewMilliseconds = 1000000;          //bez prozora nema komu pokazati
        }catch(const std::exception& failure){
            std::printf("Nema Vulkana (%s) - render na procesoru\n", failure.what());
            driver.reset();
            loom.reset();
        }
    }

    Loom::RenderSession session;
    session.attachGpu(driver != nullptr);
    if(driver) std::printf("Render na kartici (Vulkan compute)\n");
    session.start(stage, options, folder);
    size_t printed = 0;
    std::string lastStatus;
    auto lastPrint = std::chrono::steady_clock::now();
    while(true){
        if(driver){
            driver->beforeFrame(session);
            if(driver->busy() && loom->renderer.beginFrame()){
                driver->inFrame(session);
                loom->renderer.endFrame();
            }else if(!driver->busy()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }else std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const Loom::RenderSession::State state = session.snapshot();
        for(; printed < state.log.size(); ++printed) std::printf("%s\n", state.log[printed].c_str());
        const auto now = std::chrono::steady_clock::now();
        if(state.running && state.status != lastStatus && state.status.rfind("Rendering", 0) == 0 &&
           now - lastPrint > std::chrono::milliseconds(200)){
            std::printf("\r%s   ", state.status.c_str());
            std::fflush(stdout);
            lastStatus = state.status;
            lastPrint = now;
        }
        if(!state.running){
            std::printf("\n");
            session.join();
            if(loom) loom->waitIdle();
            return state.finished ? 0 : 1;
        }
    }
}
