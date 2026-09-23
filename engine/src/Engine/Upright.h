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

//Odakle je dosao "gore"
enum class UprightSource{
    None,       //nije primijenjeno - ni kamere ni scena ne odredjuju gore
    Cameras,    //okomito na desne osi kamera; snimatelj je drzao horizont ravno
    Plane       //normala dominantne vodoravne ravnine - stol ili pod
};

struct UprightFrame{
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};   //stari sustav -> uspravni
    glm::vec3 origin{0.0f};                        //u STARIM koordinatama; oduzima se prije rotacije
    float coherence = 0.0f;                        //duljina prosjeka gornjih osi, 0 do 1 (izvjestaj)
    float rollDegrees = 0.0f;                      //koliko su horizonti kamera prosjecno kosi
    float disagreementDegrees = 0.0f;              //kut izmedju gore iz desnih osi i prosjeka gornjih
    float instabilityDegrees = 0.0f;               //najvece razilazenje gore iz POLOVICA kamera
    float tiltDegrees = 0.0f;                      //koliko je scena bila nagnuta
    bool fromRightAxes = false;                    //gore iz desnih osi; false = iz prosjeka gornjih
    UprightSource source = UprightSource::None;
    float planeShare = 0.0f;                       //udio tocaka na ravnini, kad je gore iz ravnine
    float planeDegrees = 0.0f;                     //kut normale ravnine od prosjeka gornjih osi
    bool applied = false;                          //false kad se kamere ne slazu oko smjera "gore"
};

struct UprightConfig{
    //Iznad ovoga nagiba horizonta (srednji kvadratni, u stupnjevima) kamere NE odredjuju gore.
    //Izmjereno na pravim snimkama: vrata 0.83 st, kameni zid 3.77 st - a joystick 17.88 st, jer
    //kamera okrenuta gotovo ravno dolje u stol nema horizont pa je ruka slobodno vrti. Lazna
    //rjesenja uz kose horizonte takodjer padaju oko 18-19 st. Prvi prag (20) je bio iznad oboje
    float maximumRollDegrees = 10.0f;

    //RAVNINA SCENE ima prednost pred kamerama (vidi uprightFrame). Najveca ravnina na koju kamere
    //gledaju ODOZGO je stol ili pod, i njezina normala JEST gore
    //Normala okrenuta prema kamerama, pomnozena s prosjekom gornjih osi: pod daje cos(pogled dolje),
    //zid u koji se gleda odozgo daje negativno. Prvi prag je bio 45 st BEZ predznaka od prosjeka
    //gornjih osi - i odbio je stol, jer je kod pogleda 60 st dolje prosjek nagnut upravo 60 st.
    //0.3 pusta pod do oko 72 st pogleda dolje, a zid u koji se gleda odozdo tek preko 17 st
    float minimumUpAlignment = 0.3f;
    float minimumPlaneShare = 0.15f;         //koliki dio tocaka mora lezati na ravnini

    //Druga najmanja svojstvena vrijednost od zbroja r r' mjeri koliko se kamera zakretala oko
    //okomice. Ispod ovoga desne osi ne odredjuju gore i uzima se prosjek gornjih osi
    double minimumYawSpread = 0.005;

    //Prosjek gornjih osi mora biti SLOZAN (duljina do 1): to odbija kose horizonte, kad desne osi
    //daju vodoravni smjer pogleda umjesto gore. Obje negativne kontrole padaju bas ovdje.
    //
    //Razilazenje s procjenom iz desnih osi smije biti VELIKO. Prvi prag je bio 45 st, postavljen
    //prema testu s pogledom 30 st dolje - i odbio je stvarnu snimku joysticka: kamera iz ruke gleda
    //predmet na stolu prosjecno 48 st odozgo, razilazenje je 47.7 st, scena je ostala u sustavu
    //prve kamere i u pregledniku stajala naopako. Razilazenje je kod ispravnog slucaja jednako kutu
    //pogleda prema dolje. Iznad 80 st (pogled gotovo ravno dolje, dron u nadiru) prosjek gornjih osi
    //je vodoravan i vise ne kaze ni predznak - tada se ne dira nista
    float minimumCoherence = 0.5f;
    float maximumDisagreementDegrees = 80.0f;

    //STABILNOST: gore iz jedne polovice kamera se mora slagati s gore iz svih. To je mjera
    //sigurnosti koja ne laze - preostali nagib horizonta laze, jer ga prilagodba osi upije
    float maximumInstabilityDegrees = 3.0f;
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
