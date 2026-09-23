#pragma once
#include "Engine/Reconstruct.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Engine{

//=============================================================================================
// USPRAVNI KOORDINATNI SUSTAV za rijesenu scenu.
//
// ZASTO. Rekonstrukcija iz slika ne zna sto je "gore". Njezin sustav je sustav PRVE KAMERE: ta je
// u ishodistu s jedinicnom rotacijom (vidi sidro gaugea u bundleu). Ako je prva kamera bila malo
// nagnuta, nagnuta je cijela scena - u loom-u, u SplatVieweru, u Nukeu i Blenderu. Pod ide koso,
// kocka "na podu" stoji na zidu, a orbita u pregledniku vrti oko krive osi.
//
// "GORE" SE CITA IZ KAMERA. Snimatelj drzi horizont ravno - kamera se naginje gore i dolje i
// zakrece lijevo i desno, ali se ne vrti oko osi pogleda. Zato je njezina DESNA os vodoravna, pa je
// gore smjer okomit na sve desne osi (vidi uprightFrame - prosjek gornjih osi je bio prva ideja i
// test ju je srusio). Iz toga:
//
//   gore        okomito na desne osi posavljenih kamera -> +Y
//   naprijed    smjer pogleda PRVE kamere, spusten u vodoravnu ravninu -> -Z
//   ishodiste   medijan rijesenih tocaka, po osima - pa se scena vrti oko sebe, ne oko prve kamere
//
// KAD SE NE PRIMJENJUJE. Ako su horizonti kamera kosi - snimka koja se vrti oko osi pogleda - desne
// osi nemaju zajednicku vodoravnu ravninu i "gore" iz njih ne postoji. Srednji nagib horizonta se
// mjeri; iznad praga se sustav ne dira i to se kaze, jer je nagnuta scena bolja od scene okrenute
// u nasumicnom smjeru.
//
// NISTA SE NE MIJENJA U KVALITETI. Transformacija je kruta (rotacija i pomak, bez mjerila), a
// projekcija tocke kroz kameru ne ovisi o tome u kojem je sustavu opisana:
//
//   x_kamera = conj(R q) * (R(x - o) - R(c - o)) = conj(q) * (x - c)
//
// Reprojekcija, baza, izdvojena opazanja i polje ostataka ostaju bit po bit ista - test to brani
//=============================================================================================

struct UprightFrame{
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};   //stari sustav -> uspravni
    glm::vec3 origin{0.0f};                        //u STARIM koordinatama; oduzima se prije rotacije
    float coherence = 0.0f;                        //duljina prosjeka gornjih osi, 0 do 1 (izvjestaj)
    float rollDegrees = 0.0f;                      //koliko su horizonti kamera prosjecno kosi
    float disagreementDegrees = 0.0f;              //kut izmedju gore iz desnih osi i prosjeka gornjih
    float tiltDegrees = 0.0f;                      //koliko je scena bila nagnuta
    bool fromRightAxes = false;                    //gore iz desnih osi; false = iz prosjeka gornjih
    bool applied = false;                          //false kad se kamere ne slazu oko smjera "gore"
};

struct UprightConfig{
    //Iznad ovoga nagiba horizonta (srednji kvadratni, u stupnjevima) se ne dira nista: kamere
    //tada nemaju zajednicki vodoravni smjer i "gore" iz njih ne postoji
    float maximumRollDegrees = 20.0f;

    //Druga najmanja svojstvena vrijednost od zbroja r r' mjeri koliko se kamera zakretala oko
    //okomice. Ispod ovoga desne osi ne odredjuju gore i uzima se prosjek gornjih osi
    double minimumYawSpread = 0.005;

    //Prosjek gornjih osi mora biti SLOZAN (duljina do 1) i s procjenom iz desnih osi se ne smije
    //razilaziti vise od ovoga. Pogled ravno dolje u stol od 30 st daje razilazenje od 27 st - dakle
    //45 pusta taj slucaj, a odbija vodoravni smjer pogleda koji desne osi daju kad su horizonti kosi
    float minimumCoherence = 0.5f;
    float maximumDisagreementDegrees = 45.0f;
};

//Racuna uspravni sustav. Ne mijenja nista - samo kaze sto bi trebalo
UprightFrame uprightFrame(const std::vector<Pose>& poses, const std::vector<uint8_t>& posed,
                          const std::vector<glm::vec3>& points, const std::vector<uint8_t>& solved,
                          const UprightConfig& config = {});

//Primijeni sustav na poze i tocke. Ne radi nista ako frame.applied nije postavljen
void applyUpright(const UprightFrame& frame, std::vector<Pose>& poses, std::vector<glm::vec3>& points);

//Isto, na cijelu rekonstrukciju
void applyUpright(const UprightFrame& frame, Reconstruction& reconstruction);

}
