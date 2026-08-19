#pragma once
#include <glm/glm.hpp>

enum class LightType{
    Directional,
    Point
};

struct LightConfig{
    LightType type = LightType::Directional;

    //direction the light travels( from the light towards the scene)
    glm::vec3 direction = {-0.5f, -1.0f, -0.3f};

    //position of light
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    float range = 10.0f;

    glm::vec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

class Light{
    public:
    Light(const LightConfig& config = {}) : config(config) {}

    LightType getType() const {return config.type;}
    glm::vec3 getDirection() const {return glm::normalize(config.direction);}
    const glm::vec3& getPosition() const {return config.position;}
    float getRange() const {return config.range;}
    glm::vec3 getColor() const {return config.color * config.intensity;}
    const LightConfig& getConfig() const {return config;}

    void setType(const LightType newType) {config.type = newType;}
    void setDirection(const glm::vec3& newDireciton) {config.direction = newDireciton;}
    void setPosition(const glm::vec3& newPosition) {config.position = newPosition;}
    void setRange(float newRange) {config.range = newRange;}
    void setColor(const glm::vec3& newColor) {config.color = newColor;}
    void setIntensity(float newIntensity) {config.intensity = newIntensity;}

    private:
    LightConfig config;


};