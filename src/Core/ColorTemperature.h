#pragma once
#include <glm/glm.hpp>

//LIGHT COLOR, SPOKEN AS A TEMPERATURE.
//
//Light already had a color before - three numbers multiplying both the diffuse and the
//specular term. The problem was not that a color cannot be set, but that it cannot be set
//without also changing the AMOUNT of light. The triplet {1, 0.82, 0.55}, which sat hardcoded
//in LoomApp, has Rec.709 luminance 0.839: that "warm tone" was at the same time light sixteen
//percent dimmer. Two such colors cannot then be compared, because a difference in tone also
//carries a difference in brightness - the same trap that forced penumbra grain to be measured
//on the visibility factor rather than on darkening.
//
//Temperature solves that on its own, and that is the only reason it is here. The black body is
//computed via chromaticity (x, y), and Y - which IS luminance - is set to 1 in the process.
//So a color from Kelvin carries exactly unit luminance by construction, and changes nothing but
//the tone. Measured over a range of 2000-25000 K: luminance stays 1.0000 to within 5e-5.

//Rec.709 luminance of a linear color. This is literally the Y row from the XYZ -> linear sRGB
//matrix, so this is not "some formula for brightness" but the same definition the conversion
//below uses
inline float luminance(const glm::vec3& linear){
    return 0.2126f * linear.r + 0.7152f * linear.g + 0.0722f * linear.b;
}

//The same color, with luminance exactly 1. That way intensity stays the only thing changing
//the amount of light, and color the only thing changing the tone
inline glm::vec3 normalizeLuminance(const glm::vec3& linear){
    const float y = luminance(linear);
    return (y > 1e-6f) ? linear / y : linear;
}

//The black body color at a given temperature, in LINEAR sRGB.
//
//Four steps, and each one has its reason:
//
//  1. T -> (x, y) on the Planckian locus. Cubic approximation (Kim et al.), because the exact
//     computation of the Planck radiation integral over three CIE curves - a table that would
//     sit here only to give the same five decimals
//  2. (x, y) + Y = 1 -> XYZ. This is where the key decision is made: UNIT luminance is set,
//     so temperature changes only the chromaticity
//  3. XYZ -> linear sRGB, with the matrix for Rec.709 primaries and D65 white
//  4. clamping the negative. Below roughly 1900 K the red-orange black body color exits the
//     sRGB triangle and the blue channel comes out negative; there the color can no longer be
//     displayed, only moved toward the edge of the gamut
//
//HONEST ABOUT WHAT THIS IS NOT: 6500 K is not sRGB white. Channels equalize only at 6532 K,
//and even there they are not equal - green is 5.6% lower than the other two. It is not an error
//of the approximation but the way things are: D65 is daylight, and daylight is not a black
//body, so the Planckian locus does not pass through sRGB's white point at all
inline glm::vec3 colorFromKelvin(float kelvin){
    //Outside this range the approximation does not hold, and there is nothing to offer either:
    //below, the color has long since left the gamut; above, it no longer changes
    const double t = double(glm::clamp(kelvin, 1500.0f, 25000.0f));

    double x;
    if(t <= 4000.0){
        x = -0.2661239e9 / (t*t*t) - 0.2343589e6 / (t*t) + 0.8776956e3 / t + 0.179910;
    }
    else{
        x = -3.0258469e9 / (t*t*t) + 2.1070379e6 / (t*t) + 0.2226347e3 / t + 0.240390;
    }

    //Three pieces of the curve, because at its low end the locus is too curved for a single
    //polynomial to follow it to five decimals
    double y;
    if(t <= 2222.0){
        y = -1.1063814*x*x*x - 1.34811020*x*x + 2.18555832*x - 0.20219683;
    }
    else if(t <= 4000.0){
        y = -0.9549476*x*x*x - 1.37418593*x*x + 2.09137015*x - 0.16748867;
    }
    else{
        y =  3.0817580*x*x*x - 5.87338670*x*x + 3.75112997*x - 0.37001483;
    }

    //Y = 1: the light carries unit luminance, and x and y only say where it is colored
    const double X = x / y;
    const double Y = 1.0;
    const double Z = (1.0 - x - y) / y;

    const double r =  3.2404542*X - 1.5371385*Y - 0.4985314*Z;
    const double g = -0.9692660*X + 1.8760108*Y + 0.0415560*Z;
    const double b =  0.0556434*X - 0.2040259*Y + 1.0572252*Z;

    return glm::vec3(float(r < 0.0 ? 0.0 : r),
                     float(g < 0.0 ? 0.0 : g),
                     float(b < 0.0 ? 0.0 : b));
}
