#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>

Camera::Camera(const CameraConfig& config) : config(config) , position(config.position) {
    lookAt(config.target, config.up);
}

void Camera::lookAt(const glm::vec3& target, const glm::vec3& up){
    glm::vec3 direction = target - position;
    if(glm::length(direction) < 1e-6f){
        return;
    }
    orientation = glm::quatLookAt(glm::normalize(direction),up);
}

glm::mat4 Camera::getView() const {
    return glm::mat4_cast(glm::conjugate(orientation)) * glm::translate(glm::mat4(1.0f), -position);
}

glm::mat4 Camera::getProjection(uint32_t width, uint32_t height) const {
    float aspect = float(width) / float(height); //for example 16:9 or 1.777..

    //Frustum on near plane (Y+ up);
    float top = config.nearPlane * std::tan(config.fovY * 0.5f);
    float bottom = -top;
    float right = top * aspect;
    float left = -right;

    //value of one pixel in units near plane
    float unitX = (right - left) / float(width);
    float unitY = (top - bottom) /float(height);

    left -= config.principalPointX * unitX;
    right -= config.principalPointX * unitX;
    bottom += config.principalPointY * unitY;
    top += config.principalPointY * unitY;

    //Y - flip for vulkan ( changing top and bottom)
    return glm::frustum(left, right, top, bottom, config.nearPlane, config.farPlane);
}

glm::mat4 Camera::getViewProjection(uint32_t width, uint32_t height) const {
    return getProjection(width, height) * getView();
}