#pragma once
#include <string>

namespace Treadle{

//=============================================================================================
// Font je RASTER (podebljani Noto Sans crtan 15 puta veci nego sto se vidi) a ne skup pravokutnika.
// Oznaka je jedan kvadratic tinte iz atlasa po znaku, pa se tekst ispisuje otprilike dva
// pravokutnika po slovu umjesto desetak, i to pravi font s nagibima i s pravim razmacima.
//
// Atlas i metrike su GENERIRANI u FontData.h (tools/ui/make_font.py). Tintu je generator
// izmjerio IZ RASLJERE, ne iz metrika fonta, pa se brojcevi ovdje i slika na zaslonu ne
// mogu raziici - a UV kvadrat se racunom gradi iz iste mjere, dakle iz istog izvora.
//
// Svi koraci i velicine su u LOGICKIM JEDINICAMA za scale = 1, i mnoze se s mjerilom tamo
// gdje se crta. Jedna logicka jedinica je kFontAtlasScale atlas piksela.
//=============================================================================================

//Metrike jednog znaka za scale = 1. Uvijek cijela velicina: znak bez tinte (razmak) ima
//advance ali nule za okvir tinte, pa pozivatelj ne mora razlikovati prazne znakove
struct GlyphMetrics{
    float advance = 0.0f;   //koliko olovka odmakne do sljedeceg znaka
    float bearing = 0.0f;   //od olovke do lijeve tinte; moze biti negativna (J, j)
    float top = 0.0f;       //od vrha retka do vrha tinte
    float width = 0.0f;     //okvir tinte
    float height = 0.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;   //atlas interval tinte
};

//Metrike jednog znaka. Znak izvan 32..126 je razmak: advance razmaka, bez tinte
const GlyphMetrics& glyphMetrics(char character);

//Atlas: R8 pokrivenost, bez boje. Uvijek iste dimenzije (kFontAtlasWidth/Height iz FontData.h),
//pa se shader moze vezati jednom i zaboraviti
const unsigned char* fontAtlas(int& width, int& height);

//Koliko bi taj tekst bio sirok i visok pri danom mjerilu. Raspored ovo mora znati PRIJE nego
//sto se ista nacrta, pa je racun ovdje a ne u crtanju - i to je i obeCANJE sirine: crtanje
//vraca isti broj (vidi DrawList::text), a raspored racuna s njim
float textWidth(const std::string& value, float scale = 1.0f);
float textHeight(float scale = 1.0f);

//Tekst skracen da stane u zadanu sirinu, s ".." na kraju kad je skracen. Crtac ne reze, pa bi
//predugo ime iz stupca editora bez ovoga iscurilo preko pogleda
std::string fitText(const std::string& value, float width, float scale = 1.0f);

}