#pragma once
#include <glm/glm.hpp>
#include <cstdint>

struct FrameData{
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec4 cameraPosition = glm::vec4(0.0f);
    glm::vec4 ambientColor = glm::vec4(1.0f);
    uint32_t lightCount = 0;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};


//Koliko shadow karata set 0 nosi. MORA se poklapati s velicinom polja u shaderima
//(triangle.slang i textured.slang) - polje deskriptora i polje u shaderu su dvije strane
//istog broja, i ako se raziđu prevodenje pipelinea prolazi a citanje je izvan granica.
//Kocka je skuplja sest puta, pa ih je manje
inline constexpr uint32_t maxShadowMaps = 4;
inline constexpr uint32_t maxShadowCubes = 2;

//one light as the shader sees it. Lives in the storage buffer at set 0 , binding 1.
//Growing this is cheap precisely because it is a storage buffer and not a push constant:
//there is no 128 byte ceiling to run into
struct GpuLight{
    //xyz = direction of travel ( w = 0 ) or world positon ( w = 1)
    glm::vec4 positionOrDirection = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
    glm::vec4 color = glm::vec4(0.0f);
    //x = range, y = which kind of shadow (0 none, 1 a flat map, 2 a cube), z = depth bias
    //for the lookup, w = near plane of a cube's faces
    glm::vec4 params = glm::vec4(0.0f);

    //x = which map of that kind, in the array on set 0. The rest is reserved.
    //Sitting in its own vec4 rather than squeezed into params because an index encoded into
    //a field that already means something else is a bug waiting for the second reader
    glm::vec4 shadow = glm::vec4(0.0f);

    //World space to this light's clip space. Only means anything when params.y is 1.
    //Three vec4s ahead of it is 48 bytes, a multiple of 16, so std430 puts the matrix
    //exactly here and the C++ and the shader agree without any padding in between
    glm::mat4 lightViewProjection = glm::mat4(1.0f);
};

struct ObjectData{
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 normalMatrix = glm::mat4(1.0f);
};

