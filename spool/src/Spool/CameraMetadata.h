#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Spool{

//=============================================================================================
// METAPODACI KAMERE IZ SNIMKE - zasad Sonyjev 'rtmd' trag (MP4 s kamera poput ZV-E10 II, FX,
// A7). Uz svaki kadar kamera zapise KLV paket: objektiv, ekspoziciju, senzor, ziroskop.
//
// ZASTO. Splat se trenira iz kadrova, a kadar nije trenutna slika: senzor se cita redak po redak
// (rolling shutter) i skuplja svjetlo cijelo vrijeme ekspozicije (zamucenje pokretom). Oba se daju
// modelirati kad se zna koliko traju - a to kamera zapise:
//
//   8106 / 8109    kadrova u sekundi i vrijeme ekspozicije, kao razlomci
//   810b           ISO po kadru (auto-ISO ga mijenja kroz snimku)
//   e40a           dva broja ciji je omjer VJEROJATNO vrijeme citanja senzora u kadrovima: na
//                  C0257 je 0.562, a iz same snimke je izmjereno 0.55-0.60 (Engine/RollingShutter)
//                  - pretpostavka dok se ne potvrdi na drugom nacinu snimanja
//   8005 / 8004    zarisna objektiva, stvarna i ekvivalent za 35 mm (skup objektiva, RDD 18). Zapis
//                  je 16 bita: gornja cetiri su predznacni dekadski eksponent, donjih dvanaest
//                  mantisa, u metrima - b708 = 1800e-5 m = 18 mm. Na C0257 (Sigma 18-50 na 18 mm)
//                  ekvivalent je 36.8 mm, dakle 2.04 puta: APS-C 1.53 i jos oko 1.33 reza videa
//   e435 / e439 / e43b   ziroskop: uzoraka u sekundi, jedinica po stupnju u sekundi, i uzorci
//                        (po tri osi, 16 bita) - osi su u koordinatama senzora kamere, ne slike
//
// Oznake i zapis su izmjereni na ZV-E10 II (vidi test_camera_metadata), nisu iz specifikacije.
//=============================================================================================

struct CameraMetadata{
    bool present = false;              //snimka ima trag koji se zna citati
    std::string source;                //"sony rtmd"

    double framesPerSecond = 0.0;
    double exposureSeconds = 0.0;      //prvi kadar; 0 kad se ne zna
    double readoutFrames = 0.0;        //vidi e40a gore; 0 kad se ne zna
    std::vector<uint32_t> isoPerFrame;
    double focalMillimetres = 0.0;            //prvi kadar; 0 kad se ne zna
    double equivalentFocalMillimetres = 0.0;  //za 35 mm, s rezom koji kamera sama uracuna

    uint32_t gyroRate = 0;             //uzoraka u sekundi
    float gyroUnitsPerDegreePerSecond = 0.0f;
    std::vector<std::array<int16_t, 3>> gyro;
    uint32_t frames = 0;

    double exposureFrames() const {return exposureSeconds * framesPerSecond;}
};

//Nikad ne baca: snimka bez traga daje present == false
CameraMetadata readCameraMetadata(const std::string& path);

}
