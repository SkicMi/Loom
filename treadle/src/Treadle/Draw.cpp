#include "Treadle/Draw.h"

#include <cmath>

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