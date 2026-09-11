#pragma once
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine{

//=============================================================================================
// Sinteticka scena s POZNATIM odgovorom: tocke, poze i opazanja koja iz njih slijede.
//
// Ovo je prvo sto faza S treba, prije ijedne linije solvera. Solver koji se mjeri samo na pravoj
// snimci moze konvergirati u krivo i izgledati uvjerljivo - reprojekcija bude mala, a kamera na
// pogresnom mjestu. Ovdje se tocan odgovor ZNA, pa se greska mjeri prema njemu, a ne prema tome
// koliko je solver zadovoljan sam sa sobom.
//
// KONVENCIJA JE LOOMOVA, i to nije sitnica: poze koje solver vrati na kraju idu u Loomovu kameru.
// Kamera gleda niz -Z, orijentacija je kvaternion, a dubina je -z u koordinatama kamere. Test to
// i provjerava protiv Loomove Camera + projectToPixels, jer bi se razlika u konvenciji inace
// pokazala tek kad se scena nacrta naopako.
//=============================================================================================

//Intrinsike u pikselima. fx i fy su OBA pozitivna: slikovni v raste prema dolje, i to se vidi u
//projekciji (minus uz y), a ne skriveno u predznaku zarista
struct Intrinsics{
    float fx = 600.0f;
    float fy = 600.0f;
    float cx = 320.0f;
    float cy = 240.0f;
    uint32_t width = 640;
    uint32_t height = 480;
};

//Gdje je kamera i kako je okrenuta, u svijetu. Tocka iz svijeta u kameru:
//   xCamera = conjugate(orientation) * (xWorld - position)
struct Pose{
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
};

//Jedno opazanje: koja kamera je vidjela koju tocku i gdje na slici
struct Observation{
    uint32_t camera = 0;
    uint32_t point = 0;
    glm::vec2 pixel{0.0f};
};

struct SyntheticScene{
    std::vector<glm::vec3> points;
    std::vector<Pose> poses;
    std::vector<Observation> observations;
    Intrinsics intrinsics;
};

struct SyntheticConfig{
    uint32_t pointCount = 300;
    uint32_t cameraCount = 8;

    //Kamere stoje na luku oko ishodista i gledaju u njega - kao dron koji obilazi objekt.
    //Luk, a ne puni krug: poze koje se gotovo ne razlikuju ne govore solveru nista novo
    float radius = 8.0f;
    float arc = 1.4f;        //radijani, ukupno
    float height = 1.5f;

    //Poluosi kvadra u kojem su tocke, oko ishodista
    glm::vec3 extent{3.0f, 2.0f, 3.0f};

    //Sum na opazanjima, u pikselima (standardna devijacija po osi). Nula znaci savrsena mjerenja
    float noisePixels = 0.0f;

    uint32_t seed = 1;
    Intrinsics intrinsics{};
};

//Tocka -> piksel. Vraca false kad je tocka iza kamere ili pada izvan slike
bool project(const Pose& pose, const Intrinsics& intrinsics, const glm::vec3& point, glm::vec2& pixel);

//Ista scena za isto sjeme, do zadnjeg bita
SyntheticScene makeSyntheticScene(const SyntheticConfig& config = {});

//"Sve kamere" za medianReprojection
constexpr uint32_t allCameras = 0xFFFFFFFFu;

//Medijan reprojekcije opazanja scene za DANE poze i tocke, u pikselima.
//
//Medijan a ne prosjek: jedno promaseno opazanje pomakne prosjek, a medijan kaze kako stoji
//vecina - i to je mjera po kojoj se faza S i mjeri (cilj: ispod 0.3 px).
//
//I PO KAMERI, ne samo preko cijele scene. Jedna losa kamera od osam u zajednickom medijanu ne
//postoji: sedam dobrih drzi sredinu. Prva verzija testa je na tome i pala - pomak kamere za
//milimetar dao je medijan 0.0000 px, jer je vecina opazanja bila tudja. Solver ce ovo trebati
//jednako: kamera koja je odlutala vidi se samo u svom vlastitom broju
double medianReprojection(const SyntheticScene& scene,
                          const std::vector<Pose>& poses,
                          const std::vector<glm::vec3>& points,
                          uint32_t camera = allCameras);

}
