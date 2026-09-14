#pragma once
#include "Engine/Describe.h"
#include "Engine/TwoView.h"

#include <vector>

namespace Engine{

//=============================================================================================
// Korespondencije iz POKLAPANJA, ne iz pracenja.
//
// RAZLIKA U VRSTI. Tracker nosi ugao iz kadra u kadar i umire cim ugao izadje iz slike; sto je
// jednom izgubljeno, izgubljeno je. Ovdje se u svakom kljucnom kadru znacajke nadju NEOVISNO i
// zatim povezu s onima u drugim kadrovima. Trag je time posljedica poklapanja, a ne njegov uvjet.
//
// ZASTO JE TO VAZNO BAS OVDJE. Izmjereno na pravoj snimci: nas lanac je davao oko 300 opazanja po
// kljucnom kadru, COLMAP 3564, i njegov solver je rjesavao 65 kamera od 101 dok je nas rjesavao 7
// od 93. Spajanje vec pracenih tragova je popravilo reprojekciju s 1.553 na 0.924 px, ali broj
// kamera nije pomaknulo - jer spajanje popravlja tocke, a ne stvara nove veze. Ovo ih stvara.
//
// TRI SITA, ista kao kod spajanja i iz istog razloga - krivo poklapanje daje tocku koje nema, a ta
// tocka zatim povlaci pozu za sobom:
//
//   potpis       uzajamno najbolji uz prag omjera
//   polumjer     kandidat mora biti blizu u slici. Na snimci se kamera izmedju bliskih kadrova
//                pomakne ograniceno, pa je poklapanje na drugom kraju slike greska a ne nalaz
//   geometrija   RANSAC nad dvoprizornom pozom; prolaze samo parovi koji se slazu s JEDNIM rjesenjem
//
// TRAG JE POVEZANA KOMPONENTA poklapanja. Ako je A u kadru 1 isto sto i B u kadru 2, a B isto sto i
// C u kadru 3, onda su sva tri jedna tocka - i to bez ijednog pracenja kroz kadrove izmedju njih.
//=============================================================================================

struct MatchGraphConfig{
    TrackConfig detect;        //za detectCorners; maxCorners je ovdje bitno veci nego pri pracenju
    DescribeConfig describe;
    RansacConfig ransac;

    //Koliko kadrova unaprijed se usporedjuje. COLMAP za video koristi deset
    uint32_t window = 10;

    //Najveci pomak u slici koji se jos smatra mogucim, kao udio sirine
    float searchFraction = 0.25f;

    //Koliko se parova mora sloziti s dvoprizornom pozom da bi se paru kadrova vjerovalo
    uint32_t minInliers = 20;

    //Tocka mora biti vidjena iz barem toliko kadrova da udje u rezultat
    uint32_t minViews = 2;

    //Izvodi li se velicina zakrpe iz sirine slike. Vidi mjerenje u MatchGraph.cpp - razlika izmedju
    //zakrpe 16 i 96 na 4K je cetrnaest puta vise dobrih parova
    bool patchFromWidth = true;
};

struct MatchGraphResult{
    std::vector<Observation> observations;
    uint32_t pointCount = 0;

    uint32_t comparedFrames = 0;
    uint32_t acceptedFrames = 0;
    uint32_t featuresTotal = 0;
    double medianMatchesPerPair = 0.0;
};

MatchGraphResult buildMatchGraph(const std::vector<GrayImage>& images,
                                 const Intrinsics& intrinsics,
                                 const MatchGraphConfig& config = {});

}
