// Magla u kutiji (volumetrijsko svjetlo): tracer na procesoru i kartici, most iz Warpa, USD.
//
//   UPIJANJE     crna magla debljine 2 i gustoce 0.4 ispred svijetlog zida: exp(-0.8) = 0.4493
//   BIJELA PEC   bijela magla (albedo 1, g 0.5) pod jednolikim nebom 1: slika ostaje 1 (energija
//                se ni ne gubi ni ne stvara, ma koliko puta se svjetlo rasprsi)
//   JEDNO RASPRSENJE  sunce ravno odozgo (E = pi), pogled vodoravno kroz kutiju 2x2x2 gustoce
//                0.05: L = E p(90 st) e^(-s) (1 - e^(-2s)); p = 1/4pi za g 0, HG(0, 0.6) za g 0.6
//   PRILAGODLJIVO  rijetka magla: piksel kojem 32 uzorka nista ne pogode ima varijancu 0 - ne smije
//                stati crn (susjedi nisu gotovi), srednja vrijednost ista kao bez prilagodbe
//   ZRAKE        ploca iznad lijeve polovice magle: lijevo tamno, desno svijetlo (pruga sjene)
//   KARTICA      isti brojevi na Vulkanu
//   MOST, USD    Warp::Volume -> Tracer::Volume (kutija entiteta u svijetu), kroz .usda i natrag
#include "TestHarness.h"

#include "LoomRender.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <TracerGpu/GpuTracer.h>
#include <Warp/Project.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>

namespace{

Tracer::Camera lookAt(const glm::vec3& eye, const glm::vec3& target, uint32_t width, uint32_t height, float focal){
    Tracer::Camera c;
    c.cameraToWorld = glm::inverse(glm::lookAt(eye, target, glm::vec3(0, 1, 0)));
    c.width = width; c.height = height; c.focalPixels = focal;
    c.centre = glm::vec2(width, height) * 0.5f;
    return c;
}

Tracer::Volume box(const glm::vec3& centre, const glm::vec3& size, glm::vec3 albedo, float density, float g){
    Tracer::Volume v;
    v.toWorld = glm::translate(glm::mat4(1.0f), centre) * glm::scale(glm::mat4(1.0f), size);
    v.albedo = albedo; v.density = density; v.anisotropy = g;
    return v;
}

Tracer::Scene absorber(){
    Tracer::Scene s;
    Tracer::Material wall; wall.baseColor = glm::vec3(0.0f); wall.emission = glm::vec3(1.0f); wall.emissionStrength = 1.0f;
    s.addMesh(Tracer::unitPlane(), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f)) *
                                   glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(1, 0, 0)) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(20.0f)), s.addMaterial(wall));
    s.volumes.push_back(box({0.0f, 0.0f, -2.5f}, {10.0f, 10.0f, 2.0f}, glm::vec3(0.0f), 0.4f, 0.0f));
    s.camera = lookAt({0, 0, 0}, {0, 0, -1}, 16, 16, 400.0f);
    return s;
}

Tracer::Scene furnace(){
    Tracer::Scene s;
    s.environment.color = glm::vec3(1.0f);
    s.volumes.push_back(box({0, 0, 0}, {2, 2, 2}, glm::vec3(1.0f), 2.0f, 0.5f));
    s.camera = lookAt({0, 0, 5}, {0, 0, 0}, 16, 16, 40.0f);
    return s;
}

Tracer::Scene sunlit(float g, bool occluder){
    Tracer::Scene s;
    s.volumes.push_back(box({0, 0, 0}, {2, 2, 2}, glm::vec3(1.0f), 0.05f, g));
    Tracer::Light sun; sun.direction = glm::vec3(0, -1, 0); sun.intensity = glm::pi<float>(); sun.angle = 0.0f;
    s.lights.push_back(sun);
    if(occluder){
        Tracer::Material black; black.baseColor = glm::vec3(0.0f);
        s.addMesh(Tracer::unitPlane(), glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 1.5f, 0.0f)) *
                                       glm::scale(glm::mat4(1.0f), glm::vec3(4.0f)), s.addMaterial(black));
        s.camera = lookAt({0, 0, 10}, {0, 0, 0}, 32, 8, 100.0f);
    }else s.camera = lookAt({0, 0, 10}, {0, 0, 0}, 16, 16, 400.0f);
    return s;
}

Tracer::RenderSettings settingsFor(uint32_t samples, uint32_t bounces){
    Tracer::RenderSettings st;
    st.samples = samples;
    st.maxBounces = bounces;
    st.indirectClamp = 0.0f;
    return st;
}

Tracer::Frame onCpu(Tracer::Scene s, const Tracer::RenderSettings& st){
    Tracer::Renderer r(std::move(s));
    r.render(st);
    return r.frame(false);
}

//Srednja zelena u stupcima [x0, x1) svih redaka
double meanCg(const Tracer::Frame& f, uint32_t x0, uint32_t x1){
    double sum = 0.0;
    for(uint32_t y = 0; y < f.height; ++y) for(uint32_t x = x0; x < x1; ++x) sum += f.cg[(size_t(y) * f.width + x) * 4 + 1];
    return sum / double(f.height * (x1 - x0));
}

double meanComposite(const Tracer::Frame& f){
    const std::vector<float> c = Tracer::composite(f, Tracer::Backdrop::Environment);
    double sum = 0.0;
    for(size_t i = 0; i < f.pixelCount(); ++i) sum += (c[i * 4] + c[i * 4 + 1] + c[i * 4 + 2]) / 3.0;
    return sum / double(f.pixelCount());
}

//Magla po visini: ishodiste na y = 0, gustoca d0, e-pad na `height`
Tracer::Volume heightFog(float d0, float height, glm::vec3 albedo, float g = 0.0f){
    Tracer::Volume v;
    v.shape = Tracer::Volume::Shape::Height;
    v.density = d0; v.height = height; v.albedo = albedo; v.anisotropy = g;
    return v;
}
//Upijajuca magla po visini, svijetli zid ili strop; kamera na visini 0.5 gleda vodoravno ili gore
Tracer::Scene heightAbsorber(bool up){
    Tracer::Scene s;
    Tracer::Material wall; wall.baseColor = glm::vec3(0.0f); wall.emission = glm::vec3(1.0f); wall.emissionStrength = 1.0f;
    if(up) s.addMesh(Tracer::unitPlane(), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.5f, 0.0f)) *
                     glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(20.0f)), s.addMaterial(wall));
    else s.addMesh(Tracer::unitPlane(), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, -5.0f)) *
                   glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(20.0f)), s.addMaterial(wall));
    s.volumes.push_back(up ? heightFog(0.5f, 1.0f, glm::vec3(0.0f)) : heightFog(0.2f, 1.0f, glm::vec3(0.0f)));
    s.camera = up ? lookAt({0, 0.5f, 0}, {0, 10.5f, 0.001f}, 16, 16, 800.0f) : lookAt({0, 0.5f, 0}, {0, 0.5f, -1}, 16, 16, 800.0f);
    return s;
}
//Bijeli pod (albedo 1) na dnu magle: bez njega magla prema dolje gusne bez granice i putanja se
//rasprsuje tisucama puta prije nego izadje - energija bi se izgubila na granici odbijanja
Tracer::Scene heightFurnace(){
    Tracer::Scene s;
    s.environment.color = glm::vec3(1.0f);
    Tracer::Material white; white.baseColor = glm::vec3(1.0f); white.specular = 0.0f; white.roughness = 1.0f;
    s.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(1000.0f)), s.addMaterial(white));
    Tracer::Volume v = heightFog(0.4f, 1.5f, glm::vec3(1.0f), 0.6f);
    v.anisotropy2 = -0.4f; v.lobeMix = 0.3f;
    s.volumes.push_back(v);
    s.camera = lookAt({0, 1.0f, 0}, {0, 0.6f, -1}, 16, 16, 30.0f);
    return s;
}
//Delta sunce odozgo, kamera vodoravno na visini 1, bez geometrije: L = p(90) E e^(-sigma(1)/k)
Tracer::Scene heightSunlit(float g, float g2, float mix){
    Tracer::Scene s;
    Tracer::Volume v = heightFog(0.3f, 2.0f, glm::vec3(1.0f), g);
    v.anisotropy2 = g2; v.lobeMix = mix;
    s.volumes.push_back(v);
    Tracer::Light sun; sun.direction = glm::vec3(0, -1, 0); sun.intensity = glm::pi<float>(); sun.angle = 0.0f;
    s.lights.push_back(sun);
    s.camera = lookAt({0, 1.0f, 0}, {0, 1.0f, -1}, 16, 16, 800.0f);
    return s;
}
double heightSingle(float g, float g2, float mix){
    const double sigma = 0.3 * std::exp(-0.5), k = 0.5;
    auto hg = [](double gg){ return (1.0 - gg * gg) / (4.0 * glm::pi<double>() * std::pow(1.0 + gg * gg, 1.5)); };
    const double p = (1.0 - mix) * hg(g) + mix * hg(g2);
    return p * glm::pi<double>() * std::exp(-sigma / k);
}

double single(float g){
    const double sigma = 0.05, E = glm::pi<double>();
    const double p = (1.0 - g * g) / (4.0 * glm::pi<double>() * std::pow(1.0 + g * g, 1.5));
    return E * p * std::exp(-sigma) * (1.0 - std::exp(-2.0 * sigma));
}

}

int main(){
    TestReport report("T6 magla u kutiji");

    const double absorbed = meanCg(onCpu(absorber(), settingsFor(256, 4)), 0, 16);
    report.check("upijanje: exp(-0.8)", std::abs(absorbed - std::exp(-0.8)) < 0.01, fmt("%.4f (ocekivano %.4f)", absorbed, std::exp(-0.8)));

    const double white = meanComposite(onCpu(furnace(), settingsFor(512, 256)));
    report.check("bijela pec", std::abs(white - 1.0) < 0.01, fmt("%.4f (ocekivano 1)", white));

    const double s0 = meanCg(onCpu(sunlit(0.0f, false), settingsFor(1024, 1)), 0, 16);
    const double s6 = meanCg(onCpu(sunlit(0.6f, false), settingsFor(1024, 1)), 0, 16);
    report.check("jedno rasprsenje, g 0", std::abs(s0 / single(0.0f) - 1.0) < 0.02, fmt("%.5f (ocekivano %.5f)", s0, single(0.0f)));
    report.check("jedno rasprsenje, g 0.6", std::abs(s6 / single(0.6f) - 1.0) < 0.02, fmt("%.5f (ocekivano %.5f)", s6, single(0.6f)));

    {
        Tracer::RenderSettings st = settingsFor(256, 1);
        st.adaptiveThreshold = 0.02f;
        const Tracer::Frame f = onCpu(sunlit(0.0f, false), st);
        size_t black = 0;
        for(size_t i = 0; i < f.pixelCount(); ++i) black += f.cg[i * 4 + 1] <= 0.0f;
        const double mean = meanCg(f, 0, 16);
        report.check("prilagodljivo: bez crnih piksela", black == 0 && std::abs(mean / single(0.0f) - 1.0) < 0.03,
                     fmt("%zu crnih od %zu, srednja %.5f (ocekivano %.5f)", black, f.pixelCount(), mean, single(0.0f)));
    }

    const Tracer::Frame rays = onCpu(sunlit(0.0f, true), settingsFor(1024, 1));
    const double shadowed = meanCg(rays, 8, 14), lit = meanCg(rays, 18, 25);
    report.check("zrake: pruga sjene", lit > 0.8 * single(0.0f) && shadowed < 0.03 * lit, fmt("sjena %.5f, svjetlo %.5f", shadowed, lit));

    //-- magla po visini -----------------------------------------------------------------------------
    const double across = meanCg(onCpu(heightAbsorber(false), settingsFor(64, 4)), 0, 16);
    const double acrossTruth = std::exp(-0.2 * std::exp(-0.5) * 5.0);
    report.check("visina: vodoravno", std::abs(across / acrossTruth - 1.0) < 0.01, fmt("%.4f (ocekivano %.4f)", across, acrossTruth));
    const double upward = meanCg(onCpu(heightAbsorber(true), settingsFor(64, 4)), 0, 16);
    const double upwardTruth = std::exp(-0.5 * std::exp(-0.5) * (1.0 - std::exp(-3.0)));
    report.check("visina: gore do stropa", std::abs(upward / upwardTruth - 1.0) < 0.01, fmt("%.4f (ocekivano %.4f)", upward, upwardTruth));
    const double heightWhite = meanComposite(onCpu(heightFurnace(), settingsFor(512, 256)));
    report.check("visina: bijela pec (dva rezanja)", std::abs(heightWhite - 1.0) < 0.015, fmt("%.4f (ocekivano 1)", heightWhite));
    const double h0 = meanCg(onCpu(heightSunlit(0.0f, 0.0f, 0.0f), settingsFor(1024, 1)), 0, 16);
    const double h2 = meanCg(onCpu(heightSunlit(0.5f, -0.3f, 0.4f), settingsFor(1024, 1)), 0, 16);
    report.check("visina: jedno rasprsenje", std::abs(h0 / heightSingle(0, 0, 0) - 1.0) < 0.02 && std::abs(h2 / heightSingle(0.5f, -0.3f, 0.4f) - 1.0) < 0.02,
                 fmt("g 0: %.5f (ocekivano %.5f), dva rezanja: %.5f (ocekivano %.5f)", h0, heightSingle(0, 0, 0), h2, heightSingle(0.5f, -0.3f, 0.4f)));

    //-- holdout: magla iza stvarne plohe se ne broji -------------------------------------------------
    {
        Tracer::Scene s = sunlit(0.0f, false);
        s.volumes[0].density = 0.5f;
        Tracer::Scene held = s;
        //stvarna ploha na dubini 10 (sredina kutije, koja je od 9 do 11 duz pogleda)
        held.holdout.width = held.holdout.height = 4;
        held.holdout.srgb = false; held.holdout.repeat = false;
        held.holdout.floats.assign(4 * 4 * 4, 0.0f);
        for(int i = 0; i < 16; ++i) held.holdout.floats[size_t(i) * 4] = 10.0f;
        auto alpha = [](const Tracer::Frame& f){ double a = 0.0; for(size_t i = 0; i < f.pixelCount(); ++i) a += f.cg[i * 4 + 3]; return a / double(f.pixelCount()); };
        const double open = alpha(onCpu(s, settingsFor(256, 1))), cut = alpha(onCpu(held, settingsFor(256, 1)));
        //holdout uzima 2 % pricuve iza stvarne plohe: magla se broji do 10.2, kutija pocinje na 9
        const double expectOpen = 1.0 - std::exp(-0.5 * 2.0), expectCut = 1.0 - std::exp(-0.5 * (10.0 * 1.02 - 9.0));
        report.check("holdout: magla iza zida", std::abs(open - expectOpen) < 0.02 && std::abs(cut - expectCut) < 0.02,
                     fmt("pokrivenost bez %.3f (%.3f), s holdoutom %.3f (%.3f)", open, expectOpen, cut, expectCut));
    }

    {
        LoomConfig config;
        config.width = 64; config.height = 64; config.headless = true;
        config.appName = "test_tracer_volume"; config.engineName = "Loom tests";
        LoomInitializer loom(config);
        TracerGpu::Pipelines pipelines(loom);
        auto card = [&](Tracer::Scene s, const Tracer::RenderSettings& st){
            TracerGpu::GpuTracer tracer(loom, pipelines, Tracer::compile(std::move(s)), st);
            tracer.renderAll();
            return tracer.readFrame(false);
        };
        const double a = meanCg(card(absorber(), settingsFor(256, 4)), 0, 16);
        const double w = meanComposite(card(furnace(), settingsFor(512, 256)));
        const double g6 = meanCg(card(sunlit(0.6f, false), settingsFor(1024, 1)), 0, 16);
        const Tracer::Frame r = card(sunlit(0.0f, true), settingsFor(1024, 1));
        const double hAcross = meanCg(card(heightAbsorber(false), settingsFor(64, 4)), 0, 16);
        const double hWhite = meanComposite(card(heightFurnace(), settingsFor(512, 256)));
        const double hSingle = meanCg(card(heightSunlit(0.5f, -0.3f, 0.4f), settingsFor(1024, 1)), 0, 16);
        report.check("kartica: magla po visini", std::abs(hAcross / acrossTruth - 1.0) < 0.01 && std::abs(hWhite - 1.0) < 0.015 &&
                     std::abs(hSingle / heightSingle(0.5f, -0.3f, 0.4f) - 1.0) < 0.02,
                     fmt("vodoravno %.4f, pec %.4f, rasprsenje %.5f", hAcross, hWhite, hSingle));
        report.check("kartica", std::abs(a - absorbed) < 0.01 && std::abs(w - 1.0) < 0.01 && std::abs(g6 / single(0.6f) - 1.0) < 0.02 &&
                     meanCg(r, 8, 14) < 0.03 * meanCg(r, 18, 25),
                     fmt("upijanje %.4f, pec %.4f, g 0.6 %.5f, sjena/svjetlo %.5f/%.5f", a, w, g6, meanCg(r, 8, 14), meanCg(r, 18, 25)));
        report.checkNoValidationMessages();
    }

    //-- most i USD ----------------------------------------------------------------------------------
    {
        Warp::Stage stage;
        const Warp::Id camera = stage.create("Kamera");
        Warp::Camera lens; lens.width = 32; lens.height = 16; lens.focalPixels = 40.0f; lens.centreX = 16.0f; lens.centreY = 8.0f;
        stage.get(camera)->camera = lens;
        const Warp::Id group = stage.create("Grupa");
        stage.get(group)->local.scale = glm::vec3(2.0f);
        const Warp::Id fog = stage.create("Magla", group);
        Warp::Volume v; v.color = glm::vec3(0.9f, 0.8f, 0.7f); v.density = 0.3f; v.anisotropy = 0.4f;
        v.anisotropy2 = -0.2f; v.lobeMix = 0.3f; v.edge = 0.1f; v.noise = 0.2f; v.noiseScale = 0.5f;
        stage.get(fog)->volume = v;
        const Warp::Id haze = stage.create("Izmaglica");
        Warp::Volume h; h.shape = Warp::Volume::Shape::Height; h.density = 0.05f; h.height = 3.0f;
        stage.get(haze)->volume = h;
        stage.get(fog)->local.translation = glm::vec3(0.0f, 0.0f, -3.0f);
        stage.get(fog)->local.scale = glm::vec3(1.0f, 2.0f, 3.0f);
        Loom::RenderOptions options; options.plate = false;
        Loom::RenderAssets assets;
        Loom::BuiltScene built;
        std::string error;
        const bool ok = Loom::buildTracerScene(stage, 1.0, options, assets, built, error);
        const bool bridged = ok && built.scene.volumes.size() == 2 && built.scene.volumes[1].shape == Tracer::Volume::Shape::Height &&
                             built.scene.volumes[1].height == 3.0f && built.scene.volumes[0].lobeMix == 0.3f &&
                             glm::length(glm::vec3(built.scene.volumes[0].toWorld[3]) - glm::vec3(0, 0, -6)) < 1e-5f &&
                             std::abs(glm::length(glm::vec3(built.scene.volumes[0].toWorld[2])) - 6.0f) < 1e-5f &&
                             built.scene.volumes[0].density == 0.3f && built.scene.volumes[0].anisotropy == 0.4f;
        report.check("most: kutija u svijetu", bridged, fmt("%s %zu volumena", error.c_str(), built.scene.volumes.size()));

        const std::string path = (std::filesystem::temp_directory_path() / "loom_volume.usda").string();
        Warp::Stage back;
        const bool io = Warp::saveProject(stage, path, error) && Warp::loadProject(path, back, error);
        const Warp::Entity* read = back.get(back.find("/Grupa/Magla"));
        const Warp::Entity* readHaze = back.get(back.find("/Izmaglica"));
        report.check("USD", io && read && read->volume && glm::length(read->volume->color - v.color) < 1e-5f &&
                            read->volume->density == 0.3f && read->volume->anisotropy == 0.4f && back.fingerprint() == stage.fingerprint() &&
                            readHaze && readHaze->volume && readHaze->volume->shape == Warp::Volume::Shape::Height,
                     fmt("%s", error.c_str()));
        std::filesystem::remove(path);
    }
    return report.result();
}
