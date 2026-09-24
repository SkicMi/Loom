#pragma once
#include <cstdint>
#include <memory>
#include <vector>

class LoomInitializer;

//=============================================================================================
// POTPISI PROSTORA MJERILA NA KARTICI (shaders/sift_blur_rows, sift_blur_columns,
// sift_gradients, sift_describe).
//
// Isto sto Engine::describeSiftScaled radi na procesoru: znacajke se sloze u pojaseve po mjerilu,
// slika se za svaki pojas zamuti, izracunaju se gradijenti, pa za svaku znacajku smjer okoline i
// histogram 4x4x8. Na procesoru je to 38 od 45 s znacajki na 60 kadrova C0257, jer se CIJELA 4K
// slika zamucuje i derivira petnaestak puta po kadru.
//
// ZAMUCENJE JE ISTO DO BITA: jezgra se racuna na procesoru, zbraja se istim redom i bez spojenog
// mnozenja i zbrajanja. Gradijenti i potpis prolaze kroz sqrt, atan2, exp, sin i cos kartice, koji
// se u zadnjem bitu razlikuju od procesorskih - pa ponekoj vrijednosti potpisa pomaknu zaokruzenje
// za jedan (test_gpu_sift mjeri koliko).
//
// Biblioteka ne zna za Engine: slika su bajtovi, znacajka dva floata i mjerilo. VideoSolve ga
// priljepi na MatchGraphConfig::siftScaledDescriber.
//=============================================================================================
class SiftDescriber{
public:
    struct Settings{
        uint32_t scaleBands = 3;        //SiftConfig::scaleBands
        float patchPerScale = 6.0f;     //SiftConfig::patchPerScale
        float clamp = 0.2f;             //SiftConfig::clamp
        bool orient = true;             //SiftConfig::orient
    };
    struct Output{
        std::vector<uint8_t> values;    //128 bajtova po znacajki
        std::vector<float> angles;
        std::vector<uint8_t> valid;
    };

    explicit SiftDescriber(LoomInitializer& loom);
    ~SiftDescriber();

    //points: count * 2 floata (x, y), scales: count. Nije za vise dretvi odjednom
    Output describe(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride,
                    const float* points, const float* scales, uint32_t count, const Settings& settings);
private:
    struct State;
    LoomInitializer& loom;
    std::unique_ptr<State> state;
};
