// Render iz kamere, cijelim putem: Warp scena s rijesenom kamerom i snimkom -> LoomRender.h ->
// LoomTracer -> EXR i PNG na disku -> procitano natrag.
//
// STO SE OVDJE MOZE POKVARITI, A TRACER TEST TO NE BI VIDIO:
//
//   - kamera: poza iz kljuceva u tom kadru, zarisna i glavna tocka u pikselima snimke, mjerilo
//     rezolucije. Kocka mora pasti na isti piksel na koji je pogled editora stavlja
//   - snimka: kadar snimke koji stoji iza kadra timelinea (plateFirstFrame + kadar - 1)
//   - shadow catcher: ravnina hvata sjenu i NE mijenja snimku ondje gdje sjene nema - VFX
//     zahtjev, ploca se ne smije ni posvijetliti ni potamnjeti
//   - zapis: EXR s imenovanim slojevima (R G B A, cg.*, shadow.*, Z), PNG kako se vidi
//
// Snimka je napravljena ovdje, bez gubitka (ffv1 u RGB-u), pa se zna tocno koji piksel u njoj stoji.
#include "TestHarness.h"

#include "LoomRender.h"
#include "LoomRenderGpu.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "LoomViewport.h"

#include <Spool/Sequence.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace{

const uint32_t PlateWidth = 160, PlateHeight = 90;

Spool::Image plateFrame(uint32_t index){
    std::vector<uint8_t> pixels(size_t(PlateWidth) * PlateHeight * 4);
    for(uint32_t y = 0; y < PlateHeight; ++y) for(uint32_t x = 0; x < PlateWidth; ++x){
        uint8_t* p = pixels.data() + (size_t(y) * PlateWidth + x) * 4;
        p[0] = uint8_t(40 + x);
        p[1] = uint8_t(30 + 2 * y);
        p[2] = uint8_t(60 + 40 * index);
        p[3] = 255;
    }
    return Spool::imageFromPixels(pixels.data(), PlateWidth, PlateHeight);
}

glm::quat lookRotation(const glm::vec3& eye, const glm::vec3& target){
    return glm::quat_cast(glm::inverse(glm::lookAt(eye, target, glm::vec3(0, 1, 0))));
}

}

int main(){
    TestReport report("R1 render iz kamere");
    namespace fs = std::filesystem;
    if(std::system("ffmpeg -version > /dev/null 2>&1") != 0){
        report.check("ffmpeg", false, "nije na putanji - snimka iza scene se ne moze napraviti");
        return report.result();
    }
    const fs::path work = fs::temp_directory_path() / "loom_render_bridge";
    fs::remove_all(work);
    fs::create_directories(work);

    //-- snimka: tri kadra, bez gubitka -------------------------------------------------------------
    {
        Spool::SequenceConfig sequence;
        sequence.directory = (work / "src").string();
        Spool::SequenceWriter writer(sequence);
        for(uint32_t i = 0; i < 3; ++i) writer.write(plateFrame(i));
    }
    const std::string video = (work / "plate.mkv").string();
    const int encoded = std::system(("ffmpeg -y -framerate 24 -i " + (work / "src" / "frame_%04d.png").string() +
                                     " -c:v ffv1 -pix_fmt gbrp " + video + " > /dev/null 2>&1").c_str());
    report.check("snimka", encoded == 0 && fs::exists(video), video);

    //-- scena: rijesena kamera koja se krece, pod i kocka --------------------------------------------
    Warp::Stage stage;
    stage.startFrame = 1.0;
    stage.endFrame = 3.0;
    const Warp::Id cameraId = stage.create("Kamera");
    {
        Warp::Entity& c = *stage.get(cameraId);
        Warp::Camera lens;
        lens.focalPixels = 150.0f;
        lens.centreX = 82.3f;
        lens.centreY = 44.1f;
        lens.width = PlateWidth;
        lens.height = PlateHeight;
        lens.plate = video;
        lens.plateFirstFrame = 0;
        c.camera = lens;
        for(int k = 0; k < 3; ++k){
            const glm::vec3 eye(0.4f * float(k) - 0.4f, 1.6f, 5.0f);
            c.translationKeys.set(double(k + 1), eye);
            c.rotationKeys.set(double(k + 1), lookRotation(eye, glm::vec3(0.0f, 0.4f, 0.0f)));
        }
    }
    const Warp::Id floorId = stage.create("Pod");
    stage.get(floorId)->mesh = Warp::Mesh{Warp::Shape::Plane};
    stage.get(floorId)->local.scale = glm::vec3(30.0f);
    const Warp::Id cubeId = stage.create("Kocka");
    stage.get(cubeId)->mesh = Warp::Mesh{Warp::Shape::Cube, glm::vec3(0.8f, 0.3f, 0.1f)};
    stage.get(cubeId)->local.translation = glm::vec3(0.0f, 0.5f, 0.0f);
    report.check("pod je stvarna scena", Loom::isRealSceneGeometry(*stage.get(floorId)) && !Loom::isRealSceneGeometry(*stage.get(cubeId)),
                 "ravnina hvata sjenu, kocka je CG");

    Loom::RenderOptions options;
    options.camera = cameraId;
    options.firstFrame = options.lastFrame = 2.0;
    options.samples = 32;
    options.denoise = false;
    options.sunElevation = 45.0f;
    options.sunAzimuth = 250.0f;
    options.normal = true;
    options.albedo = true;

    //-- 1. scena u kadru 2 ---------------------------------------------------------------------------
    Loom::RenderAssets assets;
    Loom::BuiltScene built;
    std::string error;
    const bool ok = Loom::buildTracerScene(stage, 2.0, options, assets, built, error);
    report.check("scena", ok && built.plateLoaded && built.catchers == 1 && built.objects == 2 &&
                          built.scene.camera.width == PlateWidth && built.scene.camera.height == PlateHeight,
                 fmt("%s ploca %d, %zu objekata, %zu catcher, %zu trokuta", error.c_str(), int(built.plateLoaded),
                     built.objects, built.catchers, built.scene.triangles.size()));
    //Kadar 2 timelinea je kadar 1 snimke (od nule): plavi kanal 60 + 40
    const uint8_t plateBlue = built.scene.backplate.bytes.size() > 10 ? built.scene.backplate.bytes[2] : 0;
    report.check("kadar snimke", plateBlue == 100, fmt("plavo %u (ocekivano 100)", plateBlue));

    //-- 2. kamera: isti piksel kao pogled editora ------------------------------------------------------
    {
        Loom::ViewportState state;
        state.lookThrough = cameraId;
        const Loom::ViewCamera editor = Loom::viewCameraFor(stage, 2.0, Treadle::Rect{0.0f, 0.0f, float(PlateWidth), float(PlateHeight)}, state);
        float worst = 0.0f;
        for(const glm::vec3& p : {glm::vec3(0.5f, 1.0f, 0.5f), glm::vec3(-0.5f, 0.0f, 0.5f), glm::vec3(2.0f, 0.0f, -3.0f)}){
            glm::vec2 a, b;
            Loom::project(editor, p, a);
            built.scene.camera.project(p, b);
            worst = std::max(worst, glm::length(a - b));
        }
        report.check("kamera = pogled", worst < 1e-3f, fmt("najveca razlika %.2e px", double(worst)));
    }

    //-- 3. render i zapis ----------------------------------------------------------------------------
    const fs::path out = work / "render";
    Tracer::Renderer renderer(std::move(built.scene));
    Tracer::RenderSettings settings;
    settings.samples = options.samples;
    renderer.render(settings);
    const Tracer::Frame frame = renderer.frame(false);
    const std::vector<std::string> files = Loom::writeRender(frame, renderer.scene(), options, true, true, out.string(),
                                                             Loom::frameStem(options, 2.0), error);
    report.check("zapis", error.empty() && fs::exists(out / "render.exr") && fs::exists(out / "render.png") &&
                          fs::exists(out / "render_depth.png") && fs::exists(out / "render_normal.png"),
                 fmt("%zu datoteka %s", files.size(), error.c_str()));

    //-- 4. EXR natrag: snimka netaknuta izvan sjene, sjena postoji, dubina tocna -----------------------
    {
        const Spool::ExrImage exr = Spool::loadExr((out / "render.exr").string());
        const Spool::ExrChannel* r = exr.find("R");
        const Spool::ExrChannel* a = exr.find("cg.A");
        const Spool::ExrChannel* s = exr.find("shadow.G");
        const Spool::ExrChannel* z = exr.find("Z");
        const bool layers = r && a && s && z && exr.find("N.X") && exr.find("albedo.R") && exr.width == PlateWidth;
        report.check("slojevi", layers, fmt("%zu kanala", exr.channels.size()));
        if(layers){
            const Spool::Image plate = plateFrame(1);
            size_t untouched = 0, shadowed = 0, covered = 0;
            float worst = 0.0f;
            for(size_t i = 0; i < size_t(PlateWidth) * PlateHeight; ++i){
                if(a->values[i] >= 1.0f) ++covered;
                if(s->values[i] < 0.5f) ++shadowed;
                if(a->values[i] != 0.0f || s->values[i] != 1.0f) continue;
                const float expected = Tracer::srgbToLinear(float(plate.pixels[i * 4]) / 255.0f);
                worst = std::max(worst, std::abs(r->values[i] - expected) / expected);
                ++untouched;
            }
            report.check("snimka netaknuta", untouched > 5000 && worst < 1e-3f,
                         fmt("%zu piksela bez CG-a i sjene, najveca rel. razlika %.1e (half)", untouched, double(worst)));
            report.check("sjena na snimci", shadowed > 30 && covered > 300, fmt("%zu piksela u sjeni, %zu pokriva kocka", shadowed, covered));

            //Dubina u tocki gornje plohe kocke: udaljenost duz osi kamere
            const glm::vec3 top(0.0f, 1.0f, 0.2f);
            glm::vec2 pixel;
            renderer.scene().camera.project(top, pixel);
            const glm::vec3 local = glm::vec3(glm::inverse(renderer.scene().camera.cameraToWorld) * glm::vec4(top, 1.0f));
            const float measured = z->values[size_t(pixel.y) * PlateWidth + size_t(pixel.x)];
            report.check("dubina", std::abs(measured - (-local.z)) < 0.03f * -local.z, fmt("%.4f, ocekivano ~%.4f", measured, -local.z));
        }
    }

    //-- 5. PNG: izvan sjene i CG-a je bajt snimke ---------------------------------------------------------
    {
        const Spool::Image png = Spool::loadImage((out / "render.png").string());
        const Spool::ExrImage exr = Spool::loadExr((out / "render.exr").string());
        const Spool::ExrChannel* a = exr.find("cg.A");
        const Spool::ExrChannel* s = exr.find("shadow.G");
        const Spool::Image plate = plateFrame(1);
        int worst = 0;
        for(size_t i = 0; a && s && i < size_t(PlateWidth) * PlateHeight; ++i){
            if(a->values[i] != 0.0f || s->values[i] != 1.0f) continue;
            for(int k = 0; k < 3; ++k) worst = std::max(worst, std::abs(int(png.pixels[i * 4 + size_t(k)]) - int(plate.pixels[i * 4 + size_t(k)])));
        }
        report.check("png = snimka", png.width == PlateWidth && worst <= 1, fmt("najveca razlika %d", worst));
    }

    //-- 6. sekvenca: imena kadrova i cijela sesija u pozadini ----------------------------------------------
    {
        Loom::RenderOptions sequence = options;
        sequence.sequence = true;
        sequence.firstFrame = 1.0;
        sequence.lastFrame = 3.0;
        sequence.samples = 4;
        sequence.resolutionScale = 0.5f;
        sequence.writeExr = false;
        sequence.depth = sequence.normal = sequence.albedo = false;
        Loom::RenderSession session;
        session.start(stage, sequence, (work / "sequence").string());
        while(session.snapshot().running) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        session.join();
        const Loom::RenderSession::State state = session.snapshot();
        const bool named = fs::exists(work / "sequence" / "render_0001.png") && fs::exists(work / "sequence" / "render_0003.png");
        const Spool::Image half = named ? Spool::loadImage((work / "sequence" / "render_0002.png").string()) : Spool::Image{};
        report.check("sekvenca", state.finished && named && half.width == PlateWidth / 2,
                     fmt("%s, %zu poruka, sirina %u", state.status.c_str(), state.log.size(), half.width));
    }

    //-- 7. isti render kroz GPU pogon (kartica bez prozora): posao iz sesije, isti zapis --------------
    {
        LoomConfig config;
        config.width = 64;
        config.height = 64;
        config.headless = true;
        config.appName = "test_render_bridge";
        config.engineName = "Loom tests";
        LoomInitializer loom(config);
        Loom::GpuRenderDriver driver(loom, 0.05);
        Loom::RenderSession session;
        session.attachGpu(true);
        Loom::RenderOptions gpuOptions = options;
        gpuOptions.samples = 32;
        gpuOptions.gpu = true;
        session.start(stage, gpuOptions, (work / "gpu").string());
        bool usedGpu = false;
        while(session.snapshot().running){
            driver.beforeFrame(session);
            usedGpu = usedGpu || driver.busy();
            if(driver.busy() && loom.renderer.beginFrame()){
                driver.inFrame(session);
                loom.renderer.endFrame();
            }else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        session.join();
        loom.waitIdle();
        const Loom::RenderSession::State state = session.snapshot();
        bool gpuLog = false;
        for(const std::string& line : state.log) gpuLog = gpuLog || line.find("GPU render failed") != std::string::npos;
        const bool written = fs::exists(work / "gpu" / "render.png") && fs::exists(work / "gpu" / "render.exr");
        report.check("gpu: sesija", state.finished && usedGpu && !gpuLog && written, state.status);
        if(written){
            const Spool::ExrImage exr = Spool::loadExr((work / "gpu" / "render.exr").string());
            const Spool::ExrChannel* r = exr.find("R");
            const Spool::ExrChannel* a = exr.find("cg.A");
            const Spool::ExrChannel* sh = exr.find("shadow.G");
            const Spool::Image plate = plateFrame(1);
            size_t untouched = 0, shadowed = 0;
            float worst = 0.0f;
            for(size_t i = 0; r && a && sh && i < size_t(PlateWidth) * PlateHeight; ++i){
                if(sh->values[i] < 0.5f) ++shadowed;
                if(a->values[i] != 0.0f || sh->values[i] != 1.0f) continue;
                const float expected = Tracer::srgbToLinear(float(plate.pixels[i * 4]) / 255.0f);
                worst = std::max(worst, std::abs(r->values[i] - expected) / expected);
                ++untouched;
            }
            report.check("gpu: snimka netaknuta", untouched > 5000 && worst < 1e-3f && shadowed > 30,
                         fmt("%zu piksela bez CG-a i sjene (rel. %.1e), %zu u sjeni", untouched, double(worst), shadowed));
        }
    }

    fs::remove_all(work);
    return report.result();
}
