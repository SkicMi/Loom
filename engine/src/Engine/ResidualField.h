#pragma once
#include "Engine/SyntheticScene.h"

#include <cstdint>
#include <vector>

namespace Engine{

//=============================================================================================
// Prostorno polje reprojekcijskih ostataka.
//
// Jedan broj reprojekcije ne razlikuje bijeli sum od sustavnog kvara modela kamere. Ovdje se
// slika dijeli na celije i u svakoj racuna SREDNJI VEKTOR ostatka. Nezavisan sum sredine ide prema
// nuli kao 1/sqrt(N); distorzija, stabilizacija i rolling shutter ostaju kao polje.
//
// Prije mjerenja polja uklanja se srednji vektor cijelog kadra. Jednolik pomak svih opazanja moze
// nastati iz poze i nije dokaz da pinhole model ne vrijedi. Ono sto ostane mora ovisiti o mjestu u
// slici da bi bilo proglaseno poljem.
//=============================================================================================

enum class ResidualDiagnosis{
    InsufficientData,
    White,
    Static,
    Changing
};

const char* residualDiagnosisName(ResidualDiagnosis diagnosis);

struct ResidualFieldConfig{
    uint32_t columns = 8;
    uint32_t rows = 8;
    uint32_t minSamplesPerCell = 8;
    uint32_t minCellsPerFrame = 16;
    uint32_t minFrames = 6;

    //Polje ispod cetvrt piksela nije razlucivo od lokalizacije znacajke na pravoj snimci.
    double minimumMagnitudePixels = 0.25;

    //Uz apsolutni pod, polje mora biti i ovoliko puta vece od izmjerene pogreske sredine celije.
    double significance = 4.0;
};

struct ResidualCell{
    glm::vec2 mean{0.0f};
    glm::vec2 centredMean{0.0f};
    uint32_t samples = 0;
    double meanNoise = 0.0;
    bool valid = false;
};

struct ResidualFrame{
    std::vector<ResidualCell> cells;
    glm::vec2 mean{0.0f};
    uint32_t validCells = 0;
    double spatialRms = 0.0;
    bool valid = false;
};

struct ResidualFieldResult{
    ResidualDiagnosis diagnosis = ResidualDiagnosis::InsufficientData;
    std::vector<ResidualFrame> frames;

    uint32_t evaluatedFrames = 0;
    uint32_t evaluatedCells = 0;

    //RMS polja nakon uklanjanja jednolikog pomaka kadra.
    double spatialRms = 0.0;
    double spatialSignalRms = 0.0;

    //Dio polja koji je jednak kroz kadrove, odnosno dio koji se mijenja.
    double staticRms = 0.0;
    double temporalRms = 0.0;
    double temporalSignalRms = 0.0;

    //Pogreska sredine celije izvedena iz rasapa opazanja unutar nje.
    double meanNoiseRms = 0.0;
    double decisionThreshold = 0.0;
};

//observationUsed je izborni niz usporedan s observations. Kad je zadan, u polje ulaze samo
//opazanja koja su stvarno prezivjela rekonstrukciju; odbaceni promasaj nije ostatak modela.
ResidualFieldResult analyzeResidualField(const std::vector<Observation>& observations,
                                         const std::vector<Pose>& poses,
                                         const std::vector<glm::vec3>& points,
                                         const Intrinsics& intrinsics,
                                         const ResidualFieldConfig& config = {},
                                         const std::vector<uint8_t>& observationUsed = {});

}
