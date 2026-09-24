#pragma once
#include "Treadle/Font.h"

#include <cstdint>
#include <vector>

namespace Treadle{

//Boja s prozirnoscu, 0..1. Obicni brojevi, jer Treadle ne zna u kojem ce prostoru zavrsiti
struct Color{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

//Jedan vrh onoga sto treba nacrtati: mjesto i boja U PIKSELIMA, pa podaci kojima fragment
//shader oblikuje cetverokut (vidjeti shaders/ui.slang).
//
//U pikselima a ne u NDC-u zato sto raspored i pogadjanje misem racunaju u pikselima - kad bi
//se ovdje vec pretvaralo, dva bi racuna radila isti posao i mogla bi se raziici. Pretvorbu
//radi shader, kojemu se velicina ekrana ionako mora reci.
//
//Mode 0: RAVAN cetverokut (scena u pogledu editora) - ostala polja se ne citaju.
//Mode 1: ispuna ZAOBLJENOG pravokutnika; radius ogranici shader sam.
//Mode 2: tekst - kvadratic tinte iz atlasa; u,v su atlas interval.
//Mode 3: obrub - prsten na konturi zaobljenog pravokutnika, radius/thickness unutarnji
struct Vertex{
    float x = 0.0f, y = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    float centerX = 0.0f, centerY = 0.0f;   //srediste cetverokuta, pikseli
    float u = 0.0f, v = 0.0f;               //tekst: atlas interval dvostrukog vrha
    float halfX = 0.0f, halfY = 0.0f;       //pola sirine i visine cetverokuta
    float radius = 0.0f;                    //zaobljenje kuta (mode 1 i 3)
    float thickness = 0.0f;                 //debljina obruba (mode 3)
    float mode = 0.0f;
    float padding = 0.0f;
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
//SVE JE TROKUT, I SLOVA TAKODJER - ali je slovo samo JEDAN kvadratic tinte iz atlasa s UV
//koordinatama, a ne skup pravokutnika. Geometriju oblikuje fragment shader, pa je prostor
//izmedju oznake i "slike": pravi font bez ikakve ovisnosti Treadlea o Vulkanu, s time da
//crtac mora znati UV za svaki vrh
struct DrawList{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    //Zaobljenje kuta svih pravokutnika i obruba nacrtanih od sada do clear(). Nula je ravno.
    //Mijenja SAMO IZGLED, ne i raspored ni pogadjanje misem - za to se i koristi; kad bi se
    //pogadjanje mijenjalo s izgledom, klikanje usred plohe bi preskakalo widgete ispod KUTA
    float cornerRadius = 0.0f;

    void clear(){vertices.clear(); indices.clear();}
    bool empty() const {return indices.empty();}

    //Pravokutnik; zaobljen ako je cornerRadius veci od nule
    void rect(const Rect& box, const Color& color);
    void rect(float x, float y, float width, float height, const Color& color);

    //RAVAN pravokutnik, bez obzira na cornerRadius. Za pozadinu plohe i izbornika, kojoj se
    //visina upise naknadno: shader oblikuje po VEZANIM poljima (srediste, pola), a zakrpa
    //mijenja samo donje vrhove - pa bi zaobljena pozadina ostala visoka jedan piksel
    void rectFlat(float x, float y, float width, float height, const Color& color);
    void rectFlat(const Rect& box, const Color& color);

    //DUZINA proizvoljnog smjera, debljine thickness, kao jedan cetverokut. Postoji zbog prikaza
    //napretka u loom-u: putanja kamere je niz duzina, a pravokutnik moze biti samo vodoravan ili
    //okomit. Crtac ne reze poledjinu (vidi rect), pa redoslijed vrhova ne ovisi o smjeru duzine
    void line(float x0, float y0, float x1, float y1, float thickness, const Color& color);

    //Trokut proizvoljnog oblika, za popunjene plohe u pogledu editora (stranica kocke). Crtac ne
    //reze poledjinu, pa redoslijed vrhova nije vazan
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& color);

    //Samo obrub, debljine thickness prema UNUTRA. Prema unutra jer se obrub tada nikad ne
    //prosiri preko onoga sto je raspored izmjerio. Zaobljen kao i ispuna
    void outline(const Rect& box, float thickness, const Color& color);

    //STILSKA SLICICA M A P E: traka i tijelo kao dva ravna pravokutnika. Cista geometrija,
    //bez teksta - nista se ne ovisi o fontu. Velicina je sirina kvadrata; mapa je uza i niza
    void folderIcon(float x, float y, float size, const Color& color);

    //Tekst, gornji lijevi kut retka na (x, y). Vraca sirinu koju je zauzeo - isti broj koji
    //obeca textWidth, pa raspored moze nacrtati tocan okvir PRIJE nego se slova stignu vidjeti
    float text(float x, float y, const std::string& value, const Color& color, float scale = 1.0f);

    private:
    //Pravokutnik od cetiri vrha u poretku gore-lijevo, gore-desno, dolje-desno, dolje-lijevo.
    //Uvijek ISTI oblik, razlikuje se samo rezim: sve ostalo oblikuje fragment shader
    void emitQuad(float x, float y, float width, float height, const Color& color,
                  float mode, float thickness, float radius,
                  float u0, float v0, float u1, float v1);
};

}