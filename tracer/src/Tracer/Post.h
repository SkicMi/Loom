#pragma once
//=============================================================================================
// POST PROCESSING - ono sto leca, senzor i film rade slici POSLIJE scene.
//
// Tracer racuna svjetlo koje stigne do kamere. Prava kamera ga ne zapise tako cisto: jako svjetlo
// se rasprsi u staklu objektiva (bloom), rubovi kadra dobiju manje svjetla (vinjeta), boje se na
// rubu razidju (kromatska aberacija), a senzor doda zrno. CG bez toga izgleda "previse cisto" uz
// pravu snimku - i obrnuto, snimka to vec ima, pa CG koji to nema odmah odskoci.
//
// REDOSLIJED je fizikalni, od svjetla prema zapisu:
//
//   ekspozicija -> bloom -> kromatska aberacija -> vinjeta     (leca, na linearnom svjetlu)
//               -> balans bijele -> kontrast, zasicenje         (obrada, kao u kameri/gradingu)
//               -> zrno                                          (senzor/film, zadnje)
//
// Sve je na LINEARNOJ slici (prije prikaza, Standard ili AgX). Bloom je OCUVANJE ENERGIJE: slika
// se mijesa sa svojom zamucenom kopijom, pa zbroj svjetla ostaje isti - samo se preraspodijeli
// oko svijetlih mjesta (test_post to mjeri). S pragom 0 svijetli sve malo, kao prava leca; prag
// ogranici sjaj na ono sto je svjetlije od njega.
//
// EXR ostaje CIST (bez posta): kompozitor u Nukeu to radi sam i treba sirovo svjetlo. Post ide u
// PNG i u prozor, i moze se mijenjati POSLIJE rendera bez ponovnog racunanja.
//=============================================================================================
#include <cstdint>
#include <vector>

namespace Tracer{

struct PostSettings{
    bool enabled = true;
    float exposure = 0.0f;              //blende; ovdje se pece, pa prikaz ide s 0

    float bloom = 0.0f;                 //udio svjetla koji se rasprsi (0.02-0.1 prirodno)
    float bloomRadius = 0.06f;          //doseg, udio sirine slike
    float bloomThreshold = 0.0f;        //luminancija ispod koje nema sjaja (0: sve malo svijetli)

    float chromaticAberration = 0.0f;  //pomak crvenog/plavog u pikselima na rubu kadra
    float vignette = 0.0f;              //0..1: koliko svjetla gube kutovi

    float temperature = 6500.0f;        //K; balans bijele (6500 neutralno, nize toplije)
    float tint = 0.0f;                  //-1..1: zeleno <-> magenta
    float contrast = 1.0f;              //oko srednje sive 0.18, u logaritmu
    float saturation = 1.0f;

    float grain = 0.0f;                 //standardna devijacija zrna na srednje sivoj
    uint32_t grainSeed = 0;             //drugi kadar sekvence, drugo zrno

    //Mijenja li ista sliku
    bool active() const;
};

//Linearni RGBA (premultiplicirano), sirina x visina. Alfa se ne dira (osim kod aberacije, gdje
//putuje s bojom). Radi na svim jezgrama
void applyPost(std::vector<float>& rgba, uint32_t width, uint32_t height, const PostSettings& settings);

//Bijela tocka crnog tijela u linearnom sRGB-u, luminancije 1 (Kim et al., Planckov lokus)
void blackBody(float kelvin, float rgb[3]);

}
