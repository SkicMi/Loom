#pragma once
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class LoomInitializer;

//=============================================================================================
// POKLAPANJE OPISNIKA NA KARTICI (shaders/sift_match_best, sift_match_reverse).
//
// Isto sto Engine::matchSiftNear radi na procesoru - kandidati unutar radijusa u slici,
// najblizi i drugi najblizi (ali ne susjed najblizeg), prag omjera, najveca udaljenost, i
// uzajamno najbolji par - ali za sve parove kadrova odjednom. Na C0257 je poklapanje na
// procesoru uzelo 336 s od 42 minute solvea.
//
// Biblioteka ne zna za Engine: opisnik je 128 bajtova, polozaj dva floata, i to je sve.
// VideoSolve ga priljepi na MatchGraphConfig::siftPairMatcher.
//=============================================================================================
class DescriptorMatcher{
public:
    struct Frame{
        const uint8_t* descriptors = nullptr;    //count * 128 bajtova
        const float* positions = nullptr;        //count * 2
        const uint8_t* valid = nullptr;          //count
        uint32_t count = 0;
    };
    struct Rules{
        float radius = 0.0f;                     //pikseli, i velicina celije za redoslijed
        float maxDistance = 300.0f;
        float ratio = 0.8f;
        float secondBestApart = 10.0f;
    };
    struct Match{ uint32_t from, to; float distance; };

    explicit DescriptorMatcher(LoomInitializer& loom);
    ~DescriptorMatcher();

    //Parovi (a, b) indeksiraju frames; za svaki par popis uzajamno najboljih poklapanja
    std::vector<std::vector<Match>> match(const std::vector<Frame>& frames,
                                          const std::vector<std::pair<uint32_t, uint32_t>>& pairs,
                                          const Rules& rules);
private:
    struct State;
    LoomInitializer& loom;
    std::unique_ptr<State> state;
};
