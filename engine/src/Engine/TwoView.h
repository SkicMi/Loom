#pragma once
#include "Engine/SyntheticScene.h"

namespace Engine{

//=============================================================================================
// Inicijalizacija iz DVA pogleda: relativna poza iz samih opazanja, bez ijedne poznate poze.
//
// Sve dosad (S1 triangulacija, S2 PnP, S3 bundle) pretpostavlja da poze vec otprilike znamo. Na
// pravoj snimci ih nemamo. Odavde krece: dva kadra, ista tocka vidjena u oba, i nista vise.
//
// Osmotockovni algoritam: za tocku vidjenu kao a u prvoj i b u drugoj kameri vrijedi b' E a = 0,
// gdje je E = [t]x R. To je linearno u devet clanova E, pa osam tocaka daje sustav ciji je
// nul-prostor trazeno rjesenje. E se zatim projicira natrag na svoj oblik (singularne vrijednosti
// 1, 1, 0) i rastavi na cetiri kandidata (R, t); pravi se bira po tome koje tocke leze ISPRED
// obje kamere.
//
// MJERILA NEMA. Iz dvije slike se ne moze doznati je li scena mala i blizu ili velika i daleko -
// pomak je zato jedinicne duljine, i to je svojstvo, ne nedostatak. Isto mjerilo koje je u S3
// ostalo slobodno uz fiksnu kameru.
//
// KONVENCIJA: Loom i Engine gledaju niz -Z s +Y gore, a epipolarna geometrija se pise u klasicnoj
// gdje kamera gleda +Z s +Y dolje. Pretvorba je zrcaljenje diag(1,-1,-1), i racuna se unutra -
// izvod stoji uz kod, jer je to mjesto gdje se greska ne vidi dok ne okrene cijelu scenu naopako.
//=============================================================================================

struct TwoViewResult{
    //Kamera B u sustavu kamere A: A je u ishodistu s jedinicnom orijentacijom, a |position| = 1
    Pose pose;

    uint32_t used = 0;      //koliko je parova uslo u racun
    uint32_t inFront = 0;   //koliko ih je ispred obje kamere za izabrano rjesenje
    bool solved = false;

    //Za robusnu inacicu: 1 za par koji se slaze s rjesenjem, 0 za promasen. Prazno kad se racunalo
    //nad svim parovima bez provjere
    std::vector<uint8_t> inliers;
    uint32_t inlierCount = 0;
};

//RANSAC: osam nasumicnih parova, procjena, pa se prebroje oni koji se s njom slazu. Ponovi se
//dovoljno puta da barem jedan uzorak bude bez ijednog promasaja, i na kraju se racuna jos jednom -
//samo nad onima koji se slazu
struct RansacConfig{
    uint32_t iterations = 300;

    //Koliko piksela smije promasiti par da bi se jos smatrao ispravnim. Sampsonova udaljenost, u
    //pikselima - ne u normaliziranim jedinicama, jer prag u pikselima je ono sto se da procijeniti
    double thresholdPixels = 1.5;

    uint32_t seed = 1;
};

//Relativna poza iz parova opazanja. pixelsA[i] i pixelsB[i] su ista tocka u dva kadra.
//SVI parovi ulaze u racun - jedan promasen par povlaci rjesenje, pa je ovo za cist ulaz
TwoViewResult relativePose(const std::vector<glm::vec2>& pixelsA,
                           const std::vector<glm::vec2>& pixelsB,
                           const Intrinsics& intrinsics);

//Isto, ali s RANSAC-om: krivo poklapanje je na pravoj snimci pravilo a ne iznimka, i najmanji
//kvadrati ga ne mogu prezivjeti - jedan par koji promasuje za pola slike vuce jednako jako kao
//stotinu ispravnih
TwoViewResult relativePoseRobust(const std::vector<glm::vec2>& pixelsA,
                                 const std::vector<glm::vec2>& pixelsB,
                                 const Intrinsics& intrinsics,
                                 const RansacConfig& config = {});

}
