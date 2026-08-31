#pragma once
#include <glm/glm.hpp>
#include "Camera.h"
#include "ColorTemperature.h"
#include <cstdint>

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

    //TON JE TON, A NE I SVJETLINA.
    //
    //Boja svjetla mnozi svaki kanal, pa svaki ton koji nije bijel ujedno oduzima svjetla:
    //{1, 0.82, 0.55} nosi luminanciju 0.839, dakle sesnaest posto manje od bijelog na istom
    //intensityju. Dok je to tako, dvije boje se ne daju usporediti - razlika u tonu nosi i
    //razliku u svjetlini.
    //
    //S ovim upaljenim se boja normalizira na jedinicnu luminanciju, pa intensity ostane
    //jedina stvar koja kaze KOLIKO, a boja jedina koja kaze KAKO. Iskljuceno je po defaultu
    //jer bi inace svaka postojeca scena s obojenim svjetlom promijenila svjetlinu
    bool normalizeColor = false;

    //Shadow map projection. A directional light has no position - it is a direction and
    //nothing else - so the box it renders has to be said out loud: centred here, this wide,
    //between these two planes. Anything outside the box casts no shadow, which is why the
    //box wants to be the smallest one that still holds the scene
    glm::vec3 shadowCenter = {0.0f, 0.0f, 0.0f};
    float shadowExtent = 10.0f; //half width of the orthographic box
    float shadowNear = 0.1f;
    float shadowFar = 50.0f;
};

//A light's pair of matrices, handed over together because a fitted light computes both at
//once and the caller needs them to agree
struct LightMatrices{
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::mat4 viewProjection = glm::mat4(1.0f);
};

class Light{
    public:
    Light(const LightConfig& config = {}) : config(config) {}

    LightType getType() const {return config.type;}
    glm::vec3 getDirection() const {return glm::normalize(config.direction);}
    const glm::vec3& getPosition() const {return config.position;}
    float getRange() const {return config.range;}
    glm::vec3 getColor() const {
        return (config.normalizeColor ? normalizeLuminance(config.color) : config.color)
             * config.intensity;
    }
    const LightConfig& getConfig() const {return config;}

    //The scene as this light sees it. Same pair a Camera gives, so a pass can be driven by
    //either one. Directional only for now - a point light casts in every direction at once
    //and needs six of these plus a cube map
    glm::mat4 getView() const;
    glm::mat4 getProjection() const;
    glm::mat4 getViewProjection() const;

    //The same pair, but with the box fitted to the slice of the camera's frustum that
    //shadows are wanted in, instead of to whatever shadowCenter and shadowExtent were set to
    //by hand. Two things make this worth doing properly rather than just taking an AABB:
    //
    //  - the box is sized by the frustum's bounding SPHERE, whose radius depends only on the
    //    frustum's shape and not on where the camera is pointing. An AABB of the corners
    //    grows and shrinks as the camera turns, and a shadow map that changes size every
    //    frame makes every shadow edge crawl
    //  - the box is then snapped to whole shadow map texels, so turning or walking the camera
    //    moves the map by whole texels. Without this the same edge lands on a different part
    //    of a texel each frame and the whole scene shimmers
    //
    //distance is how far from the camera shadows reach: the whole shadow map is spent on
    //that slice, so smaller is sharper
    //The six faces of a point light's shadow cube. A point light has no single direction, so
    //it gets ninety degrees six times over, from its own position outwards. Face order is
    //Vulkan's own: +X, -X, +Y, -Y, +Z, -Z, which is the order the cube's layers are in
    glm::mat4 getCubeView(uint32_t face) const;
    glm::mat4 getCubeProjection() const;
    glm::mat4 getCubeViewProjection(uint32_t face) const;

    //Near plane of the cube faces. The far plane is the light's range, because past its
    //range the light contributes nothing and there is nothing left to shadow
    float getShadowNear() const {return config.shadowNear;}

    LightMatrices fitToCamera(const Camera& camera,
                              uint32_t viewWidth, uint32_t viewHeight,
                              uint32_t shadowResolution,
                              float distance) const;

    void setType(const LightType newType) {config.type = newType;}
    void setDirection(const glm::vec3& newDireciton) {config.direction = newDireciton;}
    void setPosition(const glm::vec3& newPosition) {config.position = newPosition;}
    void setRange(float newRange) {config.range = newRange;}
    void setColor(const glm::vec3& newColor) {config.color = newColor;}

    //Boja recena kao temperatura. Ne cuva se Kelvin nego boja koja iz njega izade - inace bi
    //postojala dva izvora iste istine, pa bi setColor i setTemperature mogli ostati u sporu
    void setTemperature(float kelvin) {config.color = colorFromKelvin(kelvin);}
    void setNormalizeColor(bool normalize) {config.normalizeColor = normalize;}
    void setIntensity(float newIntensity) {config.intensity = newIntensity;}

    void setShadowCenter(const glm::vec3& newCenter) {config.shadowCenter = newCenter;}
    void setShadowExtent(float newExtent) {config.shadowExtent = newExtent;}
    void setShadowClipPlanes(float nearP, float farP) {config.shadowNear = nearP; config.shadowFar = farP;}

    private:
    LightConfig config;


};