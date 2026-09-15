#include "Treadle/Draw.h"

namespace Treadle{

void DrawList::rect(float x, float y, float width, float height, const Color& color){
    if(width <= 0.0f || height <= 0.0f) return;

    const uint32_t base = uint32_t(vertices.size());

    //Poredak je gore-lijevo, gore-desno, dolje-desno, dolje-lijevo. Uz ishodiste gore lijevo
    //to je u zaslonskim koordinatama obrnuto od kazaljke, pa crtac ne smije rezati poledjinu
    vertices.push_back({x,         y,          color.r, color.g, color.b, color.a});
    vertices.push_back({x + width, y,          color.r, color.g, color.b, color.a});
    vertices.push_back({x + width, y + height, color.r, color.g, color.b, color.a});
    vertices.push_back({x,         y + height, color.r, color.g, color.b, color.a});

    indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
    indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
}

void DrawList::rect(const Rect& box, const Color& color){
    rect(box.x, box.y, box.width, box.height, color);
}

void DrawList::outline(const Rect& box, float thickness, const Color& color){
    if(thickness <= 0.0f) return;

    //Cetiri trake prema unutra. Vodoravne idu punom sirinom, okomite izmedju njih, pa se
    //uglovi ne crtaju dvaput - s prozirnom bojom bi dvostruki ugao bio tamniji od ostatka
    const float t = thickness < box.height * 0.5f ? thickness : box.height * 0.5f;
    rect(box.x, box.y, box.width, t, color);
    rect(box.x, box.y + box.height - t, box.width, t, color);
    rect(box.x, box.y + t, t, box.height - 2.0f * t, color);
    rect(box.x + box.width - t, box.y + t, t, box.height - 2.0f * t, color);
}

float DrawList::text(float x, float y, const std::string& value, const Color& color, float scale){
    if(scale <= 0.0f) return 0.0f;

    for(size_t index = 0; index < value.size(); ++index){
        const float left = x + float(index * glyphAdvance) * scale;

        for(int row = 0; row < glyphHeight; ++row){
            //NIZOVI, NE POJEDINACNE TOCKE. Slovo ima do 35 upaljenih tocaka, a spajanjem
            //susjednih u retku ih ostane oko 14 - isti piksel, dvaput manje trokuta. Ovo je
            //jedini razlog zasto crtanje teksta bez teksture uopce ostaje jeftino
            int run = 0;
            for(int column = 0; column <= glyphWidth; ++column){
                const bool lit = column < glyphWidth && glyphPixel(value[index], column, row);
                if(lit){
                    ++run;
                    continue;
                }
                if(run > 0){
                    rect(left + float(column - run) * scale, y + float(row) * scale,
                         float(run) * scale, scale, color);
                    run = 0;
                }
            }
        }
    }

    return textWidth(value, scale);
}

}
