// Alati editora bez prozora: ploha iz vucenja misa, kocka na njoj, mapa iz media prozora, model
// na podu ispod pogleda.
//
// ZASTO SE OVO TESTIRA. Racun plohe je provjeren u test_surface_select; ovdje se provjerava
// SPOJ - ono sto korisnik radi misem. Pravokutnik vucen unatrag (desno-dolje prema gore-lijevo)
// mora dati isti odabir, kratki klik ne smije obrisati plohu, a model iz glTF-a mora stajati NA
// podu (dno granica na y = 0), ne s ishodistem na podu i pola ispod njega.
#include "TestHarness.h"

#include "../src/LoomEditorTools.h"

#include <cmath>
#include <filesystem>
#include <fstream>

int main(){
    TestReport report("E5 alati editora");

    //Pod (y = 0, 4 x 4) s tockama, kamera odozgo-sprijeda
    Warp::Stage stage;
    const Warp::Id cloud = stage.create("Tocke");
    Warp::Points points;
    for(int i = 0; i < 4000; ++i){
        const float a = std::sin(float(i) * 12.9898f) * 43758.5453f, b = std::sin(float(i) * 78.233f) * 12345.678f;
        points.positions.push_back(glm::vec3((a - std::floor(a)) * 4.0f - 2.0f, 0.0f, (b - std::floor(b)) * 4.0f - 2.0f));
    }
    stage.get(cloud)->points = points;
    Loom::ViewportState state;
    state.orbit.target = glm::vec3(0.0f);
    state.orbit.distance = 5.0f;
    state.orbit.pitch = 0.7f;
    const Treadle::Rect viewport{100.0f, 50.0f, 800.0f, 600.0f};
    const Loom::ViewCamera camera = Loom::viewCameraFor(stage, 1.0, viewport, state);
    glm::vec2 centre;
    Loom::project(camera, glm::vec3(0.0f), centre);

    //-- 1. vucenje: odabir, ploha, kocka ----------------------------------------------------------
    {
        Loom::SurfaceTool tool;
        tool.active = true;
        //Unatrag: pritisak desno-dolje, pustanje gore-lijevo
        Loom::surfaceToolMouse(tool, true, false, centre + glm::vec2(60.0f, 40.0f), true, stage, 1.0, camera, {});
        Loom::surfaceToolMouse(tool, true, true, centre - glm::vec2(60.0f, 40.0f), true, stage, 1.0, camera, {});
        const bool consumed = Loom::surfaceToolMouse(tool, false, true, centre - glm::vec2(60.0f, 40.0f), true, stage, 1.0, camera, {});
        const float normalError = glm::degrees(std::acos(std::min(1.0f, tool.fit.normal.y)));
        report.check("pravokutnik vucen unatrag odabere pod i nadje mu normalu", consumed && tool.fit.valid &&
                     tool.selected.size() > 50 && normalError < 0.1f,
            fmt("%zu tocaka, normala promasi %.3f st", tool.selected.size(), normalError));

        //Kratki klik ne smije obrisati plohu
        const size_t before = tool.selected.size();
        Loom::surfaceToolMouse(tool, true, false, centre, true, stage, 1.0, camera, {});
        Loom::surfaceToolMouse(tool, false, true, centre + glm::vec2(1.0f, 1.0f), true, stage, 1.0, camera, {});
        report.check("klik bez vucenja ne brise plohu", tool.selected.size() == before && tool.fit.valid, "ostaje");

        const Warp::Id cube = Loom::placeOnSurface(stage, tool, Warp::Shape::Cube);
        const Warp::Entity* entity = stage.get(cube);
        const float size = entity ? entity->local.scale.x : 0.0f;
        report.check("kocka na plohi: dno na podu, velicina iz plohe", entity && entity->mesh &&
                     std::fabs(entity->local.translation.y - 0.5f * size) < 1e-4f && size > 0.05f,
            fmt("velicina %.3f, srediste na visini %.4f", size, entity ? entity->local.translation.y : -1.0f));
    }

    //-- 2. mapa iz media prozora u naoruzani slot ------------------------------------------------
    {
        Warp::Material m;
        m.name = "Drvo";
        const int index = stage.addMaterial(m);
        Loom::MaterialPanelState panel;
        const bool idle = !Loom::assignArmedImage(stage, panel, "/slike/drvo.png");
        panel.armedMaterial = index;
        panel.armedSlot = 2;
        const bool assigned = Loom::assignArmedImage(stage, panel, "/slike/drvo_normal.png");
        report.check("slika ide samo u naoruzanu mapu, i razoruza je", idle && assigned && !panel.armed() &&
                     stage.materials[size_t(index)].normalMap.source == "/slike/drvo_normal.png" &&
                     stage.materials[size_t(index)].baseColorMap.empty(),
            stage.materials[size_t(index)].normalMap.source);
    }

    //-- 3. model na podu ispod pogleda --------------------------------------------------------------
    {
        //Najmanji glTF: trokut od y = -0.5 do 1.5 (ishodiste nije na dnu), buffer kao data URI
        const float vertices[9] = {-0.5f, -0.5f, 0.0f,  0.5f, -0.5f, 0.0f,  0.0f, 1.5f, 0.0f};
        std::string base64;
        const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(vertices);
        for(size_t i = 0; i < 36; i += 3){
            const uint32_t n = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8) | bytes[i + 2];
            for(int k = 3; k >= 0; --k) base64 += alphabet[(n >> (6 * k)) & 63];
        }
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "loom_trokut.gltf";
        std::ofstream(path) << "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
                               "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
                               "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
                               "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"
                               "\"buffers\":[{\"byteLength\":36,\"uri\":\"data:application/octet-stream;base64," + base64 + "\"}]}";
        const Loom::SceneExtent extent = Loom::sceneExtent(stage, 1.0);
        const Loom::ModelImportReport imported = Loom::importModelAtView(stage, path, 1.0, camera, extent);
        const Warp::Entity* group = stage.get(imported.group);
        //Dno trokuta (y = -0.5 u modelu) mora pasti na pod
        const float scale = group ? group->local.scale.y : 0.0f;
        const float bottom = group ? group->local.translation.y + scale * -0.5f : -1.0f;
        const glm::vec3 eye = camera.eye;
        report.check("model stoji na podu, mjerilo iz visine kamere (1.5 m)", imported.problem.empty() && group &&
                     std::fabs(bottom) < 1e-4f && std::fabs(scale - eye.y / 1.5f) < 1e-4f && imported.meshes == 1,
            imported.problem.empty() ? fmt("dno na %.5f, mjerilo %.3f (kamera na %.3f)", bottom, scale, eye.y) : imported.problem);
        std::filesystem::remove(path);
    }

    return report.result();
}
