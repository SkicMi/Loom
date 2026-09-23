#pragma once
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Warp{

//=============================================================================================
// Citanje animirane kamere iz .usda koju pise Engine (writeUsdScene).
//
// ZASTO SAMO TO, a ne USD. Pravi USD citac je OpenUSD - stotine tisuca redaka i Python. Ovdje
// treba jedno: kamera iz solvea, s uzorkom na SVAKOM KADRU snimke. COLMAP-ov model nosi samo
// kljucne kadrove (svaki n-ti); kamera.usda nosi i medjukadrove koje je VideoSolve lokalizirao,
// a bez njih bi kocka na timelineu izmedju kljucnih kadrova klizila po pravcu umjesto da ide
// s kamerom.
//
// Cita se ono sto nas izvoz pise: zaglavlje (raspon, fps), focalLength i otvori, i
// matrix4d xformOp:transform.timeSamples. Ostatak datoteke se preskace.
//
// KONVENCIJA. USD matrica je po retcima s vektorom retkom, pa je redak i datoteke STUPAC i nase
// glm matrice - tocno inverz onoga sto izvoz radi. Test to zatvara krugom kroz Engineov izvoz
//=============================================================================================

struct UsdCamera{
    double startTimeCode = 1.0;
    double endTimeCode = 1.0;
    double framesPerSecond = 25.0;
    float focalLength = 0.0f;               //u istim jedinicama kao otvor, najcesce mm
    float horizontalAperture = 0.0f;
    float verticalAperture = 0.0f;
    std::vector<double> times;
    std::vector<glm::mat4> transforms;      //kamera -> svijet, gleda niz -Z
};

//False kad datoteke nema ili u njoj nema nijednog uzorka kamere
bool readUsdCamera(const std::string& path, UsdCamera& out);

}
