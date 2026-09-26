#pragma once
//=============================================================================================
// PREVEDENA SCENA: ono sto render stvarno cita - BVH, svjetla spremna za uzorkovanje, tablice
// neba i zastavice trokuta. Gradi se jednom po kadru i dijele je oba izvrsitelja:
//
//   Tracer::Renderer      procesor, sve jezgre (referenca, i rezerva bez kartice)
//   TracerGpu::GpuTracer  kartica (Vulkan compute) - ista polja prepisana u storage buffere
//
// Zato je sve ovdje ravno i javno: GPU ne smije imati svoju gradnju BVH-a ni svoju tezinu
// svjetala, jer bi se dva renderera tada razisla u necemu sto nijedan test ne vidi.
//=============================================================================================
#include "Tracer/Bvh.h"
#include "Tracer/Environment.h"
#include "Tracer/Scene.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Tracer{

enum TriangleFlag : uint8_t{
    AlphaTested = 1,
    Catcher = 2,
    NoCamera = 4,
    NoShadow = 8,
    Transmissive = 16,          //staklo: zraka sjene kroz njega prolazi oslabljena (RenderSettings::glassShadows)
};

//Svjetlo spremno za uzorkovanje
struct LightRecord{
    enum Kind : uint32_t{ Sun = 0, Sphere = 1, Triangle = 2, Sky = 3 };
    Kind kind = Sun;
    bool delta = false;
    glm::vec3 radiance{0.0f};           //Sun: radijancija diska (ili ozracenost kad je delta);
                                        //Sphere: radijancija plohe (ili intenzitet kad je delta)
    glm::vec3 position{0.0f};
    glm::vec3 axis{0.0f, 1.0f, 0.0f};   //Sun: prema suncu; Spot: smjer u kojem svijetli
    float cosMax = 1.0f, oneMinusCos = 0.0f;
    float radius = 0.0f;
    bool spot = false;
    float cosOuter = -1.0f, cosInner = -1.0f;
    uint32_t triangle = 0;
    float area = 0.0f;
    int index = 0;                      //u lights
};

//STABLO SVJETALA (Conty & Kulla 2018) nad lokalnim svjetlima - kugle, reflektori, svijetleci
//trokuti. Cvor: kutija, snaga (intenzitet prema osi), stozac osi (thetaO: koliko se osi
//svjetala raspu) i stozac emisije (thetaE: dokle svjetlo svijetli oko osi). Vaznost cvora za
//tocku p: snaga / udaljenost^2, puta koliko je p unutar stosca emisije (uz kut koji kutija
//zauzima), puta koliko je kutija iznad plohe u p (normala n; nula = bez tog uvjeta). Izbor ide
//od korijena, na svakom cvoru razmjerno vaznosti djece; pdf svjetla je umnozak tih udjela.
//Uvjeti su konzervativni: vaznost 0 znaci da svjetlo u p stvarno ne moze doprinijeti
struct LightTreeNode{
    glm::vec3 min{0.0f}, max{0.0f};
    glm::vec3 axis{0.0f, 0.0f, 1.0f};
    float thetaO = 0.0f, thetaE = 0.0f;
    float power = 0.0f;
    uint32_t left = 0, right = 0;       //unutarnji: djeca; list: left = indeks svjetla
    bool leaf = false;
};

struct CompiledScene{
    Scene world;
    Bvh tree;
    EnvironmentSampler sky;             //pokazuje u world.environment: CompiledScene se ne mice
    std::vector<LightRecord> lights;
    std::vector<float> lightPick;       //vjerojatnost izbora, zbroj 1
    std::vector<float> lightCumulative; //zbroj do ukljucivo i - izbor binarnom pretragom
    std::vector<uint32_t> suns;         //diskovi sunaca (ne delta), za zrake koje pobjegnu
    std::vector<uint32_t> spheres;      //kugle svjetla (ne tocke), za zrake koje ih presijeku
    std::vector<int> emitterOfTriangle; //indeks u lights ili -1
    std::vector<uint8_t> triangleFlags;
    float sceneRadius = 1.0f;
    glm::mat4 cameraInverse{1.0f};      //svijet -> kamera
    std::vector<glm::mat4> volumeInverse;   //svijet -> kutija, po Scene::volumes
    double buildSeconds = 0.0;
    uint32_t refits = 0;                //koliko je puta ovo stablo osvjezeno od zadnje gradnje (0: gradjeno)

    CompiledScene() = default;
    CompiledScene(const CompiledScene&) = delete;
    CompiledScene& operator=(const CompiledScene&) = delete;

    //Indeks svjetla za broj u [0,1)
    uint32_t pickLight(float choice) const;

    //-- izbor svjetla za tocku: lokalna iz stabla, beskonacna (sunce, nebo) po snazi -------------
    std::vector<LightTreeNode> lightTree;       //0 je korijen
    std::vector<uint32_t> lightTreeParent;      //po cvoru
    std::vector<uint32_t> lightTreeLeaf;        //po svjetlu: list u stablu ili ~0u (beskonacno)
    std::vector<uint32_t> infiniteLights;       //sunca i nebo
    std::vector<float> infiniteCumulative;
    float localPick = 0.0f;                     //vjerojatnost da se bira iz stabla

    float lightTreeImportance(uint32_t node, const glm::vec3& p, const glm::vec3& n) const;
    //Izbor svjetla za tocku p (normala n ili nula); pdf izbora u `probability`. false: nista
    //tree false: stari izbor samo po snazi (za usporedbu)
    bool chooseLight(float choice, const glm::vec3& p, const glm::vec3& n, uint32_t& light, float& probability, bool tree = true) const;
    //Vjerojatnost da chooseLight u (p, n) izabere ovo svjetlo (za MIS kad ga pogodi BSDF)
    float choiceProbability(uint32_t light, const glm::vec3& p, const glm::vec3& n, bool tree = true) const;

    //ZA ODSJECAK ZRAKE (o + t d, t u [a, b]) - ekviangularno u magli. Cvor se procjenjuje u tocki
    //odsjecka najblizoj njegovom sredistu, a vaznost pada s 1/udaljenost umjesto 1/udaljenost^2:
    //integral 1/r^2 duz pravca na udaljenosti D je pi/D. Zraka koja prolazi uz lampu tu lampu
    //bira i kad joj je sredina daleko
    float lightTreeImportanceOnSegment(uint32_t node, const glm::vec3& o, const glm::vec3& d, float a, float b) const;
    bool chooseLightOnSegment(float choice, const glm::vec3& o, const glm::vec3& d, float a, float b,
                              uint32_t& light, float& probability, bool tree = true) const;
    float choiceProbabilityOnSegment(uint32_t light, const glm::vec3& o, const glm::vec3& d, float a, float b, bool tree = true) const;

private:
    //Jedan obilazak stabla za tocku ili odsjecak: importance(cvor) daje vaznost
    template<class Importance> bool chooseWith(float choice, const Importance& importance, uint32_t& light, float& probability) const;
    template<class Importance> float probabilityWith(uint32_t light, const Importance& importance) const;
};

//Scena se preuzima (move). Nikad ne vraca nullptr
std::shared_ptr<const CompiledScene> compile(Scene scene);

//Sekvenca: kad prosli kadar ima iste trokute (indeksi, materijali, objekti), BVH se ne gradi
//nego osvjezi (Bvh::refit) - raspored iz proslog kadra, nove kutije. Svakih `rebuildEvery`
//osvjezavanja se ipak gradi iznova, da se stablo ne istrosi kako se scena mice
std::shared_ptr<const CompiledScene> compile(Scene scene, const CompiledScene* previous, uint32_t rebuildEvery = 8);

}
