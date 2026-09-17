#pragma once
#include "Engine/Reconstruct.h"

#include <string>

namespace Engine{

//=============================================================================================
// Zapis rekonstrukcije u COLMAP-ov tekstualni format.
//
// To je tocka na kojoj nas solve prelazi dalje: treneri gaussian splattinga ocekuju bas taj
// format (cameras.txt, images.txt, points3D.txt), pa se nasa rekonstrukcija moze trenirati bez
// ijedne promjene u njihovom alatu.
//
// KONVENCIJA JE OVDJE JEDINO STO MOZE TIHO PROMASITI. COLMAP zapisuje rotaciju i pomak iz SVIJETA
// U KAMERU, u klasicnoj konvenciji (+Z naprijed, +Y dolje); nasa Pose je obrnuta (kamera u svijet,
// -Z naprijed). Izvod:
//
//   x_kamera_nasa      = R'(x - c)
//   x_kamera_klasicna  = M x_kamera_nasa = (M R') x - (M R') c
//   dakle R_colmap = M R',  t_colmap = -R_colmap c
//
// Test to i provjerava tako da procita natrag vlastiti zapis i projicira po COLMAP-ovim
// pravilima: ako konvencija promasi, pikseli se ne poklope - a trening bi inace dao kasu i
// krivnja bi se trazila drugdje.
//=============================================================================================

//Zapisuje cameras.txt, images.txt i points3D.txt u zadanu mapu. Mapa mora postojati.
//Imena slika su frame_0000.png, frame_0001.png ... - onim redom kojim kamere idu
//imageNames, ako nije prazan, daje IME po kameri - onako kako se slika stvarno zove na disku.
//
//Bez toga se pisalo frame_0000.png i tako dalje, sto je za usporedbu dvaju rjesenja bilo dovoljno
//jer se imena nisu ni citala. Cim rezultat treba PROCI DALJE, nije: trener splatova model i slike
//spaja bas po imenu, pa je nas solver sve do sada mogao dati poze koje se ne mogu nahraniti
//nicim. Lanac od snimke do scene je ondje bio prekinut, a nigdje to nije pisalo
//=============================================================================================
// Slika u boji, onoliko koliko treba za uzimanje boje tocke. Cetiri bajta po pikselu (RGBA), jer
// je to ono sto dekoder i citac slika vec daju; stride je u PIKSELIMA, ne u bajtovima.
//
// Smije biti i smanjena: boja tocke ne treba punu razlucivost, a 101 kadar 4K u boji je 3.3 GB.
// Tko ju smanji, mora reci koliko - vidi shrink
//=============================================================================================
struct ColourImage{
    const uint8_t* pixels = nullptr;
    uint32_t width = 0, height = 0, stride = 0;
};

//=============================================================================================
// BOJA TOCKE IZ KADROVA KOJI JU VIDE.
//
// MEDIJAN PO KANALU, ne prosjek: tocku u jednom kadru moze zakloniti nesto prolazno, a prosjek bi
// to razmazao preko svih. Medijan jedan takav kadar naprosto ne izabere.
//
// shrink kaze koliko su slike manje od onih u kojima su izmjerena opazanja. Jedan znaci iste
//=============================================================================================
std::vector<glm::u8vec3> pointColours(const Reconstruction& reconstruction,
                                      const std::vector<Observation>& observations,
                                      const std::vector<ColourImage>& images,
                                      uint32_t shrink = 1);

//=============================================================================================
// BOJE TOCAKA NISU UKRAS.
//
// Trener splatova iz points3D.txt cita polozaj I BOJU, a boja postaje pocetna boja gaussiane.
// Dugo je ovdje stajalo "200 200 200" za sve, pa je svaka nasa scena kretala jednolicno siva i
// trener ju je morao cijelu prebojiti - dok COLMAP-ova krece s priblizno tocnim bojama.
//
// To je tiho kostalo SVAKU usporedbu koju smo napravili, jer se nije vidjelo ni u jednoj mjeri
// poza ni tocaka.
//
// Prazan niz znaci kao prije, siva. Inace mora imati onoliko clanova koliko rekonstrukcija ima
// tocaka; tko nema boju, dobiva sivu
//=============================================================================================
bool writeColmapText(const std::string& directory,
                     const Reconstruction& reconstruction,
                     const Intrinsics& intrinsics,
                     const std::vector<Observation>& observations,
                     const std::vector<std::string>& imageNames = {},
                     const std::vector<glm::u8vec3>& colours = {});

}
