// Vremenska stabilnost sekvence (Tracer::stabilize) poslije filtra suma.
//
//   MIRNO      ista kamera, 8 kadrova s razlicitim sumom (16 spp, ocisceno): titranje (vremenska
//              devijacija po pikselu) manje, greska prema referenci (256 spp) ne veca
//   KAMERA     kamera se pomice i okrece: povijest se prebaci na pravo mjesto - greska manja nego bez
//   GIBANJE    kutija putuje preko poda: u pikselima koje kutija mijenja nema duhova (greska ne veca)
#include "TestHarness.h"

#include <Tracer/Denoise.h>
#include <Tracer/Renderer.h>
#include <Tracer/Temporal.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace{

constexpr uint32_t W = 64, H = 48, Frames = 8;

Tracer::Scene sceneAt(uint32_t frame, bool moveCamera, bool moveBox){
    Tracer::Scene s;
    Tracer::Texture checker;
    checker.width = checker.height = 8;
    for(uint32_t y = 0; y < 8; ++y) for(uint32_t x = 0; x < 8; ++x){
        const uint8_t v = ((x + y) & 1) ? 200 : 70;
        checker.bytes.insert(checker.bytes.end(), {v, v, uint8_t(v * 3 / 4), 255});
    }
    Tracer::Material floor; floor.baseColorTexture = int(s.addTexture(checker)); floor.roughness = 0.8f;
    s.addMesh(Tracer::unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(8.0f)), s.addMaterial(floor));
    Tracer::Material red; red.baseColor = glm::vec3(0.7f, 0.2f, 0.15f);
    const float boxX = moveBox ? -1.2f + 0.3f * float(frame) : 0.0f;
    s.addMesh(Tracer::unitCube(), glm::translate(glm::mat4(1.0f), glm::vec3(boxX, 0.35f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.7f)),
              s.addMaterial(red));
    Tracer::Material blue; blue.baseColor = glm::vec3(0.2f, 0.3f, 0.7f); blue.roughness = 0.4f;
    s.addMesh(Tracer::uvSphere(0.4f, 32, 16), glm::translate(glm::mat4(1.0f), glm::vec3(1.1f, 0.4f, -0.8f)), s.addMaterial(blue));
    s.environment.color = glm::vec3(0.6f, 0.7f, 0.9f);
    Tracer::Light sun; sun.direction = -glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f)); sun.intensity = 2.5f; sun.angle = glm::radians(3.0f);
    s.lights.push_back(sun);
    const float t = moveCamera ? float(frame) : 0.0f;
    const glm::vec3 eye(-0.8f + 0.12f * t, 2.0f, 3.5f), target(0.05f * t, 0.2f, 0.0f);
    Tracer::Camera c;
    c.cameraToWorld = glm::inverse(glm::lookAt(eye, target, glm::vec3(0, 1, 0)));
    c.width = W; c.height = H; c.focalPixels = 60.0f; c.centre = glm::vec2(W, H) * 0.5f;
    s.camera = c;
    return s;
}

Tracer::Frame render(uint32_t frame, bool moveCamera, bool moveBox, uint32_t samples, uint32_t seed, bool denoise){
    Tracer::Renderer r(sceneAt(frame, moveCamera, moveBox));
    Tracer::RenderSettings st;
    st.samples = samples;
    st.seed = seed;
    r.render(st);
    return r.frame(denoise);
}

double lum(const Tracer::Frame& f, size_t i){ return 0.2126 * f.cg[i * 4] + 0.7152 * f.cg[i * 4 + 1] + 0.0722 * f.cg[i * 4 + 2]; }

struct Run{ std::vector<Tracer::Frame> plain, stable, truth; };

Run sequence(bool moveCamera, bool moveBox){
    Run run;
    Tracer::TemporalHistory history;
    for(uint32_t f = 0; f < Frames; ++f){
        Tracer::Frame noisy = render(f, moveCamera, moveBox, 16, 1000u + f * 7919u, true);
        run.plain.push_back(noisy);
        Tracer::stabilize(noisy, sceneAt(f, moveCamera, moveBox).camera, history, 0.5f);
        run.stable.push_back(noisy);
        run.truth.push_back(render(f, moveCamera, moveBox, 256, 77u, false));
    }
    return run;
}

//RMSE luminancije od 3. kadra (povijest se napunila); mask: samo ti pikseli
double error(const std::vector<Tracer::Frame>& frames, const std::vector<Tracer::Frame>& truth, const std::vector<uint8_t>* mask = nullptr){
    double sum = 0.0;
    size_t n = 0;
    for(uint32_t f = 3; f < Frames; ++f) for(size_t i = 0; i < frames[f].pixelCount(); ++i){
        if(mask && !(*mask)[(f * W * H) + i]) continue;
        const double d = lum(frames[f], i) - lum(truth[f], i);
        sum += d * d; ++n;
    }
    return n ? std::sqrt(sum / double(n)) : 0.0;
}

//Titranje: vremenska devijacija luminancije po pikselu (mirna kamera), srednja
double flicker(const std::vector<Tracer::Frame>& frames){
    double total = 0.0;
    const size_t n = frames[0].pixelCount();
    for(size_t i = 0; i < n; ++i){
        double s = 0.0, s2 = 0.0;
        for(uint32_t f = 3; f < Frames; ++f){ const double v = lum(frames[f], i); s += v; s2 += v * v; }
        const double m = s / (Frames - 3);
        total += std::sqrt(std::max(0.0, s2 / (Frames - 3) - m * m));
    }
    return total / double(n);
}

}

int main(){
    TestReport report("T24 vremenska stabilnost");
    std::string where;
    std::printf("        filtar: %s\n", Tracer::oidnAvailable(&where) ? ("OIDN (" + Tracer::oidnDevice() + ")").c_str() : "A-trous");

    {
        const Run still = sequence(false, false);
        const double before = flicker(still.plain), after = flicker(still.stable);
        const double ePlain = error(still.plain, still.truth), eStable = error(still.stable, still.truth);
        report.check("mirno: manje titranja", after < 0.7 * before && eStable <= ePlain * 1.02,
                     fmt("titranje %.5f -> %.5f (%.0f %%), greska %.5f -> %.5f", before, after, 100.0 * after / before, ePlain, eStable));
    }
    {
        const Run pan = sequence(true, false);
        const double ePlain = error(pan.plain, pan.truth), eStable = error(pan.stable, pan.truth);
        report.check("kamera se pomice: povijest na pravom mjestu", eStable < ePlain,
                     fmt("greska bez %.5f, sa stabilizacijom %.5f", ePlain, eStable));
    }
    {
        const Run moving = sequence(false, true);
        //Pikseli koje kutija mijenja: referenca ovog i proslog kadra se jako razlikuju
        std::vector<uint8_t> mask(Frames * W * H, 0);
        size_t changed = 0;
        for(uint32_t f = 3; f < Frames; ++f) for(size_t i = 0; i < W * H; ++i)
            if(std::abs(lum(moving.truth[f], i) - lum(moving.truth[f - 1], i)) > 0.1){ mask[f * W * H + i] = 1; ++changed; }
        const double ePlain = error(moving.plain, moving.truth, &mask), eStable = error(moving.stable, moving.truth, &mask);
        report.check("kutija se giba: bez duhova", changed > 100 && eStable <= ePlain * 1.05,
                     fmt("%zu promijenjenih piksela: greska bez %.5f, sa stabilizacijom %.5f", changed, ePlain, eStable));
    }
    return report.result();
}
