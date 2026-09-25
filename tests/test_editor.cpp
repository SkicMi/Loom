// Editor: raspored prozora i pogled - cisti racun, bez prozora.
//
// ZASTO SE OVO TESTIRA. Pogled kroz rijesenu kameru je MJERILO matchmovea: umjetnik gleda stoji
// li kocka na istom mjestu snimke. Ako pogled projicira i za pola piksela drukcije od solvera,
// kocka "pliva" iako je solve dobar - i trazi se greska ondje gdje je nema. Zato se projekcija
// pogleda usporedjuje s Engineovom, piksel za piksel, i to kroz grupu koja nosi rotaciju, pomak i
// MJERILO (umjetnik skalira scenu u metre upravo tako).
//
// Raspored se provjerava jer greska u njemu ne rusi nista: stupac koji prekrije pola pogleda je
// samo "ruzno", dok se ne pokusa kliknuti.
#include "TestHarness.h"

#include "../src/LoomEditor.h"
#include "../src/LoomPlate.h"
#include "../src/LoomViewport.h"

#include "Engine/SyntheticScene.h"

#include <cmath>

namespace{

float area(const Treadle::Rect& r){return r.width * r.height;}

bool overlaps(const Treadle::Rect& a, const Treadle::Rect& b){
    return a.x < b.x + b.width - 0.01f && b.x < a.x + a.width - 0.01f &&
           a.y < b.y + b.height - 0.01f && b.y < a.y + a.height - 0.01f;
}

}

int main(){
    TestReport report("E1 editor");

    //-- 1. raspored: bez preklapanja, pokriva prozor, pogled ostaje velik ----------------------
    {
        bool allGood = true;
        std::string detail;
        for(const glm::vec2 size : {glm::vec2(1600, 940), glm::vec2(1180, 760), glm::vec2(800, 600), glm::vec2(2560, 1440)}){
            const Loom::EditorLayout l = Loom::layoutEditor(size.x, size.y);
            const Treadle::Rect parts[] = {l.toolbar, l.media, l.viewport, l.hierarchy, l.properties, l.timeline};
            bool clean = true;
            float covered = 0.0f;
            for(int i = 0; i < 6; ++i){
                covered += area(parts[i]);
                for(int j = i + 1; j < 6; ++j) if(overlaps(parts[i], parts[j])) clean = false;
            }
            //Stupci su odvojeni namjernim razmakom od 6 px (layoutEditor, panelGap), pa nepokriven
            //smije ostati samo taj razmak po dva stupca - ne i rupa u rasporedu
            const float gaps = 2.0f * 6.0f * l.viewport.height;
            const bool full = covered <= size.x * size.y + 1.0f && size.x * size.y - covered <= gaps + 1.0f;
            const bool roomy = l.viewport.width >= size.x * 0.39f && l.viewport.height >= size.y * 0.5f;
            if(!(clean && full && roomy)) allGood = false;
            detail += fmt("%.0fx%.0f: pogled %.0fx%.0f; ", size.x, size.y, l.viewport.width, l.viewport.height);
        }
        report.check("stupci se ne preklapaju, pokriju prozor, pogled ostane velik", allGood, detail);
    }

    //-- 2. kroz kameru: isti piksel kao solver, kroz grupu s mjerilom ---------------------------
    Warp::Stage stage;
    const Warp::Id group = stage.create("Solve");
    stage.get(group)->local.rotation = glm::angleAxis(0.4f, glm::normalize(glm::vec3(0.3f, 1.0f, -0.2f)));
    stage.get(group)->local.translation = glm::vec3(1.5f, -0.7f, 2.0f);
    stage.get(group)->local.scale = glm::vec3(2.5f);
    const Warp::Id cameraId = stage.create("Kamera", group);
    Warp::Camera lens;
    lens.focalPixels = 1500.0f;
    lens.width = 1920; lens.height = 1080;
    lens.centreX = 971.0f; lens.centreY = 533.0f;       //glavna tocka namjerno nije u sredini
    stage.get(cameraId)->camera = lens;

    Engine::Pose pose;
    pose.position = glm::vec3(0.4f, 0.2f, 5.0f);
    pose.orientation = glm::normalize(glm::angleAxis(0.15f, glm::vec3(0, 1, 0)) * glm::angleAxis(-0.1f, glm::vec3(1, 0, 0)));
    stage.get(cameraId)->translationKeys.set(7.0, pose.position);
    stage.get(cameraId)->rotationKeys.set(7.0, pose.orientation);
    stage.get(cameraId)->translationKeys.set(8.0, pose.position + glm::vec3(0.1f, 0.0f, 0.0f));
    stage.get(cameraId)->rotationKeys.set(8.0, pose.orientation);

    Engine::Intrinsics intrinsics;
    intrinsics.fx = intrinsics.fy = lens.focalPixels;
    intrinsics.cx = lens.centreX; intrinsics.cy = lens.centreY;
    intrinsics.width = lens.width; intrinsics.height = lens.height;

    Loom::ViewportState state;
    state.lookThrough = cameraId;
    const Treadle::Rect viewportRect{320.0f, 40.0f, 960.0f, 710.0f};
    const Loom::ViewCamera through = Loom::viewCameraFor(stage, 7.0, viewportRect, state);
    {
        float worst = 0.0f;
        int compared = 0;
        const float scale = through.frame.width / float(lens.width);
        for(int i = 0; i < 50; ++i){
            //Tocka u koordinatama SOLVEA (unutar grupe), pa u svijet kroz grupu
            const glm::vec3 local(std::sin(i * 1.3f) * 1.5f, std::cos(i * 0.7f) * 0.8f, std::sin(i * 0.37f) * 1.2f);
            glm::vec2 expected;
            if(!Engine::project(pose, intrinsics, local, expected)) continue;
            const glm::vec3 world = glm::vec3(stage.worldMatrix(group, 7.0) * glm::vec4(local, 1.0f));
            glm::vec2 pixel;
            if(!Loom::project(through, world, pixel)) continue;
            const glm::vec2 inImage = (pixel - glm::vec2(through.frame.x, through.frame.y)) / scale;
            worst = std::max(worst, glm::length(inImage - expected));
            ++compared;
        }
        report.check("kroz kameru: isti piksel kao Engine, kroz grupu s mjerilom 2.5", compared > 30 && worst < 0.02f,
            fmt("%d tocaka, najveca razlika %.4f px snimke", compared, worst));

        const float aspect = through.frame.width / through.frame.height;
        report.check("kadar snimke upisan u pogled, omjer ocuvan",
            std::fabs(aspect - 1920.0f / 1080.0f) < 1e-3f && std::fabs(through.frame.width - 960.0f) < 0.01f &&
            through.frame.y > viewportRect.y,
            fmt("kadar %.0fx%.0f na y %.0f", through.frame.width, through.frame.height, through.frame.y));
    }

    //-- 3. crta se samo unutar kadra: letterbox i odsijecanje ----------------------------------
    {
        const Warp::Id cube = stage.create("Kocka");
        stage.get(cube)->mesh = Warp::Mesh{};
        stage.get(cube)->local.scale = glm::vec3(3.0f);
        const Warp::Id cloud = stage.create("Tocke", group);
        Warp::Points points;
        for(int i = 0; i < 4000; ++i){
            points.positions.push_back(glm::vec3(std::sin(i * 0.9f) * 20.0f, std::cos(i * 0.31f) * 10.0f, std::sin(i * 0.17f) * 20.0f));
        }
        stage.get(cloud)->points = points;

        Treadle::DrawList list;
        const Loom::SceneExtent extent = Loom::sceneExtent(stage, 7.0);
        const Loom::ViewportReport painted = Loom::paintStage(stage, 7.0, through, state, extent, cube, list);
        float worst = 0.0f;
        for(const Treadle::Vertex& v : list.vertices){
            const float outX = std::max(through.frame.x - v.x, v.x - (through.frame.x + through.frame.width));
            const float outY = std::max(through.frame.y - v.y, v.y - (through.frame.y + through.frame.height));
            worst = std::max(worst, std::max(outX, outY));
        }
        //Debljina crte smije prijeci rub za pola sebe: odsijeca se sredina duzine, ne njezin rub
        report.check("kroz kameru nista ne izlazi iz kadra", painted.drawnPoints > 0 && worst <= 2.0f,
            fmt("%u tocaka, najdalje %.2f px izvan kadra", painted.drawnPoints, worst));

        //Duzina koja prolazi iza kamere se odsijece na blizu ravninu, ne preskoci i ne prevrne
        Loom::ViewportState orbitState;
        orbitState.orbit.target = glm::vec3(0.0f);
        orbitState.orbit.distance = 5.0f;
        const Loom::ViewCamera orbit = Loom::viewCameraFor(stage, 7.0, viewportRect, orbitState);
        Treadle::DrawList line;
        const glm::vec3 behind = orbit.eye + (orbit.eye - orbitState.orbit.target) * 3.0f;
        Loom::segment(line, orbit, orbitState.orbit.target, behind, 1.0f, Treadle::Color{});
        bool inside = !line.vertices.empty();
        for(const Treadle::Vertex& v : line.vertices){
            if(!Treadle::Rect{viewportRect.x - 1.0f, viewportRect.y - 1.0f, viewportRect.width + 2.0f,
                              viewportRect.height + 2.0f}.contains(v.x, v.y)) inside = false;
        }
        report.check("duzina iza kamere se odsijece na blizu ravninu i na pogled", inside,
            fmt("%zu vrhova", line.vertices.size()));
    }

    //-- 4. odabir klikom -----------------------------------------------------------------------
    {
        Loom::ViewportState orbitState;
        orbitState.orbit.target = glm::vec3(0.0f);
        orbitState.orbit.distance = 12.0f;
        const Loom::ViewCamera orbit = Loom::viewCameraFor(stage, 7.0, viewportRect, orbitState);
        const Warp::Id cube = stage.find("/Kocka");
        glm::vec2 at;
        Loom::project(orbit, glm::vec3(0.0f), at);
        const Warp::Id hit = Loom::pickEntity(stage, 7.0, orbit, at + glm::vec2(5.0f, -4.0f));
        const Warp::Id miss = Loom::pickEntity(stage, 7.0, orbit, glm::vec2(viewportRect.x + 3.0f, viewportRect.y + 3.0f));
        report.check("klik na kocku je bira, klik u prazno ne bira nista", hit == cube && miss == Warp::None,
            fmt("pogodak %u (kocka %u), promasaj %u", hit, cube, miss));
    }

    //-- 5. strelice: os pod misem i pomak koliko je mis prosao --------------------------------
    {
        Loom::ViewportState orbitState;
        orbitState.orbit.target = glm::vec3(0.0f);
        orbitState.orbit.distance = 12.0f;
        const Loom::ViewCamera orbit = Loom::viewCameraFor(stage, 7.0, viewportRect, orbitState);
        const Loom::Gizmo gizmo = Loom::gizmoFor(orbit, glm::vec3(0.0f));
        glm::vec2 origin, tipX;
        Loom::project(orbit, glm::vec3(0.0f), origin);
        Loom::project(orbit, glm::vec3(gizmo.length, 0.0f, 0.0f), tipX);
        const float onScreen = glm::length(tipX - origin);
        const int axis = Loom::gizmoAxisAt(orbit, gizmo, origin + (tipX - origin) * 0.8f + glm::vec2(0.0f, 3.0f));
        const float moved = Loom::gizmoDrag(orbit, gizmo, 0, (tipX - origin) * 0.5f);
        //Okomito na os mis ne pomice nista
        const glm::vec2 across(-(tipX - origin).y, (tipX - origin).x);
        const float sideways = Loom::gizmoDrag(orbit, gizmo, 0, across);
        //90 px je duljina strelice OKOMITE na pogled; os gledana ukoso je skracena, nikad dulja
        report.check("strelica X: hvata se uz os, pomak pola strelice = pola duljine",
            axis == 0 && onScreen <= 90.5f && onScreen > 40.0f && std::fabs(moved - gizmo.length * 0.5f) < 1e-4f &&
            std::fabs(sideways) < 1e-4f,
            fmt("na ekranu %.1f px, pomak %.4f od %.4f, poprijeko %.1e", onScreen, moved, gizmo.length, sideways));
    }

    //-- 6. kocka sjeda na povrsinu pod pikselom, a odbjegla tocka ju ne povuce -----------------
    {
        Warp::Stage wall;
        const Warp::Id cloud = wall.create("Zid");
        Warp::Points points;
        for(int i = 0; i < 3000; ++i){
            points.positions.push_back(glm::vec3(std::sin(i * 0.37f) * 1.0f, std::cos(i * 0.91f) * 0.8f, -6.0f));
        }
        //Odbjegle tocke ispred zida, bas na zraci kroz sredinu
        for(int i = 0; i < 3; ++i) points.positions.push_back(glm::vec3(0.0f, 0.0f, -1.0f - 0.1f * i));
        wall.get(cloud)->points = points;

        Loom::ViewportState front;
        front.orbit.target = glm::vec3(0.0f, 0.0f, -6.0f);
        front.orbit.yaw = 0.0f; front.orbit.pitch = 0.0f; front.orbit.distance = 6.0f;
        const Loom::ViewCamera camera = Loom::viewCameraFor(wall, 1.0, viewportRect, front);
        glm::vec3 hit(0.0f);
        const bool found = Loom::surfaceAt(wall, 1.0, camera, camera.centre, hit);
        glm::vec3 none(0.0f);
        const bool empty = Loom::surfaceAt(wall, 1.0, camera, glm::vec2(viewportRect.x + 2.0f, viewportRect.y + 2.0f), none);
        report.check("nova kocka sjeda na zid pod pikselom, odbjegle tocke ju ne povuku",
            found && std::fabs(hit.z + 6.0f) < 1e-3f && glm::length(glm::vec2(hit.x, hit.y)) < 1e-3f && !empty,
            fmt("pogodak (%.3f, %.3f, %.3f); u kutu bez tocaka %s", hit.x, hit.y, hit.z, empty ? "nasao" : "nista"));
    }

    //-- 7. ploca se smanjuje prosjekom, ne preskakanjem ----------------------------------------
    {
        //4x2 slika, faktor 2: svaki izlazni piksel je prosjek kvadrata 2x2
        std::vector<uint8_t> source(4 * 2 * 4, 0);
        const uint8_t reds[8] = {0, 100, 200, 40, 20, 80, 0, 0};
        for(int i = 0; i < 8; ++i){ source[size_t(i) * 4] = reds[i]; source[size_t(i) * 4 + 3] = 255; }
        std::vector<uint8_t> out;
        uint32_t width = 0, height = 0;
        Loom::PlateStream::shrink(source, 4, 2, 2, out, width, height);
        //lijevi: (0 + 100 + 20 + 80) / 4 = 50; desni: (200 + 40 + 0 + 0) / 4 = 60
        report.check("ploca: smanjenje prosjekom kvadrata", width == 2 && height == 1 && out[0] == 50 && out[4] == 60 && out[3] == 255,
            fmt("%ux%u, crveno %u i %u (ocekivano 50 i 60)", width, height, out.size() > 4 ? out[0] : 0, out.size() > 4 ? out[4] : 0));
    }

    //-- 8. krugovi za rotaciju: kut u prostoru, ne na ekranu --------------------------------------
    //
    //Iz kosog pogleda krug je elipsa i kut na ekranu laze. Kut se zato mjeri u ravnini kruga:
    //mis prijedje s tocke kruga pod 20 st na tocku pod 65 st, i kocka se mora okrenuti tocno 45 st -
    //i odozgo i odozdo, gdje se smjer vrtnje na ekranu obrne
    {
        Warp::Stage empty;
        bool allExact = true;
        std::string detail;
        for(const float pitch : {0.6f, -0.7f}){
            Loom::ViewportState state;
            state.orbit.target = glm::vec3(0.3f, 0.2f, -0.1f);
            state.orbit.distance = 6.0f;
            state.orbit.yaw = 0.8f;
            state.orbit.pitch = pitch;
            const Loom::ViewCamera camera = Loom::viewCameraFor(empty, 1.0, viewportRect, state);
            const Loom::Gizmo gizmo = Loom::gizmoFor(camera, state.orbit.target);
            glm::vec2 from, to;
            Loom::project(camera, Loom::ringPoint(gizmo, 1, glm::radians(20.0f)), from);
            Loom::project(camera, Loom::ringPoint(gizmo, 1, glm::radians(65.0f)), to);
            const float angle = glm::degrees(Loom::ringDrag(camera, gizmo, 1, from, to));
            const int picked = Loom::ringAxisAt(camera, gizmo, from + glm::vec2(2.0f, -1.0f));
            //Na ekranu bi kut bio drukciji - to je ono sto se ovdje izbjegava
            glm::vec2 centre;
            Loom::project(camera, gizmo.origin, centre);
            const float screen = glm::degrees(std::fabs(std::atan2(to.y - centre.y, to.x - centre.x) -
                                                        std::atan2(from.y - centre.y, from.x - centre.x)));
            if(std::fabs(angle - 45.0f) > 0.01f || picked != 1) allExact = false;
            detail += fmt("nagib %.1f: %.3f st (na ekranu %.1f), uhvacen krug %d; ", pitch, angle, screen, picked);
        }
        report.check("krug Y: 45 st misa = 45 st rotacije odozgo i odozdo", allExact, detail);
    }

    return report.result();
}
