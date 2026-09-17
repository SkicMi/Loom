#pragma once
#include "Engine/Track.h"

#include <glm/glm.hpp>
#include <vector>

namespace Engine{

//=============================================================================================
// ZNACAJKE U PROSTORU MJERILA, sa subpikselnim vrhom.
//
// ZASTO OVO POSTOJI. Izmjereno na pravoj snimci, i to je jedini broj koji je nakon svega ostao:
// nase su tocke udaljene 7.9 posto opsega putanje od najblize COLMAP-ove, a njegove su medjusobno
// razmaknute 0.313 posto - dakle dvadeset pet puta grublje. Racun kaze zasto: uz zariste 5285 px,
// kvantizaciju polozaja od 4 px i bazu od 4.7 stupnjeva, greska dubine izlazi oko procenta dubine.
//
// Ta kvantizacija dolazi odatle sto se znacajke traze na smanjenoj slici (960 px od 3840). A na
// smanjenu se islo jer na punoj razlucivosti Shi-Tomasijev detektor hvata sum senzora koji se
// izmedju kadrova ne ponavlja: udio poklapanja koja prezive geometriju pada s 82 na 36 posto.
//
// DVA PUTA IZ TOGA SU PROBANA I PALA:
//
//   radna sirina 1920   dvaput, i drugi put sa svime popravljenim. Gore po svakoj mjeri, jer se
//                       detektor vrati na sum: 312 542 opazanja naspram 540 040 na 960
//   dotjerivanje        polozaj se vrati na punu sliku i ondje dotjera. Vraca tridesetak posto
//                       nedosljednosti, a odustane na polovici tragova - premalo da se isplati
//
// OSTAJE ONO STO COLMAP RADI: naci znacajku NA MJERILU NA KOJEM ONA POSTOJI. Slika se zagladjuje
// niz niz mjerila, znacajka je ekstrem razlike dvaju susjednih zagladjenja, a njezin se vrh zatim
// nadje ispod piksela - kvadratnim fitom kroz susjedstvo u sve tri osi.
//
// Time je polozaj tocan ispod piksela NA PUNOJ SLICI, a potpis se racuna na mjerilu na kojem je
// znacajka nadjena - dakle ne mjeri ni sum ni strukturu koja je za njega prekrupna.
//
// STO OVO NIJE. Nije puni SIFT: nema orijentacije po histogramu gradijenata (to je u Sift.h) i
// nema visestrukih orijentacija po znacajki. Ovo je detektor; potpis je drugdje.
//=============================================================================================

struct Keypoint{
    glm::vec2 pixel{0.0f};   //u koordinatama slike koja je predana, ispod piksela
    float scale = 0.0f;      //sigma na kojoj je nadjena, u pikselima te iste slike
    float strength = 0.0f;   //|DoG| u vrhu, nakon dotjerivanja; u jedinicama 0..1
};

struct ScaleSpaceConfig{
    //Koliko puta se slika prepolovi. Cetiri oktave na 4K znaci da se najkrupnija znacajka trazi na
    //480 px sirine - dalje od toga nema strukture koja bi se jos zvala znacajkom
    uint32_t octaves = 4;

    //Koliko se mjerila gleda unutar oktave. Tri je Loweov izbor i nije proizvoljan: toliko ih
    //treba da se ekstrem po mjerilu uopce dade odrediti, a vise ih ne donosi vise ponovljivih
    uint32_t perOctave = 3;

    //Zagladjivanje na kojem oktava pocinje. 1.6 je isto Loweovo, i pretpostavlja da slika vec nosi
    //oko 0.5 px vlastitog zagladjivanja - sto senzorska slika i nosi
    float baseSigma = 1.6f;

    //ZASTO PRAG KONTRASTA, a ne najjacih N. Sum daje ekstreme svugdje, i to slabe; prag ih odbija
    //prije nego se uopce sortiraju. Mjeri se |DoG| nakon dotjerivanja, u jedinicama u kojima je
    //slika 0..1
    float contrast = 0.015f;

    //RUB NIJE ZNACAJKA. Duz ruba je DoG jednako jak posvuda, pa je polozaj po rubu neodredjen - i
    //to je tocno ono sto trazenju najvise steti, jer se takva znacajka u drugom kadru nadje
    //pomaknuto po rubu. Odbija se po omjeru svojstvenih brojeva Hessijana, kao kod Harrisa
    float edgeRatio = 10.0f;

    //Koliko ih najvise, po jacini. Nula znaci sve
    uint32_t maxKeypoints = 20000;

    //Najmanji razmak medju zadrzanima, u pikselima predane slike. Nula iskljucuje. Postoji iz istog
    //razloga kao kod uglova: bez njega se sve skupi na najkontrastniji detalj
    float minDistance = 0.0f;
};

//Znacajke poredane po jacini, najjaca prva
std::vector<Keypoint> detectScaleSpace(const GrayImage& image, const ScaleSpaceConfig& config = {});

}
