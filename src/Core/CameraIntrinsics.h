#pragma once
#include <glm/glm.hpp>
#include <cstdint>

//A camera described the way photography describes it, not graphics: focal lengths and the
//principal point in PIXELS.
//
//That is the form intrinsics arrive in from calibration, from EXIF and from depth estimation
//models - and the only one a pixel can be projected back into 3D from. The projection matrix
//knows the same thing, but written so that it cannot be read out of it.
//
//They are derived FROM THE MATRIX, not alongside it. Were we to compute them separately from
//fovY and the aspect ratio, the two formulas would drift apart over time and nothing in the
//code would look wrong - the scene would just come out a little too shallow. This way they
//cannot fail to match whatever it was drawn with.
struct CameraIntrinsics{
    float fx = 0.0f;   //focal length in pixels, horizontal
    float fy = 0.0f;   //vertical; NEGATIVE when the projection flips Y (Vulkan), and that is fine
    float cx = 0.0f;   //principal point in pixels, from the left edge
    float cy = 0.0f;   //from the top edge

    float nearPlane = 0.0f;
    float farPlane = 0.0f;

    //width and height are the image's dimensions in pixels that these intrinsics hold for. The
    //focal length in pixels depends on resolution: the same camera at half resolution has half
    //the fx and the same field of view. Hence this is computed at load time, not stored
    static CameraIntrinsics fromProjection(const glm::mat4& projection, uint32_t width, uint32_t height);

    //The field of view that comes out of them, in radians. Correct even when the principal point
    //is not centered, because the two angle halves are computed separately. For checking and for
    //printing - degrees are more readable than pixels
    float horizontalFov(uint32_t width) const;
    float verticalFov(uint32_t height) const;

    bool isValid() const {return fx != 0.0f && fy != 0.0f;}
};
