// Svjetla kao entiteti scene (Warp::Light, UsdLux) kroz render, i svjetlo iz snimke (relight.py).
//
// Svaka provjera je broj koji se zna bez rendera:
//   SUNCE u sceni        Lambertov pod albeda a pod suncem ozracenosti E: a*E/pi - bez obzira na
//                        to kako je entitet sunca zakrenut i pod kojom je grupom
//   LAMPA (kugla)        a*I/(pi h^2) ispod nje
//   PRAVOKUTNIK          faktor oblika tocke prema paralelnom pravokutniku; ISTI pravokutnik
//                        okrenut nalicjem prema podu ne daje NISTA (jednostrana emisija)
//   KUPOLA (gradijent)   pod pod jednolikim nebom radijancije L vraca a*L (bijela pec za nebo)
//   USD                  svjetla kroz .usda i natrag, sva polja
//   RELIGHT              relight.py JSON postane sunce (smjer, jakost, boja) i kupola (gore/dolje)
#include "TestHarness.h"

#include "LoomRender.h"

#include <Warp/Project.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>

namespace{

//Scena: kamera okomito dolje na bijeli (ili sivi) pod, bez snimke
Warp::Stage floorScene(float albedo, glm::vec3 eye = {0.0f, 3.0f, 0.0f}){
    Warp::Stage stage;
    const Warp::Id camera = stage.create("Kamera");
    Warp::Camera lens;
    lens.width = 9; lens.height = 9; lens.focalPixels = 2000.0f; lens.centreX = 4.5f; lens.centreY = 4.5f;
    stage.get(camera)->camera = lens;
    stage.get(camera)->local.translation = eye;
    stage.get(camera)->local.rotation = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1, 0, 0));
    Warp::Material white;
    white.name = "pod";
    white.baseColor = glm::vec4(albedo, albedo, albedo, 1.0f);
    white.specular = 0.0f;
    const int m = stage.addMaterial(white);
    const Warp::Id floor = stage.create("Pod");
    stage.get(floor)->mesh = Warp::Mesh{Warp::Shape::Cube, glm::vec3(1.0f), m};   //kocka, ne ravnina: ravnina bi bila catcher
    stage.get(floor)->local.scale = glm::vec3(40.0f, 0.001f, 40.0f);
    return stage;
}

float centre(const Warp::Stage& stage, uint32_t samples = 256){
    Loom::RenderOptions options;
    options.sky = Loom::RenderOptions::Sky::Scene;
    options.plate = false;
    options.samples = samples;
    Loom::RenderAssets assets;
    Loom::BuiltScene built;
    std::string error;
    if(!Loom::buildTracerScene(stage, 1.0, options, assets, built, error)) return -1.0f;
    Tracer::Renderer renderer(std::move(built.scene));
    Tracer::RenderSettings settings;
    settings.samples = samples;
    renderer.render(settings);
    const Tracer::Frame f = renderer.frame(false);
    return f.cg[(4 * 9 + 4) * 4 + 1] + f.background[(4 * 9 + 4) * 3 + 1];
}

}

int main(){
    TestReport report("R2 svjetla scene");

    //-- sunce, zakrenuto pod zakrenutom grupom ------------------------------------------------------
    {
        Warp::Stage stage = floorScene(0.5f);
        const Warp::Id group = stage.create("Solve");
        stage.get(group)->local.rotation = glm::angleAxis(0.7f, glm::vec3(0, 1, 0));
        const Warp::Id sun = stage.create("Sunce", group);
        Warp::Light light;
        light.type = Warp::Light::Type::Distant;
        light.intensity = 3.0f;
        stage.get(sun)->light = light;
        //Lokalna -Z prema dolje: zakret -90 st oko X
        stage.get(sun)->local.rotation = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1, 0, 0));
        const float v = centre(stage, 32), expected = 0.5f * 3.0f / glm::pi<float>();
        report.check("sunce", std::abs(v - expected) < expected * 0.005f, fmt("%.5f, ocekivano %.5f", v, expected));
    }

    //-- lampa -----------------------------------------------------------------------------------------
    {
        Warp::Stage stage = floorScene(1.0f, {2.0f, 1.0f, 0.0f});
        stage.get(stage.find("/Kamera"))->local.rotation = glm::quat_cast(glm::inverse(glm::lookAt(glm::vec3(2, 1, 0), glm::vec3(0), glm::vec3(0, 1, 0))));
        const Warp::Id lamp = stage.create("Lampa");
        Warp::Light light;
        light.type = Warp::Light::Type::Sphere;
        light.intensity = 8.0f;
        light.radius = 0.02f;
        stage.get(lamp)->light = light;
        stage.get(lamp)->local.translation = glm::vec3(0, 2, 0);
        const float v = centre(stage), expected = 8.0f / (glm::pi<float>() * 4.0f);
        report.check("lampa", std::abs(v - expected) < expected * 0.01f, fmt("%.5f, ocekivano %.5f", v, expected));
    }

    //-- pravokutnik, licem i nalicjem --------------------------------------------------------------------
    {
        auto withPanel = [](bool facingFloor){
            Warp::Stage stage = floorScene(1.0f, {0.3f, 0.4f, 0.0f});
            stage.get(stage.find("/Kamera"))->local.rotation =
                glm::quat_cast(glm::inverse(glm::lookAt(glm::vec3(0.3f, 0.4f, 0.0f), glm::vec3(0), glm::vec3(0, 1, 0))));
            const Warp::Id panel = stage.create("Panel");
            Warp::Light light;
            light.type = Warp::Light::Type::Rect;
            light.width = light.height = 2.0f;
            light.intensity = 1.0f;
            stage.get(panel)->light = light;
            stage.get(panel)->local.translation = glm::vec3(0, 1, 0);
            //Svijetli niz lokalnu -Z; -90 st oko X je okrene dolje
            stage.get(panel)->local.rotation = glm::angleAxis((facingFloor ? -1.0f : 1.0f) * glm::half_pi<float>(), glm::vec3(1, 0, 0));
            return stage;
        };
        auto corner = [](float x, float y){
            const float sx = std::sqrt(1.0f + x * x), sy = std::sqrt(1.0f + y * y);
            return (x / sx * std::atan(y / sx) + y / sy * std::atan(x / sy)) / (2.0f * glm::pi<float>());
        };
        const float v = centre(withPanel(true), 1024), expected = 4.0f * corner(1.0f, 1.0f);
        const float back = centre(withPanel(false), 256);
        report.check("pravokutnik", std::abs(v - expected) < expected * 0.02f, fmt("%.5f, ocekivano %.5f", v, expected));
        report.check("pravokutnik nalicjem", back == 0.0f, fmt("%.6f (jednostrano: 0)", back));
    }

    //-- kupola: jednoliko nebo -------------------------------------------------------------------------
    {
        Warp::Stage stage = floorScene(0.5f);
        const Warp::Id sky = stage.create("Nebo");
        Warp::Light dome;
        dome.type = Warp::Light::Type::Dome;
        dome.skyTop = dome.skyBottom = glm::vec3(0.8f);
        dome.intensity = 1.5f;
        stage.get(sky)->light = dome;
        const float v = centre(stage, 256), expected = 0.5f * 0.8f * 1.5f;
        report.check("kupola", std::abs(v - expected) < expected * 0.01f, fmt("%.5f, ocekivano %.5f", v, expected));
    }

    //-- USD i natrag -------------------------------------------------------------------------------------
    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_render_lights";
    std::filesystem::create_directories(work);
    {
        Warp::Stage stage;
        const Warp::Id a = stage.create("Reflektor");
        Warp::Light spot;
        spot.type = Warp::Light::Type::Spot;
        spot.color = glm::vec3(1.0f, 0.8f, 0.6f);
        spot.intensity = 12.5f; spot.radius = 0.07f; spot.coneAngle = 30.0f; spot.coneSoftness = 0.3f;
        stage.get(a)->light = spot;
        const Warp::Id b = stage.create("Kupola");
        Warp::Light dome;
        dome.type = Warp::Light::Type::Dome;
        dome.texture = "/nebo/studio.hdr";
        dome.skyTop = glm::vec3(0.1f, 0.2f, 0.3f);
        stage.get(b)->light = dome;
        const Warp::Id c = stage.create("Panel");
        Warp::Light rect;
        rect.type = Warp::Light::Type::Rect;
        rect.width = 2.5f; rect.height = 0.5f;
        stage.get(c)->light = rect;
        std::string problem;
        Warp::Stage back;
        const std::string path = (work / "svjetla.usda").string();
        const bool ok = Warp::saveProject(stage, path, problem) && Warp::loadProject(path, back, problem);
        const Warp::Entity* sa = back.get(back.find("/Reflektor"));
        const Warp::Entity* sb = back.get(back.find("/Kupola"));
        const Warp::Entity* sc = back.get(back.find("/Panel"));
        const bool same = ok && sa && sa->light && sa->light->type == Warp::Light::Type::Spot && sa->light->intensity == 12.5f &&
                          sa->light->coneAngle == 30.0f && sa->light->coneSoftness == 0.3f && sa->light->color == spot.color &&
                          sb && sb->light && sb->light->type == Warp::Light::Type::Dome && sb->light->texture == dome.texture &&
                          sb->light->skyTop == dome.skyTop && sc && sc->light && sc->light->type == Warp::Light::Type::Rect &&
                          sc->light->width == 2.5f && sc->light->height == 0.5f && back.fingerprint() == stage.fingerprint();
        report.check("usd", same, problem);
    }

    //-- svjetlo iz snimke (relight.py) --------------------------------------------------------------------
    {
        const std::string json = (work / "splat_svjetlo.json").string();
        std::ofstream(json) << "{\n \"opis\": \"...\",\n \"gore\": [\n  0.0,\n  0.0,\n  1.0\n ],\n"
                               " \"sunce_smjer_prema_svjetlu\": [0.6, 0.0, 0.8],\n \"sunce_boja\": [3.0, 2.5, 2.0],\n"
                               " \"nebo_gore\": [0.3, 0.4, 0.6],\n \"nebo_dolje\": [0.1, 0.09, 0.08]\n}\n";
        Warp::Stage stage;
        const Warp::Id group = stage.create("Solve");
        const Loom::RelightImport imported = Loom::importRelight(stage, json, group);
        const Warp::Entity* sun = stage.get(imported.sun);
        const Warp::Entity* sky = stage.get(imported.sky);
        bool ok = imported.problem.empty() && sun && sky && sun->parent == group && sun->light && sky->light;
        float directionError = 1.0f, strength = 0.0f;
        glm::vec3 up(0.0f);
        if(ok){
            //Svjetlo putuje niz lokalnu -Z: smjer prema suncu je lokalna +Z
            const glm::vec3 toward = glm::vec3(stage.worldMatrix(imported.sun, 1.0)[2]);
            directionError = glm::length(glm::normalize(toward) - glm::vec3(0.6f, 0.0f, 0.8f));
            strength = sun->light->intensity;
            up = glm::normalize(glm::vec3(stage.worldMatrix(imported.sky, 1.0)[1]));
        }
        const float expectedStrength = 0.2126f * 3.0f + 0.7152f * 2.5f + 0.0722f * 2.0f;
        report.check("svjetlo iz snimke", ok && directionError < 1e-4f && std::abs(strength - expectedStrength) < 1e-4f &&
                     glm::length(up - glm::vec3(0, 0, 1)) < 1e-4f && sky->light->skyTop == glm::vec3(0.3f, 0.4f, 0.6f),
                     fmt("smjer %.1e, jakost %.4f (%.4f), gore (%.2f %.2f %.2f) %s", double(directionError), strength,
                         expectedStrength, up.x, up.y, up.z, imported.problem.c_str()));
        //Pogresna datoteka se odbije s razlogom
        std::ofstream(work / "krivo.json") << "{\"nesto\": 1}";
        Warp::Stage other;
        const Loom::RelightImport bad = Loom::importRelight(other, (work / "krivo.json").string(), Warp::None);
        report.check("svjetlo iz snimke: kriva datoteka", !bad.problem.empty() && other.size() == 0, bad.problem);
    }
    std::filesystem::remove_all(work);
    return report.result();
}
