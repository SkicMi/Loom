#pragma once

#include <Engine/SyntheticScene.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine{

//Rezultat skupog dijela VideoSolvea: koji su kadrovi odabrani i view-graph koji ih povezuje.
//Cache namjerno ne sadrzi poze ni kalibraciju. Isti izmjereni graf zato se moze vise puta
//rekonstruirati s razlicitim solverom, bez ponavljanja trackinga i matchinga.
struct SolveCache{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t step = 0;
    uint32_t requestedFrames = 0;
    uint32_t usedFrames = 0;
    uint32_t pointCount = 0;
    float localizationPixels = 1.0f;

    //Identitet ulaza i nacina gradnje. VideoSolve ih provjerava prije upotrebe kako cache druge
    //snimke ili drugog moda ne bi izgledao kao valjan rezultat.
    uint64_t sourceBytes = 0;
    int64_t sourceWriteTime = 0;
    uint64_t buildSignature = 0;

    std::vector<uint32_t> keyframes;
    std::vector<Observation> observations;
};

//Format je verzioniran, little-endian i ima checksum cijelog payloada. Floatovi se spremaju po
//bitovima, pa round-trip ne mijenja ni zadnji bit izmjerenog polozaja znacajke.
bool writeSolveCache(const std::filesystem::path& path,
                     const SolveCache& cache,
                     std::string* error = nullptr);
bool readSolveCache(const std::filesystem::path& path,
                    SolveCache& cache,
                    std::string* error = nullptr);

}
