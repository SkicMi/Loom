// LoomTracer: staklene sjene i prilagodljivo uzorkovanje, na procesoru i na kartici.
//
//   STAKLO, IZRAVNO   sunce ravno odozgo kroz tanku staklenu plocu (ior 1.5) na Lambertov pod:
//                     pod ispod ploce = pod bez ploce * (1 - F0)^2 = 0.9216 (dvije plohe, F0 0.04).
//                     Bez staklenih sjena delta sunce kroz staklo ne stize nikako (crno)
//   BEZ DVOSTRUKOG    ravnoparalelna ploca ne skrece zrake, pa je prava kaustika (putanja kroz
//                     staklo do sunca, uz veliko sunce da je BSDF nadje) jednaka ravnoj
//                     propusnosti. Staklene sjene moraju dati isto - dvaput brojeno bilo bi ~2x
//   PRILAGODLJIVO     ista greska prema referenci kao pun broj uzoraka, s manje uzoraka; nebo i
//                     mirni dijelovi stanu na najmanjem broju
//   KARTICA           isto pravilo zaustavljanja i iste staklene sjene: slika = procesorska
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <TracerGpu/GpuTracer.h>
#include <Tracer/Renderer.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace{

Tracer::Camera lookAt(const glm::vec3& eye, const glm::vec3& target, uint32_t width, uint32_t height, float focal){
    Tracer::Camera c;
    c.cameraToWorld = glm::inverse(glm::lookAt(eye, target, glm::vec3(0, 1, 0)));
    c.width = width;
    c.height = height;
    c.focalPixels = focal;
    c.centre = glm::vec2(width, height) * 0.5f;
    return c;
}

//Pod pod suncem; ploca stakla na visini 1.5 (ili bez nje). Kamera s boka gleda pod ispod ploce,
//a njena zraka ne prolazi kroz staklo
Tracer::Scene slab(bool withGlass, float sunAngleDegrees){
    Tracer::Scene scene;
    Tracer::Material floor; floor.baseColor = glm::vec3(0.5f); floor.specular = 0.0f; floor.roughness = 1.0f;
    scene.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(40.0f)), scene.addMaterial(floor));
    if(withGlass){
        Tracer::Material glass; glass.baseColor = glm::vec3(1.0f); glass.transmission = 1.0f; glass.roughness = 0.0f; glass.ior = 1.5f;
        scene.addMesh(Tracer::unitCube(), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, 0.0f)) *
                                          glm::scale(glm::mat4(1.0f), glm::vec3(6.0f, 0.05f, 6.0f)), scene.addMaterial(glass));
    }
    Tracer::Light sun;
    sun.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    sun.intensity = 3.0f;
    sun.angle = glm::radians(sunAngleDegrees);
    scene.lights.push_back(sun);
    scene.camera = lookAt({0.0f, 0.6f, 2.5f}, {0.0f, 0.0f, 0.0f}, 24, 16, 60.0f);
    return scene;
}

//Pikseli sredine (pod ispod ploce)
float middle(const Tracer::Frame& f){
    double sum = 0.0;
    int n = 0;
    for(uint32_t y = f.height / 2 - 3; y < f.height / 2 + 3; ++y)
        for(uint32_t x = f.width / 2 - 4; x < f.width / 2 + 4; ++x){ sum += f.cg[(size_t(y) * f.width + x) * 4 + 1]; ++n; }
    return float(sum / std::max(1, n));
}

Tracer::RenderSettings settingsFor(uint32_t samples, uint32_t bounces, bool glass, float adaptive = 0.0f, uint32_t seed = 0,
                                   float clamp = 0.0f){
    Tracer::RenderSettings s;
    s.samples = samples;
    s.maxBounces = bounces;
    s.glassShadows = glass;
    s.adaptiveThreshold = adaptive;
    s.adaptiveMinSamples = 32;
    s.seed = seed;
    s.indirectClamp = clamp;
    return s;
}

Tracer::Frame onCpu(Tracer::Scene scene, const Tracer::RenderSettings& settings, double* average = nullptr){
    Tracer::Renderer renderer(std::move(scene));
    renderer.render(settings);
    if(average) *average = renderer.averageSamples();
    return renderer.frame(false);
}

Tracer::Frame onCard(LoomInitializer& loom, TracerGpu::Pipelines& pipelines, Tracer::Scene scene, const Tracer::RenderSettings& settings){
    TracerGpu::GpuTracer tracer(loom, pipelines, Tracer::compile(std::move(scene)), settings);
    tracer.renderAll();
    return tracer.readFrame(false);
}

double meanOf(const Tracer::Frame& f){
    double sum = 0.0;
    for(size_t i = 0; i < f.pixelCount(); ++i) sum += f.cg[i * 4] + f.cg[i * 4 + 1] + f.cg[i * 4 + 2];
    return sum / double(std::max<size_t>(1, f.pixelCount()));
}

double rmse(const Tracer::Frame& a, const Tracer::Frame& b){
    double sum = 0.0;
    //Na zaslonu ionako odrezano: rijetka krijesnica ne smije sama odluciti usporedbu
    for(size_t i = 0; i < a.cg.size() && i < b.cg.size(); ++i){
        const double d = std::min(double(a.cg[i]), 2.0) - std::min(double(b.cg[i]), 2.0);
        sum += d * d;
    }
    return std::sqrt(sum / double(std::max<size_t>(1, a.cg.size())));
}

//Scena za prilagodljivo: pola kadra nebo (stane odmah), pod s mekom sjenom, sjajna kugla, staklo
Tracer::Scene mixed(){
    Tracer::Scene scene;
    Tracer::Material floor; floor.baseColor = glm::vec3(0.6f); floor.roughness = 0.8f;
    scene.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(8.0f)), scene.addMaterial(floor));
    Tracer::Material ball; ball.baseColor = glm::vec3(0.9f, 0.6f, 0.3f); ball.metallic = 1.0f; ball.roughness = 0.3f;
    scene.addMesh(Tracer::uvSphere(0.5f, 48, 24), glm::translate(glm::mat4(1.0f), glm::vec3(-0.6f, 0.5f, 0.0f)), scene.addMaterial(ball));
    Tracer::Material glass; glass.transmission = 1.0f; glass.roughness = 0.05f; glass.baseColor = glm::vec3(0.9f, 1.0f, 0.95f);
    scene.addMesh(Tracer::unitCube(), glm::translate(glm::mat4(1.0f), glm::vec3(0.7f, 0.4f, 0.3f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.8f)),
                  scene.addMaterial(glass));
    scene.environment.color = glm::vec3(0.4f, 0.5f, 0.7f);
    Tracer::Light sun; sun.direction = -glm::normalize(glm::vec3(0.4f, 0.5f, 0.3f)); sun.intensity = 3.0f; sun.angle = glm::radians(8.0f);
    scene.lights.push_back(sun);
    scene.camera = lookAt({0.3f, 1.0f, 3.2f}, {0, 0.9f, 0}, 64, 48, 60.0f);
    return scene;
}

}

int main(){
    TestReport report("T3 staklene sjene i prilagodljivo uzorkovanje");

    LoomConfig config;
    config.width = 64;
    config.height = 64;
    config.headless = true;
    config.appName = "test_tracer_adaptive";
    config.engineName = "Loom tests";
    LoomInitializer loom(config);
    TracerGpu::Pipelines pipelines(loom);

    //-- 1. sunce kroz staklo, samo izravno ---------------------------------------------------------
    {
        const float open = middle(onCpu(slab(false, 0.0f), settingsFor(16, 1, true)));
        const float glass = middle(onCpu(slab(true, 0.0f), settingsFor(16, 1, true)));
        const float dark = middle(onCpu(slab(true, 0.0f), settingsFor(16, 1, false)));
        const float card = middle(onCard(loom, pipelines, slab(true, 0.0f), settingsFor(16, 1, true)));
        const float expected = (1.0f - 0.04f) * (1.0f - 0.04f);
        report.check("staklo: (1-F0)^2", open > 0.0f && std::abs(glass / open - expected) < 0.01f,
                     fmt("pod %.4f, ispod stakla %.4f, omjer %.4f (ocekivano %.4f)", open, glass, glass / open, expected));
        report.check("staklo: bez staklenih sjena crno", dark < 0.01f * open, fmt("%.5f", dark));
        report.check("staklo: kartica", std::abs(card / open - expected) < 0.01f, fmt("omjer %.4f", card / open));
    }

    //-- 2. staklene sjene = prava kaustika ravnoparalelne ploce (nista dvaput) -----------------------
    {
        const Tracer::RenderSettings path = settingsFor(2048, 8, false);
        const Tracer::RenderSettings fake = settingsFor(256, 8, true);
        const float open = middle(onCpu(slab(false, 20.0f), settingsFor(256, 8, true)));
        const float caustic = middle(onCpu(slab(true, 20.0f), path));
        const float glass = middle(onCpu(slab(true, 20.0f), fake));
        const float card = middle(onCard(loom, pipelines, slab(true, 20.0f), fake));
        report.check("bez dvostrukog brojanja", std::abs(glass / caustic - 1.0f) < 0.04f,
                     fmt("kaustika putanjama %.4f, staklene sjene %.4f, bez ploce %.4f", caustic, glass, open));
        report.check("bez dvostrukog: kartica", std::abs(card / glass - 1.0f) < 0.02f, fmt("%.4f prema %.4f", card, glass));
    }

    //-- 3. prilagodljivo uzorkovanje ----------------------------------------------------------------
    {
        const Tracer::Frame reference = onCpu(mixed(), settingsFor(4096, 12, true, 0.0f, 99, 16.0f));
        double fullAverage = 0.0, adaptiveAverage = 0.0;
        const Tracer::Frame full = onCpu(mixed(), settingsFor(512, 12, true, 0.0f, 0, 16.0f), &fullAverage);
        const Tracer::Frame adaptive = onCpu(mixed(), settingsFor(512, 12, true, 0.02f, 0, 16.0f), &adaptiveAverage);
        const Tracer::Frame card = onCard(loom, pipelines, mixed(), settingsFor(512, 12, true, 0.02f, 0, 16.0f));
        const double errorFull = rmse(full, reference), errorAdaptive = rmse(adaptive, reference), errorCard = rmse(card, reference);
        //Isti broj uzoraka koliko je prilagodljivo prosjecno potrosilo, jednoliko: losiji od prilagodljivog
        const Tracer::Frame equal = onCpu(mixed(), settingsFor(uint32_t(std::lround(adaptiveAverage)), 12, true, 0.0f, 0, 16.0f));
        const double errorEqual = rmse(equal, reference);
        //Pikseli neba stanu na najmanjem broju
        report.check("prilagodljivo: manje uzoraka", adaptiveAverage < 0.6 * fullAverage,
                     fmt("prosjecno %.0f od %.0f uzoraka po pikselu", adaptiveAverage, fullAverage));
        report.check("prilagodljivo: ista kvaliteta", errorAdaptive < 1.5 * errorFull,
                     fmt("RMSE %.4f prema punih %.4f (jednako uzoraka bez prilagodbe %.4f)", errorAdaptive, errorFull, errorEqual));
        report.check("prilagodljivo: bolje od jednolikog", errorAdaptive < errorEqual,
                     fmt("%.4f < %.4f", errorAdaptive, errorEqual));
        //Zaustavljanje po procijenjenoj varijanci smije stati prije rijetkog svijetlog uzorka - to bi
        //potamnilo sliku. Srednja svjetlina mora ostati ista kao referenca
        const double bias = meanOf(adaptive) / meanOf(reference) - 1.0, biasFull = meanOf(full) / meanOf(reference) - 1.0;
        report.check("prilagodljivo: ne tamni", std::abs(bias) < 0.01,
                     fmt("srednja svjetlina %+.2f %% (pun broj %+.2f %%)", 100.0 * bias, 100.0 * biasFull));
        report.check("prilagodljivo: kartica", errorCard < 1.3 * errorAdaptive + 1e-3,
                     fmt("RMSE kartice %.4f, procesora %.4f", errorCard, errorAdaptive));
    }

    report.checkNoValidationMessages();
    return report.result();
}
