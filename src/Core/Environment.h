#pragma once

#include <glm/glm.hpp>

struct EnvironmentConfig{
    //Uniform light arriving from all directions
    glm::vec3 ambientColor ={1.0f, 1.0f, 1.0f};
};

class Environment{
    public:
    Environment(const EnvironmentConfig config = {}) : config(config) {}

    //getters
    const glm::vec3& getAmbient() const {return config.ambientColor;}
    const EnvironmentConfig getConfig() const { return config;}

    //setters
    void setAmbient(const glm::vec3& newAmbient) {config.ambientColor = newAmbient;}

    private:
    EnvironmentConfig config;
};