#pragma once
#include <glm/glm.hpp>

struct FrameData{
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec4 cameraPosition = glm::vec4(0.0f);
    glm::vec4 lightDirection = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
    glm::vec4 lightColor = glm::vec4(0.0f);
    glm::vec4 ambientColor = glm::vec4(1.0f);
};

struct ObjectData{
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 normalMatrix = glm::mat4(1.0f);
};

