#pragma once

#include <Spool/Gltf.h>
#include <Warp/Stage.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Loom{

// Unit-scale editor primitives. Meshes are cached once by the viewport renderer.
inline Spool::GltfPrimitive unitShape(Warp::Shape shape){
    Spool::GltfPrimitive p;
    auto vertex = [&](const glm::vec3& position, const glm::vec3& normal, glm::vec2 uv){
        p.positions.insert(p.positions.end(), {position.x, position.y, position.z});
        p.normals.insert(p.normals.end(), {normal.x, normal.y, normal.z});
        p.uv0.insert(p.uv0.end(), {uv.x, uv.y});
    };
    auto quad = [&](glm::vec3 centre, glm::vec3 u, glm::vec3 v){
        const glm::vec3 n = glm::normalize(glm::cross(u, v));
        const uint32_t base = uint32_t(p.vertexCount());
        const glm::vec3 corners[4] = {centre - u - v, centre + u - v, centre + u + v, centre - u + v};
        const glm::vec2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for(int k = 0; k < 4; ++k) vertex(corners[k], n, uvs[k]);
        p.indices.insert(p.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec3& outward){
        glm::vec3 normal = glm::cross(b - a, c - a);
        if(glm::dot(normal, outward) < 0.0f) std::swap(b, c);
        normal = glm::normalize(glm::cross(b - a, c - a));
        const uint32_t base = uint32_t(p.vertexCount());
        vertex(a, normal, {0.5f, 0.0f});
        vertex(b, normal, {0.0f, 1.0f});
        vertex(c, normal, {1.0f, 1.0f});
        p.indices.insert(p.indices.end(), {base, base + 1, base + 2});
    };

    if(shape == Warp::Shape::Plane){
        quad({0, 0, 0}, {0, 0, 0.5f}, {0.5f, 0, 0});
        return p;
    }
    if(shape == Warp::Shape::Cube){
        constexpr float h = 0.5f;
        quad({0, 0, h}, {h, 0, 0}, {0, h, 0});
        quad({0, 0, -h}, {-h, 0, 0}, {0, h, 0});
        quad({h, 0, 0}, {0, 0, -h}, {0, h, 0});
        quad({-h, 0, 0}, {0, 0, h}, {0, h, 0});
        quad({0, h, 0}, {h, 0, 0}, {0, 0, -h});
        quad({0, -h, 0}, {h, 0, 0}, {0, 0, h});
        return p;
    }
    if(shape == Warp::Shape::Pyramid){
        constexpr float h = 0.5f;
        quad({0, -h, 0}, {h, 0, 0}, {0, 0, h});
        const glm::vec3 base[4] = {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}};
        const glm::vec3 tip{0, h, 0};
        for(int i = 0; i < 4; ++i){
            const glm::vec3 a = base[i], b = base[(i + 1) % 4];
            triangle(a, b, tip, (a + b + tip) / 3.0f);
        }
        return p;
    }

    constexpr int segments = 32;
    constexpr float radius = 0.5f;
    struct Ring{ float y, r; glm::vec3 normalCentre; bool spherical; };
    std::vector<Ring> rings;
    if(shape == Warp::Shape::Capsule){
        constexpr int hemiRings = 8;
        constexpr float halfPi = 1.57079632679489661923f;
        for(int i = 0; i <= hemiRings; ++i){
            const float a = halfPi * float(i) / float(hemiRings);
            rings.push_back({0.5f + radius * std::cos(a), radius * std::sin(a), {0, 0.5f, 0}, true});
        }
        rings.push_back({-0.5f, radius, {0, 0, 0}, false});
        for(int i = 1; i <= hemiRings; ++i){
            const float a = halfPi + halfPi * float(i) / float(hemiRings);
            rings.push_back({-0.5f + radius * std::cos(a), radius * std::sin(a), {0, -0.5f, 0}, true});
        }
    }else{
        constexpr int latitudeRings = 16;
        constexpr float pi = 3.14159265358979323846f;
        for(int i = 0; i <= latitudeRings; ++i){
            const float a = pi * float(i) / float(latitudeRings);
            rings.push_back({radius * std::cos(a), radius * std::sin(a), {0, 0, 0}, true});
        }
    }

    for(size_t i = 0; i < rings.size(); ++i){
        const Ring& ring = rings[i];
        for(int j = 0; j <= segments; ++j){
            const float phi = 6.28318530717958647692f * float(j) / float(segments);
            const glm::vec3 radial{std::cos(phi), 0.0f, std::sin(phi)};
            const glm::vec3 position{ring.r * radial.x, ring.y, ring.r * radial.z};
            const glm::vec3 normal = ring.spherical
                ? glm::normalize(position - ring.normalCentre) : radial;
            vertex(position, normal, {float(j) / float(segments), 1.0f - float(i) / float(rings.size() - 1)});
        }
    }
    const uint32_t stride = segments + 1;
    for(uint32_t i = 0; i + 1 < rings.size(); ++i){
        for(uint32_t j = 0; j < segments; ++j){
            const uint32_t a = i * stride + j, b = (i + 1) * stride + j;
            const uint32_t c = b + 1, d = a + 1;
            p.indices.insert(p.indices.end(), {a, c, b, a, d, c});
        }
    }
    return p;
}

} // namespace Loom
