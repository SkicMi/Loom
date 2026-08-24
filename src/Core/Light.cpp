#include "Light.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
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

LightMatrices Light::fitToCamera(const Camera& camera,
                                uint32_t viewWidth, uint32_t viewHeight,
                                uint32_t shadowResolution,
                                float distance) const{
    if(config.type != LightType::Directional){
        throw std::runtime_error("Light: fitToCamera is for a directional light - a point light's box is its own range, not the camera's frustum");
    }
    if(viewWidth == 0 || viewHeight == 0 || shadowResolution == 0){
        throw std::runtime_error("Light: fitToCamera needs a real viewport and a real shadow map size");
    }

    const glm::mat4 cameraProjection = camera.getProjection(viewWidth, viewHeight);
    const glm::mat4 cameraViewProjection = cameraProjection * camera.getView();
    const glm::mat4 inverseViewProjection = glm::inverse(cameraViewProjection);

    //Where the shadow slice ends, read back out of the camera's own projection rather than
    //rebuilt from its fov. Whatever Camera::getProjection does - the principal point offset,
    //the Y flip - this follows it instead of guessing at it
    const float clamped = std::max(distance, camera.getConfig().nearPlane * 2.0f);
    const float farNdcZ = (cameraProjection[2][2] * (-clamped) + cameraProjection[3][2]) / clamped;

    //The eight corners of that slice, unprojected back into the world
    glm::vec3 corners[8];
    int index = 0;
    for(float z : {0.0f, farNdcZ}){
        for(float y : {-1.0f, 1.0f}){
            for(float x : {-1.0f, 1.0f}){
                const glm::vec4 point = inverseViewProjection * glm::vec4(x, y, z, 1.0f);
                corners[index++] = glm::vec3(point) / point.w;
            }
        }
    }

    //The centroid, and the distance to the farthest corner from it. This is the rotation
    //invariant part: turn the camera and the corners all move, but their spread around their
    //own centre does not, so the radius - and with it the size of the box - stays put. An
    //axis aligned box of the same corners would grow and shrink as the camera turned, and a
    //shadow map that changes scale every frame makes every edge in the scene crawl
    glm::vec3 center(0.0f);
    for(const glm::vec3& corner : corners){
        center += corner;
    }
    center /= 8.0f;

    float radius = 0.0f;
    for(const glm::vec3& corner : corners){
        radius = std::max(radius, glm::length(corner - center));
    }

    //Rounded up to a whole texel, so the radius cannot wobble in its last decimal place
    const float texelSize = (2.0f * radius) / float(shadowResolution);
    radius = std::ceil(radius / texelSize) * texelSize;

    const glm::vec3 direction = getDirection();
    const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                       : glm::vec3(0.0f, 1.0f, 0.0f);

    //The eye is pulled a whole radius further back than the sphere itself needs, so that
    //anything standing between the light and the visible slice still casts into the map.
    //Those casters are invisible to the camera and would otherwise simply be missing
    const float pullBack = radius;
    const glm::vec3 eye = center - direction * (radius + pullBack);

    LightMatrices out;
    out.view = glm::lookAt(eye, center, up);

    //Near at zero rather than at the sphere: everything from the eye forward is a caster.
    //Top and bottom swapped, the same Y flip Camera::getProjection does
    out.projection = glm::ortho(-radius, radius, radius, -radius, 0.0f, 2.0f * radius + pullBack);

    //Texel snapping. The world origin is pushed through the finished matrix, measured in half
    //texels, rounded to a whole one, and the difference folded back into the projection's
    //translation. The map now steps in whole texels as the camera moves, so a shadow edge
    //stays on the texel it was on instead of crawling across it
    const glm::mat4 shadowMatrix = out.projection * out.view;
    glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shadowOrigin *= float(shadowResolution) * 0.5f;

    const glm::vec4 rounded = glm::round(shadowOrigin);
    glm::vec4 offset = (rounded - shadowOrigin) * (2.0f / float(shadowResolution));
    offset.z = 0.0f;  //depth must not be nudged, only the two axes the texels live on
    offset.w = 0.0f;

    out.projection[3] += offset;
    out.viewProjection = out.projection * out.view;

    return out;
}

glm::mat4 Light::getCubeProjection() const{
    if(config.type != LightType::Point){
        throw std::runtime_error("Light: a cube projection is for a point light - a directional light has one box, not six faces");
    }

    //Ninety degrees, square: six of these seal a whole sphere with no gap and no overlap.
    //The far plane is the light's range - past it the light contributes nothing, so there is
    //nothing there left to shadow, and a shorter range means better depth precision
    const float far = std::max(config.range, config.shadowNear * 2.0f);
    return glm::perspective(glm::radians(90.0f), 1.0f, config.shadowNear, far);
}

glm::mat4 Light::getCubeView(uint32_t face) const{
    if(config.type != LightType::Point){
        throw std::runtime_error("Light: a cube view is for a point light - a directional light has one view, not six");
    }
    if(face > 5){
        throw std::runtime_error("Light: a cube has six faces, numbered 0 to 5");
    }

    //Vulkan's cube face order and orientation. The up vectors are not a free choice: a cube
    //map is sampled by direction, and these are the orientations that make a sampled
    //direction land on the texel the face rendered it into. Getting one of them wrong does
    //not tilt a shadow slightly, it puts it on the wrong face
    static const glm::vec3 directions[6] = {
        { 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        { 0.0f, 1.0f, 0.0f}, { 0.0f,-1.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f}, { 0.0f, 0.0f,-1.0f},
    };
    static const glm::vec3 ups[6] = {
        { 0.0f,-1.0f, 0.0f}, { 0.0f,-1.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f}, { 0.0f, 0.0f,-1.0f},
        { 0.0f,-1.0f, 0.0f}, { 0.0f,-1.0f, 0.0f},
    };

    return glm::lookAt(config.position, config.position + directions[face], ups[face]);
}

glm::mat4 Light::getCubeViewProjection(uint32_t face) const{
    return getCubeProjection() * getCubeView(face);
}
