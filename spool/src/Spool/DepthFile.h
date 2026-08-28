#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Spool{

//Karta dubine: jedan broj po pikselu, ne boja.
//
//Nije Image i namjerno nije: osam bita po kanalu je za dubinu premalo. Scena duboka trideset
//metara u 256 koraka ima korak od 12 centimetara, a normala izvedena iz takve dubine je
//stepenica. Zato se sve ucitava u float, kakav god bio zapis u fileu.
//
//Sto broj ZNACI - metri, disparitet, sto je blizu a sto daleko - ovdje ne pise. Spool
//izvjestava sto u fileu stoji; tumacenje je na sloju koji zna kroz kakvu je kameru snimljeno.
struct DepthImage{
    std::vector<float> values;
    uint32_t width = 0;
    uint32_t height = 0;

    //Koliko je bita nosio zapis: 8, 16 ili 32. Jedini nacin da se poslije zna je li grubost
    //u slici dosla iz modela ili iz formata u koji je spremljena
    uint32_t sourceBits = 0;

    //Raspon koji je u fileu stvarno bio. Cjelobrojni zapisi se dijele na 0..1, pa je ovo
    //jedini trag o tome koliko je od tog raspona iskoristeno
    float minValue = 0.0f;
    float maxValue = 0.0f;

    bool isValid() const {return width > 0 && height > 0 && !values.empty();}
    size_t pixelCount() const {return size_t(width) * height;}

    float at(uint32_t x, uint32_t y) const {return values[size_t(y) * width + x];}
};

//Iz datoteke. Prepoznaje se po sadrzaju, ne po nastavku:
//
//   PFM  - float po pikselu, bez gubitka. Format kojim se dubina razmjenjuje u istrazivanju
//          (MiDaS, DPT i dalje), i jedini ovdje koji nista ne zaokruzuje
//   PGM  - 8 ili 16 bita, cjelobrojno
//   PNG i ostalo sto stb razumije - 16 bita ako ih file nosi, inace 8
//
//Baca s putanjom i razlogom. Dubina koja se tiho ucitala kao nista je scena bez ijedne
//plohe, i to se vidi tek tri sloja dalje
DepthImage loadDepthImage(const std::string& path);

//U PFM. Float, bez gubitka, i cita ga svaki alat koji dubinu uopce cita.
//
//PNG namjerno nije ponuden za pisanje: encoder koji Spool nosi zna samo osam bita, a dubina
//u osam bita je dubina koju smo upravo bacili
void saveDepthImage(const std::string& path, const DepthImage& depth);

}
