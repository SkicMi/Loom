// LoomTracer na kartici: isti analiticki odgovori kao test_tracer, i ista slika kao procesor.
//
// Kartica racuna u floatu, drugim redom zbrajanja i drugim sin/cos/pow - bit po bit ista slika se
// ne moze ocekivati. Ocekivanje mora biti isto: bijela pec 1, sunce a*E/pi, tockasto svjetlo
// I/(pi h^2), faktor oblika svijetlog kvadrata, projekcija na piksel pinhole formule. A slika
// iste scene s mnogo uzoraka mora pasti na procesorsku unutar suma (RMSE).
//
// NEGATIVNA KONTROLA: ista usporedba izmedju dva RAZLICITA materijala mora pasti - inace prag
// RMSE-a ne razlikuje nista.
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <TracerGpu/GpuTracer.h>
#include <Tracer/Renderer.h>
#include <Tracer/Denoise.h>
#include <Tracer/Post.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>

namespace{


Tracer::Camera lookAt(const glm::vec3& eye, const glm::vec3& target, uint32_t width, uint32_t height, float focal,
              glm::vec2 centre = glm::vec2(-1.0f)){
    Tracer::Camera c;
    const glm::vec3 up = std::abs(glm::normalize(target - eye).y) > 0.99f ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
    c.cameraToWorld = glm::inverse(glm::lookAt(eye, target, up));
    c.width = width;
    c.height = height;
    c.focalPixels = focal;
    c.centre = centre.x < 0.0f ? glm::vec2(width, height) * 0.5f : centre;
    return c;
}

glm::vec3 coveredAverage(const Tracer::Frame& f){
    glm::dvec3 sum(0.0);
    size_t count = 0;
    for(size_t i = 0; i < f.pixelCount(); ++i){
        if(f.cg[i * 4 + 3] < 0.999f) continue;
        sum += glm::dvec3(f.cg[i * 4], f.cg[i * 4 + 1], f.cg[i * 4 + 2]);
        ++count;
    }
    return count ? glm::vec3(sum / double(count)) : glm::vec3(-1.0f);
}

Tracer::Frame onCard(LoomInitializer& loom, TracerGpu::Pipelines& pipelines, Tracer::Scene scene, uint32_t samples, uint32_t bounces = 12,
             float clamp = 16.0f, double* seconds = nullptr, bool rayQuery = true){
    Tracer::RenderSettings settings;
    settings.samples = samples;
    settings.maxBounces = bounces;
    settings.indirectClamp = clamp;
    TracerGpu::GpuTracer tracer(loom, pipelines, Tracer::compile(std::move(scene)), settings, rayQuery);
    const auto start = std::chrono::steady_clock::now();
    tracer.renderAll();
    if(seconds) *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return tracer.readFrame(false);
}

Tracer::Frame onCpu(Tracer::Scene scene, uint32_t samples, uint32_t bounces = 12, float clamp = 16.0f){
    Tracer::Renderer renderer(std::move(scene));
    Tracer::RenderSettings settings;
    settings.samples = samples;
    settings.maxBounces = bounces;
    settings.indirectClamp = clamp;
    renderer.render(settings);
    return renderer.frame(false);
}

Tracer::Scene furnace(const Tracer::Material& material){
    Tracer::Scene scene;
    scene.environment.color = glm::vec3(1.0f);
    scene.addMesh(Tracer::uvSphere(1.0f, 96, 48), glm::mat4(1.0f), scene.addMaterial(material), "kugla");
    scene.camera = lookAt({0, 0, 4}, {0, 0, 0}, 16, 16, 80.0f);
    return scene;
}

double rmse(const Tracer::Frame& a, const Tracer::Frame& b){
    double sum = 0.0;
    for(size_t i = 0; i < a.cg.size(); ++i){ const double d = double(a.cg[i]) - double(b.cg[i]); sum += d * d; }
    return std::sqrt(sum / double(std::max<size_t>(1, a.cg.size())));
}

//Scena s puno toga odjednom: tekstura, alfa maska, nebo po vaznosti, sunce, staklo, metal, lak
Tracer::Scene mixed(float ballRoughness){
    Tracer::Scene scene;
    Tracer::Texture checker;
    checker.width = checker.height = 8;
    for(uint32_t y = 0; y < 8; ++y) for(uint32_t x = 0; x < 8; ++x){
        const uint8_t v = ((x + y) & 1) ? 220 : 60;
        checker.bytes.insert(checker.bytes.end(), {v, uint8_t(v / 2), 40, 255});
    }
    Tracer::Material floor; floor.baseColorTexture = int(scene.addTexture(checker)); floor.roughness = 0.7f;
    scene.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(8.0f)), scene.addMaterial(floor));
    Tracer::Material ball; ball.baseColor = glm::vec3(0.9f, 0.6f, 0.3f); ball.metallic = 1.0f; ball.roughness = ballRoughness;
    scene.addMesh(Tracer::uvSphere(0.5f, 48, 24), glm::translate(glm::mat4(1.0f), glm::vec3(-0.6f, 0.5f, 0.0f)), scene.addMaterial(ball));
    //Staklo 0.01 iznad poda: donja ploha na y = 0 bila bi koplanarna s podom, a koji od dva
    //trokuta na istom t pobijedi, stvar je obilaska (BVH i hardverske zrake biraju razlicito)
    Tracer::Material glass; glass.transmission = 1.0f; glass.roughness = 0.05f; glass.baseColor = glm::vec3(0.9f, 1.0f, 0.95f);
    scene.addMesh(Tracer::unitCube(), glm::translate(glm::mat4(1.0f), glm::vec3(0.7f, 0.41f, 0.3f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.8f)),
                  scene.addMaterial(glass));
    Tracer::Material coat; coat.baseColor = glm::vec3(0.1f, 0.2f, 0.7f); coat.clearcoat = 1.0f;
    scene.addMesh(Tracer::unitCube(), glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, 0.2f, -0.9f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.4f)),
                  scene.addMaterial(coat));
    scene.environment.map = Tracer::makeSky(glm::normalize(glm::vec3(0.4f, 0.5f, 0.3f)), 3.0f, 0.3f, glm::vec3(0.1f), 128, 64);
    Tracer::Light sun; sun.direction = -glm::normalize(glm::vec3(0.4f, 0.5f, 0.3f)); sun.intensity = 3.0f; sun.angle = glm::radians(2.0f);
    scene.lights.push_back(sun);
    scene.camera = lookAt({0.3f, 1.4f, 3.2f}, {0, 0.3f, 0}, 64, 48, 60.0f);
    return scene;
}

}

int main(){
    TestReport report("T2 LoomTracer na kartici");

    LoomConfig config;
    config.width = 64;
    config.height = 64;
    config.headless = true;
    config.appName = "test_tracer_gpu";
    config.engineName = "Loom tests";
    LoomInitializer loom(config);
    TracerGpu::Pipelines pipelines(loom);

    //-- 1. bijela pec -----------------------------------------------------------------------------
    {
        struct Case{ const char* name; Tracer::Material material; float tolerance; };
        std::vector<Case> cases;
        Tracer::Material lambert; lambert.baseColor = glm::vec3(1.0f); lambert.specular = 0.0f;
        cases.push_back({"pec: lambert", lambert, 0.01f});
        Tracer::Material plastic; plastic.baseColor = glm::vec3(1.0f); plastic.roughness = 0.4f;
        cases.push_back({"pec: plastika", plastic, 0.02f});
        Tracer::Material metal; metal.baseColor = glm::vec3(1.0f); metal.metallic = 1.0f; metal.roughness = 1.0f;
        cases.push_back({"pec: hrapavi metal", metal, 0.02f});
        Tracer::Material glass; glass.baseColor = glm::vec3(1.0f); glass.transmission = 1.0f; glass.roughness = 0.0f;
        cases.push_back({"pec: staklo", glass, 0.02f});
        Tracer::Material coat = plastic; coat.clearcoat = 1.0f; coat.clearcoatRoughness = 0.1f;
        cases.push_back({"pec: lak", coat, 0.03f});
        for(const Case& c : cases){
            const glm::vec3 v = coveredAverage(onCard(loom, pipelines, furnace(c.material), 128, 64, 0.0f));
            const float worst = std::max({std::abs(v.r - 1.0f), std::abs(v.g - 1.0f), std::abs(v.b - 1.0f)});
            report.check(c.name, worst < c.tolerance, fmt("%.4f %.4f %.4f", v.r, v.g, v.b));
        }
    }

    //-- 2. sunce, tockasto svjetlo, svijetli kvadrat -------------------------------------------------
    {
        Tracer::Scene scene;
        Tracer::Material floor; floor.baseColor = glm::vec3(0.5f); floor.specular = 0.0f;
        scene.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(20.0f)), scene.addMaterial(floor));
        Tracer::Light sun; sun.direction = {0, -1, 0}; sun.intensity = 3.0f; sun.angle = glm::radians(0.53f);
        scene.lights.push_back(sun);
        scene.camera = lookAt({0, 5, 0.001f}, {0, 0, 0}, 16, 16, 60.0f);
        const float v = coveredAverage(onCard(loom, pipelines, std::move(scene), 32)).g;
        const float expected = 0.5f * 3.0f / glm::pi<float>();
        report.check("sunce", std::abs(v - expected) < expected * 0.004f, fmt("%.5f, ocekivano %.5f", v, expected));
    }
    {
        Tracer::Scene scene;
        Tracer::Material floor; floor.baseColor = glm::vec3(1.0f); floor.specular = 0.0f;
        scene.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(20.0f)), scene.addMaterial(floor));
        Tracer::Light bulb; bulb.type = Tracer::Light::Type::Sphere; bulb.position = {0, 2, 0}; bulb.radius = 0.05f; bulb.intensity = 8.0f;
        scene.lights.push_back(bulb);
        scene.camera = lookAt({3, 1, 0}, {0, 0, 0}, 9, 9, 2000.0f);
        const Tracer::Frame f = onCard(loom, pipelines, std::move(scene), 256);
        const float v = f.cg[(4 * 9 + 4) * 4 + 1], expected = 8.0f / (glm::pi<float>() * 4.0f);
        report.check("kugla svjetla", std::abs(v - expected) < expected * 0.01f, fmt("%.5f, ocekivano %.5f", v, expected));
    }
    {
        const float a = 1.0f, h = 1.0f;
        Tracer::Scene scene;
        Tracer::Material floor; floor.baseColor = glm::vec3(1.0f); floor.specular = 0.0f;
        scene.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(0.02f)), scene.addMaterial(floor));
        Tracer::Material lamp; lamp.baseColor = glm::vec3(0.0f); lamp.specular = 0.0f; lamp.emission = glm::vec3(1.0f);
        Tracer::ObjectFlags hidden; hidden.cameraVisible = false;
        scene.addMesh(Tracer::unitPlane(), glm::translate(glm::mat4(1.0f), glm::vec3(0, h, 0)) *
                      glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f * a)),
                      scene.addMaterial(lamp), "lampa", hidden);
        scene.camera = lookAt({0.3f, 0.4f, 0.0f}, {0, 0, 0}, 5, 5, 4000.0f);
        const Tracer::Frame f = onCard(loom, pipelines, std::move(scene), 1024);
        const float v = f.cg[(2 * 5 + 2) * 4 + 1];
        auto corner = [](float x, float y){
            const float sx = std::sqrt(1.0f + x * x), sy = std::sqrt(1.0f + y * y);
            return (x / sx * std::atan(y / sx) + y / sy * std::atan(x / sy)) / (2.0f * glm::pi<float>());
        };
        const float expected = 4.0f * corner(a / h, a / h);
        report.check("svijetli kvadrat", std::abs(v - expected) < expected * 0.015f, fmt("%.5f, ocekivano %.5f", v, expected));
    }

    //-- 2b. jednostrani svijetli pravokutnik: licem daje faktor oblika, nalicjem nista ------------------
    for(bool facing : {true, false}){
        Tracer::Scene scene;
        Tracer::Material floor; floor.baseColor = glm::vec3(1.0f); floor.specular = 0.0f;
        scene.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(0.02f)), scene.addMaterial(floor));
        Tracer::Material lamp; lamp.baseColor = glm::vec3(0.0f); lamp.specular = 0.0f; lamp.emission = glm::vec3(1.0f);
        lamp.emissionTwoSided = false;
        Tracer::ObjectFlags hidden; hidden.cameraVisible = false; hidden.castsShadows = false;
        //unitPlane gleda +Y; zakret za pi oko X ga okrene dolje (licem prema podu)
        scene.addMesh(Tracer::unitPlane(), glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)) *
                      glm::rotate(glm::mat4(1.0f), facing ? glm::pi<float>() : 0.0f, glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)),
                      scene.addMaterial(lamp), "lampa", hidden);
        scene.camera = lookAt({0.3f, 0.4f, 0.0f}, {0, 0, 0}, 5, 5, 4000.0f);
        const Tracer::Frame f = onCard(loom, pipelines, std::move(scene), facing ? 1024 : 64);
        const float v = f.cg[(2 * 5 + 2) * 4 + 1];
        auto corner = [](float x, float y){
            const float sx = std::sqrt(1.0f + x * x), sy = std::sqrt(1.0f + y * y);
            return (x / sx * std::atan(y / sx) + y / sy * std::atan(x / sy)) / (2.0f * glm::pi<float>());
        };
        const float expected = facing ? 4.0f * corner(1.0f, 1.0f) : 0.0f;
        report.check(facing ? "jednostrano: lice" : "jednostrano: nalicje", std::abs(v - expected) < 0.015f * std::max(expected, 0.01f) + 1e-6f,
                     fmt("%.5f, ocekivano %.5f", v, expected));
    }

    //-- 3. projekcija i dubina -----------------------------------------------------------------------
    {
        Tracer::Scene scene;
        Tracer::Material white; white.emission = glm::vec3(1.0f); white.baseColor = glm::vec3(0.0f);
        const glm::vec3 where(0.7f, -0.3f, -0.2f);
        scene.addMesh(Tracer::uvSphere(0.05f, 48, 24), glm::translate(glm::mat4(1.0f), where), scene.addMaterial(white));
        scene.camera = lookAt({1.5f, 0.8f, 3.0f}, {0, 0, 0}, 320, 180, 260.0f, glm::vec2(171.3f, 83.7f));
        glm::vec2 expected;
        scene.camera.project(where, expected);
        const glm::vec3 local = glm::vec3(glm::inverse(scene.camera.cameraToWorld) * glm::vec4(where, 1.0f));
        const Tracer::Frame f = onCard(loom, pipelines, std::move(scene), 32);
        glm::dvec2 centroid(0.0);
        double mass = 0.0;
        size_t best = 0;
        for(uint32_t y = 0; y < f.height; ++y) for(uint32_t x = 0; x < f.width; ++x){
            const size_t i = size_t(y) * f.width + x;
            const float c = f.cg[i * 4 + 3];
            centroid += glm::dvec2(x + 0.5, y + 0.5) * double(c);
            mass += c;
            if(f.depth[i] < f.depth[best]) best = i;
        }
        centroid /= std::max(mass, 1e-9);
        const float error = glm::length(glm::vec2(centroid) - expected);
        report.check("projekcija", error < 0.15f, fmt("greska %.3f px", error));
        report.check("dubina", std::abs(f.depth[best] - (-local.z - 0.05f)) < 0.02f, fmt("%.4f, ocekivano ~%.4f", f.depth[best], -local.z - 0.05f));
    }

    //-- 3b. distorzija lece na kartici -----------------------------------------------------------------
    {
        Tracer::Scene scene;
        Tracer::Material white; white.emission = glm::vec3(1.0f); white.baseColor = glm::vec3(0.0f);
        const glm::vec3 where(1.45f, 0.95f, -0.2f);
        scene.addMesh(Tracer::uvSphere(0.04f, 48, 24), glm::translate(glm::mat4(1.0f), where), scene.addMaterial(white));
        scene.camera = lookAt({0.0f, 0.3f, 3.0f}, {0, 0.2f, 0}, 320, 180, 260.0f, glm::vec2(161.0f, 88.0f));
        scene.camera.lens = glm::vec4(255.0f, 255.0f, 158.0f, 91.0f);
        scene.camera.k1 = -0.18f;
        scene.camera.k2 = 0.03f;
        glm::vec2 expected;
        scene.camera.project(where, expected);
        const Tracer::Frame f = onCard(loom, pipelines, std::move(scene), 32);
        glm::dvec2 centroid(0.0);
        double mass = 0.0;
        for(uint32_t y = 0; y < f.height; ++y) for(uint32_t x = 0; x < f.width; ++x){
            const float c = f.cg[(size_t(y) * f.width + x) * 4 + 3];
            centroid += glm::dvec2(x + 0.5, y + 0.5) * double(c);
            mass += c;
        }
        centroid /= std::max(mass, 1e-9);
        const float error = glm::length(glm::vec2(centroid) - expected);
        report.check("distorzija", error < 0.2f, fmt("greska %.3f px", error));
    }

    //-- 4. ista slika kao procesor (i negativna kontrola) -----------------------------------------------
    {
        double gpuSeconds = 0.0;
        const Tracer::Frame gpu = onCard(loom, pipelines, mixed(0.3f), 512, 12, 16.0f, &gpuSeconds);
        const auto start = std::chrono::steady_clock::now();
        const Tracer::Frame cpu = onCpu(mixed(0.3f), 512);
        const double cpuSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const Tracer::Frame other = onCpu(mixed(0.8f), 512);
        const Tracer::Frame cpuAgain = [&]{ Tracer::Scene s = mixed(0.3f); Tracer::Renderer r(std::move(s)); Tracer::RenderSettings st; st.samples = 512; st.seed = 7; r.render(st); return r.frame(false); }();
        const double same = rmse(gpu, cpu), noise = rmse(cpuAgain, cpu), different = rmse(other, cpu);
        report.check("kartica = procesor", same < 2.0 * noise + 1e-3 && different > 3.0 * same,
                     fmt("RMSE kartica-procesor %.4f, sum procesora %.4f, drugi materijal %.4f", same, noise, different));
        std::printf("        vrijeme 64x48x512: kartica %.2f s, procesor %.2f s\n", gpuSeconds, cpuSeconds);

        //Slika za prikaz s kartice = procesorski composite + toDisplay (+-1 zbog zaokruzivanja)
        Tracer::RenderSettings settings;
        settings.samples = 16;
        TracerGpu::GpuTracer tracer(loom, pipelines, Tracer::compile(mixed(0.3f)), settings);
        tracer.renderAll();
        loom.renderer.beginFrame();
        TracerGpu::DisplayOptions options;
        options.view = Tracer::ViewTransform::AgX;
        options.exposure = 0.5f;
        tracer.recordDisplay(options);
        loom.renderer.endFrame();
        const std::vector<uint8_t> shown = tracer.readDisplay();
        const Tracer::Frame frame = tracer.readFrame(false);
        const std::vector<uint8_t> expected = Tracer::toDisplay(Tracer::composite(frame, Tracer::Backdrop::Environment), frame.width, frame.height,
                                                        Tracer::ViewTransform::AgX, 0.5f);
        int worst = 0;
        for(size_t i = 0; i < shown.size(); ++i) worst = std::max(worst, std::abs(int(shown[i]) - int(expected[i])));
        report.check("prikaz = procesor", shown.size() == expected.size() && worst <= 1, fmt("najveca razlika %d", worst));
    }

    //-- 4b. hardverske zrake (ray query) = vlastiti BVH na kartici --------------------------------------
    {
        const bool available = loom.device.hasRayQuery();
        Tracer::RenderSettings probe;
        probe.samples = 1;
        TracerGpu::GpuTracer check(loom, pipelines, Tracer::compile(mixed(0.3f)), probe);
        double rqSeconds = 0.0, bvhSeconds = 0.0;
        const Tracer::Frame rq = onCard(loom, pipelines, mixed(0.3f), 256, 12, 16.0f, &rqSeconds, true);
        const Tracer::Frame bvh = onCard(loom, pipelines, mixed(0.3f), 256, 12, 16.0f, &bvhSeconds, false);
        const Tracer::Frame cpu = onCpu(mixed(0.3f), 256);
        const Tracer::Frame cpuOther = [&]{ Tracer::Scene s = mixed(0.3f); Tracer::Renderer r(std::move(s)); Tracer::RenderSettings st; st.samples = 256; st.seed = 7; r.render(st); return r.frame(false); }();
        const double noise = rmse(cpuOther, cpu);
        report.check("hardverske zrake", check.usesRayQuery() == available && rmse(rq, bvh) < noise && rmse(rq, cpu) < noise,
                     fmt("%s; RMSE ray query - BVH %.4f, ray query - procesor %.4f, sum %.4f; 64x48x256: ray query %.2f s, BVH %.2f s (lavapipe)",
                         available ? "kartica ih ima" : "kartica ih nema - oba puta su BVH", rmse(rq, bvh), rmse(rq, cpu), noise, rqSeconds, bvhSeconds));
    }

    //-- 5. filtar i post na kartici = procesorski A-trous + composite + applyPost + toDisplay -----------
    {
        Tracer::RenderSettings settings;
        settings.samples = 32;
        TracerGpu::GpuTracer tracer(loom, pipelines, Tracer::compile(mixed(0.3f)), settings);
        tracer.renderAll();
        TracerGpu::DisplayOptions options;
        options.view = Tracer::ViewTransform::AgX;
        options.exposure = 0.4f;
        options.denoise = true;
        options.post.enabled = true;
        options.post.bloom = 0.15f;
        options.post.bloomRadius = 0.05f;
        options.post.bloomThreshold = 0.5f;
        options.post.vignette = 0.3f;
        options.post.chromaticAberration = 1.5f;
        options.post.temperature = 5000.0f;
        options.post.contrast = 1.2f;
        options.post.saturation = 0.8f;
        options.post.grain = 0.05f;
        options.grainSeed = 1234u;
        const auto start = std::chrono::steady_clock::now();
        loom.renderer.beginFrame();
        tracer.recordDisplay(options);
        loom.renderer.endFrame();
        const std::vector<uint8_t> shown = tracer.readDisplay();
        const double gpuSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Tracer::Frame frame = tracer.readFrame(false);
        const auto cpuStart = std::chrono::steady_clock::now();
        Tracer::denoiseFrame(frame, Tracer::Denoiser::ATrous);
        std::vector<float> beauty = Tracer::composite(frame, Tracer::Backdrop::Environment);
        Tracer::PostSettings post = options.post;
        post.exposure = options.exposure;
        post.grainSeed = options.grainSeed;
        Tracer::applyPost(beauty, frame.width, frame.height, post);
        const std::vector<uint8_t> expected = Tracer::toDisplay(beauty, frame.width, frame.height, Tracer::ViewTransform::AgX, 0.0f);
        const double cpuSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - cpuStart).count();
        int worst = 0;
        double mean = 0.0;
        for(size_t i = 0; i < shown.size() && i < expected.size(); ++i){
            const int d = std::abs(int(shown[i]) - int(expected[i]));
            worst = std::max(worst, d);
            mean += d;
        }
        mean /= double(std::max<size_t>(1, shown.size()));
        //Filtar bez posta mijenja sliku (inace test ne bi mjerio filtar)
        options.post.enabled = false;
        options.denoise = false;
        loom.renderer.beginFrame();
        tracer.recordDisplay(options);
        loom.renderer.endFrame();
        const std::vector<uint8_t> plain = tracer.readDisplay();
        int changed = 0;
        for(size_t i = 0; i < plain.size(); ++i) changed = std::max(changed, std::abs(int(plain[i]) - int(shown[i])));
        report.check("filtar i post na kartici = procesor", shown.size() == expected.size() && worst <= 3 && mean < 0.2 && changed > 20,
                     fmt("najveca razlika %d, srednja %.3f (bez filtra i posta razlika %d); kartica %.3f s, procesor %.3f s",
                         worst, mean, changed, gpuSeconds, cpuSeconds));
    }

    report.checkNoValidationMessages();
    return report.result();
}
