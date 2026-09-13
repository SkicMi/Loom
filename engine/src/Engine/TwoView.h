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
};

//Relativna poza iz parova opazanja. pixelsA[i] i pixelsB[i] su ista tocka u dva kadra
TwoViewResult relativePose(const std::vector<glm::vec2>& pixelsA,
                           const std::vector<glm::vec2>& pixelsB,
                           const Intrinsics& intrinsics);

}
