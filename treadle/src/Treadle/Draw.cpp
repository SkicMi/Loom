#include "Treadle/Draw.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Treadle{

void DrawList::emitQuad(float x, float y, float width, float height, const Color& color,
                        float mode, float thickness, float radius,
                        float u0, float v0, float u1, float v1){
    if(width <= 0.0f || height <= 0.0f) return;

    const uint32_t base = uint32_t(vertices.size());

    //Poredak je gore-lijevo, gore-desno, dolje-desno, dolje-lijevo. Uz ishodiste gore lijevo
    //to je u zaslonskim koordinatama obrnuto od kazaljke, pa crtac ne smije rezati poledjinu.
    //Zakrpa visine u Ui::closePanel/endMenu racuna na [2] (dolje-desno) i [3] (dolje-lijevo)
    const float cx = x + width * 0.5f, cy = y + height * 0.5f;
    const float hx = width * 0.5f, hy = height * 0.5f;

    vertices.push_back({x,         y,          color.r, color.g, color.b, color.a,
                        cx, cy, u0, v0, hx, hy, radius, thickness, mode, 0.0f});
    vertices.push_back({x + width, y,          color.r, color.g, color.b, color.a,
                        cx, cy, u1, v0, hx, hy, radius, thickness, mode, 0.0f});
    vertices.push_back({x + width, y + height, color.r, color.g, color.b, color.a,
                        cx, cy, u1, v1, hx, hy, radius, thickness, mode, 0.0f});
    vertices.push_back({x,         y + height, color.r, color.g, color.b, color.a,
                        cx, cy, u0, v1, hx, hy, radius, thickness, mode, 0.0f});

    indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
    indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
}

void DrawList::rect(float x, float y, float width, float height, const Color& color){
    emitQuad(x, y, width, height, color,
             cornerRadius > 0.0f ? 1.0f : 0.0f, 0.0f, cornerRadius,
             0.0f, 0.0f, 0.0f, 0.0f);
}

void DrawList::rectFlat(float x, float y, float width, float height, const Color& color){
    emitQuad(x, y, width, height, color, 0.0f, 0.0f, 0.0f,
             0.0f, 0.0f, 0.0f, 0.0f);
}

void DrawList::rectFlat(const Rect& box, const Color& color){
    rectFlat(box.x, box.y, box.width, box.height, color);
}

void DrawList::rect(const Rect& box, const Color& color){
    rect(box.x, box.y, box.width, box.height, color);
}

void DrawList::marble(const Rect& box, const Color& vein, const Color& shadow, float timeSeconds){
    if(box.width < 20.0f || box.height < 36.0f) return;

    const int count = std::clamp(int(box.width / 52.0f), 2, 7);
    auto pointOnVein = [&](int index, float t){
        const float phase = float(index) * 1.83f + 0.55f;
        const float base = (float(index) + 0.5f) / float(count);
        const float drift = 0.19f * (t - 0.5f) +
                            0.105f * std::sin(t * 5.1f + phase) +
                            0.045f * std::sin(t * 16.4f + phase * 1.7f) +
                            0.018f * std::sin(t * 37.0f + phase * 2.3f);
        const float x = box.x + box.width * std::clamp(base + drift, 0.035f, 0.965f);
        const float y = box.y + 3.0f + t * std::max(0.0f, box.height - 6.0f);
        return std::pair<float, float>{x, y};
    };

    constexpr int segments = 42;
    for(int veinIndex = 0; veinIndex < count; ++veinIndex){
        auto previous = pointOnVein(veinIndex, 0.0f);
        for(int segment = 1; segment <= segments; ++segment){
            const float t = float(segment) / float(segments);
            const auto point = pointOnVein(veinIndex, t);
            line(previous.first, previous.second, point.first, point.second, 2.6f, shadow);
            line(previous.first + 1.1f, previous.second, point.first + 1.1f, point.second,
                 1.05f, vein);
            const float pulseWave = 0.5f + 0.5f * std::sin(t * 15.0f - timeSeconds * 2.4f + float(veinIndex) * 2.7f);
            const float pulse = std::pow(std::max(0.0f, pulseWave), 12.0f);
            float fleckPosition = std::fmod(t * 2.8f - timeSeconds * 0.22f + float(veinIndex) * 0.31f, 1.0f);
            if(fleckPosition < 0.0f) fleckPosition += 1.0f;
            const float travelingFleck = std::pow(std::max(0.0f,
                1.0f - std::fabs(fleckPosition - 0.5f) * 2.0f), 8.0f);
            line(previous.first + 1.1f, previous.second, point.first + 1.1f, point.second,
                 3.8f, Color{0.27f, 0.98f, 0.53f, 0.025f + 0.15f * pulse + 0.12f * travelingFleck});
            line(previous.first + 1.1f, previous.second, point.first + 1.1f, point.second,
                 0.72f, Color{0.78f, 1.0f, 0.80f, 0.035f + 0.46f * pulse + 0.24f * travelingFleck});
            previous = point;
        }

        //Povremene grane daju zili prirodan tok bez pravilnog uzorka ili zlatnih ukrasa.
        if(veinIndex % 2 == 0){
            const float startT = 0.20f + 0.17f * float(veinIndex % 3);
            const auto start = pointOnVein(veinIndex, startT);
            const float direction = veinIndex % 3 == 0 ? 1.0f : -1.0f;
            auto previousBranch = start;
            constexpr int branchSegments = 12;
            for(int segment = 1; segment <= branchSegments; ++segment){
                const float u = float(segment) / float(branchSegments);
                const float t = startT + 0.19f * u;
                const auto root = pointOnVein(veinIndex, t);
                const float bend = direction * box.width * 0.17f * u +
                                   box.width * 0.024f * std::sin(u * 3.14159f);
                const float x = std::clamp(root.first + bend, box.x + 3.0f, box.x + box.width - 3.0f);
                const float pulse = std::pow(std::max(0.0f, 0.5f + 0.5f *
                    std::sin(u * 9.0f - timeSeconds * 2.0f + float(veinIndex))), 10.0f);
                line(previousBranch.first, previousBranch.second, x, root.second, 2.4f,
                     Color{0.27f, 0.98f, 0.53f, 0.035f + pulse * 0.13f});
                line(previousBranch.first, previousBranch.second, x, root.second, 0.75f,
                     Color{0.72f, 0.98f, 0.74f, 0.035f + pulse * 0.28f});
                previousBranch = {x, root.second};
            }
        }
    }
}

void DrawList::line(float x0, float y0, float x1, float y1, float thickness, const Color& color){
    const float dx = x1 - x0, dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if(length < 1e-4f || thickness <= 0.0f) return;

    //Okomica na duzinu, pola debljine na svaku stranu
    const float nx = -dy / length * thickness * 0.5f;
    const float ny =  dx / length * thickness * 0.5f;

    const uint32_t base = uint32_t(vertices.size());

    //Ravan cetverokut (mode 0): shader ne cita geometrijska polja, pa im vrijednost ne treba
    vertices.push_back({x0 + nx, y0 + ny, color.r, color.g, color.b, color.a});
    vertices.push_back({x1 + nx, y1 + ny, color.r, color.g, color.b, color.a});
    vertices.push_back({x1 - nx, y1 - ny, color.r, color.g, color.b, color.a});
    vertices.push_back({x0 - nx, y0 - ny, color.r, color.g, color.b, color.a});

    indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
    indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
}

void DrawList::triangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& color){
    const uint32_t base = uint32_t(vertices.size());
    vertices.push_back({x0, y0, color.r, color.g, color.b, color.a});
    vertices.push_back({x1, y1, color.r, color.g, color.b, color.a});
    vertices.push_back({x2, y2, color.r, color.g, color.b, color.a});
    indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
}

void DrawList::outline(const Rect& box, float thickness, const Color& color){
    if(box.width <= 0.0f || box.height <= 0.0f || thickness <= 0.0f) return;

    //JEDAN cetverokut za cijeli obrub: fragment shader izracuna udaljenost do konture i uzme
    //prsten unutarnje debljine thickness. Cetiri trake iz rane verzije crtale su ugao dvaput,
    //a to se s prozirnom bojom vidjelo kao tamniji ugao
    emitQuad(box.x, box.y, box.width, box.height, color,
             3.0f, thickness, cornerRadius, 0.0f, 0.0f, 0.0f, 0.0f);
}

float DrawList::text(float x, float y, const std::string& value, const Color& color, float scale){
    if(scale <= 0.0f) return 0.0f;

    //Slovo je JEDAN kvadratic tinte iz atlasa, pa je crtanje skoro isto sto i racun sirine
    //u Font.cpp: olovka odmakne dance-a po slovo, tinta pocinje na bearing iza olovke
    float pen = 0.0f;
    for(char character : value){
        const GlyphMetrics& glyph = glyphMetrics(character);
        if(glyph.width > 0.0f && glyph.height > 0.0f){
            emitQuad(x + (pen + glyph.bearing) * scale, y + glyph.top * scale,
                     glyph.width * scale, glyph.height * scale, color,
                     2.0f, 0.0f, 0.0f, glyph.u0, glyph.v0, glyph.u1, glyph.v1);
        }
        pen += glyph.advance;
    }

    return textWidth(value, scale);
}

void DrawList::folderIcon(float x, float y, float size, const Color& color){
    if(size <= 0.0f) return;

    const float tabH = size * 0.30f;
    //Traka na vrhu, pa tijelo ispod: dva ravna pravokutnika i gotova mapa. Skladiste je
    //XHR umjetnost - cist oblik, bez fonta i bez ikakve rezolucije koja bi trepurala
    rectFlat(x, y, size * 0.55f, tabH, color);
    rectFlat(x, y + tabH, size, size - tabH, color);
}

}