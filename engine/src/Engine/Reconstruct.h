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

    //Koliko se parova provjeri pri izboru pocetnog para. Mjera nije koliko tocaka par dijeli nego
    //koliko ih se iz njega dade triangulirati, a to trazi RANSAC po paru - pa se provjerava samo
    //najprometnije. Vidi komentar uz izbor u Reconstruct.cpp
    uint32_t initialPairCandidates = 30;

    //PARALAKSA. Koliko se dubini smije vjerovati ne odlucuje kut sam po sebi nego kut zajedno sa
    //zaristem i sumom, pa se prag ne zadaje nego IZVODI:
    //
    //    sum od s piksela na zaristu f daje relativnu gresku dubine  s / (f * kut u radijanima)
    //    pa je najmanji smisleni kut                                 s / (f * dopustena greska)
    //
    //Zato ovdje stoji ono sto se stvarno trazi - kolika se greska dubine prihvaca - a ne kut.
    //Isti broj onda vrijedi i za mobitel i za dron i za GoPro, jer se zariste razlikuje a
    //zahtjev ne. Nula gasi provjeru i vraca ponasanje otprije S10.
    //
    //Izmjereno na dronskoj snimci (f = 649 px, 24 kadra): bez provjere p90 udaljenosti tocaka je
    //362304 dosega putanje - cisto smece koje reprojekcija ne kaznjava jer daleka tocka uredno
    //reprojicira ma gdje po svojoj zraki bila. Uz 0.15 rep nestane (p99 = 0.87), 218 tocaka
    //ostane, svih 24 kamera ostane, a reprojekcija se ne pomakne (0.191 px)
    double maxRelativeDepthError = 0.15;

    //Sum u pikselima koji se pripisuje pracenju uglova. Nije mjerenje nego pretpostavka, i zato
    //stoji ovdje gdje se vidi. Mjerena reprojekcija bi bila kriva zamjena: bundle je namjesti na
    //podatke pa ispadne manja od pravog suma, i prag bi izasao prenizak
    double assumedPixelNoise = 0.5;

    uint32_t bundleIterations = 15;

    //=========================================================================================
    // CISCENJE I PONOVNA TRIANGULACIJA, u krug, nakon sto se kamere iscrpe.
    //
    // Postoji zato sto je izmjereno da bundle nije kriv: pusten na COLMAP-ovo gotovo rjesenje
    // iste snimke on ga drzi i jos neznatno popravi (0.7461 -> 0.7380 px), a na nasoj
    // rekonstrukciji sjedne na 3.34 px i ne mice se koliko god iteracija dobio. Rjesenje dakle
    // nije lose zato sto bundle ne zna sici nego zato sto ga put dovede u drugi minimum - a iz
    // njega se izlazi samo mijenjanjem onoga sto bundle dobije.
    //=========================================================================================

    //Koliko krugova. Nula iskljucuje i vraca ponasanje otprije
    uint32_t refineRounds = 5;

    //Donja granica praga za izbacivanje opazanja, u pikselima
    double filterPixels = 4.0;

    //Prag je visekratnik TRENUTNOG medijana dok je on iznad filterPixels. Fiksni prag nad
    //rjesenjem koje je tek na tri i pol piksela odbacio bi u prvom krugu pola scene; ovako je
    //ciscenje u pocetku blago i steze se samo od sebe
    double filterMedians = 2.5;

    //KOLIKO CESTO USRED GRADNJE, kao visekratnik broja vec rijesenih kamera. 1.25 znaci: ocisti
    //kad ih naraste za cetvrtinu.
    //
    //Ciscenje samo na kraju lijeci posljedicu umjesto uzroka - do tada je put vec zasao u losiji
    //minimum, a lokalni korak iz njega ne izlazi. Usput se u njega uopce ne ulazi.
    //
    //Nula znaci samo na kraju. Cijena je linearna u broju ciscenja, a svako je jedan prolaz kroz
    //sve tocke plus bundle
    double refineGrowth = 1.25;
};

struct Reconstruction{
    std::vector<Pose> poses;
    std::vector<uint8_t> posed;          //1 za kameru koja je rijesena

    std::vector<glm::vec3> points;
    std::vector<uint8_t> solved;         //1 za tocku koja je triangulirana

    uint32_t posedCameras = 0;
    uint32_t solvedPoints = 0;
    double medianReprojection = 0.0;     //po opazanjima koja su usla u rekonstrukciju
    double parallaxLimitDegrees = 0.0;   //kut izveden iz zarista i suma, onaj koji je stvarno vrijedio
    bool ok = false;
};

Reconstruction reconstruct(const std::vector<Observation>& observations,
                           size_t cameraCount,
                           size_t pointCount,
                           const Intrinsics& intrinsics,
                           const ReconstructConfig& config = {});

}
