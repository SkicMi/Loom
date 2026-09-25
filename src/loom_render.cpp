// loom-render: render projekta iz kamere, bez prozora.
//
//   loom-render projekt.usda [--kamera /Solve/Kamera] [--kadar 42 | --od 1 --do 120]
//               [--uzorci 256] [--odbijanja 12] [--rezolucija 0.5] [--izlaz mapa] [--ime render]
//               [--bez-ploce] [--prozirno] [--bez-catchera] [--bez-neba]
//               [--dubina | --bez-dubine] [--normale] [--albedo] [--bez-filtra]
//               [--sunce elevacija azimut jakost] [--velicina-sunca 0.53] [--zamucenost 3] [--nebo 0.35]
//               [--hdri nebo.hdr] [--hdri-jakost 1] [--hdri-rotacija 0] [--jednoliko]
//               [--agx] [--ekspozicija 0] [--bez-exr] [--bez-png] [--dretve N]
//
// Isti most (LoomRender.h) i isti tracer kao gumb Render u editoru - pa se render provjerava i na
// stroju bez kartice i bez zaslona, i pokrece u noci za cijelu sekvencu. Na kraju ispise sto je
// zapisao; greska je izlazni kod 1 s razlogom, nikad tiho prazna slika.
#include "LoomRender.h"

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
        "  --agx --ekspozicija EV --bez-exr --bez-png --dretve N\n");
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
        else if(a == "--agx") options.view = Tracer::ViewTransform::AgX;
        else if(a == "--ekspozicija") options.exposure = float(number(i));
        else if(a == "--bez-exr") options.writeExr = false;
        else if(a == "--bez-png") options.writePng = false;
        else if(a == "--dretve") options.threads = uint32_t(std::max(0.0, number(i)));
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

    Loom::RenderSession session;
    session.start(stage, options, folder);
    size_t printed = 0;
    std::string lastStatus;
    while(true){
        const Loom::RenderSession::State state = session.snapshot();
        for(; printed < state.log.size(); ++printed) std::printf("%s\n", state.log[printed].c_str());
        if(state.running && state.status != lastStatus && state.status.rfind("Rendering", 0) == 0){
            std::printf("\r%s   ", state.status.c_str());
            std::fflush(stdout);
            lastStatus = state.status;
        }
        if(!state.running){
            std::printf("\n");
            session.join();
            return state.finished ? 0 : 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
