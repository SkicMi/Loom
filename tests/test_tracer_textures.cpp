// LoomTracer: mipmape po stoscu zrake, na procesoru i na kartici.
//
//   DALEKO    sah od 1-tekselnih polja (crno/bijelo), 8 teksela po pikselu, pod jednolikim nebom
//             (Lambert: radijancija = albedo). Tocna vrijednost svakog piksela je 0.5 (linearno).
//             S mipmapama 4 uzorka daju 0.5 u svakom pikselu; bez njih piksel je 4 slucajna
//             teksela - sum ~0.25
//   BLIZU     teksel veci od piksela: mipmape ne smiju zamutiti (ista slika kao bez njih)
//   RAZINE    usrednjeno linearno: sRGB 0/255 sah na razini 1 je sRGB 188 (linearno 0.5), ne 128
//   KARTICA   iste razine i isti stozac
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <TracerGpu/GpuTracer.h>
#include <Tracer/Renderer.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace{

//flat: jednolika siva 0.5 (sRGB 188) umjesto saha - sum samog svjetla, pod kojim nista ne moze
Tracer::Scene checkerFloor(float cameraHeight, bool flat = false){
    Tracer::Scene scene;
    Tracer::Texture checker;
    checker.width = checker.height = 256;
    for(uint32_t y = 0; y < 256; ++y) for(uint32_t x = 0; x < 256; ++x){
        const uint8_t v = flat ? 188 : (((x + y) & 1) ? 255 : 0);
        checker.bytes.insert(checker.bytes.end(), {v, v, v, 255});
    }
    Tracer::Material floor; floor.baseColorTexture = int(scene.addTexture(checker)); floor.specular = 0.0f; floor.roughness = 1.0f;
    floor.baseColor = glm::vec3(1.0f);
    scene.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(4.0f)), scene.addMaterial(floor));
    scene.environment.color = glm::vec3(1.0f);
    Tracer::Camera c;
    c.cameraToWorld = glm::inverse(glm::lookAt(glm::vec3(0.0f, cameraHeight, 0.0f), glm::vec3(0.0f), glm::vec3(0, 0, -1)));
    c.width = c.height = 48;
    c.focalPixels = 32.0f * cameraHeight / 4.0f;       //pod od 4 jedinice = 32 piksela: 8 teksela po pikselu
    c.centre = glm::vec2(24.0f);
    scene.camera = c;
    return scene;
}

Tracer::RenderSettings settingsFor(uint32_t samples, bool mipmaps){
    Tracer::RenderSettings s;
    s.samples = samples;
    s.maxBounces = 1;
    s.mipmaps = mipmaps;
    return s;
}

//Srednja vrijednost i standardna devijacija zelenog po pikselima unutar poda (sredina kadra)
void stats(const Tracer::Frame& f, double& mean, double& deviation){
    double sum = 0.0, sum2 = 0.0;
    int n = 0;
    for(uint32_t y = 12; y < 36; ++y) for(uint32_t x = 12; x < 36; ++x){
        const double v = f.cg[(size_t(y) * f.width + x) * 4 + 1];
        sum += v; sum2 += v * v; ++n;
    }
    mean = sum / n;
    deviation = std::sqrt(std::max(0.0, sum2 / n - mean * mean));
}

Tracer::Frame onCpu(Tracer::Scene scene, const Tracer::RenderSettings& settings){
    Tracer::Renderer renderer(std::move(scene));
    renderer.render(settings);
    return renderer.frame(false);
}

double rmse(const Tracer::Frame& a, const Tracer::Frame& b){
    double sum = 0.0;
    for(size_t i = 0; i < a.cg.size() && i < b.cg.size(); ++i){ const double d = double(a.cg[i]) - double(b.cg[i]); sum += d * d; }
    return std::sqrt(sum / double(std::max<size_t>(1, a.cg.size())));
}

}

int main(){
    TestReport report("T4 mipmape");

    //-- razine ----------------------------------------------------------------------------------------
    {
        Tracer::Scene s = checkerFloor(10.0f);
        Tracer::Texture& t = s.textures[0];
        t.buildMips();
        const bool count = t.mips.size() == 8 && t.mips.back().width == 1;
        const uint8_t level1 = t.mips.empty() ? 0 : t.mips[0].bytes[0];
        const glm::vec4 top = t.sample(glm::vec2(0.3f, 0.6f), 8.0f);
        report.check("razine: linearno usrednjene", count && std::abs(int(level1) - 188) <= 1 && std::abs(top.g - 0.5f) < 0.01f,
                     fmt("%zu razina, razina 1 = %u (sRGB 188 = linearno 0.5), vrh %.3f", t.mips.size(), level1, top.g));
    }

    //-- daleko: bez titranja ----------------------------------------------------------------------------
    //Sum svjetla (4 uzorka neba) je pod kojim nista ne moze: jednolika siva ploha
    double meanMip, devMip, meanRaw, devRaw, meanFlat, devFlat;
    const Tracer::Frame farMip = onCpu(checkerFloor(10.0f), settingsFor(4, true));
    stats(farMip, meanMip, devMip);
    stats(onCpu(checkerFloor(10.0f), settingsFor(4, false)), meanRaw, devRaw);
    stats(onCpu(checkerFloor(10.0f, true), settingsFor(4, false)), meanFlat, devFlat);
    report.check("daleko: 0.5, sum samo od svjetla", std::abs(meanMip - 0.5) < 0.02 && devMip < 1.15 * devFlat + 0.002,
                 fmt("s mipmapama %.3f +- %.3f, jednolika siva %.3f +- %.3f", meanMip, devMip, meanFlat, devFlat));
    report.check("daleko: bez mipmapa titra", devRaw > 2.5 * devMip && std::abs(meanRaw - 0.5) < 0.03,
                 fmt("bez mipmapa %.3f +- %.3f", meanRaw, devRaw));

    //-- blizu: ostro -------------------------------------------------------------------------------------
    {
        //20x zarisna: pod od 4 jedinice je 640 piksela, teksel 2.5 piksela
        Tracer::Scene near = checkerFloor(10.0f);
        near.camera.focalPixels = 32.0f * 10.0f / 4.0f * 20.0f;
        Tracer::Scene nearRaw = near;
        const Tracer::Frame a = onCpu(std::move(near), settingsFor(64, true));
        const Tracer::Frame b = onCpu(std::move(nearRaw), settingsFor(64, false));
        double mean, deviation;
        stats(a, mean, deviation);
        report.check("blizu: ostro kao bez mipmapa", rmse(a, b) < 0.01 && deviation > 0.1,
                     fmt("RMSE %.4f, kontrast %.3f", rmse(a, b), deviation));
    }

    //-- kartica -------------------------------------------------------------------------------------------
    {
        LoomConfig config;
        config.width = 64;
        config.height = 64;
        config.headless = true;
        config.appName = "test_tracer_textures";
        config.engineName = "Loom tests";
        LoomInitializer loom(config);
        TracerGpu::Pipelines pipelines(loom);
        TracerGpu::GpuTracer tracer(loom, pipelines, Tracer::compile(checkerFloor(10.0f)), settingsFor(4, true));
        tracer.renderAll();
        const Tracer::Frame card = tracer.readFrame(false);
        double mean, deviation;
        stats(card, mean, deviation);
        report.check("kartica", std::abs(mean - meanMip) < 0.01 && deviation < 1.15 * devFlat + 0.002 && rmse(card, farMip) < 0.02,
                     fmt("%.3f +- %.3f, RMSE prema procesoru %.4f", mean, deviation, rmse(card, farMip)));
        report.checkNoValidationMessages();
    }
    return report.result();
}
