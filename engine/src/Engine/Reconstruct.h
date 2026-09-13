#pragma once
#include "Engine/Bundle.h"
#include "Engine/TwoView.h"

namespace Engine{

//=============================================================================================
// Cijeli lanac: iz samih opazanja do poza i tocaka, bez ijedne poznate poze.
//
//   1 pocetni par     dvije kamere, relativna poza RANSAC-om (S4). Prva kamera postaje ishodiste
//   2 prve tocke      triangulacija onoga sto oba kadra vide (S1)
//   3 nova kamera     PnP iz vec rijesenih tocaka (S2), pa triangulacija onoga sto ta kamera
//                     otkljucava
//   4 bundle          poze i tocke zajedno (S3), s Huberom (S5)
//
// POCETNI PAR: uzima se najudaljeniji kadar koji s prvim jos dijeli dovoljno tocaka, jer sitna
// baza znaci lose odredjenu dubinu (S1 je odbio bazu od milimetra).
//
// KOLIKO TO VRIJEDI, IZMJERENO NA DVIJE SCENE - jer prva sama po sebi zavarava:
//
//   luk 80 st    susjedni par daje ISTI rezultat do zadnje znamenke; razlikuje se samo mjerilo
//                (0.62 naspram 0.097). Kasniji bundle izravna razliku
//   luk 6 st     susjedni par: rotacija 3.29 st, tocke 21.7 m, reprojekcija 1.28 px
//                siroki par:   rotacija 0.021 st, tocke 0.079 m, reprojekcija 0.527 px
//
// Dakle sirok par nije ukras, ali se to vidi tek kad je cijeli luk uzak - kad ni najsira baza nije
// siroka. Na prvoj sceni sam to pokusao dokazati i nisam mogao; dokaz je dala tek druga.
//
// SIRINA LUKA MEDJUTIM JEST BITNA, i to za TOCKE a ne za kamere. Izmjereno na istoj sceni:
//
//   luk kamera     6 st     11 st    23 st    46 st    80 st
//   tocke        0.079 m   0.082    0.012    0.0074   0.0056
//   kamere       0.021 st  0.070    0.036    0.045    0.021
//
// Sve kamere se rijese u svakom slucaju - drzi ih mnostvo tocaka - a dubina tocaka trpi, jer je
// kut pod kojim se zraka sijeku malen.
//
// PNP TREBA POCETNU POZU, a P3P jos nemamo. Nova kamera zato krece od poze najblizeg vec
// rijesenog kadra. Za snimku iz drona je to razumno - susjedni kadrovi su blizu - i u S2 je
// izmjereno da PnP stize i s metra i petnaest stupnjeva promasaja. Kad zatreba pravi P3P, ovo je
// mjesto gdje ulazi.
//
// MJERILO OSTAJE SLOBODNO: pomak pocetnog para je jedinicni, pa je cijela rekonstrukcija tocna do
// jednog broja. Isto kao u S3 i S5.
//=============================================================================================

struct ReconstructConfig{
    RansacConfig ransac;
    double huberPixels = 2.0;

    //Koliko piksela smije promasiti kamera da bi se prihvatila kao rijesena
    double acceptPixels = 4.0;

    //Najmanje vec rijesenih tocaka koje nova kamera mora vidjeti
    uint32_t minPointsForPose = 12;

    uint32_t bundleIterations = 15;
};

struct Reconstruction{
    std::vector<Pose> poses;
    std::vector<uint8_t> posed;          //1 za kameru koja je rijesena

    std::vector<glm::vec3> points;
    std::vector<uint8_t> solved;         //1 za tocku koja je triangulirana

    uint32_t posedCameras = 0;
    uint32_t solvedPoints = 0;
    double medianReprojection = 0.0;     //po opazanjima koja su usla u rekonstrukciju
    bool ok = false;
};

Reconstruction reconstruct(const std::vector<Observation>& observations,
                           size_t cameraCount,
                           size_t pointCount,
                           const Intrinsics& intrinsics,
                           const ReconstructConfig& config = {});

}
