#pragma once
#include <cstdint>

//How many pixels share one fragment shader execution.
//
//Rasterization, depth and coverage stay per pixel - only how many times SHADING is computed
//changes. Hence this is a saving without losing geometry: a triangle's edge is just as sharp
//at 2x2 as at 1x1, and the color within a block is one.
//
//Deliberately without a single Vulkan type: ladder 1 needs the same notion, and Vulkan's
//packing (log2 of width in the top two bits, log2 of height in the bottom two) is a detail
//that happens below.
enum class ShadingRate{
    Full,       //1x1 - one shading per pixel, as it has always been
    Wide,       //2x1 - two pixels in width share one
    Tall,       //1x2
    Quarter,    //2x2 - a quarter of the work
    Sixteenth   //4x4 - a sixteenth, and visibly coarse
};

//What happens when both the material and the shading rate image have an opinion about the same
//pixel.
//
//By default the coarser of the two wins, because the material's default rate is Full - so the
//rate image, which knows about distance, has the last word. A material that does not want that
//(mirror, reflection, face) says Critical and the image no longer touches it.
enum class ShadingImportance{
    Normal,     //the rate image may coarsen this
    Critical    //this gets shaded at whatever rate the material said, and that is that
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

//How many times less shading. For 2x2 it is four
inline uint32_t shadingRateSavings(ShadingRate rate){
    const ShadingRateExtent extent = shadingRateExtent(rate);
    return extent.width * extent.height;
}
