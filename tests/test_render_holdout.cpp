// Holdout iz splata: stvarna scena (gaussian splat) zaklanja CG koji stoji iza nje.
//
//   ZID        zid gaussiana na z = -3 pokriva lijevu polovicu kadra. CG kutija na z = -6 preko
//              sredine: lijevi dio (iza zida) nestane - alfa 0, snimka se vidi - desni ostane
//   ISPRED     mala kutija na z = -2 ispred zida ostane cijela
//   KONTROLA   bez holdouta cijela velika kutija je vidljiva (inace test ne mjeri nista)
//   KARTICA    isti holdout na Vulkanu
//   BRZINA     milijun gaussiana u dubinu 1920x1080
#include "TestHarness.h"

#include "LoomRender.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <TracerGpu/GpuTracer.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>

namespace{

constexpr uint32_t Width = 64, Height = 32;

Warp::Stage scene(const std::string& splatPath){
    Warp::Stage stage;
    const Warp::Id camera = stage.create("Kamera");
    Warp::Camera lens;
    lens.width = Width; lens.height = Height; lens.focalPixels = 40.0f; lens.centreX = 32.0f; lens.centreY = 16.0f;
    stage.get(camera)->camera = lens;
    const Warp::Id far = stage.create("Daleko");
    stage.get(far)->mesh = Warp::Mesh{Warp::Shape::Cube, glm::vec3(0.8f)};
    stage.get(far)->local.translation = glm::vec3(0.0f, 0.0f, -6.0f);
    stage.get(far)->local.scale = glm::vec3(4.0f, 2.0f, 0.5f);
    const Warp::Id near = stage.create("Blizu");
    stage.get(near)->mesh = Warp::Mesh{Warp::Shape::Cube, glm::vec3(0.8f)};
    stage.get(near)->local.translation = glm::vec3(-1.0f, 0.0f, -2.0f);
    stage.get(near)->local.scale = glm::vec3(0.4f);
    const Warp::Id splat = stage.create("Splat");
    stage.get(splat)->splat = Warp::Splat{splatPath};
    return stage;
}

//Zid gaussiana x [-3, 0], y [-2, 2] na z = -3, razmak i sigma 0.03, neprozirni
void writeWall(const std::string& path){
    Spool::GaussianCloud cloud;
    for(float y = -2.0f; y <= 2.0f; y += 0.03f) for(float x = -3.0f; x <= 0.0f; x += 0.03f){
        Spool::Gaussian g;
        g.position[0] = x; g.position[1] = y; g.position[2] = -3.0f;
        g.opacity = 4.0f;
        g.scale[0] = g.scale[1] = g.scale[2] = std::log(0.03f);
        cloud.gaussians.push_back(g);
    }
    Spool::saveGaussianPly(path, cloud);
}

//Srednja pokrivenost CG-a u pravokutniku stupaca [x0, x1) i redaka [y0, y1)
float coverage(const Tracer::Frame& f, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1){
    double sum = 0.0;
    for(uint32_t y = y0; y < y1; ++y) for(uint32_t x = x0; x < x1; ++x) sum += f.cg[(size_t(y) * f.width + x) * 4 + 3];
    return float(sum / double((x1 - x0) * (y1 - y0)));
}

}

int main(){
    TestReport report("R4 holdout iz splata");
    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_render_holdout";
    std::filesystem::create_directories(work);
    const std::string wall = (work / "zid.ply").string();
    writeWall(wall);
    const Warp::Stage stage = scene(wall);

    Loom::RenderOptions options;
    options.plate = false;
    options.transparent = true;
    options.splatHoldout = true;
    Loom::RenderAssets assets;
    Loom::BuiltScene built, control;
    std::string error;
    const bool ok = Loom::buildTracerScene(stage, 1.0, options, assets, built, error);
    options.splatHoldout = false;
    Loom::buildTracerScene(stage, 1.0, options, assets, control, error);
    report.check("dubina splata", ok && built.holdoutSplats == 1 && built.scene.holdout.valid() &&
                 std::abs(built.scene.holdout.fetch(10, 16).r - 3.0f) < 0.01f && built.scene.holdout.fetch(50, 16).r > 1e9f,
                 fmt("%s; lijevo %.3f (zid na 3), desno %.1e", error.c_str(), built.scene.holdout.fetch(10, 16).r,
                     built.scene.holdout.fetch(50, 16).r));

    Tracer::RenderSettings settings;
    settings.samples = 32;
    settings.maxBounces = 2;
    auto render = [&](Tracer::Scene s){
        Tracer::Renderer renderer(std::move(s));
        renderer.render(settings);
        return renderer.frame(false);
    };
    const Tracer::Frame held = render(built.scene), open = render(control.scene);
    //Velika kutija: stupci 19..45, redci 10..22; mala: stupci 8..16, redci 12..20
    const float behind = coverage(held, 21, 30, 12, 20), beside = coverage(held, 35, 44, 12, 20);
    const float inFront = coverage(held, 9, 15, 13, 19), behindOpen = coverage(open, 21, 30, 12, 20);
    report.check("iza zida nestane", behind < 0.01f, fmt("pokrivenost %.3f", behind));
    report.check("pokraj zida ostane", beside > 0.99f, fmt("pokrivenost %.3f", beside));
    report.check("ispred zida ostane", inFront > 0.99f, fmt("pokrivenost %.3f", inFront));
    report.check("kontrola bez holdouta", behindOpen > 0.99f, fmt("pokrivenost %.3f", behindOpen));

    {
        LoomConfig config;
        config.width = 64;
        config.height = 64;
        config.headless = true;
        config.appName = "test_render_holdout";
        config.engineName = "Loom tests";
        LoomInitializer loom(config);
        TracerGpu::Pipelines pipelines(loom);
        TracerGpu::GpuTracer tracer(loom, pipelines, Tracer::compile(built.scene), settings);
        tracer.renderAll();
        const Tracer::Frame card = tracer.readFrame(false);
        const float b = coverage(card, 21, 30, 12, 20), s = coverage(card, 35, 44, 12, 20), f = coverage(card, 9, 15, 13, 19);
        report.check("kartica", b < 0.01f && s > 0.99f && f > 0.99f, fmt("iza %.3f, pokraj %.3f, ispred %.3f", b, s, f));
        report.checkNoValidationMessages();
    }

    //Brzina: milijun gaussiana nasumice ispred kamere, 1080p
    {
        Loom::RenderAssets::SplatPoints points;
        std::mt19937 random(7);
        std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
        for(int i = 0; i < 1000000; ++i){
            const float z = -2.0f - 8.0f * (0.5f + 0.5f * unit(random));
            points.position.emplace_back(unit(random) * z * 0.9f, unit(random) * z * 0.5f, z);
            points.alpha.push_back(0.9f);
            points.sigma.push_back(0.01f);
        }
        Tracer::Camera camera;
        camera.width = 1920; camera.height = 1080; camera.focalPixels = 1500.0f; camera.centre = glm::vec2(960.0f, 540.0f);
        std::vector<float> depth;
        const auto start = std::chrono::steady_clock::now();
        Loom::splatDepth(points, glm::mat4(1.0f), camera, glm::mat4(1.0f), depth);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        size_t covered = 0;
        for(float d : depth) covered += d < 1e9f;
        report.check("brzina", seconds < 2.0 && covered > depth.size() / 4,
                     fmt("%.3f s za 1 M gaussiana u 1920x1080, pokriveno %.0f %%", seconds, 100.0 * double(covered) / double(depth.size())));
    }
    std::filesystem::remove_all(work);
    return report.result();
}
