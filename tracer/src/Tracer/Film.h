#pragma once
//=============================================================================================
// FILM - sto render vrati, i kako se to slozi u sliku.
//
// Render NE vraca gotovu sliku nego slojeve, jer je slaganje odluka umjetnika, a ne tracera:
//
//   cg          CG objekti, RGBA, PREMULTIPLICIRANO (alfa = pokrivenost piksela objektima)
//   background  ono sto kamera vidi gdje objekata nema: nebo (ako ga kamera vidi)
//   shadow      mnozitelj za sve sto NIJE CG: 1 = nema sjene; 0.4 = shadow catcher tu prima
//               sjenu koja uzme 60 % svjetla. Snimka se pomnozi njime - tako CG objekt baca
//               sjenu na pravi pod
//   albedo, normal, depth - pomocni slojevi (AOV): za kompoziting i za uklanjanje suma
//
// Slaganje (composite):
//
//   Environment   cg + shadow * background              (nebo iza, kao u Blenderu)
//   Transparent   cg, a sjena postane crna s alfom      (za Nuke/AE preko bilo cega)
//   Plate         cg + (1 - a) * shadow * snimka        (CG preko prave snimke)
//
// Sve je LINEARNO. Snimka se prije slaganja pretvori iz sRGB-a u linearno, pa natrag - kroz
// Standard prikaz vraca se bit po bit ista, sto je VFX zahtjev: ploca se ne smije promijeniti.
//=============================================================================================
#include "Tracer/Scene.h"

#include <cstdint>
#include <vector>

namespace Tracer{

constexpr float NoDepth = 1e10f;            //dubina piksela u kojem nema nicega (kao Blenderov clip end)

struct Frame{
    uint32_t width = 0, height = 0;
    uint32_t samples = 0;
    bool denoised = false;
    std::vector<float> cg;                  //RGBA
    std::vector<float> background;          //RGB
    std::vector<float> shadow;              //RGB
    std::vector<float> albedo;              //RGB
    std::vector<float> normal;              //XYZ, svjetski prostor
    std::vector<float> depth;               //udaljenost duz -Z kamere; NoDepth kad nema pogotka
    std::vector<float> variance;            //varijanca procjene luminancije (sum)
    size_t pixelCount() const {return size_t(width) * height;}
};

enum class Backdrop{ Environment, Transparent, Plate };

//RGBA linearno. plate: linearna ili sRGB tekstura bilo koje velicine, rastegnuta preko kadra
std::vector<float> composite(const Frame& frame, Backdrop backdrop, const Texture* plate = nullptr);

//PRIKAZ. Standard: sRGB krivulja, sve iznad 1 odrezano (tocno za snimku). AgX: filmska krivulja
//(Troy Sobotka) - svijetlo se ne odreze nego zasiti prema bijelom kao na filmu; za cisti CG
enum class ViewTransform{ Standard, AgX };
std::vector<uint8_t> toDisplay(const std::vector<float>& rgba, uint32_t width, uint32_t height,
                               ViewTransform view, float exposureStops = 0.0f);

//Dubina kao siva slika: blizu bijelo, daleko crno, raspon od najblize do najdalje plohe
std::vector<uint8_t> depthToDisplay(const Frame& frame);
//Normala kao boja: 0.5 + 0.5 * n
std::vector<uint8_t> normalToDisplay(const Frame& frame);
std::vector<uint8_t> albedoToDisplay(const Frame& frame);

}
