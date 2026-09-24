#include "Treadle/Font.h"
#include "Treadle/FontData.h"

#include <algorithm>
#include <vector>

namespace Treadle{

namespace{

//Sve metrike u logickim jedinicama (za scale = 1: jedna jedinica je kFontAtlasScale atlas
//piksela). Gradi se jednom; tablica kFontGlyphs je generirana i u njene brojeve se ne dira
const std::vector<GlyphMetrics>& buildGlyphMetrics(){
    static const std::vector<GlyphMetrics> metrics = []{
        const float unit = 1.0f / float(kFontAtlasScale);
        std::vector<GlyphMetrics> table(kFontGlyphCount);

        for(int index = 0; index < kFontGlyphCount; ++index){
            const FontGlyphData& glyph = kFontGlyphs[index];
            GlyphMetrics& metric = table[index];

            //Gdje u atlasu pocinje tinta: u svojoj celiji, pomaknuta za marginu i ispun
            const int column = glyph.cell % kFontCols;
            const int row    = glyph.cell / kFontCols;
            const float inkX = float(column * kFontCellPx + kFontPadPx + glyph.bearingPx);
            const float inkY = float(row    * kFontCellPx + kFontPadPx + glyph.topPx);

            metric.advance = float(glyph.advancePx) * unit;
            metric.bearing = float(glyph.bearingPx) * unit;
            metric.top     = float(glyph.topPx)    * unit;
            metric.width   = float(glyph.inkWidthPx)  * unit;
            metric.height  = float(glyph.inkHeightPx) * unit;

            //UV interval tinte se gradi iz ISTE mjere kao i odsjececi, pa se slika i okvir
            //ne mogu raziici: geometry Treadle svodi na logicke, UV ostaje u atlas pikselima
            metric.u0 = inkX / float(kFontAtlasWidth);
            metric.v0 = inkY / float(kFontAtlasHeight);
            metric.u1 = (inkX + float(glyph.inkWidthPx))  / float(kFontAtlasWidth);
            metric.v1 = (inkY + float(glyph.inkHeightPx)) / float(kFontAtlasHeight);
        }
        return table;
    }();
    return metrics;
}

//Razmak: sirina onolika koliko tipkovnica trazi, bez ijedne tinte. Za znak koji font ne zna i
//za prazno polje - nepoznat znak nije greska programa nego natpisa, a natpis s rupom se vidi
const GlyphMetrics& spaceMetrics(){
    static const GlyphMetrics space = []{
        const float unit = 1.0f / float(kFontAtlasScale);
        GlyphMetrics metric;
        metric.advance = float(kFontGlyphs[0].advancePx) * unit;
        return metric;
    }();
    return space;
}

}

const GlyphMetrics& glyphMetrics(char character){
    if(character < kFontFirst || character > kFontLast) return spaceMetrics();
    return buildGlyphMetrics()[int(character) - kFontFirst];
}

const unsigned char* fontAtlas(int& width, int& height){
    width = kFontAtlasWidth;
    height = kFontAtlasHeight;
    return kFontAtlasPixels;
}

float textWidth(const std::string& value, float scale){
    if(value.empty()) return 0.0f;

    float advance = 0.0f;
    float overhang = 0.0f;
    for(char character : value){
        const GlyphMetrics& glyph = glyphMetrics(character);
        advance += glyph.advance;
        //Zadnje slovo smije viriti preko svog advancea (kursevi, j, kose crte). Vraca se s
        //najvecim prevjesom u retku, jer se ne zna koje ce slovo biti zadnje
        overhang = std::max(overhang, glyph.bearing + glyph.width - glyph.advance);
    }
    return (advance + overhang) * scale;
}

float textHeight(float scale){
    return float(kFontLinePx) / float(kFontAtlasScale) * scale;
}

std::string fitText(const std::string& value, float width, float scale){
    if(textWidth(value, scale) <= width) return value;

    //".." stane garantirano: sirina se mjeri istim racunom koji crtanje koristi
    const float budget = width - textWidth("..", scale);
    if(budget <= 0.0f) return "";

    float pen = 0.0f;
    size_t count = 0;
    for(; count < value.size(); ++count){
        const float next = pen + glyphMetrics(value[count]).advance * scale;
        if(next > budget) break;
        pen = next;
    }
    return value.substr(0, count) + "..";
}

}