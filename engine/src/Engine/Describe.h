#pragma once
#include "Engine/Track.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Engine{

//=============================================================================================
// Potpis okoline ugla, takav da se dade usporediti s bilo kojim drugim kadrom.
//
// ZASTO OVO POSTOJI. Pracenje ide iz kadra u kadar i zna samo gdje je ugao BIO; kad izadje iz
// kadra, trag umire i vise se nikad ne poveze s istim mjestom. Izmjereno na pravoj snimci: od 1508
// smrti tragova 1366 ih je bilo zato sto su izasli iz slike, a kadar 0 nije dijelio NIJEDNU tocku
// s kljucnim kadrom 10. Zbog toga siroka baza nije postojala nigdje u podacima, a bez nje dubina
// ne valja.
//
// Deskriptor to rjesava tako sto ugao nosi svoj potpis sa sobom: dva kadra koja nisu susjedna mogu
// se usporediti izravno. COLMAP na istoj snimci tako dobiva 3564 opazanja po kameri, mi oko 300.
//
// KAKO. Binarni potpis u duhu ORB-a, i svaki dio ima svoj razlog:
//
//   smjer       iz tezista svjetline u krugu oko ugla. Bez njega isti ugao snimljen pod drugim
//               kutom daje drugaciji potpis, pa se ne prepozna - a kamera koja obilazi prostor
//               upravo mijenja kut
//   parovi      256 unaprijed zadanih parova piksela; bit je "je li prvi svjetliji od drugog".
//               Usporedba je time brojanje razlicitih bitova, dakle jedna procesorska naredba
//   zamucenje   uzorkuje se iz zaglađene slike, jer pojedinacan piksel je sum a ne podatak
//
// STO OVO NIJE. Nije SIFT: nema mjerila i slabiji je na velikim promjenama pogleda. Za snimku iz
// ruke ili s gimbala, gdje se izmedju dva kljucna kadra kamera pomakne malo, to je razmjena koja
// se isplati - potpis je 32 bajta i usporedba je instantna.
//=============================================================================================

struct DescribeConfig{
    //Polumjer kruga iz kojeg se racuna smjer i iz kojeg se uzorkuju parovi
    uint32_t patch = 16;

    //Sigma zaglađivanja prije uzorkovanja. Nula znaci bez njega - i tada potpis mjeri sum
    float smoothing = 1.5f;

    //Prag omjera: najbolji pogodak mora biti barem ovoliko puta bolji od drugog po redu. Bez toga
    //se ponavljajuci uzorak - fuga, resetka, sahovnica - poklapa sam sa sobom bilo gdje
    float ratio = 0.8f;

    //Najveca dopustena udaljenost potpisa, u bitovima od 256
    uint32_t maxDistance = 64;
};

struct Descriptor{
    std::array<uint64_t, 4> bits{};   //256 bita
    float angle = 0.0f;               //smjer okoline, radijani
    bool valid = false;
};

//Potpis okoline zadane tocke. False kad je tocka preblizu rubu da bi krug stao u sliku
bool describe(const GrayImage& image, const glm::vec2& point, Descriptor& out,
              const DescribeConfig& config = {});

//Potpisi za vise tocaka odjednom. Zaglađivanje se radi JEDNOM za cijelu sliku umjesto po tocki -
//isto pravilo kao za piramidu u trackeru, i iz istog razloga
std::vector<Descriptor> describeAll(const GrayImage& image, const std::vector<glm::vec2>& points,
                                    const DescribeConfig& config = {});

//Koliko se bitova razlikuje
uint32_t distance(const Descriptor& a, const Descriptor& b);

struct Match{
    uint32_t from = 0;
    uint32_t to = 0;
    uint32_t distance = 0;
};

//Poklapanje dva skupa potpisa. Uzajamno - par prolazi samo ako je svaki drugome najbolji - i uz
//prag omjera. Oba uvjeta postoje zato sto se ponavljajuci uzorci inace poklapaju gdje god
std::vector<Match> matchDescriptors(const std::vector<Descriptor>& from,
                                    const std::vector<Descriptor>& to,
                                    const DescribeConfig& config = {});

}
