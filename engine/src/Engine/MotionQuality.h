#pragma once
//=============================================================================================
// KOLIKO JE POKRET DOBAR - izmjereno iz samog klipa, bez gledanja.
//
// Kimodo za isti opis daje vise varijanti, a do sada ih je trebalo pogledati jednu po jednu. Vecina
// onoga zbog cega se varijanta odbaci je mjerljiva i ne ovisi o opisu:
//
//   KLIZANJE STOPALA   stopalo koje stoji na podu ne smije se micati vodoravno. Mjeri se kao u
//                      literaturi o difuziji pokreta (MDM, GMD): vodoravna brzina prstiju dok su
//                      blizu poda, s tezinom koja pada kako se prst dize - cm/s
//   PROPADANJE         koliko najdublje stopalo ode ispod poda - cm
//   LEBDENJE           koliko je najnize stopalo obicno iznad poda - cm (medijan, pa skok smije)
//   TRZAJI             srednji trzaj (treca derivacija polozaja) zglobova tijela, bez prstiju
//                      i ociju - m/s^3. Glatko ljudsko kretanje ima malen trzaj; skok izmedju
//                      dva kadra ima golem
//   SKOK KORIJENA      najveca brzina kukova izmedju dva kadra - iznad sprinta je skok u putanji
//   PUTANJA            ako je zadana: koliko kukovi promase zadane tocke - cm
//
// Pod je Kimodov Root (projekcija kukova na tlo, y = 0 u svakom kadru). Kostur bez takvog Roota
// dobije pod iz niskog percentila visine prstiju, i izvjestaj to kaze (floorEstimated).
//
// Ocjena spaja mjere tako da je svaka podijeljena svojim pragom "jos prihvatljivo", pa je
// bezdimenzijska: 1 po mjeri znaci na granici. Manje je bolje.
//=============================================================================================
#include <Engine/WeaverMotion.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Engine::MotionQuality{

struct RoutePoint{
    int frame = 0;          //kadar klipa (30 Hz za Kimodo), od nule
    float x = 0.0f, z = 0.0f;
};

struct Limits{
    //Pragovi su izmjereni na Kimodovim klipovima u WeaverMotion/ (trcanje, ples, puzanje, hod):
    //tezinsko klizanje u dodiru 9-32 cm/s (medijan ~12), trzaj 60-280 m/s^3 (trcanje najvise)
    float footSkateCmPerSecond = 10.0f;
    float penetrationCm = 2.0f;
    float floatingCm = 3.0f;
    float jerkMetersPerSecond3 = 150.0f;
    float rootSpeedMetersPerSecond = 9.0f;  //ispod ovoga ne kaznjava nista; iznad je teleport
    float routeErrorCm = 25.0f;
};

struct Options{
    Limits limits;
    float contactHeightCm = 2.5f;           //prst nize od ovoga iznad svoje visine mirovanja "stoji"
    std::vector<RoutePoint> route;
};

struct Report{
    bool valid = false;
    std::string problem;
    size_t frames = 0;
    double seconds = 0.0;

    float footSkateCmPerSecond = 0.0f;
    float contactShare = 0.0f;              //udio kadrova u kojima barem jedno stopalo stoji
    float penetrationCm = 0.0f;
    float floatingCm = 0.0f;                //medijan visine najnizeg prsta iznad mirovanja
    float jerkMetersPerSecond3 = 0.0f;
    float rootSpeedMetersPerSecond = 0.0f;  //najbrzi korak kukova
    float routeErrorCm = -1.0f;             //-1: putanja nije zadana
    float floorHeightCm = 0.0f;
    bool floorEstimated = false;

    float score = 0.0f;                     //zbroj mjera podijeljenih pragovima; manje je bolje
    std::string worst;                      //mjera koja najvise pridonosi ocjeni, citljivo
};

//Svjetski polozaji svih zglobova u jednom kadru: isti racun kao uvoz u scenu (odmak + pomak, rotacija)
std::vector<glm::vec3> worldPositions(const WeaverMotion::Clip& clip, size_t frame);

Report evaluate(const WeaverMotion::Clip& clip, const Options& options = {});

//Putanja iz Kimodovog constraints.json (ono sto Loom zapise uz generirani pokret): prvi "root2d"
//blok, frame_indices i smooth_root_2d. Prazno kad putanje nema ili tekst nije taj oblik
std::vector<RoutePoint> routeFromConstraints(const std::string& json);
std::vector<RoutePoint> readRouteConstraints(const std::string& path);

//Indeks najbolje varijante po ocjeni; -1 kad nijedna nije valjana
int bestOf(const std::vector<Report>& reports);

//Kratko, za redak u suicelju: "skate 1.2 cm/s  sink 0.4 cm  jerk 31"
std::string summary(const Report& report);

}
