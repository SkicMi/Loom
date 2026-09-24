#include "CameraIntrinsics.h"
#include <cmath>
#include <stdexcept>

CameraIntrinsics CameraIntrinsics::fromProjection(const glm::mat4& projection, uint32_t width, uint32_t height){
    if(width == 0 || height == 0){
        throw std::runtime_error("CameraIntrinsics: an image with no size has no intrinsics");
    }

    //Pixel from clip space: px = (x_ndc * 0.5 + 0.5) * width, and x_ndc = clip.x / clip.w.
    //For a perspective projection clip.w = -z_view, so it works out to
    //
    //    px = (0.5 * width * m00) * x_view / (-z_view) + 0.5 * width * (1 - m20)
    //
    //which is exactly the form px = fx * x / depth + cx. Hence the two rows below
    CameraIntrinsics out;
    out.fx = 0.5f * float(width) * projection[0][0];
    out.fy = 0.5f * float(height) * projection[1][1];
    out.cx = 0.5f * float(width) * (1.0f - projection[2][0]);
    out.cy = 0.5f * float(height) * (1.0f - projection[2][1]);

    //m22 = -far/(far-near), m32 = -far*near/(far-near). Two equations, two unknowns
    const float m22 = projection[2][2];
    const float m32 = projection[3][2];

    if(std::abs(m22) > 1e-9f && std::abs(m22 + 1.0f) > 1e-9f){
        out.nearPlane = m32 / m22;
        out.farPlane = m32 / (m22 + 1.0f);
    }

    if(!out.isValid()){
        throw std::runtime_error("CameraIntrinsics: this matrix has no focal length - an orthographic projection has no pinhole to unproject through");
    }

    return out;
}

float CameraIntrinsics::horizontalFov(uint32_t width) const{
    //The two halves separately, because the principal point need not be centered. When it is,
    //this reduces to the usual 2*atan(width / (2*fx))
    const float focal = std::abs(fx);
    return std::atan((float(width) - cx) / focal) + std::atan(cx / focal);
}

float CameraIntrinsics::verticalFov(uint32_t height) const{
    const float focal = std::abs(fy);
    return std::atan((float(height) - cy) / focal) + std::atan(cy / focal);
}
