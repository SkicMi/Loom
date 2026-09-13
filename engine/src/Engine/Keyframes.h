#pragma once
#include "Engine/SyntheticScene.h"

#include <cstdint>
#include <vector>

namespace Engine{

//=============================================================================================
// Kljucni kadrovi: koji kadrovi uopce ulaze u rekonstrukciju.
//
// PRACENJE I REKONSTRUKCIJA TRAZE SUPROTNO. Lucas-Kanade prati samo pomak prozora, pa mu trebaju
// MALI koraci - izmjereno u S9: na 5.5 st po kadru greska rotacije je 2.50 st, na 0.9 st po kadru
// je 0.33 st. Rekonstrukciji trebaju SIROKE baze, jer dubina bez paralakse ne postoji (S10).
//
// Zato se ne bira jedno ni drugo nego oboje: prati se SVAKI kadar, a u rekonstrukciju ulazi samo
// podskup. Trag preskace kadrove kojih nema u podskupu i tako povezuje daleke poglede, a da ga
// pritom nijedan veliki korak nije trebao preskociti.
//
// Prije ovoga se uzimao svaki n-ti kadar i pratilo se samo njih - sto je bilo najgore od oboje:
// i veliki korak za pracenje, i malo kadrova za rekonstrukciju.
//
// KAD NASTAJE KLJUCNI KADAR. Dva razloga, i oba su nuzna:
//
//   slika se ne da objasniti zaokretom
//                         Prva verzija je gledala MEDIJAN POMAKA i to je bilo krivo: kamera koja
//                         kruzi oko tocke se zaokrece toliko da pomak u slici gotovo ponisti, pa
//                         je od 180 kadrova izabrala samo 6. Pomak u slici nije mjera baze.
//
//                         Ono sto jest: koliko se slika NE DA objasniti jednim afinim preslikom.
//                         Cisti zaokret kamere pomakne sve tocke priblizno afino, bez obzira na
//                         njihovu dubinu; pomak kamere ne moze - bliske tocke odu vise od dalekih.
//                         Zato se na zajednicke tragove namjesti afini preslik i gleda MEDIJAN
//                         OSTATKA. Ostatak je paralaksa izrazena u pikselima, i to je jedina
//                         stvar koja se u ovom trenutku o dubini da doznati - geometrije jos nema.
//                         Prag je udio SIRINE SLIKE, jer isti broj piksela ne znaci isto na 4K i
//                         na 720p
//   veza se gubi          broj tragova zajednickih sa zadnjim kljucnim kadrom padne prenisko. Tad
//                         se uzima PRETHODNI kadar, jos dok veza postoji: kljucni kadrovi koji
//                         nemaju zajednickih tragova su dva odvojena komada, ne jedna snimka
//=============================================================================================

struct KeyframeConfig{
    //Koliko ostatka nakon afinog preslika, kao udio sirine slike. Izmjereno na luku od 60 st
    //preko 180 kadrova: 0.004 daje 19 kljucnih kadrova i promasaj 0.15 st, dok medijan pomaka
    //kao kriterij daje 6 kadrova i 0.89 st
    double minParallaxFraction = 0.004;

    //Ispod ovoliko zajednickih tragova veza sa zadnjim kljucnim kadrom je pretanka
    uint32_t minSharedTracks = 40;

    //Gornja granica, da trosak rekonstrukcije ostane predvidljiv. Nula znaci bez granice
    uint32_t maxKeyframes = 0;
};

struct KeyframeSelection{
    std::vector<uint32_t> frames;          //redni brojevi kadrova iz snimke, rastuci
    std::vector<Observation> observations;  //camera je INDEKS U frames, ne broj kadra
    double medianParallaxPixels = 0.0;      //ostatak nakon afinog preslika, medju kljucnim kadrovima
};

//Izbor kljucnih kadrova iz opazanja koja pokrivaju sve kadrove. Sirina slike ulazi jer je prag
//izrazen kao njezin udio
KeyframeSelection chooseKeyframes(const std::vector<Observation>& observations,
                                  uint32_t frameCount,
                                  uint32_t width,
                                  const KeyframeConfig& config = {});

}
