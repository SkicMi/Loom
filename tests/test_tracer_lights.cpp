// Mnogo svjetala: stablo svjetala i RIS prema izboru samo po snazi.
//
//   SCENA      256 tockastih svjetala (razlicite jakosti) nisko nad Lambertovim podom, jedno
//              odbijanje. Tocan odgovor po pikselu je poznat: L = a/pi * sum I cos / d^2
//   NEPRISTRANO  srednja slika na mnogo uzoraka = analiticka, za svaki nacin izbora
//   SUM        na 16 uzoraka greska prema analitickoj: po snazi > stablo > stablo + RIS (8)
//   KARTICA    stablo i RIS na kartici = procesor (ista greska)
//   MIS        ploha svijetli i BSDF-om: jedno veliko svjetlo i mnogo malih - bez pristranosti
//   RESTIR     kartica s ponovnom upotrebom rezervoara: srednja = analiticka, greska na 1 i 4
//              spp manja od RIS-a; s velikim svjetlom (MIS s BSDF-om) srednja = procesor
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <TracerGpu/GpuTracer.h>
#include <Tracer/Renderer.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace{

uint32_t Size = 48;
constexpr float Albedo = 0.6f;

Tracer::Scene field(bool withArea = false){
    Tracer::Scene s;
    Tracer::Material floor; floor.baseColor = glm::vec3(Albedo); floor.specular = 0.0f; floor.roughness = 1.0f;
    s.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(40.0f)), s.addMaterial(floor));
    uint32_t seed = 12345u;
    auto next = [&]{ seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.0f; };
    for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x){
        Tracer::Light l;
        l.type = Tracer::Light::Type::Sphere;
        l.position = glm::vec3(-7.5f + float(x), 0.25f + 0.3f * next(), -7.5f + float(y));
        l.radius = 0.0f;
        l.intensity = 0.05f + next() * next() * 2.0f;
        l.color = glm::vec3(1.0f);
        s.lights.push_back(l);
    }
    if(withArea){
        Tracer::Material glow; glow.baseColor = glm::vec3(0.0f); glow.emission = glm::vec3(1.0f); glow.emissionStrength = 2.0f;
        s.addMesh(Tracer::unitPlane(), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 0.0f)) *
                  glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(4.0f)),
                  s.addMaterial(glow));
    }
    Tracer::Camera c;
    c.cameraToWorld = glm::inverse(glm::lookAt(glm::vec3(0.0f, 12.0f, 0.001f), glm::vec3(0.0f), glm::vec3(0, 1, 0)));
    c.width = c.height = Size; c.focalPixels = 40.0f; c.centre = glm::vec2(Size * 0.5f);
    s.camera = c;
    return s;
}

//Analiticki: tocka poda ispod sredista piksela (kamera gleda ravno dolje, bez distorzije)
std::vector<float> analytic(const Tracer::Scene& s){
    std::vector<float> out(Size * Size);
    for(uint32_t y = 0; y < Size; ++y) for(uint32_t x = 0; x < Size; ++x){
        glm::vec3 o, d;
        s.camera.ray(glm::vec2(float(x) + 0.5f, float(y) + 0.5f), glm::vec2(0.5f), o, d);
        const glm::vec3 p = o + d * (-o.y / d.y);
        double e = 0.0;
        for(const Tracer::Light& l : s.lights){
            const glm::vec3 v = l.position - p;
            const double d2 = glm::dot(v, v);
            e += double(l.intensity) * double(v.y) / std::sqrt(d2) / d2;
        }
        out[size_t(y) * Size + x] = float(e * Albedo / glm::pi<double>());
    }
    return out;
}

Tracer::RenderSettings settingsFor(uint32_t samples, bool tree, uint32_t candidates){
    Tracer::RenderSettings st;
    st.samples = samples;
    st.maxBounces = 1;
    st.indirectClamp = 0.0f;
    st.lightTree = tree;
    st.lightCandidates = candidates;
    return st;
}

Tracer::Frame onCpu(Tracer::Scene s, const Tracer::RenderSettings& st){
    Tracer::Renderer r(std::move(s));
    r.render(st);
    return r.frame(false);
}

//Relativna greska prema analitickoj (zeleni kanal), sredisnji dio kadra (bez ruba filtra)
double error(const Tracer::Frame& f, const std::vector<float>& truth, double* bias = nullptr){
    double sum = 0.0, sumTruth = 0.0, sumImage = 0.0;
    int n = 0;
    for(uint32_t y = 4; y < Size - 4; ++y) for(uint32_t x = 4; x < Size - 4; ++x){
        const size_t i = size_t(y) * Size + x;
        const double d = double(f.cg[i * 4 + 1]) - truth[i];
        sum += d * d; sumTruth += truth[i]; sumImage += f.cg[i * 4 + 1]; ++n;
    }
    if(bias) *bias = sumImage / sumTruth - 1.0;
    return std::sqrt(sum / n) / (sumTruth / n);
}

}

int main(){
    TestReport report("T9 mnogo svjetala");
    const Tracer::Scene scene = field();
    const std::vector<float> truth = analytic(scene);

    double biasPower, biasTree, biasRis;
    error(onCpu(field(), settingsFor(512, false, 1)), truth, &biasPower);
    error(onCpu(field(), settingsFor(512, true, 1)), truth, &biasTree);
    error(onCpu(field(), settingsFor(512, true, 8)), truth, &biasRis);
    report.check("nepristrano", std::abs(biasPower) < 0.01 && std::abs(biasTree) < 0.01 && std::abs(biasRis) < 0.01,
                 fmt("srednja prema analitickoj: po snazi %+.2f %%, stablo %+.2f %%, stablo + RIS %+.2f %%",
                     100 * biasPower, 100 * biasTree, 100 * biasRis));

    const double ePower = error(onCpu(field(), settingsFor(16, false, 1)), truth);
    const double eTree = error(onCpu(field(), settingsFor(16, true, 1)), truth);
    const double eRis = error(onCpu(field(), settingsFor(16, true, 8)), truth);
    report.check("stablo manje suma od snage", eTree < 0.6 * ePower, fmt("16 spp: po snazi %.3f, stablo %.3f", ePower, eTree));
    report.check("RIS jos manje", eRis < 0.8 * eTree, fmt("stablo %.3f, stablo + RIS %.3f", eTree, eRis));
    //Koliko uzoraka treba izbor po snazi za gresku stabla s RIS-om na 16
    uint32_t equal = 16;
    for(uint32_t spp = 32; spp <= 2048; spp *= 2){
        equal = spp;
        if(error(onCpu(field(), settingsFor(spp, false, 1)), truth) <= eRis) break;
    }
    std::printf("        izbor po snazi dosegne gresku stabla + RIS (16 spp) tek na %u spp\n", equal);

    //Veliko svjetlo i mnogo malih: MIS preko stabla (pdf stabla i na BSDF pogotku)
    {
        const Tracer::Frame power = onCpu(field(true), settingsFor(1024, false, 1));
        const Tracer::Frame tree = onCpu(field(true), settingsFor(1024, true, 8));
        double a = 0.0, b = 0.0;
        for(size_t i = 0; i < power.pixelCount(); ++i){ a += power.cg[i * 4 + 1]; b += tree.cg[i * 4 + 1]; }
        report.check("MIS s velikim svjetlom", std::abs(b / a - 1.0) < 0.01, fmt("srednja: po snazi %.5f, stablo + RIS %.5f", a / double(power.pixelCount()), b / double(power.pixelCount())));
    }

    {
        LoomConfig config;
        config.width = 64; config.height = 64; config.headless = true;
        config.appName = "test_tracer_lights"; config.engineName = "Loom tests";
        LoomInitializer loom(config);
        TracerGpu::Pipelines pipelines(loom);
        TracerGpu::GpuTracer tracer(loom, pipelines, Tracer::compile(field()), settingsFor(16, true, 8));
        tracer.renderAll();
        double biasCard;
        const double eCard = error(tracer.readFrame(false), truth, &biasCard);
        TracerGpu::GpuTracer many(loom, pipelines, Tracer::compile(field()), settingsFor(512, true, 8));
        many.renderAll();
        error(many.readFrame(false), truth, &biasCard);
        report.check("kartica", std::abs(eCard / eRis - 1.0) < 0.15 && std::abs(biasCard) < 0.01,
                     fmt("16 spp: kartica %.3f, procesor %.3f; 512 spp srednja %+.2f %%", eCard, eRis, 100 * biasCard));

        auto withRestir = [](Tracer::RenderSettings st){ st.restirSamples = st.samples; return st; };
        TracerGpu::GpuTracer restir(loom, pipelines, Tracer::compile(field()), withRestir(settingsFor(16, true, 8)));
        restir.renderAll();
        const double eRestir = error(restir.readFrame(false), truth);
        TracerGpu::GpuTracer restirOne(loom, pipelines, Tracer::compile(field()), withRestir(settingsFor(1, true, 8)));
        restirOne.renderAll();
        TracerGpu::GpuTracer risOne(loom, pipelines, Tracer::compile(field()), settingsFor(1, true, 8));
        risOne.renderAll();
        const double eRestirOne = error(restirOne.readFrame(false), truth), eRisOne = error(risOne.readFrame(false), truth);
        TracerGpu::GpuTracer restirMany(loom, pipelines, Tracer::compile(field()), withRestir(settingsFor(512, true, 8)));
        restirMany.renderAll();
        double biasRestir;
        error(restirMany.readFrame(false), truth, &biasRestir);
        TracerGpu::GpuTracer restirFour(loom, pipelines, Tracer::compile(field()), withRestir(settingsFor(4, true, 8)));
        restirFour.renderAll();
        TracerGpu::GpuTracer risFour(loom, pipelines, Tracer::compile(field()), settingsFor(4, true, 8));
        risFour.renderAll();
        const double eRestirFour = error(restirFour.readFrame(false), truth), eRisFour = error(risFour.readFrame(false), truth);
        report.check("ReSTIR: nepristrano, manje suma na malo uzoraka",
                     std::abs(biasRestir) < 0.01 && eRestirOne < 0.8 * eRisOne && eRestirFour < 0.95 * eRisFour,
                     fmt("512 spp srednja %+.2f %%; greska 1 spp %.3f (RIS %.3f), 4 spp %.3f (RIS %.3f), 16 spp %.3f (RIS %.3f)",
                         100 * biasRestir, eRestirOne, eRisOne, eRestirFour, eRisFour, eRestir, eCard));
        TracerGpu::GpuTracer restirArea(loom, pipelines, Tracer::compile(field(true)), withRestir(settingsFor(1024, true, 8)));
        restirArea.renderAll();
        const Tracer::Frame areaCard = restirArea.readFrame(false);
        const Tracer::Frame areaCpu = onCpu(field(true), settingsFor(1024, true, 8));
        double a = 0.0, b = 0.0;
        for(size_t i = 0; i < areaCpu.pixelCount(); ++i){ a += areaCpu.cg[i * 4 + 1]; b += areaCard.cg[i * 4 + 1]; }
        report.check("ReSTIR: MIS s velikim svjetlom", std::abs(b / a - 1.0) < 0.01,
                     fmt("srednja: procesor %.5f, kartica ReSTIR %.5f", a / double(areaCpu.pixelCount()), b / double(areaCpu.pixelCount())));
        report.checkNoValidationMessages();
    }
    return report.result();
}
