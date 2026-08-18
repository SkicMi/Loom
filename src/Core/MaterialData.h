#pragma once
#include <glm/glm.hpp>

//Per material data. Lives in descriptor set 1, bidning 1
//Layout must match the MaterialData struct in the shader
struct MaterialData{
    glm::vec4 baseColor = glm::vec4(1.0f);
    float shininess = 32.0f;
    float specularStrength = 1.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

