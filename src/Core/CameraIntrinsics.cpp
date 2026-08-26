#include "CameraIntrinsics.h"
#include <cmath>
#include <stdexcept>

CameraIntrinsics CameraIntrinsics::fromProjection(const glm::mat4& projection, uint32_t width, uint32_t height){
    if(width == 0 || height == 0){
        throw std::runtime_error("CameraIntrinsics: an image with no size has no intrinsics");
    }

    //Piksel iz clip prostora: px = (x_ndc * 0.5 + 0.5) * width, a x_ndc = clip.x / clip.w.
    //Za perspektivnu projekciju je clip.w = -z_view, pa ispada
    //
    //    px = (0.5 * width * m00) * x_view / (-z_view) + 0.5 * width * (1 - m20)
    //
    //sto je tocno oblik px = fx * x / dubina + cx. Odatle dva reda ispod
    CameraIntrinsics out;
    out.fx = 0.5f * float(width) * projection[0][0];
    out.fy = 0.5f * float(height) * projection[1][1];
    out.cx = 0.5f * float(width) * (1.0f - projection[2][0]);
    out.cy = 0.5f * float(height) * (1.0f - projection[2][1]);

    //m22 = -far/(far-near), m32 = -far*near/(far-near). Dvije jednadzbe, dvije nepoznanice
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
    //Dvije polovice odvojeno, jer glavna tocka ne mora biti u sredini. Kad jest, ovo se svede
    //na uobicajeno 2*atan(width / (2*fx))
    const float focal = std::abs(fx);
    return std::atan((float(width) - cx) / focal) + std::atan(cx / focal);
}

float CameraIntrinsics::verticalFov(uint32_t height) const{
    const float focal = std::abs(fy);
    return std::atan((float(height) - cy) / focal) + std::atan(cy / focal);
}
