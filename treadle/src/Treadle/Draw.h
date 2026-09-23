#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Treadle{

//Boja s prozirnoscu, 0..1. Obicni brojevi, jer Treadle ne zna u kojem ce prostoru zavrsiti
struct Color{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

//Jedan vrh onoga sto treba nacrtati: mjesto U PIKSELIMA i boja.
//
//U pikselima a ne u NDC-u zato sto raspored i pogadjanje misem racunaju u pikselima - kad bi
//se ovdje vec pretvaralo, dva bi racuna radila isti posao i mogla bi se raziici. Pretvorbu
//radi shader, kojemu se velicina ekrana ionako mora reci
struct Vertex{
    float x = 0.0f, y = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

//Pravokutnik u pikselima. Postoji kao tip jer se pogadjanje misem i crtanje moraju slagati
//do piksela, a dvije cetvorke brojeva koje se prenose odvojeno prije ili kasnije se raziidju
struct Rect{
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

    bool contains(float px, float py) const{
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

//Popis trokuta koji cine jedan kadar suicelja. Aplikacija ga preda svojem crtacu i zaboravi.
//
//SVE JE TROKUT, I SLOVA TAKODJER. Nema teksture, nema atlasa i nema uzorkivaca - slovo je
//skup pravokutnika, po jedan za svaki niz upaljenih tocaka u retku. To je vise vrhova nego
//sto bi bio atlas, ali je za suicelje od par stotina znakova nekoliko tisuca vrhova po kadru,
//sto kartica ne osjeti - a zauzvrat Treadle nema nijednu ovisnost i crtac nema nijednu sliku
//za prenijeti. Kad tekst jednom postane tijelo teksta a ne oznake, atlas ce imati smisla.
struct DrawList{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    void clear(){vertices.clear(); indices.clear();}
    bool empty() const {return indices.empty();}

    void rect(const Rect& box, const Color& color);
    void rect(float x, float y, float width, float height, const Color& color);

    //DUZINA proizvoljnog smjera, debljine thickness, kao jedan cetverokut. Postoji zbog prikaza
    //napretka u loom-u: putanja kamere je niz duzina, a pravokutnik moze biti samo vodoravan ili
    //okomit. Crtac ne reze poledjinu (vidi rect), pa redoslijed vrhova ne ovisi o smjeru duzine
    void line(float x0, float y0, float x1, float y1, float thickness, const Color& color);

    //Samo obrub, debljine thickness prema UNUTRA. Prema unutra jer se obrub tada nikad ne
    //prosiri preko onoga sto je raspored izmjerio
    void outline(const Rect& box, float thickness, const Color& color);

    //Tekst, gornji lijevi kut na (x, y). Vraca sirinu koju je zauzeo
    float text(float x, float y, const std::string& value, const Color& color, float scale = 1.0f);
};

//-- font ------------------------------------------------------------------------------------
//Slovo je 5x7 tocaka, a korak je 6 - jedan stupac razmaka. Redak je 8, jedan red razmaka.
constexpr int glyphWidth = 5;
constexpr int glyphHeight = 7;
constexpr int glyphAdvance = 6;
constexpr int lineAdvance = 9;

//Koliko bi taj tekst bio sirok i visok pri danom mjerilu. Raspored ovo mora znati PRIJE nego
//sto se ista nacrta, pa je racun ovdje a ne u crtanju
float textWidth(const std::string& value, float scale = 1.0f);
float textHeight(float scale = 1.0f);

//Je li tocka (column, row) unutar slova upaljena. Postoji izvan crtanja da se font moze
//provjeriti testom - font kojemu nedostaje slovo daje rupu u natpisu, a nista ne pukne
bool glyphPixel(char character, int column, int row);

}
