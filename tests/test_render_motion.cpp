// Motion blur kroz RenderSession: scena se gradi u vise trenutaka unutar otvora zatvaraca.
//
// Provjere su brojevi koji se znaju bez rendera:
//   RAZMAZ     kocka ide 1 jedinicu po kadru; zatvarac 0.5 kadra => rub razmazan preko 0.5 jedinice,
//              tj. zbroj pokrivenosti retka ostaje sirina kocke (povrsina se cuva), a pokriveni dio
//              naraste za 0.5 jedinice u pikselima
//   RAMPA      pola razmaza lijevo, pola desno od sredine: pokrivenost linearno pada, u sredini rampe 0.5
//   KARTICA    isti razmaz kroz GPU pogon (odsjeci su poslovi kartici jedan za drugim)
//   MIRNO      scena bez kretanja s motion blurom = ista slika kao bez njega (u granici suma)
#include "TestHarness.h"

#include "LoomRender.h"
#include "LoomRenderGpu.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <glm/gtc/constants.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

namespace{

constexpr uint32_t Width = 160, Height = 48;
constexpr float Focal = 288.0f;                 //kamera 4.5 iznad vrha kocke: 64 piksela po jedinici

Warp::Stage movingCube(bool moving){
    Warp::Stage stage;
    const Warp::Id camera = stage.create("Kamera");
    Warp::Camera lens;
    lens.width = Width; lens.height = Height; lens.focalPixels = Focal;
    lens.centreX = Width * 0.5f; lens.centreY = Height * 0.5f;
    stage.get(camera)->camera = lens;
    stage.get(camera)->local.translation = glm::vec3(0.0f, 5.0f, 0.0f);
    stage.get(camera)->local.rotation = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1, 0, 0));
    const Warp::Id cube = stage.create("Kocka");
    stage.get(cube)->mesh = Warp::Mesh{Warp::Shape::Cube, glm::vec3(0.8f, 0.3f, 0.1f)};
    //Plitka kocka (visina 0.02): bocne plohe se ne vide, rub je samo gornja ploha
    stage.get(cube)->local.scale = glm::vec3(1.0f, 0.02f, 1.0f);
    stage.get(cube)->local.translation = glm::vec3(0.0f, 0.49f, 0.0f);
    if(moving){
        stage.get(cube)->translationKeys.set(1.0, glm::vec3(-1.0f, 0.49f, 0.0f));
        stage.get(cube)->translationKeys.set(3.0, glm::vec3(1.0f, 0.49f, 0.0f));
    }
    return stage;
}

Tracer::Frame render(const Warp::Stage& stage, bool blur, const std::string& folder, std::string& status,
                     LoomInitializer* gpu = nullptr){
    Loom::RenderOptions options;
    options.firstFrame = options.lastFrame = 2.0;
    options.plate = false;
    options.sky = Loom::RenderOptions::Sky::Uniform;
    options.samples = 64;
    options.denoise = false;
    options.writeExr = false;
    options.writePng = false;
    options.depth = false;
    options.gpu = gpu != nullptr;
    options.motionBlur = blur;
    options.shutter = 0.5f;
    options.motionSteps = 16;
    Loom::RenderSession session;
    session.attachGpu(gpu != nullptr);
    session.start(stage, options, folder);
    if(gpu){
        Loom::GpuRenderDriver driver(*gpu, 0.05);
        while(session.snapshot().running){
            driver.beforeFrame(session);
            if(driver.busy() && gpu->renderer.beginFrame()){
                driver.inFrame(session);
                gpu->renderer.endFrame();
            }else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        gpu->waitIdle();
    }else while(session.snapshot().running) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    session.join();
    for(const std::string& line : session.snapshot().log) if(line.find("GPU render failed") != std::string::npos) status = line;
    if(status.find("GPU render failed") != std::string::npos) return session.lastFrame();
    status = session.snapshot().status;
    return session.lastFrame();
}

//Pokrivenost (alfa CG-a) po stupcu, prosjek preko srednjih redaka
std::vector<float> coverage(const Tracer::Frame& f){
    std::vector<float> column(Width, 0.0f);
    if(f.width != Width) return column;
    for(uint32_t y = Height / 2 - 8; y < Height / 2 + 8; ++y)
        for(uint32_t x = 0; x < Width; ++x) column[x] += f.cg[(size_t(y) * Width + x) * 4 + 3] / 16.0f;
    return column;
}

}

int main(){
    TestReport report("R3 motion blur");
    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_render_motion";
    const float perUnit = Focal / 4.5f;

    std::string status;
    const Tracer::Frame sharp = render(movingCube(true), false, (work / "a").string(), status);
    const Tracer::Frame blurred = render(movingCube(true), true, (work / "b").string(), status);
    const std::vector<float> a = coverage(sharp), b = coverage(blurred);
    float areaA = 0.0f, areaB = 0.0f;
    int coveredA = 0, coveredB = 0, rampB = 0;
    for(uint32_t x = 0; x < Width; ++x){
        areaA += a[x]; areaB += b[x];
        coveredA += a[x] > 0.02f; coveredB += b[x] > 0.02f;
        rampB += b[x] > 0.1f && b[x] < 0.9f;
    }
    report.check("kocka u kadru 2 stoji u sredini", sharp.width == Width && std::abs(areaA - perUnit) < 3.0f,
                 fmt("%s; povrsina %.1f px (ocekivano %.1f)", status.c_str(), areaA, perUnit));
    report.check("razmaz cuva povrsinu", std::abs(areaB - areaA) < 2.0f, fmt("%.1f px prema %.1f", areaB, areaA));
    //16 trenutaka na sredinama odsjecaka: krajnji su 15/16 otvora razmaknuti
    const float grow = float(coveredB - coveredA), expected = 0.5f * perUnit, span = expected * (1.0f - 1.0f / 16.0f);
    report.check("razmaz je pola jedinice", std::abs(grow - span) < 0.1f * span,
                 fmt("pokriveno %d -> %d px, naraslo %.0f (ocekivano %.0f)", coveredA, coveredB, grow, span));
    //Linearna rampa sirine 32 px na svakom rubu: 80 % izmedju 0.1 i 0.9 = 2 * 25.6 px
    const float rampExpected = 2.0f * 0.8f * expected;
    report.check("linearna rampa", std::abs(float(rampB) - rampExpected) < 0.15f * rampExpected,
                 fmt("%d px u rampi (ocekivano %.0f)", rampB, rampExpected));
    //Sredina rampe: rub kocke u trenutku kadra 2, x = 0.5 jedinice => pokrivenost 0.5
    const uint32_t edge = uint32_t(Width * 0.5f + 0.5f * perUnit);
    report.check("sredina rampe", std::abs(b[edge] - 0.5f) < 0.08f, fmt("%.3f na rubu", b[edge]));

    //Isto na kartici
    {
        LoomConfig config;
        config.width = 64;
        config.height = 64;
        config.headless = true;
        config.appName = "test_render_motion";
        config.engineName = "Loom tests";
        LoomInitializer loom(config);
        std::string gpuStatus;
        const std::vector<float> g = coverage(render(movingCube(true), true, (work / "g").string(), gpuStatus, &loom));
        float area = 0.0f, worst = 0.0f;
        for(uint32_t x = 0; x < Width; ++x){ area += g[x]; worst = std::max(worst, std::abs(g[x] - b[x])); }
        report.check("kartica: isti razmaz", std::abs(area - areaB) < 2.0f && worst < 0.1f,
                     fmt("%s; povrsina %.1f px, najveca razlika stupca %.3f", gpuStatus.c_str(), area, worst));
    }

    //Mirna scena: motion blur ne smije promijeniti sliku
    const Tracer::Frame still = render(movingCube(false), false, (work / "c").string(), status);
    const Tracer::Frame stillBlur = render(movingCube(false), true, (work / "d").string(), status);
    double diff = 0.0, mean = 0.0;
    const size_t n = still.pixelCount();
    for(size_t i = 0; i < n && stillBlur.pixelCount() == n; ++i){
        for(int k = 0; k < 3; ++k){
            diff += std::abs(double(still.cg[i * 4 + k]) - stillBlur.cg[i * 4 + k]);
            mean += still.cg[i * 4 + k];
        }
    }
    //Dva neovisna rendera od 64 uzorka razlikuju se samo sumom (~0.015 relativno)
    report.check("mirna scena ista", n > 0 && diff / std::max(mean, 1e-9) < 0.03,
                 fmt("relativna razlika %.4f", diff / std::max(mean, 1e-9)));
    std::filesystem::remove_all(work);
    return report.result();
}
