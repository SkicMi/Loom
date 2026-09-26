#pragma once
//=============================================================================================
// POGLED EDITORA: scena iz Warpa nacrtana u pravokutnik prozora.
//
// Crta se u Treadleov DrawList, kao i prikaz napretka - trokuti u pikselima, projekcija na
// procesoru. Za stotinu tisuca tocaka i nekoliko kocaka to je milisekunda, a zauzvrat pogled
// ne treba vlastiti cjevovod, dubinski spremnik ni shader. Kad dodje splat, on ide kroz Loomov
// SplatRenderer ISPOD ovoga; ovo ostaje sloj za ono sto umjetnik postavlja i provjerava.
//
// DVA NACINA GLEDANJA:
//
//   orbita          slobodna kamera oko tocke, kao u Blenderu
//   kroz kameru     pogled RIJESENE kamere u kadru timelinea: njezina poza, njezina zarisna,
//                   njezin omjer slike. Tu se provjerava matchmove - kocka mora stajati na istom
//                   mjestu snimke kroz cijeli kadar
//
// PROJEKCIJA JE PINHOLE U PIKSELIMA, ista kao solverova: x = cx + f * X / -Z, y = cy - f * Y / -Z.
// Kamera gleda niz -Z s +Y gore. U nacinu kroz kameru se kadar snimke upise u pravokutnik pogleda
// uz ocuvan omjer (crne trake), a zarisna i glavna tocka se skaliraju istim mjerilom - pa kocka
// pada na isti piksel na kojem bi pala u snimci
//=============================================================================================
#include "LoomProgress.h"

#include <Treadle/Draw.h>
#include <Warp/Stage.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Loom{

struct Orbit{
    glm::vec3 target{0.0f};
    float yaw = 0.6f;
    float pitch = 0.35f;
    float distance = 8.0f;
};

struct ViewportState{
    Orbit orbit;
    Warp::Id lookThrough = Warp::None;   //kamera kroz koju se gleda; None = orbita
    bool showGrid = true;
    bool showPoints = true;
    bool showPaths = true;
    bool showCameras = true;
    //Plohe tijela crta PBR prolaz na kartici (LoomPbr.h); ovdje ostaju samo bridovi odabranog
    bool gpuMeshes = false;
};

//Kamera pogleda u ovom kadru: gdje je i kako projicira
struct ViewCamera{
    glm::mat4 view{1.0f};                //svijet -> kamera
    float focal = 800.0f;                //u pikselima prozora
    glm::vec2 centre{0.0f};
    Treadle::Rect rect;                  //gdje se smije crtati: pogled, a kroz kameru samo kadar
    Treadle::Rect frame;                 //kadar snimke unutar pogleda (kroz kameru); inace = rect
    float nearPlane = 0.001f;
    glm::vec3 eye{0.0f};
};

namespace viewport{

inline Treadle::Color mix(const Treadle::Color& a, const Treadle::Color& b, float t){
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

//Duzina odrezana na pravokutnik (Liang-Barsky). false kad je cijela vani
inline bool clipToRect(glm::vec2& a, glm::vec2& b, const Treadle::Rect& r){
    float t0 = 0.0f, t1 = 1.0f;
    const glm::vec2 d = b - a;
    const float p[4] = {-d.x, d.x, -d.y, d.y};
    const float q[4] = {a.x - r.x, r.x + r.width - a.x, a.y - r.y, r.y + r.height - a.y};
    for(int i = 0; i < 4; ++i){
        if(p[i] == 0.0f){ if(q[i] < 0.0f) return false; continue; }
        const float t = q[i] / p[i];
        if(p[i] < 0.0f){ if(t > t1) return false; t0 = std::max(t0, t); }
        else{ if(t < t0) return false; t1 = std::min(t1, t); }
    }
    const glm::vec2 start = a;
    a = start + d * t0;
    b = start + d * t1;
    return true;
}

}

//Kamera pogleda za zadani kadar. Kroz kameru samo kad entitet postoji i ima komponentu kamere
inline ViewCamera viewCameraFor(const Warp::Stage& stage, double frame, const Treadle::Rect& rect,
                                const ViewportState& state){
    ViewCamera camera;
    camera.rect = rect;
    camera.frame = rect;

    const Warp::Entity* through = state.lookThrough != Warp::None ? stage.get(state.lookThrough) : nullptr;
    if(through && through->camera && through->camera->width > 0 && through->camera->height > 0){
        const Warp::Camera& lens = *through->camera;
        const glm::mat4 world = stage.worldMatrix(through->id, frame);
        camera.view = glm::inverse(world);
        camera.eye = glm::vec3(world[3]);

        //Kadar snimke upisan u pogled, omjer ocuvan
        const float scale = std::min(rect.width / float(lens.width), rect.height / float(lens.height));
        const float width = float(lens.width) * scale, height = float(lens.height) * scale;
        camera.frame = Treadle::Rect{rect.x + (rect.width - width) * 0.5f, rect.y + (rect.height - height) * 0.5f, width, height};
        //Crta se samo unutar kadra: izvan njega snimka nema piksela, pa ni scena nema sto pokazati
        camera.rect = camera.frame;
        camera.focal = lens.focalPixels * scale;
        camera.centre = glm::vec2(camera.frame.x + lens.centreX * scale, camera.frame.y + lens.centreY * scale);
        camera.nearPlane = 1e-4f;
        return camera;
    }

    const Orbit& orbit = state.orbit;
    camera.eye = orbit.target + orbit.distance * glm::vec3(std::cos(orbit.pitch) * std::sin(orbit.yaw),
                                                           std::sin(orbit.pitch),
                                                           std::cos(orbit.pitch) * std::cos(orbit.yaw));
    camera.view = glm::lookAt(camera.eye, orbit.target, glm::vec3(0.0f, 1.0f, 0.0f));
    camera.focal = rect.height / (2.0f * std::tan(glm::radians(25.0f)));
    camera.centre = glm::vec2(rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f);
    camera.nearPlane = orbit.distance * 1e-3f;
    return camera;
}

//Tocka iz svijeta u piksele. false kad je iza kamere
inline bool project(const ViewCamera& camera, const glm::vec3& world, glm::vec2& pixel, float* depth = nullptr){
    const glm::vec3 p = glm::vec3(camera.view * glm::vec4(world, 1.0f));
    if(p.z > -camera.nearPlane) return false;
    pixel = glm::vec2(camera.centre.x + camera.focal * p.x / -p.z, camera.centre.y - camera.focal * p.y / -p.z);
    if(depth) *depth = -p.z;
    return true;
}

//Duzina u svijetu, odrezana na blizu ravninu i na pogled
inline void segment(Treadle::DrawList& list, const ViewCamera& camera, const glm::vec3& a, const glm::vec3& b,
                    float thickness, const Treadle::Color& colour){
    glm::vec3 pa = glm::vec3(camera.view * glm::vec4(a, 1.0f));
    glm::vec3 pb = glm::vec3(camera.view * glm::vec4(b, 1.0f));
    const float near = -camera.nearPlane;
    if(pa.z > near && pb.z > near) return;
    if(pa.z > near) pa = pb + (pa - pb) * ((near - pb.z) / (pa.z - pb.z));
    if(pb.z > near) pb = pa + (pb - pa) * ((near - pa.z) / (pb.z - pa.z));
    glm::vec2 sa(camera.centre.x + camera.focal * pa.x / -pa.z, camera.centre.y - camera.focal * pa.y / -pa.z);
    glm::vec2 sb(camera.centre.x + camera.focal * pb.x / -pb.z, camera.centre.y - camera.focal * pb.y / -pb.z);
    if(!viewport::clipToRect(sa, sb, camera.rect)) return;
    list.line(sa.x, sa.y, sb.x, sb.y, thickness, colour);
}

//Tipicna velicina scene: koliko je velik oblak tocaka i kamo gleda. Iz toga se biraju razmak
//mreze, velicina kamere i nova kocka - scena iz solvea nema metre, pa brojka "1" ne znaci nista
struct SceneExtent{
    glm::vec3 centre{0.0f};
    float radius = 1.0f;
};

inline SceneExtent sceneExtent(const Warp::Stage& stage, double frame){
    std::vector<float> xs, ys, zs;
    stage.walk([&](const Warp::Entity& entity, int){
        if(!entity.points || entity.points->positions.empty()) return;
        const glm::mat4 world = stage.worldMatrix(entity.id, frame);
        const std::vector<glm::vec3>& positions = entity.points->positions;
        const size_t stride = std::max<size_t>(1, positions.size() / 20000);
        for(size_t i = 0; i < positions.size(); i += stride){
            const glm::vec3 p = glm::vec3(world * glm::vec4(positions[i], 1.0f));
            xs.push_back(p.x); ys.push_back(p.y); zs.push_back(p.z);
        }
    });
    SceneExtent extent;
    if(xs.size() < 4) return extent;
    const glm::vec3 low(detail::percentile(xs, 0.05), detail::percentile(ys, 0.05), detail::percentile(zs, 0.05));
    const glm::vec3 high(detail::percentile(xs, 0.95), detail::percentile(ys, 0.95), detail::percentile(zs, 0.95));
    extent.centre = (low + high) * 0.5f;
    extent.radius = std::max(1e-3f, glm::length(high - low) * 0.5f);
    return extent;
}

//Orbita koja pokaze cijelu scenu
inline void frameAll(const Warp::Stage& stage, double frame, Orbit& orbit){
    const SceneExtent extent = sceneExtent(stage, frame);
    orbit.target = extent.centre;
    orbit.distance = extent.radius * 2.6f;
}

struct ViewportReport{
    uint32_t drawnPoints = 0;
    uint32_t drawnCameras = 0;
    uint32_t drawnMeshes = 0;
};

//Jedinicna kocka: vrhovi i stranice (po cetiri vrha, redom oko stranice)
inline const std::array<glm::vec3, 8>& cubeCorners(){
    static const std::array<glm::vec3, 8> corners = {{
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, {0.5f, -0.5f,  0.5f}, {0.5f, 0.5f,  0.5f}, {-0.5f, 0.5f,  0.5f}}};
    return corners;
}

//extent je sceneExtent() - racuna se kad se scena promijeni, ne svaki kadar
inline ViewportReport paintStage(const Warp::Stage& stage, double frame, const ViewCamera& camera,
                                 const ViewportState& state, const SceneExtent& extent, Warp::Id selected,
                                 Treadle::DrawList& list, uint32_t pointBudget = maxDisplayPoints){
    ViewportReport report;
    const Treadle::Color accent{1.0f, 0.78f, 0.25f, 1.0f};

    //-- mreza na podu (y = 0) i osi ---------------------------------------------------------
    if(state.showGrid){
        const float step = std::pow(10.0f, std::floor(std::log10(std::max(1e-4f, extent.radius * 0.5f))));
        const int lines = 12;
        const float cx = std::round(extent.centre.x / step) * step, cz = std::round(extent.centre.z / step) * step;
        const Treadle::Color colour{0.35f, 0.38f, 0.42f, 0.55f};
        for(int i = -lines; i <= lines; ++i){
            const float o = float(i) * step;
            segment(list, camera, {cx + o, 0.0f, cz - lines * step}, {cx + o, 0.0f, cz + lines * step}, 1.0f, colour);
            segment(list, camera, {cx - lines * step, 0.0f, cz + o}, {cx + lines * step, 0.0f, cz + o}, 1.0f, colour);
        }
        const float length = step * 2.0f;
        segment(list, camera, glm::vec3(0.0f), glm::vec3(length, 0.0f, 0.0f), 2.0f, {0.95f, 0.30f, 0.30f, 0.9f});
        segment(list, camera, glm::vec3(0.0f), glm::vec3(0.0f, length, 0.0f), 2.0f, {0.40f, 0.90f, 0.35f, 0.9f});
        segment(list, camera, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, length), 2.0f, {0.35f, 0.55f, 1.00f, 0.9f});
    }

    //-- oblaci tocaka -----------------------------------------------------------------------
    if(state.showPoints){
        stage.walk([&](const Warp::Entity& entity, int){
            if(!entity.visible || !entity.points) return;
            const glm::mat4 world = stage.worldMatrix(entity.id, frame);
            const std::vector<glm::vec3>& positions = entity.points->positions;
            const std::vector<glm::u8vec3>& colours = entity.points->colours;
            const bool coloured = colours.size() == positions.size();
            const size_t budget = std::max<size_t>(1, std::min<size_t>(maxDisplayPoints, pointBudget));
            const size_t stride = std::max<size_t>(1, positions.size() / budget);
            const float low = extent.centre.y - extent.radius, span = 2.0f * extent.radius;
            for(size_t i = 0; i < positions.size(); i += stride){
                const glm::vec3 p = glm::vec3(world * glm::vec4(positions[i], 1.0f));
                glm::vec2 pixel;
                float depth = 0.0f;
                if(!project(camera, p, pixel, &depth)) continue;
                if(!camera.rect.contains(pixel.x, pixel.y)) continue;
                Treadle::Color colour;
                if(coloured){
                    colour = {colours[i].r / 255.0f, colours[i].g / 255.0f, colours[i].b / 255.0f, 1.0f};
                }else{
                    const float t = std::clamp((p.y - low) / span, 0.0f, 1.0f);
                    colour = viewport::mix({0.30f, 0.55f, 0.95f, 0.9f}, {0.95f, 0.85f, 0.45f, 0.9f}, t);
                }
                const float size = depth < extent.radius * 0.5f ? 3.0f : 2.0f;
                list.rect(pixel.x - size * 0.5f, pixel.y - size * 0.5f, size, size, colour);
                ++report.drawnPoints;
            }
        });
    }

    //-- kamere: putanja i piramida u ovom kadru ---------------------------------------------
    if(state.showCameras){
        stage.walk([&](const Warp::Entity& entity, int){
            if(!entity.visible || !entity.camera) return;
            const bool isSelected = entity.id == selected;
            const Treadle::Color colour = isSelected ? accent : Treadle::Color{0.85f, 0.88f, 0.95f, 0.9f};

            if(state.showPaths && entity.translationKeys.size() > 1){
                const Warp::Id parent = entity.parent;
                const std::vector<double>& times = entity.translationKeys.times;
                const size_t stride = std::max<size_t>(1, times.size() / 600);
                glm::vec3 previous(0.0f);
                bool havePrevious = false;
                for(size_t i = 0; i < times.size(); i += stride){
                    const glm::mat4 parentWorld = parent == Warp::None ? glm::mat4(1.0f) : stage.worldMatrix(parent, times[i]);
                    const glm::vec3 p = glm::vec3(parentWorld * glm::vec4(entity.translationKeys.values[i], 1.0f));
                    if(havePrevious) segment(list, camera, previous, p, 1.5f, {0.95f, 0.45f, 0.35f, 0.75f});
                    previous = p;
                    havePrevious = true;
                }
            }

            if(entity.id == state.lookThrough) return;
            const glm::mat4 world = stage.worldMatrix(entity.id, frame);
            const Warp::Camera& lens = *entity.camera;
            const float depth = extent.radius * 0.18f;
            const float halfWidth = depth * float(lens.width) * 0.5f / lens.focalPixels;
            const float halfHeight = depth * float(lens.height) * 0.5f / lens.focalPixels;
            const glm::vec3 apex = glm::vec3(world[3]);
            glm::vec3 corners[4];
            const glm::vec2 signs[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for(int c = 0; c < 4; ++c){
                corners[c] = glm::vec3(world * glm::vec4(signs[c].x * halfWidth, signs[c].y * halfHeight, -depth, 1.0f));
            }
            const float thickness = isSelected ? 2.5f : 1.5f;
            for(int c = 0; c < 4; ++c){
                segment(list, camera, apex, corners[c], thickness, colour);
                segment(list, camera, corners[c], corners[(c + 1) % 4], thickness, colour);
            }
            //Trokutic iznad gornjeg brida: gdje je kameri gore
            const glm::vec3 up = glm::vec3(world * glm::vec4(0.0f, halfHeight * 1.5f, -depth, 1.0f));
            segment(list, camera, corners[2], up, thickness, colour);
            segment(list, camera, corners[3], up, thickness, colour);
            ++report.drawnCameras;
        });
    }

    //-- svjetla: sunce (krug i strelica), lampa (tri kruga), reflektor (stozac), panel (okvir i
    //   normala), kupola (dva luka). Velicina iz scene, jer solve nema metre
    if(state.showCameras){
        stage.walk([&](const Warp::Entity& entity, int){
            if(!entity.visible || !entity.light) return;
            const glm::mat4 world = stage.worldMatrix(entity.id, frame);
            const Warp::Light& l = *entity.light;
            const bool isSelected = entity.id == selected;
            const Treadle::Color colour = isSelected ? accent : Treadle::Color{1.0f, 0.86f, 0.35f, 0.9f};
            const float thickness = isSelected ? 2.2f : 1.4f;
            const glm::vec3 at(world[3]);
            const glm::vec3 forward = glm::normalize(glm::mat3(world) * glm::vec3(0.0f, 0.0f, -1.0f));
            const float size = std::max(1e-4f, extent.radius * 0.06f);
            auto circle = [&](const glm::vec3& centre, const glm::vec3& axis, float radius){
                const glm::vec3 a = glm::normalize(std::abs(axis.y) < 0.9f ? glm::cross(axis, glm::vec3(0, 1, 0)) : glm::cross(axis, glm::vec3(1, 0, 0)));
                const glm::vec3 b = glm::cross(axis, a);
                const int n = 20;
                for(int i = 0; i < n; ++i){
                    const float t0 = 6.2831853f * float(i) / float(n), t1 = 6.2831853f * float(i + 1) / float(n);
                    segment(list, camera, centre + (a * std::cos(t0) + b * std::sin(t0)) * radius,
                            centre + (a * std::cos(t1) + b * std::sin(t1)) * radius, thickness, colour);
                }
            };
            switch(l.type){
            case Warp::Light::Type::Distant:
                circle(at, forward, size);
                segment(list, camera, at, at + forward * size * 4.0f, thickness, colour);
                for(int k = 0; k < 4; ++k){
                    const glm::vec3 side = glm::normalize(glm::cross(forward, k % 2 ? glm::vec3(0, 1, 0.01f) : glm::vec3(1, 0.01f, 0)));
                    const glm::vec3 offset = side * (k < 2 ? size : -size);
                    segment(list, camera, at + offset, at + offset + forward * size * 2.0f, 1.0f, colour);
                }
                break;
            case Warp::Light::Type::Sphere:{
                const float r = std::max(size * 0.5f, l.radius * glm::length(glm::vec3(world[0])));
                circle(at, glm::vec3(1, 0, 0), r); circle(at, glm::vec3(0, 1, 0), r); circle(at, glm::vec3(0, 0, 1), r);
                break;
            }
            case Warp::Light::Type::Spot:{
                const float length = size * 4.0f;
                const float radius = length * std::tan(glm::radians(std::clamp(l.coneAngle, 1.0f, 85.0f)));
                const glm::vec3 base = at + forward * length;
                circle(base, forward, radius);
                const glm::vec3 a = glm::normalize(std::abs(forward.y) < 0.9f ? glm::cross(forward, glm::vec3(0, 1, 0)) : glm::cross(forward, glm::vec3(1, 0, 0)));
                const glm::vec3 b = glm::cross(forward, a);
                for(const glm::vec3& d : {a, -a, b, -b}) segment(list, camera, at, base + d * radius, thickness, colour);
                break;
            }
            case Warp::Light::Type::Rect:{
                const float hw = 0.5f * l.width, hh = 0.5f * l.height;
                const glm::vec3 c[4] = {glm::vec3(world * glm::vec4(-hw, -hh, 0, 1)), glm::vec3(world * glm::vec4(hw, -hh, 0, 1)),
                                        glm::vec3(world * glm::vec4(hw, hh, 0, 1)), glm::vec3(world * glm::vec4(-hw, hh, 0, 1))};
                for(int i = 0; i < 4; ++i) segment(list, camera, c[i], c[(i + 1) % 4], thickness, colour);
                segment(list, camera, c[0], c[2], 1.0f, colour);
                segment(list, camera, at, at + forward * size * 3.0f, thickness, colour);
                break;
            }
            case Warp::Light::Type::Dome:{
                const glm::vec3 up = glm::normalize(glm::mat3(world) * glm::vec3(0, 1, 0));
                circle(at, up, size * 2.0f);
                const glm::vec3 a = glm::normalize(std::abs(up.y) < 0.9f ? glm::cross(up, glm::vec3(0, 1, 0)) : glm::cross(up, glm::vec3(1, 0, 0)));
                const glm::vec3 b = glm::cross(up, a);
                for(const glm::vec3& side : {a, b}){
                    for(int i = 0; i < 10; ++i){
                        const float t0 = 3.14159265f * float(i) / 10.0f, t1 = 3.14159265f * float(i + 1) / 10.0f;
                        segment(list, camera, at + (side * std::cos(t0) + up * std::sin(t0)) * size * 2.0f,
                                at + (side * std::cos(t1) + up * std::sin(t1)) * size * 2.0f, thickness, colour);
                    }
                }
                break;
            }
            }
        });
    }

    //-- kosturi: kost od zgloba do roditeljskog zgloba, i tocka na zglobu --------------------
    //Zglob je entitet s oznakom Joint (vidi Warp::Joint); kost se crta samo do roditelja koji je
    //i sam zglob - korijen visi o grupi, i do nje nema kosti
    stage.walk([&](const Warp::Entity& entity, int){
        if(!entity.visible || !entity.joint) return;
        const glm::vec3 at = glm::vec3(stage.worldMatrix(entity.id, frame)[3]);
        const Warp::Entity* parent = stage.get(entity.parent);
        const bool isSelected = entity.id == selected;
        const glm::vec3 c = entity.joint->colour;
        const Treadle::Color colour = isSelected ? accent : Treadle::Color{c.r, c.g, c.b, 0.95f};
        if(parent && parent->joint){
            segment(list, camera, glm::vec3(stage.worldMatrix(parent->id, frame)[3]), at, isSelected ? 3.0f : 2.0f, colour);
        }
        glm::vec2 pixel;
        if(project(camera, at, pixel) && camera.rect.contains(pixel.x, pixel.y)){
            const float size = isSelected ? 8.0f : 5.0f;
            list.rect(pixel.x - size * 0.5f, pixel.y - size * 0.5f, size, size, colour);
        }
    });

    //-- tijela: plohe od straga prema naprijed, pa bridovi ----------------------------------
    struct Face{ glm::vec2 pixels[4]; float depth; Treadle::Color colour; };
    std::vector<Face> faces;
    struct Edge{ glm::vec3 a, b; bool selected; };
    std::vector<Edge> edges;
    stage.walk([&](const Warp::Entity& entity, int){
        if(!entity.visible || !entity.mesh) return;
        const glm::mat4 world = stage.worldMatrix(entity.id, frame);
        const bool isSelected = entity.id == selected;
        if(state.gpuMeshes && !isSelected){ ++report.drawnMeshes; return; }
        const glm::vec3 base = entity.mesh->colour;
        const glm::vec3 light = glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));

        std::vector<std::array<glm::vec3, 4>> quads;
        if(entity.mesh->shape == Warp::Shape::Cube){
            const auto& c = cubeCorners();
            const int sides[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
            for(const auto& side : sides){
                quads.push_back({glm::vec3(world * glm::vec4(c[side[0]], 1.0f)), glm::vec3(world * glm::vec4(c[side[1]], 1.0f)),
                                 glm::vec3(world * glm::vec4(c[side[2]], 1.0f)), glm::vec3(world * glm::vec4(c[side[3]], 1.0f))});
            }
        }else{
            quads.push_back({glm::vec3(world * glm::vec4(-0.5f, 0.0f, -0.5f, 1.0f)), glm::vec3(world * glm::vec4(0.5f, 0.0f, -0.5f, 1.0f)),
                             glm::vec3(world * glm::vec4(0.5f, 0.0f, 0.5f, 1.0f)), glm::vec3(world * glm::vec4(-0.5f, 0.0f, 0.5f, 1.0f))});
        }
        for(const auto& quad : quads){
            Face face;
            bool visible = true;
            float depthSum = 0.0f;
            for(int k = 0; k < 4 && visible; ++k){
                float depth = 0.0f;
                visible = project(camera, quad[k], face.pixels[k], &depth);
                depthSum += depth;
            }
            for(int k = 0; k < 4; ++k) edges.push_back({quad[k], quad[(k + 1) % 4], isSelected});
            if(!visible) continue;
            //Samo stranice okrenute kameri; ravnina se vidi s obje strane
            const glm::vec3 normal = glm::normalize(glm::cross(quad[1] - quad[0], quad[3] - quad[0]));
            const glm::vec3 centre = (quad[0] + quad[1] + quad[2] + quad[3]) * 0.25f;
            const bool facing = glm::dot(normal, camera.eye - centre) > 0.0f;
            if(entity.mesh->shape == Warp::Shape::Cube && !facing) continue;
            if(state.gpuMeshes) continue;           //plohu crta kartica, ovdje samo bridovi
            const float shade = 0.45f + 0.55f * std::fabs(glm::dot(normal, light));
            face.depth = depthSum * 0.25f;
            face.colour = {base.r * shade, base.g * shade, base.b * shade, entity.mesh->shape == Warp::Shape::Plane ? 0.45f : 0.92f};
            faces.push_back(face);
        }
        ++report.drawnMeshes;
    });
    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b){ return a.depth > b.depth; });
    for(const Face& face : faces){
        //Plohu koja izlazi iz pogleda crtac ne bi odrezao; takvu se preskoci, bridovi i dalje stoje
        bool inside = true;
        for(const glm::vec2& p : face.pixels){
            if(!camera.rect.contains(p.x, p.y)) inside = false;
        }
        if(!inside) continue;
        list.triangle(face.pixels[0].x, face.pixels[0].y, face.pixels[1].x, face.pixels[1].y, face.pixels[2].x, face.pixels[2].y, face.colour);
        list.triangle(face.pixels[0].x, face.pixels[0].y, face.pixels[2].x, face.pixels[2].y, face.pixels[3].x, face.pixels[3].y, face.colour);
    }
    for(const Edge& edge : edges){
        segment(list, camera, edge.a, edge.b, edge.selected ? 2.5f : 1.2f,
                edge.selected ? accent : Treadle::Color{0.1f, 0.1f, 0.1f, 0.9f});
    }

    //-- kadar snimke kroz kameru: okvir ------------------------------------------------------
    if(state.lookThrough != Warp::None){
        list.outline(camera.frame, 1.0f, {0.9f, 0.9f, 0.9f, 0.6f});
    }
    return report;
}

//POVRSINA POD PIKSELOM: tocke oblaka koje padaju blizu piksela, medijan njihove dubine duz zrake.
//Kocka postavljena ondje sjedi na stvarnoj plohi snimke - zidu, stolu, podu - pa se kroz kameru
//odmah vidi drzi li se nje. Medijan, ne najbliza: jedna odbjegla tocka ispred zida bi inace
//povukla kocku u zrak. false kad oko piksela nema dovoljno tocaka
inline bool surfaceAt(const Warp::Stage& stage, double frame, const ViewCamera& camera, glm::vec2 pixel,
                      glm::vec3& out, float radius = 25.0f){
    std::vector<float> depths;
    stage.walk([&](const Warp::Entity& entity, int){
        if(!entity.visible || !entity.points) return;
        const glm::mat4 world = stage.worldMatrix(entity.id, frame);
        for(const glm::vec3& p : entity.points->positions){
            glm::vec2 at;
            float depth = 0.0f;
            if(!project(camera, glm::vec3(world * glm::vec4(p, 1.0f)), at, &depth)) continue;
            if(glm::length(at - pixel) <= radius) depths.push_back(depth);
        }
    });
    if(depths.size() < 5) return false;
    std::nth_element(depths.begin(), depths.begin() + long(depths.size() / 2), depths.end());
    const float depth = depths[depths.size() / 2];
    //Zraka kroz piksel, u svijetu: obrnuto od project()
    const glm::vec3 inCamera((pixel.x - camera.centre.x) / camera.focal * depth,
                             -(pixel.y - camera.centre.y) / camera.focal * depth, -depth);
    out = glm::vec3(glm::inverse(camera.view) * glm::vec4(inCamera, 1.0f));
    return true;
}

//Entitet pod misem: kamera po vrhu piramide, tijelo po sredistu. Najblizi unutar 16 piksela
inline Warp::Id pickEntity(const Warp::Stage& stage, double frame, const ViewCamera& camera, glm::vec2 mouse){
    Warp::Id best = Warp::None;
    float bestDistance = 16.0f;
    stage.walk([&](const Warp::Entity& entity, int){
        if(!entity.visible || (!entity.camera && !entity.mesh && !entity.joint && !entity.model)) return;
        glm::vec2 pixel;
        if(!project(camera, glm::vec3(stage.worldMatrix(entity.id, frame)[3]), pixel)) return;
        const float distance = glm::length(pixel - mouse);
        if(distance < bestDistance){ bestDistance = distance; best = entity.id; }
    });
    return best;
}

//=============================================================================================
// STRELICE ZA POMICANJE: tri osi svijeta iz sredista odabranog, uhvati jednu i vuci.
//
// Duljina je STALNA NA EKRANU (90 px), ne u svijetu - scena iz solvea nema metre, pa bi strelica
// zadane duljine u jednoj snimci bila tocka, a u drugoj veca od sobe. Vuce se samo uzduz osi:
// pomak misa se projicira na smjer osi na ekranu, pa kocka ide tocno onoliko koliko je mis prosao
// preko strelice. Radi i kroz rijesenu kameru - tamo se kocka namjesta na pod snimke
//=============================================================================================
struct Gizmo{
    glm::vec3 origin{0.0f};
    float length = 1.0f;            //u svijetu, tako da na ekranu bude 90 px
    bool visible = false;
};

inline Gizmo gizmoFor(const ViewCamera& camera, const glm::vec3& origin){
    Gizmo gizmo;
    gizmo.origin = origin;
    glm::vec2 pixel;
    float depth = 0.0f;
    if(!project(camera, origin, pixel, &depth)) return gizmo;
    gizmo.length = 90.0f * depth / camera.focal;
    gizmo.visible = true;
    return gizmo;
}

inline glm::vec3 gizmoAxis(int axis){
    return axis == 0 ? glm::vec3(1, 0, 0) : axis == 1 ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
}

//Os pod misem, -1 kad nijedna. Udaljenost od duzine na ekranu, do 8 px
inline int gizmoAxisAt(const ViewCamera& camera, const Gizmo& gizmo, glm::vec2 mouse){
    if(!gizmo.visible) return -1;
    glm::vec2 origin;
    if(!project(camera, gizmo.origin, origin)) return -1;
    int best = -1;
    float bestDistance = 8.0f;
    for(int axis = 0; axis < 3; ++axis){
        glm::vec2 tip;
        if(!project(camera, gizmo.origin + gizmoAxis(axis) * gizmo.length, tip)) continue;
        const glm::vec2 d = tip - origin;
        const float lengthSquared = glm::dot(d, d);
        if(lengthSquared < 1.0f) continue;          //os gleda ravno u kameru: ne da se vuci
        const float t = std::clamp(glm::dot(mouse - origin, d) / lengthSquared, 0.15f, 1.0f);
        const float distance = glm::length(mouse - (origin + d * t));
        if(distance < bestDistance){ bestDistance = distance; best = axis; }
    }
    return best;
}

//Koliko se u svijetu pomaknuti uzduz osi za zadani pomak misa
inline float gizmoDrag(const ViewCamera& camera, const Gizmo& gizmo, int axis, glm::vec2 mouseDelta){
    glm::vec2 origin, tip;
    if(axis < 0 || !project(camera, gizmo.origin, origin) ||
       !project(camera, gizmo.origin + gizmoAxis(axis) * gizmo.length, tip)) return 0.0f;
    const glm::vec2 d = tip - origin;
    const float lengthSquared = glm::dot(d, d);
    if(lengthSquared < 1.0f) return 0.0f;
    return glm::dot(mouseDelta, d) / lengthSquared * gizmo.length;
}

inline void paintGizmo(Treadle::DrawList& list, const ViewCamera& camera, const Gizmo& gizmo, int hotAxis){
    if(!gizmo.visible) return;
    const Treadle::Color colours[3] = {{1.0f, 0.16f, 0.28f, 1.0f}, {0.18f, 1.0f, 0.42f, 1.0f}, {0.24f, 0.52f, 1.0f, 1.0f}};
    for(int axis = 0; axis < 3; ++axis){
        const Treadle::Color colour = axis == hotAxis ? Treadle::Color{1.0f, 1.0f, 0.6f, 1.0f} : colours[axis];
        const glm::vec3 tip = gizmo.origin + gizmoAxis(axis) * gizmo.length;
        Treadle::Color glow{colour.r, colour.g, colour.b, 0.22f};
        segment(list, camera, gizmo.origin, tip, axis == hotAxis ? 9.0f : 7.0f, glow);
        segment(list, camera, gizmo.origin, tip, axis == hotAxis ? 3.5f : 2.5f, colour);
        glm::vec2 pixel;
        if(project(camera, tip, pixel) && camera.rect.contains(pixel.x, pixel.y)){
            list.rect(pixel.x - 5.0f, pixel.y - 5.0f, 10.0f, 10.0f, colour);
        }
    }
}

//=============================================================================================
// KRUGOVI ZA ROTACIJU: tri kruga oko osi svijeta kroz srediste odabranog, uhvati jedan i vuci.
//
// KUT SE RACUNA U PROSTORU, ne na ekranu. Zraka misa se presijece s ravninom kruga, i kut je
// izmedju dviju tocaka u toj ravnini oko sredista. Kut izmjeren na ekranu bi lagao cim krug
// nije gledan ravno - elipsa skraceno pokazuje kut, pa bi se kocka okretala brze ili sporije
// nego mis, ovisno o tome odakle se gleda. Ovako je 90 st misa 90 st kocke iz svakog kuta.
//
// Samo kad je krug gotovo bocno (ravnina skoro paralelna zraci) presjek bjezi u beskonacnost;
// tada se kut uzima s ekrana, oko projiciranog sredista - ondje je jedino on stabilan
//=============================================================================================
struct Ray{
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

//Zraka iz kamere pogleda kroz piksel prozora
inline Ray rayThrough(const ViewCamera& camera, glm::vec2 pixel){
    const glm::mat4 inverse = glm::inverse(camera.view);
    Ray ray;
    ray.origin = glm::vec3(inverse[3]);
    ray.direction = glm::normalize(glm::vec3(inverse * glm::vec4((pixel.x - camera.centre.x) / camera.focal,
                                                                 -(pixel.y - camera.centre.y) / camera.focal, -1.0f, 0.0f)));
    return ray;
}

//Tocke kruga oko osi, polumjera iz gizma (90 px na ekranu kad je okomit na pogled)
inline glm::vec3 ringPoint(const Gizmo& gizmo, int axis, float angle){
    const glm::vec3 a = gizmoAxis((axis + 1) % 3), b = gizmoAxis((axis + 2) % 3);
    return gizmo.origin + (a * std::cos(angle) + b * std::sin(angle)) * gizmo.length;
}

//Krug pod misem, -1 kad nijedan. Udaljenost od izlomljene crte kruga na ekranu, do 8 px
inline int ringAxisAt(const ViewCamera& camera, const Gizmo& gizmo, glm::vec2 mouse){
    if(!gizmo.visible) return -1;
    int best = -1;
    float bestDistance = 8.0f;
    constexpr int segments = 64;
    for(int axis = 0; axis < 3; ++axis){
        glm::vec2 previous;
        bool havePrevious = false;
        for(int i = 0; i <= segments; ++i){
            glm::vec2 at;
            if(!project(camera, ringPoint(gizmo, axis, 6.2831853f * float(i) / segments), at)){ havePrevious = false; continue; }
            if(havePrevious){
                const glm::vec2 d = at - previous;
                const float lengthSquared = std::max(1e-6f, glm::dot(d, d));
                const float t = std::clamp(glm::dot(mouse - previous, d) / lengthSquared, 0.0f, 1.0f);
                const float distance = glm::length(mouse - (previous + d * t));
                if(distance < bestDistance){ bestDistance = distance; best = axis; }
            }
            previous = at;
            havePrevious = true;
        }
    }
    return best;
}

//Za koliko radijana se okrenuti oko osi kad mis ode iz from u to. Desno pravilo oko +osi
inline float ringDrag(const ViewCamera& camera, const Gizmo& gizmo, int axis, glm::vec2 from, glm::vec2 to){
    if(axis < 0 || !gizmo.visible) return 0.0f;
    const glm::vec3 normal = gizmoAxis(axis);
    const Ray a = rayThrough(camera, from), b = rayThrough(camera, to);
    const float facingA = glm::dot(a.direction, normal), facingB = glm::dot(b.direction, normal);
    if(std::fabs(facingA) > 0.08f && std::fabs(facingB) > 0.08f){
        const glm::vec3 pa = a.origin + a.direction * (glm::dot(gizmo.origin - a.origin, normal) / facingA) - gizmo.origin;
        const glm::vec3 pb = b.origin + b.direction * (glm::dot(gizmo.origin - b.origin, normal) / facingB) - gizmo.origin;
        if(glm::length(pa) > 1e-6f && glm::length(pb) > 1e-6f){
            return std::atan2(glm::dot(glm::cross(pa, pb), normal), glm::dot(pa, pb));
        }
    }
    //Krug bocno: kut na ekranu oko projiciranog sredista. Na ekranu y ide dolje, pa je rast
    //atan2 kretanje u smjeru kazaljke - a to je negativna rotacija oko osi okrenute prema kameri
    glm::vec2 centre;
    if(!project(camera, gizmo.origin, centre)) return 0.0f;
    const float screen = std::atan2(to.y - centre.y, to.x - centre.x) - std::atan2(from.y - centre.y, from.x - centre.x);
    const float wrapped = std::atan2(std::sin(screen), std::cos(screen));
    const bool towardCamera = glm::dot(normal, camera.eye - gizmo.origin) > 0.0f;
    return towardCamera ? -wrapped : wrapped;
}

inline void paintRings(Treadle::DrawList& list, const ViewCamera& camera, const Gizmo& gizmo, int hotAxis){
    if(!gizmo.visible) return;
    const Treadle::Color colours[3] = {{0.95f, 0.30f, 0.30f, 1.0f}, {0.40f, 0.90f, 0.35f, 1.0f}, {0.35f, 0.55f, 1.00f, 1.0f}};
    constexpr int segments = 64;
    for(int axis = 0; axis < 3; ++axis){
        const Treadle::Color colour = axis == hotAxis ? Treadle::Color{1.0f, 1.0f, 0.6f, 1.0f} : colours[axis];
        for(int i = 0; i < segments; ++i){
            const glm::vec3 a = ringPoint(gizmo, axis, 6.2831853f * float(i) / segments);
            const glm::vec3 b = ringPoint(gizmo, axis, 6.2831853f * float(i + 1) / segments);
            //Straznja polovica kruga (iza sredista gledano iz kamere) tanja i blijeda
            const bool behind = glm::dot((a + b) * 0.5f - gizmo.origin, camera.eye - gizmo.origin) < 0.0f;
            Treadle::Color c = colour;
            if(behind) c.a = 0.35f;
            if(!behind){
                Treadle::Color glow{colour.r, colour.g, colour.b, 0.16f};
                segment(list, camera, a, b, axis == hotAxis ? 7.0f : 5.0f, glow);
            }
            segment(list, camera, a, b, axis == hotAxis ? 3.5f : (behind ? 1.5f : 2.25f), c);
        }
    }
}

}
