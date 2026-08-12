#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct CameraConfig{
    //Projection
    float fovY = glm::radians(45.0f);
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    //Projection asymetry ( pixels from center of image ; 0.0 = centered)
    float principalPointX = 0.0f;
    float principalPointY = 0.0f;

    //Position and Orientation
    glm::vec3 position = { 0.0f , 0.0f , 2.0f};
    glm::vec3 target = {0.0f , 0.0f , 0.0f};
    glm::vec3 up = {0.0f , 1.0f , 0.0f}; //Y+ is up

};

class Camera{
    public:
    Camera(const CameraConfig& config = {});


    //getters

    const glm::vec3& getPosition() const {return position;}
    const glm::quat& getOrientation() const {return orientation;}
    const CameraConfig& getConfig() const {return config;}


    //Matrices - viewport coming from outside
    glm::mat4 getProjection(uint32_t width, uint32_t height) const;
    glm::mat4 getView() const;
    glm::mat4 getViewProjection(uint32_t width, uint32_t height) const;



    //runtime setters
    void setPosition(const glm::vec3& newPosition) { position = newPosition;}
    void setOrientation(const glm::quat& newOrientation) {orientation = newOrientation;}
    void lookAt(const glm::vec3& target, const glm::vec3& up = {0.0f, 1.0f, 0.0f});

    void setFovY(float radians) {config.fovY = radians;}
    void setPrincipalPoint(float x, float y) { config.principalPointX = x; config.principalPointY = y;}
    void setClipPlanes(float nearP, float farP) {config.nearPlane = nearP; config.farPlane = farP;}






    private:
    CameraConfig config;
    glm::vec3 position;
    glm::quat orientation;


};

