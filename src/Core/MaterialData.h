#pragma once
#include <glm/glm.hpp>

//Per material data. Lives in descriptor set 1, bidning 1
//Layout must match the MaterialData struct in the shader
struct MaterialData{
    glm::vec4 baseColor = glm::vec4(1.0f);
    float shininess = 32.0f;
    float specularStrength = 1.0f;

    //Wrapped diffuse: how far light reaches PAST the edge where Lambert falls to zero.
    //
    //A sharp terminator turns every normal error into a blotch, and normals from estimated
    //depth are full of errors. Zero is exactly Lambert and therefore the default - this gets
    //enabled where the normals are known to be estimates
    float diffuseWrap = 0.0f;
    float padding1 = 0.0f;
};

