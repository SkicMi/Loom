// Filtar suma: Intel Open Image Denoise (ucitan pri pokretanju) prema vlastitom A-trousu.
//
//   ISTA SCENA      16 uzoraka po pikselu: sirovo, A-trous, OIDN - svaki prema referenci od
//                   4096 uzoraka (greska kao na zaslonu, x / (1 + x)). OIDN mora biti bolji od
//                   A-trousa, a A-trous od sirovog
//   KOLIKO UZORAKA  koliko uzoraka treba sirovi render da dosegne gresku OIDN-a na 16
//   SVJETLINA       filtar ne smije promijeniti srednju svjetlinu (> 1 %)
//   PADANJE NATRAG  Denoiser::ATrous uvijek radi, i kad OIDN-a nema
//
// Bez biblioteke (tools/oidn/fetch.sh) OIDN provjere se preskoce s porukom - osim kad je
// LOOM_REQUIRE_OIDN postavljen, tada je to greska.
#include "TestHarness.h"

#include <Tracer/Denoise.h>
#include <Tracer/Renderer.h>

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>

namespace{

Tracer::Scene mixed(){
    Tracer::Scene scene;
    Tracer::Texture checker;
    checker.width = checker.height = 8;
    for(uint32_t y = 0; y < 8; ++y) for(uint32_t x = 0; x < 8; ++x){
        const uint8_t v = ((x + y) & 1) ? 220 : 60;
        checker.bytes.insert(checker.bytes.end(), {v, uint8_t(v / 2), 40, 255});
    }
    Tracer::Material floor; floor.baseColorTexture = int(scene.addTexture(checker)); floor.roughness = 0.7f;
    scene.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(8.0f)), scene.addMaterial(floor));
    Tracer::Material ball; ball.baseColor = glm::vec3(0.9f, 0.6f, 0.3f); ball.metallic = 1.0f; ball.roughness = 0.3f;
    scene.addMesh(Tracer::uvSphere(0.5f, 48, 24), glm::translate(glm::mat4(1.0f), glm::vec3(-0.6f, 0.5f, 0.0f)), scene.addMaterial(ball));
    Tracer::Material matte; matte.baseColor = glm::vec3(0.2f, 0.5f, 0.8f);
    scene.addMesh(Tracer::unitCube(), glm::translate(glm::mat4(1.0f), glm::vec3(0.7f, 0.4f, 0.3f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.8f)),
                  scene.addMaterial(matte));
    scene.environment.color = glm::vec3(0.4f, 0.5f, 0.7f);
    Tracer::Light sun; sun.direction = -glm::normalize(glm::vec3(0.4f, 0.5f, 0.3f)); sun.intensity = 3.0f; sun.angle = glm::radians(8.0f);
    scene.lights.push_back(sun);
    Tracer::Camera c;
    c.cameraToWorld = glm::inverse(glm::lookAt(glm::vec3(0.3f, 1.4f, 3.2f), glm::vec3(0, 0.3f, 0), glm::vec3(0, 1, 0)));
    c.width = 128; c.height = 96; c.focalPixels = 120.0f; c.centre = glm::vec2(64, 48);
    scene.camera = c;
    return scene;
}

Tracer::Frame render(uint32_t samples, uint32_t seed){
    Tracer::Renderer r(mixed());
    Tracer::RenderSettings st;
    st.samples = samples;
    st.seed = seed;
    r.render(st);
    return r.frame(false);
}

double error(const Tracer::Frame& a, const Tracer::Frame& b){
    double sum = 0.0;
    for(size_t i = 0; i < a.pixelCount(); ++i) for(int k = 0; k < 3; ++k){
        const double x = std::max(0.0, double(a.cg[i * 4 + size_t(k)])), y = std::max(0.0, double(b.cg[i * 4 + size_t(k)]));
        const double d = x / (1.0 + x) - y / (1.0 + y);
        sum += d * d;
    }
    return std::sqrt(sum / double(3 * a.pixelCount()));
}

double mean(const Tracer::Frame& f){
    double s = 0.0;
    for(size_t i = 0; i < f.pixelCount(); ++i) s += f.cg[i * 4] + f.cg[i * 4 + 1] + f.cg[i * 4 + 2];
    return s / double(f.pixelCount());
}

}

int main(){
    TestReport report("T7 OIDN");
    const Tracer::Frame reference = render(4096, 99);
    const Tracer::Frame raw = render(16, 0);
    Tracer::Frame atrous = raw;
    Tracer::denoiseFrame(atrous, Tracer::Denoiser::ATrous);
    const double eRaw = error(raw, reference), eAtrous = error(atrous, reference);
    report.check("A-trous bolji od sirovog", eAtrous < eRaw, fmt("%.4f prema %.4f", eAtrous, eRaw));

    std::string where;
    const bool available = Tracer::oidnAvailable(&where);
    const bool required = std::getenv("LOOM_REQUIRE_OIDN") != nullptr;
    if(!available){
        std::printf("        OIDN nije ucitan (%s) - provjere preskocene\n", where.c_str());
        report.check("OIDN ucitan", !required, where);
        return report.result();
    }
    std::printf("        OIDN: %s\n", where.c_str());
    Tracer::Frame denoised = raw;
    const auto start = std::chrono::steady_clock::now();
    Tracer::denoiseFrame(denoised, Tracer::Denoiser::Oidn);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double eOidn = error(denoised, reference);
    report.check("OIDN bolji od A-trousa (16)", eOidn < eAtrous, fmt("%.4f prema %.4f (sirovo %.4f), %.3f s na 128x96", eOidn, eAtrous, eRaw, seconds));
    //Na 4 uzorka A-trous ostavlja mrlje, mreza ne
    {
        const Tracer::Frame few = render(4, 0);
        Tracer::Frame a = few, o = few;
        Tracer::denoiseFrame(a, Tracer::Denoiser::ATrous);
        Tracer::denoiseFrame(o, Tracer::Denoiser::Oidn);
        const double ea = error(a, reference), eo = error(o, reference);
        report.check("OIDN bolji od A-trousa (4)", eo < 0.85 * ea, fmt("%.4f prema %.4f (sirovo %.4f)", eo, ea, error(few, reference)));
    }
    const double drift = mean(denoised) / mean(raw) - 1.0;
    report.check("OIDN cuva svjetlinu", std::abs(drift) < 0.01, fmt("%+.2f %%", 100.0 * drift));
    //Koliko sirovih uzoraka za gresku OIDN-a na 16
    uint32_t equal = 16;
    for(uint32_t s = 32; s <= 1024; s *= 2){
        equal = s;
        if(error(render(s, 1), reference) <= eOidn) break;
    }
    report.check("OIDN na 16 = sirovo na vise", equal >= 64, fmt("sirovi render dosegne tu gresku tek na %u uzoraka", equal));
    return report.result();
}
