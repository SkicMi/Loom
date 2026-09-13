#pragma once
#include "Engine/SyntheticScene.h"

namespace Engine{

//=============================================================================================
// Triangulacija: iz poznatih poza i opazanja natrag do tocke u prostoru.
//
// Jedno opazanje ne odredjuje tocku nego ZRAKU - piksel kaze u kojem smjeru tocka lezi, ne koliko
// je daleko. Dvije zrake iz razlicitih kamera sijeku se u tocki, a sa sumom se mimoilaze, pa se
// trazi tocka NAJBLIZA svim zrakama. To je linearan sustav 3x3 i rjesava se izravno, bez iteracije.
//
// GDJE OVO PUCA, i zato vraca bool a ne tocku: kad su zrake gotovo paralelne. Dvije kamere koje
// gledaju s gotovo istog mjesta ne znaju nista o dubini - sustav postane gotovo singularan i
// "rjesenje" odleti u beskonacnost. Takav slucaj mora reci da ne zna, jer broj koji izgleda kao
// odgovor je gori od nikakvog odgovora.
//
// Ovo je POCETNA tocka, ne konacna: minimizira udaljenost do zraka, a ne reprojekciju u
// pikselima. Razlika je mala kad je sum mali, i nju ce popraviti bundle adjustment.
//=============================================================================================

//Jedno vidjenje iste tocke: koja kamera i gdje na slici
struct View{
    uint32_t camera = 0;
    glm::vec2 pixel{0.0f};
};

//Najveci kut izmedju bilo koje dvije zrake istog vidjenja, u stupnjevima - PARALAKSA.
//
//Ovo je mjera koja kaze koliko se dubini smije vjerovati. Tocka koju dvije kamere vide pod kutom
//od pola stupnja lezi negdje na dugackom komadu zrake i nijedna druga mjera to ne prijavljuje:
//reprojekcija je uredna ma gdje po toj zraki tocka bila. Izmjereno na dronskoj snimci gdje je
//cetvrtina tocaka odletjela u beskonacnost uz reprojekciju od 0.191 px.
//
//Racuna se preko atan2 u double aritmetici, ne preko acos: acos malog kuta iz float skalarnog
//produkta ocita nulu vec ispod 0.04 st, a nas zanima bas to podrucje
double parallaxDegrees(const std::vector<Pose>& poses,
                       const Intrinsics& intrinsics,
                       const std::vector<View>& views);

//Piksel -> smjer zrake u SVIJETU, jedinicne duljine. Obrnuto od project(), i to je jedina
//formula kojom se piksel vraca u prostor
glm::vec3 rayDirection(const Pose& pose, const Intrinsics& intrinsics, const glm::vec2& pixel);

//Tocka najbliza svim zrakama. False kad vidjenja nema dovoljno, kad su zrake gotovo paralelne,
//ili kad je paralaksa ispod zadanog praga. Prag 0 znaci da se ne trazi nista osim da sustav ne
//bude singularan - tako se ponasala prva verzija i tako se test moze vratiti u to stanje
bool triangulate(const std::vector<Pose>& poses,
                 const Intrinsics& intrinsics,
                 const std::vector<View>& views,
                 glm::vec3& point,
                 double minParallaxDegrees = 0.0);

//Sve tocke scene odjednom, iz njezinih opazanja i DANIH poza. Tocka koja se ne da triangulirati
//dobiva false u seen - pozivatelj mora znati koja, jer ta tocka poslije ne smije ulaziti u mjeru
std::vector<glm::vec3> triangulateAll(const std::vector<Observation>& observations,
                                      const std::vector<Pose>& poses,
                                      const Intrinsics& intrinsics,
                                      size_t pointCount,
                                      std::vector<uint8_t>& solved);

}
