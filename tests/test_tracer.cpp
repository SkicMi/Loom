// LoomTracer: path tracer protiv odgovora koji se znaju bez njega.
//
// ZASTO OVAKO. Render koji "izgleda dobro" ne dokazuje nista - oko prihvati i sliku koja stvara
// deset posto energije, i onu kojoj sjena pada na krivi piksel. Zato svaka provjera ima broj koji
// se zna unaprijed, iz fizike ili geometrije:
//
//   BIJELA PEC       objekt u jednolikom nebu radijancije 1, bez ikakvog upijanja, mora vratiti
//                    tocno 1. Vise znaci da BSDF stvara energiju, manje da je gubi. Hrapavi metal
//                    bez nadoknade visestrukog rasprsenja pada ispod 0.8 - to je negativna kontrola
//   SUNCE NA PODU    Lambertova ploha albeda a pod suncem ozracenosti E ima radijanciju a*E/pi
//   KUGLA SVJETLA    tockasto svjetlo intenziteta I na visini h: a*I/(pi*h^2)
//   SVIJETLI KVADRAT faktor oblika tocke prema paralelnom pravokutniku (zatvoren oblik)
//   PROJEKCIJA       kugla na poznatom mjestu pada na piksel iz pinhole formule pogleda editora
//   DUBINA           ravnina na udaljenosti 3 ima dubinu 3
//   SHADOW CATCHER   pod izvan sjene ne mijenja snimku ni za bit; pod sjenom kocke je taman
//   DETERMINIZAM     ista slika s 1 i s 4 dretve
#include "TestHarness.h"

#include <Tracer/Bsdf.h>
#include <Tracer/Bvh.h>
#include <Tracer/Denoise.h>
#include <Tracer/Environment.h>
#include <Tracer/Renderer.h>
#include <Tracer/Sampler.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <random>

namespace{

using namespace Tracer;

//Kamera na `eye` koja gleda u `target`, pinhole u pikselima
Camera lookAt(const glm::vec3& eye, const glm::vec3& target, uint32_t width, uint32_t height, float focal,
              glm::vec2 centre = glm::vec2(-1.0f)){
    Camera c;
    const glm::vec3 up = std::abs(glm::normalize(target - eye).y) > 0.99f ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
    c.cameraToWorld = glm::inverse(glm::lookAt(eye, target, up));
    c.width = width;
    c.height = height;
    c.focalPixels = focal;
    c.centre = centre.x < 0.0f ? glm::vec2(width, height) * 0.5f : centre;
    return c;
}

//Prosjek boje (cg + nebo) po pikselima u kojima je objekt pokrio cijeli piksel
glm::vec3 coveredAverage(const Frame& f, float minimumCoverage = 0.999f){
    glm::dvec3 sum(0.0);
    size_t count = 0;
    for(size_t i = 0; i < f.pixelCount(); ++i){
        if(f.cg[i * 4 + 3] < minimumCoverage) continue;
        sum += glm::dvec3(f.cg[i * 4], f.cg[i * 4 + 1], f.cg[i * 4 + 2]);
        ++count;
    }
    return count ? glm::vec3(sum / double(count)) : glm::vec3(-1.0f);
}

Frame furnace(const Material& material, uint32_t samples){
    Scene scene;
    scene.environment.color = glm::vec3(1.0f);
    const uint32_t m = scene.addMaterial(material);
    scene.addMesh(uvSphere(1.0f, 96, 48), glm::mat4(1.0f), m, "kugla");
    //Kamera blizu, uski kut: cijeli kadar je kugla
    scene.camera = lookAt({0, 0, 4}, {0, 0, 0}, 24, 24, 120.0f);
    Renderer renderer(std::move(scene));
    RenderSettings settings;
    settings.samples = samples;
    settings.maxBounces = 64;
    settings.indirectClamp = 0.0f;
    renderer.render(settings);
    return renderer.frame();
}

double rmse(const Frame& a, const Frame& b){
    double sum = 0.0;
    size_t n = 0;
    for(size_t i = 0; i < a.pixelCount(); ++i){
        for(int k = 0; k < 3; ++k){
            const double d = double(a.cg[i * 4 + size_t(k)]) - double(b.cg[i * 4 + size_t(k)]);
            sum += d * d;
            ++n;
        }
    }
    return std::sqrt(sum / double(std::max<size_t>(1, n)));
}

}

int main(){
    TestReport report("T1 LoomTracer");

    //-- 1. BVH = gruba sila ---------------------------------------------------------------------
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
        std::vector<glm::vec3> positions;
        std::vector<Triangle> triangles;
        for(uint32_t i = 0; i < 3000; ++i){
            const glm::vec3 c(uni(rng) * 5.0f, uni(rng) * 5.0f, uni(rng) * 5.0f);
            for(int k = 0; k < 3; ++k) positions.push_back(c + glm::vec3(uni(rng), uni(rng), uni(rng)) * 0.4f);
            Triangle t;
            t.v[0] = i * 3; t.v[1] = i * 3 + 1; t.v[2] = i * 3 + 2;
            triangles.push_back(t);
        }
        Bvh bvh;
        const auto start = std::chrono::steady_clock::now();
        bvh.build(positions, triangles);
        const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        int mismatches = 0, hits = 0;
        for(int r = 0; r < 3000; ++r){
            Ray ray;
            ray.origin = glm::vec3(uni(rng), uni(rng), uni(rng)) * 8.0f;
            ray.direction = glm::normalize(glm::vec3(uni(rng), uni(rng), uni(rng)));
            Hit hit;
            Ray copy = ray;
            bvh.intersect(copy, hit);
            float best = std::numeric_limits<float>::infinity();
            uint32_t bestIndex = ~0u;
            for(uint32_t i = 0; i < triangles.size(); ++i){
                const glm::vec3 a = positions[i * 3], e1 = positions[i * 3 + 1] - a, e2 = positions[i * 3 + 2] - a;
                const glm::vec3 pv = glm::cross(ray.direction, e2);
                const float det = glm::dot(e1, pv);
                if(std::abs(det) < 1e-12f) continue;
                const glm::vec3 tv = ray.origin - a;
                const float u = glm::dot(tv, pv) / det;
                const glm::vec3 qv = glm::cross(tv, e1);
                const float v = glm::dot(ray.direction, qv) / det;
                const float t = glm::dot(e2, qv) / det;
                if(u < 0 || v < 0 || u + v > 1 || t <= 0) continue;
                if(t < best){ best = t; bestIndex = i; }
            }
            if(bestIndex != ~0u) ++hits;
            if(hit.triangle != bestIndex || (bestIndex != ~0u && std::abs(hit.t - best) > 1e-4f)) ++mismatches;
            if(bvh.occluded(ray) != (bestIndex != ~0u)) ++mismatches;
        }
        report.check("bvh = gruba sila", mismatches == 0 && hits > 300,
                     fmt("%d razlika, %d pogodaka od 3000, %zu cvorova, dubina %d, %.1f ms", mismatches, hits,
                         bvh.nodeCount(), bvh.depth(), buildMs));
    }

    //-- 2. Sobol s Owenom: 256 uzoraka, po jedan u svakoj od 16x16 celija, za svaki par ------------
    {
        bool stratified = true;
        for(uint32_t seed : {1u, 99u, 12345u}){
            for(int pair = 0; pair < 4; ++pair){
                int cells[256] = {};
                for(uint32_t i = 0; i < 256; ++i){
                    Sampler s(seed, i);
                    glm::vec2 u(0.0f);
                    for(int k = 0; k <= pair; ++k) u = s.next2D();
                    ++cells[std::min(15, int(u.y * 16.0f)) * 16 + std::min(15, int(u.x * 16.0f))];
                }
                for(int c : cells) stratified = stratified && c == 1;
            }
        }
        report.check("sobol stratifikacija", stratified, "3 sjemena x 4 para dimenzija, 256/256 celija po jednom");
    }

    //-- 3. Energija GGX-a: bez nadoknade hrapavi metal gubi (negativna kontrola tablice) ---------------
    {
        const float rough = ggxAlbedo(0.5f, 1.0f), smooth = ggxAlbedo(0.9f, 0.05f);
        report.check("ggx gubi bez nadoknade", rough < 0.8f && smooth > 0.97f,
                     fmt("E(0.5, hrapavost 1) = %.3f, E(0.9, hrapavost 0.05) = %.3f", rough, smooth));
        const float a = ggxSchlickA(0.5f, 0.5f), b = ggxSchlickB(0.5f, 0.5f), e = ggxAlbedo(0.5f, 0.5f);
        report.check("schlick A + B = E", std::abs(a + b - e) < 1e-4f, fmt("%.5f + %.5f vs %.5f", a, b, e));
    }

    //-- 4. Nebo po vaznosti: gustoca je gustoca -----------------------------------------------------
    {
        Environment env;
        env.map = makeSky(glm::normalize(glm::vec3(0.3f, 0.5f, -0.4f)), 3.0f, 1.0f, glm::vec3(0.2f), 128, 64);
        env.rotation = 0.7f;
        EnvironmentSampler sampler;
        sampler.build(env);
        //Integral gustoce po sferi = 1 (uniformni smjerovi)
        double integral = 0.0;
        const int count = 200000;
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);
        for(int i = 0; i < count; ++i){
            const float z = 1.0f - 2.0f * uni(rng), r = std::sqrt(std::max(0.0f, 1.0f - z * z)), phi = 2.0f * glm::pi<float>() * uni(rng);
            integral += double(sampler.pdf(glm::vec3(r * std::cos(phi), z, r * std::sin(phi)))) * 4.0 * glm::pi<double>();
        }
        integral /= count;
        //Uzorkovanje vraca istu gustocu koju pdf() kaze za taj smjer
        float worst = 0.0f;
        for(int i = 0; i < 2000; ++i){
            glm::vec3 d;
            float pdf = 0.0f;
            sampler.sample(glm::vec2(uni(rng), uni(rng)), d, pdf);
            if(pdf > 0.0f) worst = std::max(worst, std::abs(pdf - sampler.pdf(d)) / pdf);
        }
        report.check("nebo: integral gustoce", std::abs(integral - 1.0) < 0.01, fmt("%.4f", integral));
        report.check("nebo: sample = pdf", worst < 1e-3f, fmt("najveca relativna razlika %.2e", double(worst)));
    }

    //-- 5. BIJELA PEC -------------------------------------------------------------------------------
    {
        struct Case{ const char* name; Material material; float tolerance; };
        std::vector<Case> cases;
        Material lambert; lambert.baseColor = glm::vec3(1.0f); lambert.specular = 0.0f;
        cases.push_back({"pec: lambert", lambert, 0.01f});
        Material plastic; plastic.baseColor = glm::vec3(1.0f); plastic.roughness = 0.4f;
        cases.push_back({"pec: plastika", plastic, 0.02f});
        Material metal; metal.baseColor = glm::vec3(1.0f); metal.metallic = 1.0f; metal.roughness = 1.0f;
        cases.push_back({"pec: hrapavi metal", metal, 0.02f});
        Material glass; glass.baseColor = glm::vec3(1.0f); glass.transmission = 1.0f; glass.roughness = 0.0f;
        cases.push_back({"pec: staklo", glass, 0.02f});
        Material coat = plastic; coat.clearcoat = 1.0f; coat.clearcoatRoughness = 0.1f;
        cases.push_back({"pec: lak na plastici", coat, 0.03f});
        for(const Case& c : cases){
            const auto start = std::chrono::steady_clock::now();
            const glm::vec3 v = coveredAverage(furnace(c.material, 256));
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const float worst = std::max({std::abs(v.r - 1.0f), std::abs(v.g - 1.0f), std::abs(v.b - 1.0f)});
            report.check(c.name, worst < c.tolerance, fmt("%.4f %.4f %.4f (dopusteno +-%.2f, %.1f s)", v.r, v.g, v.b, c.tolerance, s));
        }
    }

    //-- 6. Sunce na Lambertovom podu: a * E / pi ------------------------------------------------------
    {
        for(float angle : {0.0f, glm::radians(0.53f)}){
            Scene scene;
            Material floor; floor.baseColor = glm::vec3(0.5f); floor.specular = 0.0f;
            scene.addMesh(unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(20.0f)), scene.addMaterial(floor));
            Light sun;
            sun.type = Light::Type::Distant;
            sun.direction = {0, -1, 0};
            sun.intensity = 3.0f;
            sun.angle = angle;
            scene.lights.push_back(sun);
            scene.camera = lookAt({0, 5, 0.001f}, {0, 0, 0}, 16, 16, 60.0f);
            Renderer renderer(std::move(scene));
            RenderSettings settings;
            settings.samples = 64;
            renderer.render(settings);
            const glm::vec3 v = coveredAverage(renderer.frame());
            const float expected = 0.5f * 3.0f / glm::pi<float>();
            report.check(angle == 0.0f ? "sunce (tocka)" : "sunce (disk 0.53 st)", std::abs(v.g - expected) < expected * 0.004f,
                         fmt("%.5f, ocekivano %.5f", v.g, expected));
        }
    }

    //-- 7. Tockasto i kuglasto svjetlo: I / h^2 ------------------------------------------------------
    {
        for(float radius : {0.0f, 0.05f}){
            Scene scene;
            Material floor; floor.baseColor = glm::vec3(1.0f); floor.specular = 0.0f;
            scene.addMesh(unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(20.0f)), scene.addMaterial(floor));
            Light bulb;
            bulb.type = Light::Type::Sphere;
            bulb.position = {0, 2, 0};
            bulb.radius = radius;
            bulb.intensity = 8.0f;
            scene.lights.push_back(bulb);
            //Kamera gleda tocno ispod svjetla, pod kutem (kugla ne zaklanja pogled)
            scene.camera = lookAt({3, 1, 0}, {0, 0, 0}, 9, 9, 2000.0f);
            Renderer renderer(std::move(scene));
            RenderSettings settings;
            settings.samples = 256;
            renderer.render(settings);
            const Frame f = renderer.frame();
            const float v = f.cg[(4 * 9 + 4) * 4 + 1];
            //Mala kugla: ozracenost se od tocke razlikuje za (R/h)^2 red velicine - zanemarivo
            const float expected = 1.0f * 8.0f / (glm::pi<float>() * 4.0f);
            report.check(radius == 0.0f ? "tockasto svjetlo" : "kugla svjetla", std::abs(v - expected) < expected * 0.01f,
                         fmt("%.5f, ocekivano %.5f", v, expected));
        }
    }

    //-- 8. Svijetli kvadrat iznad poda: faktor oblika, preko MIS-a --------------------------------------
    {
        const float a = 1.0f, h = 1.0f;          //kvadrat stranice 2a na visini h
        Scene scene;
        Material floor; floor.baseColor = glm::vec3(1.0f); floor.specular = 0.0f;
        scene.addMesh(unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(0.02f)), scene.addMaterial(floor));
        Material lamp; lamp.baseColor = glm::vec3(0.0f); lamp.specular = 0.0f; lamp.emission = glm::vec3(1.0f);
        glm::mat4 lampWorld = glm::translate(glm::mat4(1.0f), glm::vec3(0, h, 0)) *
                              glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1, 0, 0)) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(2.0f * a));
        ObjectFlags hidden; hidden.cameraVisible = false;
        scene.addMesh(unitPlane(), lampWorld, scene.addMaterial(lamp), "lampa", hidden);
        scene.camera = lookAt({0.3f, 0.4f, 0.0f}, {0, 0, 0}, 5, 5, 4000.0f);
        Renderer renderer(std::move(scene));
        RenderSettings settings;
        settings.samples = 1024;
        renderer.render(settings);
        const Frame f = renderer.frame();
        const float v = f.cg[(2 * 5 + 2) * 4 + 1];
        //Faktor oblika tocke prema pravokutniku a x b s kutem iznad nje, na visini h
        auto corner = [](float x, float y){
            const float sx = std::sqrt(1.0f + x * x), sy = std::sqrt(1.0f + y * y);
            return (x / sx * std::atan(y / sx) + y / sy * std::atan(x / sy)) / (2.0f * glm::pi<float>());
        };
        const float expected = 4.0f * corner(a / h, a / h);        //radijancija = albedo * L * F
        report.check("svijetli kvadrat", std::abs(v - expected) < expected * 0.015f, fmt("%.5f, ocekivano %.5f", v, expected));
    }

    //-- 9. Projekcija: kugla pada na piksel iz pinhole formule, i s pomaknutom glavnom tockom ---------
    {
        Scene scene;
        Material white; white.emission = glm::vec3(1.0f); white.baseColor = glm::vec3(0.0f);
        const glm::vec3 where(0.7f, -0.3f, -0.2f);
        scene.addMesh(uvSphere(0.05f, 48, 24), glm::translate(glm::mat4(1.0f), where), scene.addMaterial(white));
        scene.camera = lookAt({1.5f, 0.8f, 3.0f}, {0, 0, 0}, 320, 180, 260.0f, glm::vec2(171.3f, 83.7f));
        glm::vec2 expected;
        scene.camera.project(where, expected);
        //Formula pogleda editora (LoomViewport.h): x = cx + f*X/-Z, y = cy - f*Y/-Z
        const glm::vec3 local = glm::vec3(glm::inverse(scene.camera.cameraToWorld) * glm::vec4(where, 1.0f));
        const glm::vec2 editor(171.3f + 260.0f * local.x / -local.z, 83.7f - 260.0f * local.y / -local.z);
        Renderer renderer(std::move(scene));
        RenderSettings settings;
        settings.samples = 64;
        renderer.render(settings);
        const Frame f = renderer.frame();
        glm::dvec2 centroid(0.0);
        double mass = 0.0;
        for(uint32_t y = 0; y < f.height; ++y) for(uint32_t x = 0; x < f.width; ++x){
            const float c = f.cg[(size_t(y) * f.width + x) * 4 + 3];
            centroid += glm::dvec2(x + 0.5, y + 0.5) * double(c);
            mass += c;
        }
        centroid /= std::max(mass, 1e-9);
        const float error = glm::length(glm::vec2(centroid) - expected);
        report.check("projekcija", error < 0.15f && glm::length(editor - expected) < 1e-3f,
                     fmt("teziste (%.2f, %.2f), formula (%.2f, %.2f), greska %.3f px", centroid.x, centroid.y,
                         expected.x, expected.y, error));
    }

    //-- 9b. Distorzija lece: kugla pada na distort(pinhole projekcije), i undistort(distort(p)) = p --
    {
        Scene scene;
        Material white; white.emission = glm::vec3(1.0f); white.baseColor = glm::vec3(0.0f);
        const glm::vec3 where(1.45f, 0.95f, -0.2f);
        scene.addMesh(uvSphere(0.04f, 48, 24), glm::translate(glm::mat4(1.0f), where), scene.addMaterial(white));
        scene.camera = lookAt({0.0f, 0.3f, 3.0f}, {0, 0.2f, 0}, 320, 180, 260.0f, glm::vec2(161.0f, 88.0f));
        scene.camera.lens = glm::vec4(255.0f, 255.0f, 158.0f, 91.0f);
        scene.camera.k1 = -0.18f;
        scene.camera.k2 = 0.03f;
        glm::vec2 expected;
        scene.camera.project(where, expected);
        const glm::vec3 local = glm::vec3(glm::inverse(scene.camera.cameraToWorld) * glm::vec4(where, 1.0f));
        const glm::vec2 pinhole(161.0f + 260.0f * local.x / -local.z, 88.0f - 260.0f * local.y / -local.z);
        float roundTrip = 0.0f;
        for(float x = 0.0f; x <= 320.0f; x += 32.0f) for(float y = 0.0f; y <= 180.0f; y += 30.0f)
            roundTrip = std::max(roundTrip, glm::length(scene.camera.undistortPixel(scene.camera.distortPixel({x, y})) - glm::vec2(x, y)));
        Renderer renderer(std::move(scene));
        RenderSettings settings;
        settings.samples = 64;
        renderer.render(settings);
        const Frame f = renderer.frame();
        glm::dvec2 centroid(0.0);
        double mass = 0.0;
        for(uint32_t y = 0; y < f.height; ++y) for(uint32_t x = 0; x < f.width; ++x){
            const float c = f.cg[(size_t(y) * f.width + x) * 4 + 3];
            centroid += glm::dvec2(x + 0.5, y + 0.5) * double(c);
            mass += c;
        }
        centroid /= std::max(mass, 1e-9);
        const float error = glm::length(glm::vec2(centroid) - expected);
        report.check("distorzija", error < 0.2f && glm::length(expected - pinhole) > 3.0f && roundTrip < 0.01f,
                     fmt("teziste (%.2f, %.2f), distort (%.2f, %.2f), pinhole bi bio (%.2f, %.2f); greska %.3f px, povratak %.4f px",
                         centroid.x, centroid.y, expected.x, expected.y, pinhole.x, pinhole.y, error, roundTrip));
    }

    //-- 10. Dubina i shadow catcher -----------------------------------------------------------------
    {
        Scene scene;
        Material grey; grey.baseColor = glm::vec3(0.5f);
        ObjectFlags catcher; catcher.shadowCatcher = true;
        scene.addMesh(unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(40.0f)), scene.addMaterial(grey), "pod", catcher);
        scene.addMesh(unitCube(), glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.5f, 0)), scene.addMaterial(grey), "kocka");
        Light sun; sun.direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f)); sun.intensity = 3.0f; sun.angle = 0.0f;
        scene.lights.push_back(sun);
        //Kamera okomito dolje s visine 6: pod je na dubini 6, vrh kocke na 5
        scene.camera = lookAt({0, 6, 0.0001f}, {0, 0, 0}, 64, 64, 64.0f);
        Renderer renderer(std::move(scene));
        RenderSettings settings;
        settings.samples = 32;
        renderer.render(settings);
        const Frame f = renderer.frame();
        const size_t corner = 2 * 64 + 2, centre = 32 * 64 + 32;
        report.check("dubina", std::abs(f.depth[corner] - 6.0f) < 1e-3f && std::abs(f.depth[centre] - 5.0f) < 1e-3f,
                     fmt("pod %.5f (6), kocka %.5f (5)", f.depth[corner], f.depth[centre]));
        //Kocka pokriva sredinu (alfa 1); pod nije CG (alfa 0), izvan sjene sjena je 1
        report.check("catcher: pokrivenost", f.cg[centre * 4 + 3] == 1.0f && f.cg[corner * 4 + 3] == 0.0f,
                     fmt("kocka %.2f, pod %.2f", f.cg[centre * 4 + 3], f.cg[corner * 4 + 3]));
        //Snimka: jednolika siva; izvan sjene ostaje bit po bit ista
        Texture plate;
        plate.width = plate.height = 4;
        plate.srgb = true;
        plate.bytes.assign(4 * 4 * 4, 140);
        const std::vector<float> comp = composite(f, Backdrop::Plate, &plate);
        const float plateLinear = srgbToLinear(140.0f / 255.0f);
        const float free = comp[corner * 4 + 1];
        report.check("catcher: snimka netaknuta", std::abs(free - plateLinear) < 1e-6f, fmt("%.6f vs %.6f", free, plateLinear));
        //Sunce ravno odozgo: sjena kocke je tocno ispod nje; kamera je kroz kocku ne vidi, pa se
        //mjeri s kamerom koja gleda koso - sjena na podu uz kocku
        Scene side;
        side.addMesh(unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(40.0f)), side.addMaterial(grey), "pod", catcher);
        side.addMesh(unitCube(), glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.5f, 0)), side.addMaterial(grey), "kocka");
        Light low; low.direction = glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f)); low.intensity = 3.0f; low.angle = 0.0f;
        side.lights.push_back(low);
        side.camera = lookAt({1.0f, 6, 0.0001f}, {1.0f, 0, 0}, 64, 64, 64.0f);
        Renderer sideRenderer(std::move(side));
        sideRenderer.render(settings);
        const Frame s = sideRenderer.frame();
        //Sjena pada na +X od kocke: tocka (1, 0, 0) je u sjeni, (-2, 0, 0) nije... ovdje (1.0, 0, 0) je sredina kadra
        glm::vec2 inShadow, lit;
        sideRenderer.scene().camera.project({1.2f, 0.0f, 0.0f}, inShadow);
        sideRenderer.scene().camera.project({1.2f, 0.0f, 2.0f}, lit);
        const float shaded = s.shadow[(size_t(inShadow.y) * 64 + size_t(inShadow.x)) * 3 + 1];
        const float open = s.shadow[(size_t(lit.y) * 64 + size_t(lit.x)) * 3 + 1];
        report.check("catcher: sjena", shaded < 0.02f && open > 0.999f, fmt("u sjeni %.4f, izvan %.4f", shaded, open));
    }

    //-- 11. Determinizam: 1 dretva = 4 dretve --------------------------------------------------------
    {
        auto build = []{
            Scene scene;
            Material m; m.baseColor = glm::vec3(0.7f, 0.4f, 0.2f); m.roughness = 0.3f;
            scene.addMesh(uvSphere(1.0f), glm::mat4(1.0f), scene.addMaterial(m));
            scene.environment.map = makeSky(glm::normalize(glm::vec3(0.2f, 0.6f, 0.3f)), 3.0f, 1.0f, glm::vec3(0.3f), 64, 32);
            scene.camera = lookAt({0, 0.5f, 3.5f}, {0, 0, 0}, 48, 32, 40.0f);
            return scene;
        };
        Renderer one(build()), four(build());
        RenderSettings settings;
        settings.samples = 8;
        settings.threads = 1;
        one.render(settings);
        settings.threads = 4;
        four.render(settings);
        const Frame a = one.frame(), b = four.frame();
        report.check("determinizam", a.cg == b.cg && a.background == b.background, "1 i 4 dretve, bit po bit");
    }

    //-- 12. Uklanjanje suma priblizi sliku referenci ----------------------------------------------------
    {
        auto build = []{
            Scene scene;
            Material floor; floor.baseColor = glm::vec3(0.6f);
            scene.addMesh(unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)), scene.addMaterial(floor));
            Material ball; ball.baseColor = glm::vec3(0.8f, 0.3f, 0.2f); ball.roughness = 0.4f;
            scene.addMesh(uvSphere(0.5f), glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.5f, 0)), scene.addMaterial(ball));
            scene.environment.map = makeSky(glm::normalize(glm::vec3(0.4f, 0.5f, 0.3f)), 3.0f, 1.0f, glm::vec3(0.2f), 128, 64);
            Light sun; sun.direction = -glm::normalize(glm::vec3(0.4f, 0.5f, 0.3f)); sun.intensity = 2.0f;
            scene.lights.push_back(sun);
            scene.camera = lookAt({0, 1.2f, 3.0f}, {0, 0.3f, 0}, 64, 48, 60.0f);
            return scene;
        };
        Renderer noisy(build()), reference(build());
        RenderSettings settings;
        settings.samples = 16;
        noisy.render(settings);
        settings.samples = 1024;
        const auto start = std::chrono::steady_clock::now();
        reference.render(settings);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const Frame truth = reference.frame();
        const double raw = rmse(noisy.frame(false), truth), clean = rmse(noisy.frame(true), truth);
        report.check("uklanjanje suma", clean < raw * 0.6, fmt("RMSE sirovo %.4f, filtrirano %.4f (referenca 1024 uzoraka, %.1f s)",
                                                                 raw, clean, seconds));
        const double pixelsPerSecond = 64.0 * 48.0 * 1024.0 / seconds;
        std::printf("        brzina: %.2f M uzoraka piksela/s\n", pixelsPerSecond / 1e6);
    }

    //-- 13. Alfa maska: list koji je proziran se ne vidi -----------------------------------------------
    {
        Scene scene;
        Texture holes;
        holes.width = holes.height = 2;
        holes.bytes = {255, 255, 255, 0,   255, 255, 255, 0,   255, 255, 255, 0,   255, 255, 255, 0};
        Material leaf; leaf.baseColorTexture = int(scene.addTexture(holes)); leaf.alphaMode = Material::Alpha::Mask;
        scene.addMesh(unitPlane(), glm::scale(glm::mat4(1.0f), glm::vec3(4.0f)), scene.addMaterial(leaf));
        scene.camera = lookAt({0, 3, 0.001f}, {0, 0, 0}, 8, 8, 8.0f);
        Renderer renderer(std::move(scene));
        RenderSettings settings;
        settings.samples = 4;
        renderer.render(settings);
        const Frame f = renderer.frame();
        float coverage = 0.0f;
        for(size_t i = 0; i < f.pixelCount(); ++i) coverage += f.cg[i * 4 + 3];
        report.check("alfa maska", coverage == 0.0f, fmt("pokrivenost %.2f", coverage));
    }

    return report.result();
}
