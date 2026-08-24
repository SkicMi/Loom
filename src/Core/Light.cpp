#include "Light.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <stdexcept>

glm::mat4 Light::getView() const{
    if(config.type != LightType::Directional){
        throw std::runtime_error("Light: only a directional light has a single view matrix - a point light casts in every direction and needs a cube map");
    }

    const glm::vec3 direction = getDirection();

    //A directional light has no position, so one is invented: far enough back along the
    //direction of travel that the whole shadow box sits in front of the near plane. Half the
    //far distance puts the centre of the box in the middle of the depth range
    const glm::vec3 eye = config.shadowCenter - direction * (config.shadowFar * 0.5f);

    //Any up vector will do except one parallel to the direction, and a light pointing
    //straight down is the most ordinary case there is
    const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                       : glm::vec3(0.0f, 1.0f, 0.0f);

    return glm::lookAt(eye, config.shadowCenter, up);
}

glm::mat4 Light::getProjection() const{
    if(config.type != LightType::Directional){
        throw std::runtime_error("Light: only a directional light has a single projection - a point light needs six faces of a cube map");
    }

    const float extent = config.shadowExtent;

    //Parallel rays mean an orthographic box, not a frustum: a directional light does not get
    //weaker or narrower with distance. Top and bottom are swapped, exactly the way
    //Camera::getProjection flips Y for Vulkan, so the shadow map and the camera image agree
    //on which way is up and on which way a triangle winds
    return glm::ortho(-extent, extent, extent, -extent, config.shadowNear, config.shadowFar);
}

glm::mat4 Light::getViewProjection() const{
    return getProjection() * getView();
}
