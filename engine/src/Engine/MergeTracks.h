#pragma once
#include "Engine/Describe.h"
#include "Engine/TwoView.h"

#include <vector>

namespace Engine{

//=============================================================================================
// Spajanje tragova koji su isti fizicki kut, a nastali su odvojeno.
//
// STO SE POPRAVLJA. Pracenje ide iz kadra u kadar; kad ugao izadje iz slike, trag umire. Kad se
// kamera vrati na isto mjesto, isti kut se nadje ponovno - ali kao NOVI trag, bez veze sa starim.
// Rekonstrukcija ih vidi kao dvije razlicite tocke, i veza izmedju ta dva dijela snimke ne postoji.
//
// Izmjereno na pravoj snimci: kadar 0 dijeli 236 tocaka s kljucnim kadrom 5 i NIJEDNU s kadrom 10.
// Zbog toga siroka baza nije postojala nigdje, pa je nas solver rjesavao 7 kamera od 93 dok ih je
// COLMAP rjesavao 65 od 101 - a on poklapa svaku sliku s iducih deset umjesto samo sa susjednom.
//
// KRIVO SPAJANJE JE GORE OD NIJEDNOG, jer iz njega triangulacija dobije tocku koje nema, a ta tocka
// zatim povlaci pozu za sobom. Zato prolazi samo par koji je prosao TRI sita:
//
//   potpis       uzajamno najbolji, uz prag omjera - odbija ponavljajuce uzorke
//   geometrija   RANSAC nad dvoprizornom pozom; prolaze samo parovi koji se slazu s JEDNIM
//                rjesenjem, a slucajno poklapanje se s njim ne slaze
//   vecina       dva kadra moraju imati dovoljno takvih parova da se rjesenju uopce vjeruje
//
// Trece sito postoji jer RANSAC nad osam nasumicnih parova uvijek nesto nadje; pitanje je slaze li
// se s tim dovoljno njih.
//=============================================================================================

struct MergeConfig{
    DescribeConfig describe;
    RansacConfig ransac;

    //Koliko kljucnih kadrova unaprijed se usporedjuje. COLMAP za video koristi deset, i to je broj
    //koji je i nas nalaz podrzao: veza pukne negdje izmedju petog i desetog kadra
    uint32_t window = 10;

    //Susjedni kadrovi vec dijele tragove kroz pracenje, pa se od ovog razmaka nadalje trazi
    uint32_t nearest = 2;

    //Koliko se parova mora sloziti s dvoprizornom pozom da bi se paru kadrova vjerovalo
    uint32_t minInliers = 20;
};

struct MergeResult{
    std::vector<Observation> observations;   //isti niz, s prepisanim brojevima tocaka
    uint32_t pointCount = 0;                 //koliko ih je ostalo nakon spajanja
    uint32_t mergedPairs = 0;                //koliko je spajanja napravljeno
    uint32_t comparedFrames = 0;             //koliko je parova kadrova uopce usporedjeno
    uint32_t acceptedFrames = 0;             //koliko ih je proslo geometrijsku provjeru
};

//Slike moraju biti poredane isto kao kamere u opazanjima: images[k] je kadar iz kojeg dolaze
//opazanja s camera == k
MergeResult mergeTracks(const std::vector<Observation>& observations,
                        const std::vector<GrayImage>& images,
                        uint32_t pointCount,
                        const Intrinsics& intrinsics,
                        const MergeConfig& config = {});

}
