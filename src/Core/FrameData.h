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


//one light as the shader sees it. Lives in the storage buffer at set 0 , binding 1
struct GpuLight{
    //xyz = direction of travel ( w = 0 ) or world positon ( w = 1)
    glm::vec4 positionOrDirection = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
    glm::vec4 color = glm::vec4(0.0f);
    //x = range, rest reserved
    glm::vec4 params = glm::vec4(0.0f);

};

struct ObjectData{
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 normalMatrix = glm::mat4(1.0f);
};

