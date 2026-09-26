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
};

//Scena se preuzima (move). Nikad ne vraca nullptr
std::shared_ptr<const CompiledScene> compile(Scene scene);

//Sekvenca: kad prosli kadar ima iste trokute (indeksi, materijali, objekti), BVH se ne gradi
//nego osvjezi (Bvh::refit) - raspored iz proslog kadra, nove kutije. Svakih `rebuildEvery`
//osvjezavanja se ipak gradi iznova, da se stablo ne istrosi kako se scena mice
std::shared_ptr<const CompiledScene> compile(Scene scene, const CompiledScene* previous, uint32_t rebuildEvery = 8);

}
