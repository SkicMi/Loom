#pragma once
#include <glm/glm.hpp>

struct LightConfig{
    //direction the light travels( from the light towards the scene)
    glm::vec3 direction = {-0.5f, -1.0f, -0.3f};
    glm::vec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    glm::vec3 ambient = {0.05f, 0.05f, 0.05f};
};

class Light{
    public:
    Light(const LightConfig& config = {}) : config(config) {}

    glm::vec3 getDirection() const {return glm::normalize(config.direction);}
    glm::vec3 getColor() const {return config.color * config.intensity;}
    const glm::vec3& getAmbient() const {return config.ambient;}
    const LightConfig& getConfig() const {return config;}

    void setDirection(const glm::vec3& newDireciton) {config.direction = newDireciton;}
    void setColor(const glm::vec3& newColor) {config.color = newColor;}
    void setIntensity(float newIntensity) {config.intensity = newIntensity;}
    void setAmbient(const glm::vec3& newAmbient) {config.ambient = newAmbient;}

    private:
    LightConfig config;


};