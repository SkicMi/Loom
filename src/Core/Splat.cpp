#include "Splat.h"

#include <algorithm>
#include <cmath>

namespace SplatMath{

float activateOpacity(float logit){
    return 1.0f / (1.0f + std::exp(-logit));
}

glm::vec3 activateScale(const glm::vec3& logScale){
    return glm::vec3(std::exp(logScale.x), std::exp(logScale.y), std::exp(logScale.z));
}

glm::quat activateRotation(const glm::vec4& stored){
    //glm::quat prima (w, x, y, z), a zapis nabraja rot_0..rot_3 istim redom
    const glm::quat raw(stored.x, stored.y, stored.z, stored.w);

    //Duljina nula nije rotacija. Takav gaussian u zapisu ne bi trebao postojati, ali ako se
    //pojavi bolje je da bude neokrenut nego da cijela matrica postane NaN i otruje sve dalje
    const float length = std::sqrt(raw.w*raw.w + raw.x*raw.x + raw.y*raw.y + raw.z*raw.z);
    if(length <= 0.0f){
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return raw / length;
}

glm::vec3 colorFromSH0(const glm::vec3& dc){
    //Nulti SH bazis je konstanta 0.5 * sqrt(1/pi)
    constexpr float sh0 = 0.28209479177387814f;
    return glm::vec3(0.5f) + sh0 * dc;
}

glm::mat3 covariance3D(const glm::vec3& scale, const glm::quat& rotation){
    const glm::mat3 R = glm::mat3_cast(rotation);

    //M = R * S, pa je kovarijanca M * M'. Mnozenje stupaca umjesto pune dijagonalne matrice:
    //isti rezultat, i vidi se da S samo rasteze osi rotacije
    glm::mat3 M;
    M[0] = R[0] * scale.x;
    M[1] = R[1] * scale.y;
    M[2] = R[2] * scale.z;

    return M * glm::transpose(M);
}

glm::vec2 projectToPixels(const glm::vec3& position,
                          const glm::mat4& view,
                          float focalX, float focalY,
                          float principalX, float principalY){
    const glm::vec3 viewPosition = glm::vec3(view * glm::vec4(position, 1.0f));
    const float depth = -viewPosition.z;
    if(depth <= 0.0f){
        return glm::vec2(0.0f);
    }
    return glm::vec2(principalX + focalX * viewPosition.x / depth,
                     principalY + focalY * viewPosition.y / depth);
}

bool prepare(const Splat& splat,
             const glm::mat4& view,
             float focalX, float focalY,
             float principalX, float principalY,
             float blur,
             PreparedSplat& out){
    const glm::vec3 viewPosition = glm::vec3(view * glm::vec4(splat.position, 1.0f));
    const float depth = -viewPosition.z;
    if(depth <= 0.0f){
        return false;
    }

    //Ogranicenje omjera se izvodi iz same slike: dopusta se pola sirine preko ruba, sto je
    //dovoljno da mrlja koja jos zahvaca kadar bude tocna, a dalje od toga se ionako ne vidi
    const float limitX = 1.3f * principalX / focalX;
    const float limitY = 1.3f * principalY / std::fabs(focalY);

    const glm::mat3 covariance = covariance3D(splat.scale, splat.rotation);
    const glm::mat2 screen = covariance2D(covariance, splat.position, view,
                                          focalX, focalY, limitX, limitY, blur);

    const float determinant = screen[0][0] * screen[1][1] - screen[0][1] * screen[1][0];
    if(determinant <= 0.0f){
        return false;   //elipsa bez povrsine se ne da invertirati, a ni vidjeti
    }

    const float inverse = 1.0f / determinant;

    out.centerConic.x = principalX + focalX * viewPosition.x / depth;
    out.centerConic.y = principalY + focalY * viewPosition.y / depth;
    out.centerConic.z =  screen[1][1] * inverse;
    out.centerConic.w = -screen[0][1] * inverse;
    out.conicOpacityDepth.x =  screen[0][0] * inverse;
    out.conicOpacityDepth.y = splat.opacity;
    out.conicOpacityDepth.z = depth;

    //POLUMJER NA KOJEM SE MRLJA ODSIJECA: tri sigme po duzoj osi elipse.
    //
    //Duza os je veca svojstvena vrijednost kovarijance, a za matricu 2x2 se dobije bez ikakve
    //iteracije: sredina traga plus korijen iz razlike kvadrata. Pod korijenom stoji donja
    //granica jer ga zaokruzivanje zna gurnuti malo ispod nule za gotovo kruzne mrlje.
    //
    //Tri sigme drze 98.89 % mase (1 - exp(-4.5)); ostatak je rep koji se u osam bita ionako ne
    //vidi, a placao bi se pločicama koje splat jedva dira
    const float mid = 0.5f * (screen[0][0] + screen[1][1]);
    const float spread = std::sqrt(std::max(0.1f, mid * mid - determinant));
    out.conicOpacityDepth.w = std::ceil(3.0f * std::sqrt(mid + spread));
    out.color = glm::vec4(splat.color, 0.0f);
    return true;
}

glm::mat2 covariance2D(const glm::mat3& covariance,
                       const glm::vec3& position,
                       const glm::mat4& view,
                       float focalX, float focalY,
                       float tangentLimitX, float tangentLimitY,
                       float blur){
    const glm::vec3 viewPosition = glm::vec3(view * glm::vec4(position, 1.0f));

    //Kamera gleda niz -Z, pa je dubina pozitivna kad je tocka ispred nje
    const float depth = -viewPosition.z;
    if(depth <= 0.0f){
        return glm::mat2(0.0f);
    }

    //Omjer prema osi se ogranicava PRIJE nego udje u Jakobijan. Jakobijan je tocan samo blizu
    //tocke u kojoj je uzet; na rubu kadra bi razvukao mrlju preko pola ekrana. Ogranicava se
    //samo ono sto ulazi u derivaciju - sama dubina ostaje prava
    const float limitedX = std::clamp(viewPosition.x / depth, -tangentLimitX, tangentLimitX) * depth;
    const float limitedY = std::clamp(viewPosition.y / depth, -tangentLimitY, tangentLimitY) * depth;

    //Derivacija projekcije (x/z, y/z) pomnozena zaristem. Tako se dobiju pikseli po jedinici
    //scene, i zato su fx i fy u pikselima
    const float inverseDepth = 1.0f / depth;
    const float inverseDepth2 = inverseDepth * inverseDepth;

    glm::mat3 J(0.0f);
    J[0][0] = focalX * inverseDepth;
    J[2][0] = focalX * limitedX * inverseDepth2;
    J[1][1] = focalY * inverseDepth;
    J[2][1] = focalY * limitedY * inverseDepth2;

    //Samo rotacija pogleda: pomak ne mijenja oblik mrlje
    const glm::mat3 W = glm::mat3(view);
    const glm::mat3 T = J * W;

    const glm::mat3 projected = T * covariance * glm::transpose(T);

    glm::mat2 result;
    result[0][0] = projected[0][0] + blur;
    result[0][1] = projected[0][1];
    result[1][0] = projected[1][0];
    result[1][1] = projected[1][1] + blur;
    return result;
}

}
