// Dubinska ostrina: tanka leca u traceru (procesor i kartica) i postavke rendera (f-broj, klik).
//
//   RUB        svijetla poluravnina (rub u stupcu 48) na dubini z, ostro na z_f. Krug nejasnoce
//              ima polumjer R = a f |1/z_f - 1/z| piksela; za jednoliki krug je povrsina izmedju
//              ruba i savrsene stepenice E|x| = 4R / (3 pi). Na z = z_f samo filtar piksela
//   OTVOR      zarisna 36 mm (kut 1000 px na 1000 px sirine, puni format), f/2: promjer 18 mm
//   KLIK       dubina pod klikom, medijan 5x5: na plohi njena dubina, na rubu ne skace na
//              pozadinu, na praznom -1
#include "TestHarness.h"

#include "LoomRender.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <TracerGpu/GpuTracer.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace{

constexpr uint32_t Width = 96, Height = 8;

//Svijetla poluravnina x > 0 na dubini z, kamera u ishodistu gleda niz -Z
Tracer::Scene edge(float depth, float aperture, float focus){
    Tracer::Scene scene;
    Tracer::Material light; light.baseColor = glm::vec3(0.0f); light.emission = glm::vec3(1.0f); light.emissionStrength = 1.0f;
    const float size = 40.0f * depth;
    scene.addMesh(Tracer::unitPlane(), glm::translate(glm::mat4(1.0f), glm::vec3(0.5f * size, 0.0f, -depth)) *
                                       glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(1, 0, 0)) *
                                       glm::scale(glm::mat4(1.0f), glm::vec3(size)), scene.addMaterial(light));
    Tracer::Camera c;
    c.width = Width; c.height = Height; c.focalPixels = 100.0f; c.centre = glm::vec2(48.0f, 4.0f);
    c.apertureRadius = aperture;
    c.focusDistance = focus;
    scene.camera = c;
    return scene;
}

Tracer::RenderSettings settings(){
    Tracer::RenderSettings s;
    s.samples = 1024;
    s.maxBounces = 0;
    return s;
}

//Povrsina izmedju slike ruba i savrsene stepenice (stupci >= 48 su 1), prosjek srednjih redaka
double spread(const Tracer::Frame& f){
    double sum = 0.0;
    for(uint32_t y = 2; y < 6; ++y)
        for(uint32_t x = 0; x < Width; ++x)
            sum += std::abs(double(f.cg[(size_t(y) * Width + x) * 4 + 1]) - (x >= 48 ? 1.0 : 0.0)) / 4.0;
    return sum;
}

Tracer::Frame onCpu(Tracer::Scene scene){
    Tracer::Renderer renderer(std::move(scene));
    renderer.render(settings());
    return renderer.frame(false);
}

}

int main(){
    TestReport report("T5 dubinska ostrina");

    //-- tanka leca -----------------------------------------------------------------------------------
    const double sharp = spread(onCpu(edge(2.0f, 0.4f, 2.0f)));
    const Tracer::Frame blurredFrame = onCpu(edge(4.0f, 0.4f, 2.0f));
    const double blurred = spread(blurredFrame);
    const double radius = 0.4 * 100.0 * (1.0 / 2.0 - 1.0 / 4.0);
    const double expected = 4.0 * radius / (3.0 * glm::pi<double>());
    report.check("u fokusu ostro", sharp < 0.3, fmt("%.3f px (samo filtar piksela)", sharp));
    report.check("izvan fokusa: krug R", std::abs(blurred - expected) < 0.08 * expected,
                 fmt("%.3f px, ocekivano %.3f (R = %.0f px)", blurred, expected, radius));
    //Ista udaljenost s druge strane fokusa: z = 4/3 daje isti R
    const double near = spread(onCpu(edge(4.0f / 3.0f, 0.4f, 2.0f)));
    report.check("ispred fokusa isto", std::abs(near - expected) < 0.08 * expected, fmt("%.3f px", near));

    {
        LoomConfig config;
        config.width = 64;
        config.height = 64;
        config.headless = true;
        config.appName = "test_tracer_dof";
        config.engineName = "Loom tests";
        LoomInitializer loom(config);
        TracerGpu::Pipelines pipelines(loom);
        TracerGpu::GpuTracer tracer(loom, pipelines, Tracer::compile(edge(4.0f, 0.4f, 2.0f)), settings());
        tracer.renderAll();
        const double card = spread(tracer.readFrame(false));
        report.check("kartica", std::abs(card - blurred) < 0.05 * blurred, fmt("%.3f px prema %.3f", card, blurred));
        report.checkNoValidationMessages();
    }

    //-- otvor iz f-broja -----------------------------------------------------------------------------
    {
        Warp::Camera lens;
        lens.width = 1000; lens.height = 500; lens.focalPixels = 1000.0f;
        const float r = Loom::apertureRadiusFor(lens, 2.0f, 36.0f);
        report.check("otvor f/2 na 36 mm", std::abs(r - 0.009f) < 1e-6f, fmt("polumjer %.4f m", r));

        Warp::Stage stage;
        const Warp::Id camera = stage.create("Kamera");
        stage.get(camera)->camera = lens;
        Loom::RenderOptions options;
        options.plate = false;
        options.depthOfField = true;
        options.fStop = 2.0f;
        options.focusDistance = 3.5f;
        Loom::RenderAssets assets;
        Loom::BuiltScene built;
        std::string error;
        const bool ok = Loom::buildTracerScene(stage, 1.0, options, assets, built, error);
        options.depthOfField = false;
        Loom::BuiltScene sharpBuilt;
        Loom::buildTracerScene(stage, 1.0, options, assets, sharpBuilt, error);
        report.check("render: otvor i fokus u kameri", ok && std::abs(built.scene.camera.apertureRadius - 0.009f) < 1e-6f &&
                     built.scene.camera.focusDistance == 3.5f && sharpBuilt.scene.camera.apertureRadius == 0.0f,
                     fmt("%s polumjer %.4f, fokus %.2f", error.c_str(), built.scene.camera.apertureRadius, built.scene.camera.focusDistance));
    }

    //-- klik ------------------------------------------------------------------------------------------
    {
        const float onPlane = Loom::focusFromDepth(blurredFrame, 70.0f, 4.0f);
        const float atEdge = Loom::focusFromDepth(blurredFrame, 49.0f, 4.0f);
        const float empty = Loom::focusFromDepth(blurredFrame, 10.0f, 4.0f);
        report.check("klik: dubina pod misem", std::abs(onPlane - 4.0f) < 1e-3f && std::abs(atEdge - 4.0f) < 1e-3f && empty < 0.0f,
                     fmt("ploha %.4f, rub %.4f, prazno %.1f", onPlane, atEdge, empty));
    }
    return report.result();
}
