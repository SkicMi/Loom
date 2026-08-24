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

    //Shadow map projection. A directional light has no position - it is a direction and
    //nothing else - so the box it renders has to be said out loud: centred here, this wide,
    //between these two planes. Anything outside the box casts no shadow, which is why the
    //box wants to be the smallest one that still holds the scene
    glm::vec3 shadowCenter = {0.0f, 0.0f, 0.0f};
    float shadowExtent = 10.0f; //half width of the orthographic box
    float shadowNear = 0.1f;
    float shadowFar = 50.0f;
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

    //The scene as this light sees it. Same pair a Camera gives, so a pass can be driven by
    //either one. Directional only for now - a point light casts in every direction at once
    //and needs six of these plus a cube map
    glm::mat4 getView() const;
    glm::mat4 getProjection() const;
    glm::mat4 getViewProjection() const;

    void setType(const LightType newType) {config.type = newType;}
    void setDirection(const glm::vec3& newDireciton) {config.direction = newDireciton;}
    void setPosition(const glm::vec3& newPosition) {config.position = newPosition;}
    void setRange(float newRange) {config.range = newRange;}
    void setColor(const glm::vec3& newColor) {config.color = newColor;}
    void setIntensity(float newIntensity) {config.intensity = newIntensity;}

    void setShadowCenter(const glm::vec3& newCenter) {config.shadowCenter = newCenter;}
    void setShadowExtent(float newExtent) {config.shadowExtent = newExtent;}
    void setShadowClipPlanes(float nearP, float farP) {config.shadowNear = nearP; config.shadowFar = farP;}

    private:
    LightConfig config;


};