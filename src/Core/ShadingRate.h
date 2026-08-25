#pragma once
#include <cstdint>

//Koliko piksela dijeli jedno izvrsavanje fragment shadera.
//
//Rasterizacija, dubina i pokrivenost ostaju po pikselu - mijenja se samo koliko puta se
//SJENCANJE racuna. Zato je ovo ustedjivanje bez gubitka geometrije: rub trokuta je jednako
//ostar na 2x2 kao i na 1x1, a boja unutar bloka je jedna.
//
//Namjerno bez ijednog Vulkan tipa: isti pojam treba i stepenici 1, a Vulkanovo pakiranje
//(log2 sirine u gornja dva bita, log2 visine u donja) je detalj koji se dogada nize.
enum class ShadingRate{
    Full,       //1x1 - jedno sjencanje po pikselu, kako je oduvijek bilo
    Wide,       //2x1 - dva piksela u sirinu dijele jedno
    Tall,       //1x2
    Quarter,    //2x2 - cetvrtina posla
    Sixteenth   //4x4 - sesnaestina, i vidljivo grubo
};

//Sto se dogodi kad i materijal i slika stope imaju misljenje o istom pikselu.
//
//Po defaultu odlucuje grublje od dvoje, jer je materijalova zadana stopa Full - pa slika
//stope, koja zna za udaljenost, ima zadnju rijec. Materijal koji to ne zeli (zrcalo,
//refleksija, lice) kaze Critical i slika ga vise ne dira.
enum class ShadingImportance{
    Normal,     //slika stope smije ovo pogrubiti
    Critical    //ovo se sjenca stopom koju je materijal rekao, i tocka
};

struct ShadingRateExtent{
    uint32_t width = 1;
    uint32_t height = 1;
};

inline ShadingRateExtent shadingRateExtent(ShadingRate rate){
    switch(rate){
        case ShadingRate::Full:      return {1,1};
        case ShadingRate::Wide:      return {2,1};
        case ShadingRate::Tall:      return {1,2};
        case ShadingRate::Quarter:   return {2,2};
        case ShadingRate::Sixteenth: return {4,4};
    }
    return {1,1};
}

//Koliko se puta manje sjenca. Za 2x2 je cetiri
inline uint32_t shadingRateSavings(ShadingRate rate){
    const ShadingRateExtent extent = shadingRateExtent(rate);
    return extent.width * extent.height;
}
