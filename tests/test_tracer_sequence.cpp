// Sekvenca bez ponovnog posla: BVH se osvjezi (refit) umjesto gradnje, a spremnici koji se ne
// mijenjaju ostaju na kartici (UploadCache).
//
//   REFIT      kugla od 261 tisuce trokuta se pomakne: osvjezeno stablo daje ISTU sliku kao novo
//              (isti pogoci), a osvjezavanje je visestruko brze od gradnje
//   PONOVNO    svakih rebuildEvery osvjezavanja stablo se gradi iznova; promjena topologije uvijek
//   KARTICA    drugi kadar s istom teksturom i drugom snimkom: tekstura se ne salje ponovno, a
//              slika je ista kao bez cachea (snimka je u svom spremniku kadra)
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <TracerGpu/GpuTracer.h>
#include <Tracer/Renderer.h>

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>

namespace{

Tracer::Scene sphere(float x, uint32_t segments = 512){
    Tracer::Scene s;
    Tracer::Material m; m.baseColor = glm::vec3(0.7f);
    s.addMesh(Tracer::uvSphere(1.0f, segments, segments / 2), glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)), s.addMaterial(m));
    s.environment.color = glm::vec3(1.0f);
    Tracer::Camera c;
    c.cameraToWorld = glm::inverse(glm::lookAt(glm::vec3(0, 0, 4), glm::vec3(0), glm::vec3(0, 1, 0)));
    c.width = 48; c.height = 32; c.focalPixels = 40.0f; c.centre = glm::vec2(24, 16);
    s.camera = c;
    return s;
}

Tracer::Frame render(std::shared_ptr<const Tracer::CompiledScene> scene){
    Tracer::Renderer r(std::move(scene));
    Tracer::RenderSettings st;
    st.samples = 8;
    r.render(st);
    return r.frame(false);
}

double rmse(const Tracer::Frame& a, const Tracer::Frame& b){
    double sum = 0.0;
    for(size_t i = 0; i < a.cg.size() && i < b.cg.size(); ++i){ const double d = double(a.cg[i]) - double(b.cg[i]); sum += d * d; }
    return std::sqrt(sum / double(std::max<size_t>(1, a.cg.size())));
}

}

int main(){
    TestReport report("T8 sekvenca: refit i spremnici na kartici");

    //-- refit ----------------------------------------------------------------------------------------
    {
        const auto first = Tracer::compile(sphere(0.0f));
        const auto startBuild = std::chrono::steady_clock::now();
        const auto rebuilt = Tracer::compile(sphere(0.3f));
        const double buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startBuild).count();
        const auto startRefit = std::chrono::steady_clock::now();
        const auto refitted = Tracer::compile(sphere(0.3f), first.get());
        const double refitSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startRefit).count();
        const double difference = rmse(render(rebuilt), render(refitted));
        report.check("refit: ista slika", refitted->refits == 1 && difference < 1e-6, fmt("RMSE %.2e, osvjezeno %u puta", difference, refitted->refits));
        report.check("refit: brze od gradnje", refitSeconds < 0.5 * buildSeconds,
                     fmt("%zu trokuta: gradnja %.3f s, cijeli compile s refitom %.3f s", rebuilt->world.triangles.size(), buildSeconds, refitSeconds));
    }

    //-- ponovna gradnja --------------------------------------------------------------------------------
    {
        std::shared_ptr<const Tracer::CompiledScene> previous = Tracer::compile(sphere(0.0f, 32));
        uint32_t maxRefits = 0;
        bool rebuiltOnSchedule = false;
        for(int frame = 1; frame <= 10; ++frame){
            auto next = Tracer::compile(sphere(0.05f * float(frame), 32), previous.get(), 8);
            maxRefits = std::max(maxRefits, next->refits);
            if(frame == 8) rebuiltOnSchedule = next->refits == 0;
            previous = next;
        }
        const auto changed = Tracer::compile(sphere(0.0f, 48), previous.get(), 8);
        report.check("gradi iznova svakih 8 i na promjenu", maxRefits == 7 && rebuiltOnSchedule && changed->refits == 0,
                     fmt("najvise %u osvjezavanja, 8. kadar gradjen %d, druga topologija gradjena %d", maxRefits, int(rebuiltOnSchedule), int(changed->refits == 0)));
    }

    //-- kartica ----------------------------------------------------------------------------------------
    {
        LoomConfig config;
        config.width = 64; config.height = 64; config.headless = true;
        config.appName = "test_tracer_sequence"; config.engineName = "Loom tests";
        LoomInitializer loom(config);
        TracerGpu::Pipelines pipelines(loom);
        auto frameScene = [&](int frame){
            Tracer::Scene s = sphere(0.0f, 64);
            Tracer::Texture texture;
            texture.width = texture.height = 512;
            for(uint32_t i = 0; i < 512 * 512; ++i) texture.bytes.insert(texture.bytes.end(), {uint8_t(i), uint8_t(i >> 8), 90, 255});
            s.materials[0].baseColorTexture = int(s.addTexture(texture));
            Tracer::Texture plate;
            plate.width = 48; plate.height = 32;
            plate.bytes.assign(48 * 32 * 4, uint8_t(40 * frame));
            plate.repeat = false;
            s.backplate = plate;
            return s;
        };
        Tracer::RenderSettings st;
        st.samples = 4;
        TracerGpu::UploadCache cache;
        {
            TracerGpu::GpuTracer first(loom, pipelines, Tracer::compile(frameScene(1)), st, true, &cache);
            first.renderAll();
        }
        const uint64_t uploadedFirst = cache.uploadedBytes;
        TracerGpu::GpuTracer second(loom, pipelines, Tracer::compile(frameScene(2)), st, true, &cache);
        second.renderAll();
        const Tracer::Frame cached = second.readFrame(false);
        TracerGpu::GpuTracer fresh(loom, pipelines, Tracer::compile(frameScene(2)), st);
        fresh.renderAll();
        const Tracer::Frame plain = fresh.readFrame(false);
        const uint64_t textureBytes = 512ull * 512ull * 4ull;
        report.check("kartica: tekstura ostaje, slika ista", cache.reusedBytes >= textureBytes && rmse(cached, plain) == 0.0,
                     fmt("prvi kadar poslao %.1f MB, drugi ponovno upotrijebio %.1f MB; RMSE prema bez cachea %.2e",
                         double(uploadedFirst) / 1e6, double(cache.reusedBytes) / 1e6, rmse(cached, plain)));
        report.checkNoValidationMessages();
    }
    return report.result();
}
