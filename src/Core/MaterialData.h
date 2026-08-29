#pragma once
#include <glm/glm.hpp>

//Per material data. Lives in descriptor set 1, bidning 1
//Layout must match the MaterialData struct in the shader
struct MaterialData{
    glm::vec4 baseColor = glm::vec4(1.0f);
    float shininess = 32.0f;
    float specularStrength = 1.0f;

    //Omotani difuz: koliko svjetlo zalazi IZA ruba na kojem Lambert pada u nulu.
    //
    //Ostar terminator svaku gresku u normali pretvori u mrlju, a normale iz procijenjene
    //dubine su pune gresaka. Nula je tocno Lambert i zato je default - ovo se ukljucuje ondje
    //gdje se zna da su normale procjena
    float diffuseWrap = 0.0f;
    float padding1 = 0.0f;
};

