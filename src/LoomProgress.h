#pragma once
//=============================================================================================
// ZIVI PRIKAZ: sto je solver dosad nasao, nacrtano.
//
// VideoSolve povremeno zapise snimak (vidi LoomSnapshot.h), loom ga cita i crta ovdje - bez
// ijednog novog shadera, jer Treadleov DrawList ima pravokutnik i duzinu, a tocka na ekranu je
// mali pravokutnik.
//
// SCENA SE CRTA USPRAVNO. Rekonstrukcija ne zna sto je gore; njezin sustav je sustav prve kamere.
// Kad snimak nosi orijentacije, ovdje se iz njih izracuna isti uspravni sustav kao u VideoSolveu
// (Engine::uprightFrame), pa pod lezi vodoravno, mreza je na podu, a orbita se vrti oko okomice.
// Konacni snimak je vec uspravan pa je ondje taj korak prazan - ali tijekom rasta nije.
//
// PRIPREMA JEDNOM, CRTANJE SVAKI KADAR. Snimak stigne svakih pola sekunde; poravnanje, uzorkovanje
// i racun poda idu tada (PreparedScene). Po kadru se samo projicira - inace bi 265 000 tocaka puta
// 60 kadrova u sekundi bilo osjetno.
//
// SREDISTE, MJERILO I POD IDU PO POSTOTCIMA, ne po min/max. Rekonstrukcija redovito ima pokoju
// tocku trianguliranu iz gotovo paralelnih zraka; jedna takva na 1e5 skupila bi scenu u piksel
//=============================================================================================
#include "LoomSnapshot.h"

#include <Engine/Upright.h>
#include <Treadle/Draw.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Loom{

//Scena spremna za crtanje, u uspravnom sustavu
struct PreparedScene{
    std::vector<glm::vec3> points;           //uzorkovane, najvise maxDisplayPoints
    std::vector<glm::u8vec3> colours;        //prazno kad boje nema
    std::vector<glm::vec3> cameras;
    std::vector<glm::vec3> forwards;         //smjer pogleda po kameri, prazno kad orijentacije nema
    glm::vec3 centre{0.0f};
    float radius = 1.0f;
    float floorHeight = 0.0f;                //nizih 5 posto tocaka - ondje lezi mreza
    float lowHeight = 0.0f, highHeight = 1.0f;
    bool upright = false;                    //je li uspravni sustav stvarno primijenjen
    float tiltDegrees = 0.0f;
    uint32_t totalPoints = 0;
};

//Pogled. Orbita oko sredista scene; mis ga mijenja, a dok ga nitko ne dira, sam se okrece
struct ViewState{
    float yaw = 0.6f;              //radijani, oko okomice
    float pitch = 0.45f;           //radijani iznad obzora
    float zoom = 1.0f;             //mnozi udaljenost koja bi obuhvatila scenu
    bool autoRotate = true;

    bool showGrid = true;
    bool showAxes = true;
    bool showPath = true;
    bool showDirections = true;
};

struct PaintReport{
    uint32_t drawnPoints = 0;
};

constexpr uint32_t maxDisplayPoints = 100000;

namespace detail{

inline float percentile(std::vector<float> values, double fraction){
    if(values.empty()) return 0.0f;
    const size_t index = size_t(fraction * double(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + long(index), values.end());
    return values[index];
}

//Lijep razmak mreze: 1, 2 ili 5 puta potencija deset, oko petine polumjera
inline float niceSpacing(float radius){
    const float raw = std::max(1e-6f, radius / 5.0f);
    const float power = std::pow(10.0f, std::floor(std::log10(raw)));
    const float scaled = raw / power;
    const float step = scaled < 1.5f ? 1.0f : (scaled < 3.5f ? 2.0f : 5.0f);
    return step * power;
}

}

inline PreparedScene prepareScene(const Snapshot& snapshot){
    PreparedScene scene;
    scene.totalPoints = uint32_t(snapshot.points.size());
    if(snapshot.points.size() < 8) return scene;

    //Uzorkovanje ravnomjernim korakom - isti oblik, manje posla. Korak, a ne nasumicno, da se
    //ista scena uvijek jednako nacrta
    const size_t stride = std::max<size_t>(1, snapshot.points.size() / maxDisplayPoints + 1);
    const bool haveColours = snapshot.colours.size() == snapshot.points.size();
    for(size_t i = 0; i < snapshot.points.size(); i += stride){
        scene.points.push_back(snapshot.points[i]);
        if(haveColours) scene.colours.push_back(snapshot.colours[i]);
    }
    scene.cameras = snapshot.cameras;

    //USPRAVNO, istim racunom kao VideoSolve
    std::vector<Engine::Pose> poses;
    const bool haveOrientations = snapshot.orientations.size() == snapshot.cameras.size();
    if(haveOrientations){
        for(size_t i = 0; i < snapshot.cameras.size(); ++i){
            Engine::Pose pose;
            pose.position = snapshot.cameras[i];
            pose.orientation = snapshot.orientations[i];
            poses.push_back(pose);
        }
        const std::vector<uint8_t> posed(poses.size(), 1), solved(scene.points.size(), 1);
        const Engine::UprightFrame frame = Engine::uprightFrame(poses, posed, scene.points, solved);
        Engine::applyUpright(frame, poses, scene.points);
        scene.upright = frame.applied;
        scene.tiltDegrees = frame.tiltDegrees;

        scene.cameras.clear();
        for(const Engine::Pose& pose : poses){
            scene.cameras.push_back(pose.position);
            scene.forwards.push_back(pose.orientation * glm::vec3(0.0f, 0.0f, -1.0f));
        }
    }

    std::vector<float> xs, ys, zs;
    xs.reserve(scene.points.size()); ys.reserve(scene.points.size()); zs.reserve(scene.points.size());
    for(const glm::vec3& point : scene.points){ xs.push_back(point.x); ys.push_back(point.y); zs.push_back(point.z); }
    const glm::vec3 low(detail::percentile(xs, 0.05), detail::percentile(ys, 0.05), detail::percentile(zs, 0.05));
    const glm::vec3 high(detail::percentile(xs, 0.95), detail::percentile(ys, 0.95), detail::percentile(zs, 0.95));
    scene.centre = (low + high) * 0.5f;
    scene.radius = std::max(0.001f, glm::length(high - low) * 0.5f);
    scene.floorHeight = low.y;
    scene.lowHeight = low.y;
    scene.highHeight = std::max(high.y, low.y + 1e-3f);
    return scene;
}

inline PaintReport paintScene(const PreparedScene& scene, Treadle::DrawList& list,
                              float width, float height, const ViewState& view){
    PaintReport report;
    if(scene.points.empty()) return report;

    //Kamera pogleda: orbita oko sredista, gore je uvijek +Y
    const float distance = scene.radius * 2.8f * view.zoom;
    const glm::vec3 eye = scene.centre + distance * glm::vec3(std::cos(view.pitch) * std::sin(view.yaw),
                                                              std::sin(view.pitch),
                                                              std::cos(view.pitch) * std::cos(view.yaw));
    const glm::vec3 forward = glm::normalize(scene.centre - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, forward);
    const float focal = height * 0.95f;
    const float near = scene.radius * 0.02f;

    auto toCamera = [&](const glm::vec3& world){
        const glm::vec3 offset = world - eye;
        return glm::vec3(glm::dot(offset, right), glm::dot(offset, up), glm::dot(offset, forward));
    };
    auto toScreen = [&](const glm::vec3& camera){
        return glm::vec2(width * 0.5f + focal * camera.x / camera.z,
                         height * 0.5f - focal * camera.y / camera.z);
    };

    //Duzina u prostoru, odrezana na bliskoj ravnini - inace kraj iza pogleda projicira nasumicno
    auto segment = [&](glm::vec3 a, glm::vec3 b, float thickness, const Treadle::Color& color){
        glm::vec3 ca = toCamera(a), cb = toCamera(b);
        if(ca.z < near && cb.z < near) return;
        if(ca.z < near) ca = ca + (cb - ca) * ((near - ca.z) / (cb.z - ca.z));
        if(cb.z < near) cb = cb + (ca - cb) * ((near - cb.z) / (ca.z - cb.z));
        const glm::vec2 sa = toScreen(ca), sb = toScreen(cb);
        list.line(sa.x, sa.y, sb.x, sb.y, thickness, color);
    };

    //-- MREZA NA PODU -------------------------------------------------------------------------
    if(view.showGrid){
        const float spacing = detail::niceSpacing(scene.radius);
        const int lines = int(std::ceil(scene.radius * 1.6f / spacing));
        const float cx = std::round(scene.centre.x / spacing) * spacing;
        const float cz = std::round(scene.centre.z / spacing) * spacing;
        const float extent = float(lines) * spacing;
        for(int i = -lines; i <= lines; ++i){
            const float offset = float(i) * spacing;
            const Treadle::Color color = i == 0 ? Treadle::Color{0.45f, 0.48f, 0.55f, 0.55f}
                                                : Treadle::Color{0.30f, 0.32f, 0.37f, 0.35f};
            segment({cx + offset, scene.floorHeight, cz - extent}, {cx + offset, scene.floorHeight, cz + extent}, 1.0f, color);
            segment({cx - extent, scene.floorHeight, cz + offset}, {cx + extent, scene.floorHeight, cz + offset}, 1.0f, color);
        }
    }

    //-- OSI U ISHODISTU: X crven, Y zelen, Z plav, kao u Blenderu, Nukeu i Houdiniju -------------
    if(view.showAxes){
        const float length = scene.radius * 0.35f;
        const glm::vec3 origin(0.0f, scene.floorHeight, 0.0f);
        segment(origin, origin + glm::vec3(length, 0.0f, 0.0f), 2.0f, {0.95f, 0.30f, 0.30f, 0.9f});
        segment(origin, origin + glm::vec3(0.0f, length, 0.0f), 2.0f, {0.40f, 0.90f, 0.35f, 0.9f});
        segment(origin, origin + glm::vec3(0.0f, 0.0f, length), 2.0f, {0.35f, 0.55f, 1.00f, 0.9f});
    }

    //-- TOCKE ---------------------------------------------------------------------------------
    //Blize su vece i svjetlije; bez boje se sjene po visini, pa se oblik cita i kad sve stoji mirno
    const bool haveColours = scene.colours.size() == scene.points.size();
    const float nearDepth = std::max(near, distance - scene.radius);
    const float farDepth = distance + scene.radius;
    for(size_t i = 0; i < scene.points.size(); ++i){
        const glm::vec3 camera = toCamera(scene.points[i]);
        if(camera.z <= near) continue;
        const glm::vec2 screen = toScreen(camera);
        if(screen.x < -4.0f || screen.x > width + 4.0f || screen.y < -4.0f || screen.y > height + 4.0f) continue;

        const float closeness = 1.0f - std::clamp((camera.z - nearDepth) / (farDepth - nearDepth), 0.0f, 1.0f);
        const float size = 1.4f + 1.8f * closeness;
        Treadle::Color color;
        if(haveColours){
            const float light = 0.65f + 0.35f * closeness;
            color = {scene.colours[i].r / 255.0f * light, scene.colours[i].g / 255.0f * light,
                     scene.colours[i].b / 255.0f * light, 0.9f};
        }else{
            const float h = std::clamp((scene.points[i].y - scene.lowHeight) / (scene.highHeight - scene.lowHeight), 0.0f, 1.0f);
            const float light = 0.45f + 0.55f * closeness;
            color = {(0.55f + 0.25f * h) * light, (0.70f + 0.10f * h) * light, (0.95f - 0.25f * h) * light, 0.85f};
        }
        list.rect(screen.x - size * 0.5f, screen.y - size * 0.5f, size, size, color);
        ++report.drawnPoints;
    }

    //-- PUTANJA KAMERE, preko tocaka -----------------------------------------------------------
    if(view.showPath && scene.cameras.size() >= 2){
        for(size_t i = 1; i < scene.cameras.size(); ++i){
            segment(scene.cameras[i - 1], scene.cameras[i], 2.0f, {1.0f, 0.70f, 0.22f, 0.95f});
        }
    }

    //-- SMJER POGLEDA, svaka n-ta kamera, da gustoca ostane citljiva --------------------------
    if(view.showDirections && scene.forwards.size() == scene.cameras.size()){
        const size_t every = std::max<size_t>(1, scene.cameras.size() / 40);
        const float length = scene.radius * 0.12f;
        for(size_t i = 0; i < scene.cameras.size(); i += every){
            segment(scene.cameras[i], scene.cameras[i] + scene.forwards[i] * length, 1.2f, {1.0f, 0.85f, 0.55f, 0.7f});
        }
    }

    //Zadnja kamera je ona koju solver upravo dodaje - veca, bijela
    if(!scene.cameras.empty()){
        const glm::vec3 camera = toCamera(scene.cameras.back());
        if(camera.z > near){
            const glm::vec2 screen = toScreen(camera);
            list.rect(screen.x - 4.0f, screen.y - 4.0f, 8.0f, 8.0f, {1.0f, 1.0f, 1.0f, 0.95f});
        }
    }

    //-- OSI U KUTU: kamo gledaju X, Y i Z iz ovog pogleda -------------------------------------
    {
        const glm::vec2 anchor(56.0f, height - 56.0f);
        const float size = 34.0f;
        const struct{ glm::vec3 axis; Treadle::Color color; const char* name; } gizmo[3] = {
            {{1.0f, 0.0f, 0.0f}, {0.95f, 0.30f, 0.30f, 1.0f}, "X"},
            {{0.0f, 1.0f, 0.0f}, {0.40f, 0.90f, 0.35f, 1.0f}, "Y"},
            {{0.0f, 0.0f, 1.0f}, {0.35f, 0.55f, 1.00f, 1.0f}, "Z"},
        };
        for(const auto& g : gizmo){
            const glm::vec2 tip(anchor.x + size * glm::dot(g.axis, right), anchor.y - size * glm::dot(g.axis, up));
            list.line(anchor.x, anchor.y, tip.x, tip.y, 2.0f, g.color);
            list.text(tip.x + 3.0f, tip.y - 7.0f, g.name, g.color);
        }
    }
    return report;
}

}
