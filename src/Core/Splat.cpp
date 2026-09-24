#include "Splat.h"

#include <algorithm>
#include <cmath>

namespace SplatMath{

float activateOpacity(float logit){
    return 1.0f / (1.0f + std::exp(-logit));
}

glm::vec3 activateScale(const glm::vec3& logScale){
    return glm::vec3(std::exp(logScale.x), std::exp(logScale.y), std::exp(logScale.z));
}

glm::quat activateRotation(const glm::vec4& stored){
    //glm::quat takes (w, x, y, z), and the record enumerates rot_0..rot_3 in the same order
    const glm::quat raw(stored.x, stored.y, stored.z, stored.w);

    //Zero length is not a rotation. Such a gaussian should not exist in the record, but if one
    //shows up it is better that it end up unrotated than that the whole matrix turn into NaN and
    //poison everything downstream
    const float length = std::sqrt(raw.w*raw.w + raw.x*raw.x + raw.y*raw.y + raw.z*raw.z);
    if(length <= 0.0f){
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return raw / length;
}

glm::vec3 colorFromSH0(const glm::vec3& dc){
    //The zeroth SH basis is the constant 0.5 * sqrt(1/pi)
    constexpr float sh0 = 0.28209479177387814f;
    return glm::vec3(0.5f) + sh0 * dc;
}

glm::vec3 colorFromSH(const glm::vec3& dc,
                      const float* rest,
                      uint32_t coeffsPerChannel,
                      uint32_t degree,
                      const glm::vec3& direction){
    //Degree 0 is the only one that needs no direction, so it is not even computed through it
    glm::vec3 result = colorFromSH0(dc);
    if(degree == 0 || rest == nullptr || coeffsPerChannel == 0){
        return result;
    }

    const float length = glm::length(direction);
    if(length <= 0.0f){
        return result;   //camera exactly at the gaussian: no direction, flat color remains
    }
    const glm::vec3 d = direction / length;
    const float x = d.x, y = d.y, z = d.z;

    //Coefficient k of channel c. Here and nowhere else does the per-channel layout live
    const auto sh = [&](uint32_t k){
        return glm::vec3(rest[0 * coeffsPerChannel + k],
                         rest[1 * coeffsPerChannel + k],
                         rest[2 * coeffsPerChannel + k]);
    };

    //The constants are values of real spherical harmonics, the same ones training uses. The signs
    //are part of the basis (Condon-Shortley), so they must not be "simplified"
    constexpr float c1 = 0.4886025119029199f;

    result += -c1 * y * sh(0) + c1 * z * sh(1) - c1 * x * sh(2);

    if(degree > 1){
        const float xx = x*x, yy = y*y, zz = z*z;
        const float xy = x*y, yz = y*z, xz = x*z;

        constexpr float c20 =  1.0925484305920792f;
        constexpr float c22 =  0.31539156525252005f;
        constexpr float c24 =  0.5462742152960396f;

        result += c20 * xy * sh(3)
                - c20 * yz * sh(4)
                + c22 * (2.0f*zz - xx - yy) * sh(5)
                - c20 * xz * sh(6)
                + c24 * (xx - yy) * sh(7);

        if(degree > 2){
            constexpr float c30 = -0.5900435899266435f;
            constexpr float c31 =  2.890611442640554f;
            constexpr float c32 = -0.4570457994644658f;
            constexpr float c33 =  0.3731763325901154f;
            constexpr float c35 =  1.445305721320277f;

            result += c30 * y * (3.0f*xx - yy) * sh(8)
                    + c31 * xy * z * sh(9)
                    + c32 * y * (4.0f*zz - xx - yy) * sh(10)
                    + c33 * z * (2.0f*zz - 3.0f*xx - 3.0f*yy) * sh(11)
                    + c32 * x * (4.0f*zz - xx - yy) * sh(12)
                    + c35 * z * (xx - yy) * sh(13)
                    + c30 * x * (xx - 3.0f*yy) * sh(14);
        }
    }

    return result;
}

glm::mat3 covariance3D(const glm::vec3& scale, const glm::quat& rotation){
    const glm::mat3 R = glm::mat3_cast(rotation);

    //M = R * S, so the covariance is M * M'. Multiplying columns instead of the full diagonal
    //matrix: same result, and it is clear that S only stretches the rotation axes
    glm::mat3 M;
    M[0] = R[0] * scale.x;
    M[1] = R[1] * scale.y;
    M[2] = R[2] * scale.z;

    return M * glm::transpose(M);
}

glm::vec2 projectToPixels(const glm::vec3& position,
                          const glm::mat4& view,
                          float focalX, float focalY,
                          float principalX, float principalY){
    const glm::vec3 viewPosition = glm::vec3(view * glm::vec4(position, 1.0f));
    const float depth = -viewPosition.z;
    if(depth <= 0.0f){
        return glm::vec2(0.0f);
    }
    return glm::vec2(principalX + focalX * viewPosition.x / depth,
                     principalY + focalY * viewPosition.y / depth);
}

bool prepare(const Splat& splat,
             const glm::mat4& view,
             float focalX, float focalY,
             float principalX, float principalY,
             float blur,
             PreparedSplat& out){
    const glm::vec3 viewPosition = glm::vec3(view * glm::vec4(splat.position, 1.0f));
    const float depth = -viewPosition.z;
    if(depth <= 0.0f){
        return false;
    }

    //The ratio limit is derived from the image itself: half a width over the edge is allowed,
    //which is enough for a blotch that still reaches the frame to be correct, and beyond that it
    //is not visible anyway
    const float limitX = 1.3f * principalX / focalX;
    const float limitY = 1.3f * principalY / std::fabs(focalY);

    const glm::mat3 covariance = covariance3D(splat.scale, splat.rotation);
    const glm::mat2 screen = covariance2D(covariance, splat.position, view,
                                          focalX, focalY, limitX, limitY, blur);

    const float determinant = screen[0][0] * screen[1][1] - screen[0][1] * screen[1][0];
    if(determinant <= 0.0f){
        return false;   //an ellipse with no area cannot be inverted, or seen
    }

    const float inverse = 1.0f / determinant;

    out.centerConic.x = principalX + focalX * viewPosition.x / depth;
    out.centerConic.y = principalY + focalY * viewPosition.y / depth;
    out.centerConic.z =  screen[1][1] * inverse;
    out.centerConic.w = -screen[0][1] * inverse;
    out.conicOpacityDepth.x =  screen[0][0] * inverse;
    out.conicOpacityDepth.y = splat.opacity;
    out.conicOpacityDepth.z = depth;

    //THE RADIUS AT WHICH THE BLOTCH GETS CUT OFF: three sigmas along the ellipse's longer axis.
    //
    //The longer axis is the covariance's larger eigenvalue, and for a 2x2 matrix it comes out
    //without any iteration: half the trace plus the root of the difference of squares. A lower
    //bound stands under the root, because rounding can push it slightly below zero for nearly
    //circular blotches.
    //
    //Three sigmas hold 98.89% of the mass (1 - exp(-4.5)); the rest is a tail that is not
    //visible in eight bits anyway, and would be paid for in tiles the splat barely touches
    const float mid = 0.5f * (screen[0][0] + screen[1][1]);
    const float spread = std::sqrt(std::max(0.1f, mid * mid - determinant));
    out.conicOpacityDepth.w = std::ceil(3.0f * std::sqrt(mid + spread));
    out.color = glm::vec4(splat.color, 0.0f);
    return true;
}

glm::mat2 covariance2D(const glm::mat3& covariance,
                       const glm::vec3& position,
                       const glm::mat4& view,
                       float focalX, float focalY,
                       float tangentLimitX, float tangentLimitY,
                       float blur){
    const glm::vec3 viewPosition = glm::vec3(view * glm::vec4(position, 1.0f));

    //Camera looks down -Z, so depth is positive when the point is in front of it
    const float depth = -viewPosition.z;
    if(depth <= 0.0f){
        return glm::mat2(0.0f);
    }

    //The ratio to the axis is clamped BEFORE it enters the Jacobian. The Jacobian is exact only
    //near the point where it is taken; at the frame edge it would stretch the blotch across half
    //the screen. Only what enters the derivative is clamped - the depth itself stays real
    const float limitedX = std::clamp(viewPosition.x / depth, -tangentLimitX, tangentLimitX) * depth;
    const float limitedY = std::clamp(viewPosition.y / depth, -tangentLimitY, tangentLimitY) * depth;

    //The projection derivative (x/z, y/z) multiplied by the focal lengths. That gives pixels per
    //scene unit, which is why fx and fy are in pixels
    const float inverseDepth = 1.0f / depth;
    const float inverseDepth2 = inverseDepth * inverseDepth;

    glm::mat3 J(0.0f);
    J[0][0] = focalX * inverseDepth;
    J[2][0] = focalX * limitedX * inverseDepth2;
    J[1][1] = focalY * inverseDepth;
    J[2][1] = focalY * limitedY * inverseDepth2;

    //Only the view rotation: a translation does not change the shape of the blotch
    const glm::mat3 W = glm::mat3(view);
    const glm::mat3 T = J * W;

    const glm::mat3 projected = T * covariance * glm::transpose(T);

    glm::mat2 result;
    result[0][0] = projected[0][0] + blur;
    result[0][1] = projected[0][1];
    result[1][0] = projected[1][0];
    result[1][1] = projected[1][1] + blur;
    return result;
}

namespace{

float conicForm(float a, float b, float c, float x, float y){
    return a * x * x + 2.0f * b * x * y + c * y * y;
}

}

bool touchesTile(const PreparedSplat& splat, int tileX, int tileY, uint32_t tileSize){
    const float opacity = splat.conicOpacityDepth.y;
    if(opacity < 1.0f / 255.0f){
        return false;
    }

    const float size = float(tileSize);
    const float xLo = splat.centerConic.x - (float(tileX) * size + size - 0.5f);
    const float xHi = splat.centerConic.x - (float(tileX) * size + 0.5f);
    const float yLo = splat.centerConic.y - (float(tileY) * size + size - 0.5f);
    const float yHi = splat.centerConic.y - (float(tileY) * size + 0.5f);

    const float nearX = std::max(0.0f, std::max(xLo, -xHi));
    const float nearY = std::max(0.0f, std::max(yLo, -yHi));
    const float radius = splat.conicOpacityDepth.w;
    if(nearX * nearX + nearY * nearY > radius * radius){
        return false;
    }

    if(xLo <= 0.0f && xHi >= 0.0f && yLo <= 0.0f && yHi >= 0.0f){
        return true;
    }

    const float a = splat.centerConic.z;
    const float b = splat.centerConic.w;
    const float c = splat.conicOpacityDepth.x;
    float best = conicForm(a, b, c, xLo, std::clamp(-b * xLo / c, yLo, yHi));
    best = std::min(best, conicForm(a, b, c, xHi, std::clamp(-b * xHi / c, yLo, yHi)));
    best = std::min(best, conicForm(a, b, c, std::clamp(-b * yLo / a, xLo, xHi), yLo));
    best = std::min(best, conicForm(a, b, c, std::clamp(-b * yHi / a, xLo, xHi), yHi));

    const float threshold = 2.0f * std::log(255.0f * opacity);
    return best <= threshold * 1.001f + 1e-3f;
}


bool insideBox(const glm::vec3& point,
               const glm::vec3& center,
               const glm::vec3& halfExtent){
    const glm::vec3 away = point - center;

    return std::fabs(away.x) <= halfExtent.x
        && std::fabs(away.y) <= halfExtent.y
        && std::fabs(away.z) <= halfExtent.z;
}

bool insideBox(const glm::vec3& point,
               const glm::vec3& center,
               const glm::vec3& halfExtent,
               const glm::quat& orientation){
    //Into the box's frame: the conjugate rotates BACK. The quaternion is normalized because the
    //box comes from the UI, and there nobody guarantees length - an unnormalized one would
    //silently change the size of the box
    const glm::vec3 local = glm::conjugate(glm::normalize(orientation)) * (point - center);

    return std::fabs(local.x) <= halfExtent.x
        && std::fabs(local.y) <= halfExtent.y
        && std::fabs(local.z) <= halfExtent.z;
}

}
