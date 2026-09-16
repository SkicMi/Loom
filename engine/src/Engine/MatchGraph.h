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

    //=========================================================================================
    // NA KOJOJ SE SIRINI TRAZI I POKLAPA. Nula znaci na izvornoj.
    //
    // Na 4K detektor hvata sum senzora i najfiniju teksturu, a to se izmedju dva kadra ne
    // ponavlja - pa potpis opisuje nesto cega u drugom kadru nema. Izmjereno na dva prava kadra
    // (0050 i 0051), koliko parova prezivi geometrijsku provjeru i koliki im je udio:
    //
    //   sirina   zakrpa   zagladjivanje   poklopljeno   provjereno   prezivi
    //    3840      96          8              633          230        36 %
    //    1920      48          4              967          565        58 %
    //     960      24          2              729          598        82 %
    //
    // Dvije trecine poklapanja na 4K su kriva, na 960 ih je krivo osamnaest posto. Uz gusce
    // uglove (razmak 3 px umjesto 8) na 960 se dobije 4322 provjerena para po paru kadrova -
    // dvadeset puta vise nego s cime smo poceli, i u redu velicine COLMAP-ovih 3564 opazanja po
    // kadru.
    //
    // Opazanja se vracaju u KOORDINATAMA SLIKE KOJU JE POZIVATELJ DAO, ne u smanjenima: tko ovo
    // ukljuci ne smije morati mijenjati intrinsics
    //=========================================================================================
    //ZADANO 960, i to je mjereno na cijelom lancu a ne na paru kadrova. Slika uza od toga se ne
    //dira, pa ista postavka vrijedi i za 480x360 sintetiku i za 4K snimku
    uint32_t workingWidth = 960;
};

struct MatchGraphResult{
    std::vector<Observation> observations;
    uint32_t pointCount = 0;

    uint32_t comparedFrames = 0;
    uint32_t acceptedFrames = 0;
    uint32_t featuresTotal = 0;
    double medianMatchesPerPair = 0.0;

    //KOLIKO JE POLOZAJ ZNACAJKE TOCAN, u pikselima slike koju je pozivatelj dao. Jedan kad se
    //radilo na izvornoj sirini; inace onoliko koliko je slika smanjena.
    //
    //Postoji zato sto reconstruct ima prag prihvacanja kamere u pikselima, a taj prag mora biti
    //veci od ove nepreciznosti - inace se odbijaju kamere koje su tocne koliko podatak dopusta.
    //Izmjereno na 30 kadrova prave snimke, radna sirina 960 (dakle tocnost 4 px):
    //
    //   prag  4 px    5 od 30 kamera
    //   prag  8 px   30 od 30 kamera, reprojekcija 1.199 px
    //   prag 16 px   isto sto i 8
    //
    //Dakle dvostruko od ovoga je dovoljno, a vise ne mijenja nista
    float localizationPixels = 1.0f;
};

MatchGraphResult buildMatchGraph(const std::vector<GrayImage>& images,
                                 const Intrinsics& intrinsics,
                                 const MatchGraphConfig& config = {});

}
