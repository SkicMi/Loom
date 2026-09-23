// Splat u pogledu editora: pada li gaussian na piksel na koji pogled projicira njegovo srediste.
//
// ZASTO SE OVO TESTIRA. Rasterizator splatova i pogled editora projiciraju svaki na svoj nacin:
// rasterizator Loomovom Vulkan konvencijom (fy negativan), pogled solverovom (y = cy - f*y/z).
// Prva verzija ih je spojila s pozitivnim fy i splat je kroz rijesenu kameru ispao NAOPAKO -
// polica s vrha snimke na dnu - a nista nije puklo. Zid u sredini kadra je izgledao tocno.
//
// Zato jedan gaussian stoji IZNAD I LIJEVO od sredine (zrcaljenje po bilo kojoj osi ga odnese
// na drugu stranu), pod grupom sa zakretom, pomakom i mjerilom (splat je u koordinatama solvea,
// grupa nosi uspravnost), a glavna tocka nije u sredini slike.
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include "../src/LoomSplat.h"
#include "../src/LoomViewport.h"

#include <Spool/GaussianPly.h>

#include <cmath>
#include <filesystem>
#include <thread>

int main(){
    TestReport report("E2 splat u pogledu");

    LoomConfig config;
    config.width = 640; config.height = 480;
    config.appName = "splat u pogledu"; config.engineName = "Loom tests";
    config.headless = true;
    config.maxDescriptorSets = 256;
    LoomInitializer loom(config);

    //Jedan mali, jarki, neprozirni gaussian u koordinatama solvea
    const glm::vec3 local(-0.35f, 0.3f, 0.2f);
    Spool::GaussianCloud cloud;
    Spool::Gaussian g;
    g.position[0] = local.x; g.position[1] = local.y; g.position[2] = local.z;
    g.dc[0] = g.dc[1] = g.dc[2] = 2.0f;
    g.opacity = 6.0f;                                   //logit: ~0.998
    g.scale[0] = g.scale[1] = g.scale[2] = std::log(0.01f);
    cloud.gaussians.push_back(g);
    const std::string path = (std::filesystem::temp_directory_path() / "loom_jedan_splat.ply").string();
    Spool::saveGaussianPly(path, cloud);

    Warp::Stage stage;
    const Warp::Id group = stage.create("Solve");
    stage.get(group)->local.rotation = glm::angleAxis(0.5f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)));
    stage.get(group)->local.translation = glm::vec3(0.4f, -0.2f, 0.3f);
    stage.get(group)->local.scale = glm::vec3(1.7f);
    const Warp::Id splat = stage.create("Splat", group);
    stage.get(splat)->splat = Warp::Splat{path};

    //Kamera pogleda gleda sredinu grupe; pravokutnik pogleda nije u kutu prozora, kao u editoru
    Loom::ViewportState state;
    state.orbit.target = glm::vec3(stage.worldMatrix(group, 1.0)[3]);
    state.orbit.distance = 4.0f;
    state.orbit.yaw = 0.3f; state.orbit.pitch = 0.2f;
    const Treadle::Rect area{40.0f, 30.0f, 400.0f, 300.0f};
    const Loom::ViewCamera camera = Loom::viewCameraFor(stage, 1.0, area, state);

    Loom::ViewportSplat viewportSplat(loom);
    viewportSplat.want(path);
    for(int i = 0; i < 500 && viewportSplat.isLoading(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));

    const glm::mat4 world = stage.worldMatrix(splat, 1.0);
    const glm::vec3 eyeLocal = glm::vec3(glm::inverse(world) * glm::vec4(camera.eye, 1.0f));
    const bool ready = viewportSplat.prepare(camera.view * world, eyeLocal, camera.focal, camera.centre, camera.rect, 1.0f);
    if(ready){
        loom.renderer.beginFrame();
        viewportSplat.compute();
        loom.renderer.endFrame();
    }
    const ImageData image = viewportSplat.readPixels();

    //Najsvjetliji piksel = srediste gaussiana, u pikselima PROZORA
    glm::vec2 brightest(-1.0f);
    float best = 0.0f;
    const float* pixels = reinterpret_cast<const float*>(image.pixels.data());
    for(uint32_t y = 0; ready && y < image.extent.height; ++y){
        for(uint32_t x = 0; x < image.extent.width; ++x){
            const float alpha = pixels[(size_t(y) * image.extent.width + x) * 4 + 3];
            if(alpha > best){ best = alpha; brightest = glm::vec2(float(x) + 0.5f + area.x, float(y) + 0.5f + area.y); }
        }
    }
    glm::vec2 expected;
    Loom::project(camera, glm::vec3(world * glm::vec4(local, 1.0f)), expected);

    report.check("splat se procita i nacrta", ready && best > 0.9f && viewportSplat.count() == 1,
        fmt("%zu gaussiana, najveca pokrivenost %.3f, slika %ux%u", viewportSplat.count(), best,
            image.extent.width, image.extent.height));
    report.check("gaussian pada na piksel pogleda (gore-lijevo, kroz grupu s mjerilom)",
        glm::length(brightest - expected) < 1.5f && expected.y < camera.centre.y,
        fmt("nacrtan na (%.1f, %.1f), pogled kaze (%.1f, %.1f); naopako bi bio na y %.1f",
            brightest.x, brightest.y, expected.x, expected.y, 2.0f * camera.centre.y - expected.y));

    std::filesystem::remove(path);
    report.checkNoValidationMessages();
    return report.result();
}
