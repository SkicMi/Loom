#pragma once
//=============================================================================================
// OKOLINA PO VAZNOSTI. Nebo s HDRI-ja nije jednoliko: sunce na njemu zauzme desetinku postotka
// piksela, a nosi vecinu svjetla. Uzorkovanje smjerova ravnomjerno ga pogodi jednom u tisucu
// uzoraka i slika je puna krijesnica. Ovdje se smjer bira s vjerojatnoscu razmjernom sjaju
// piksela (puta sin(theta), jer su pikseli lat-long mape pri polovima manji), pa se svijetli
// dijelovi neba nadju u svakom uzorku - a gustoca se vrati tocno, da MIS moze tezinski spojiti
// ovaj nacin s uzorkovanjem BSDF-a.
//
// Mapa se cita po najblizem pikselu, ne bilinearno: tako je radijancija tocno stepenasta
// funkcija kakvu gustoca opisuje. Kod mape od 2K+ razlika se ne vidi
//=============================================================================================
#include "Tracer/Scene.h"

#include <glm/glm.hpp>

#include <vector>

namespace Tracer{

class EnvironmentSampler{
public:
    void build(const Environment& environment);

    bool active() const {return nonBlack;}
    //Radijancija iz svijeta u smjeru d (d je smjer U KOJEM se gleda, prema nebu)
    glm::vec3 radiance(const glm::vec3& d) const;
    //Smjer po vaznosti; vraca radijanciju, gustocu po prostornom kutu
    glm::vec3 sample(const glm::vec2& u, glm::vec3& direction, float& pdf) const;
    float pdf(const glm::vec3& d) const;
    //Prosjecna radijancija (luminancija) - za odluku koliko cesto uzorkovati nebo
    float averageLuminance() const {return average;}

private:
    const Environment* env = nullptr;
    bool nonBlack = false;
    bool textured = false;
    uint32_t width = 0, height = 0;
    float average = 0.0f;
    glm::mat3 toMap{1.0f}, fromMap{1.0f};
    std::vector<float> marginal;            //CDF po recima, height + 1
    std::vector<float> conditional;         //CDF po stupcima svakog retka, height * (width + 1)
    std::vector<float> rowWeight;           //nenormirana tezina retka

    glm::vec3 texel(uint32_t x, uint32_t y) const;
};

//Lat-long: smjer (u prostoru mape) <-> (u, v) u [0,1)^2
glm::vec2 directionToLatLong(const glm::vec3& d);
glm::vec3 latLongToDirection(const glm::vec2& uv);

}
