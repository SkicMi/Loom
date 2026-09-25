#include "Tracer/Scene.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace Tracer{

namespace{

const std::array<float, 256>& srgbTable(){
    static const std::array<float, 256> table = []{
        std::array<float, 256> t{};
        for(int i = 0; i < 256; ++i) t[size_t(i)] = srgbToLinear(float(i) / 255.0f);
        return t;
    }();
    return table;
}

int wrap(int i, int n, bool repeat){
    if(repeat){
        i %= n;
        return i < 0 ? i + n : i;
    }
    return std::clamp(i, 0, n - 1);
}

}

float srgbToLinear(float v){
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float v){
    v = std::max(0.0f, v);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

glm::vec4 Texture::fetch(int x, int y) const{
    x = wrap(x, int(width), repeat);
    y = wrap(y, int(height), repeat);
    const size_t at = (size_t(y) * width + size_t(x)) * 4;
    if(!floats.empty()) return {floats[at], floats[at + 1], floats[at + 2], floats[at + 3]};
    const uint8_t* p = bytes.data() + at;
    if(srgb){
        const std::array<float, 256>& t = srgbTable();
        return {t[p[0]], t[p[1]], t[p[2]], float(p[3]) / 255.0f};
    }
    return glm::vec4(p[0], p[1], p[2], p[3]) / 255.0f;
}

glm::vec4 Texture::sample(glm::vec2 uv) const{
    if(!valid()) return glm::vec4(1.0f);
    const float x = uv.x * float(width) - 0.5f;
    const float y = uv.y * float(height) - 0.5f;
    const float fx = std::floor(x), fy = std::floor(y);
    const float tx = x - fx, ty = y - fy;
    const int ix = int(fx), iy = int(fy);
    const glm::vec4 a = fetch(ix, iy), b = fetch(ix + 1, iy);
    const glm::vec4 c = fetch(ix, iy + 1), d = fetch(ix + 1, iy + 1);
    return glm::mix(glm::mix(a, b, tx), glm::mix(c, d, tx), ty);
}

glm::vec2 Camera::distortPixel(glm::vec2 p) const{
    if(!distorted()) return p;
    const glm::vec2 f(lens.x, lens.y), c(lens.z, lens.w);
    const glm::vec2 n = (p - c) / f;
    const float r2 = glm::dot(n, n);
    return c + f * n * (1.0f + r2 * (k1 + r2 * k2));
}

glm::vec2 Camera::undistortPixel(glm::vec2 p) const{
    if(!distorted()) return p;
    //Obrnuto fiksnom tockom, kao Engine::undistort - ali osam koraka: rub 4K kadra s k1 0.2 treba
    //vise od pet da greska padne ispod stotinke piksela (test_tracer)
    const glm::vec2 f(lens.x, lens.y), c(lens.z, lens.w);
    const glm::vec2 m = (p - c) / f;
    glm::vec2 n = m;
    for(int step = 0; step < 8; ++step){
        const float r2 = glm::dot(n, n);
        const float factor = 1.0f + r2 * (k1 + r2 * k2);
        if(factor <= 0.0f) break;
        n = m / factor;
    }
    return c + f * n;
}

glm::vec2 Camera::pixelOf(const glm::vec3& local) const{
    return distortPixel(glm::vec2(centre.x + focalPixels * local.x / -local.z, centre.y - focalPixels * local.y / -local.z));
}

void Camera::ray(glm::vec2 pixelIn, glm::vec2 lensSample, glm::vec3& origin, glm::vec3& direction) const{
    const glm::vec2 pixel = undistortPixel(pixelIn);
    glm::vec3 d((pixel.x - centre.x) / focalPixels, -(pixel.y - centre.y) / focalPixels, -1.0f);
    glm::vec3 o(0.0f);
    if(apertureRadius > 0.0f){
        //Konkoncentricno preslikavanje kvadrata na krug (Shirley-Chiu): ravnomjerno, bez nabora
        const glm::vec2 s = lensSample * 2.0f - 1.0f;
        glm::vec2 disk(0.0f);
        if(s.x != 0.0f || s.y != 0.0f){
            const float pi4 = glm::pi<float>() * 0.25f;
            if(std::abs(s.x) > std::abs(s.y)) disk = s.x * glm::vec2(std::cos(pi4 * s.y / s.x), std::sin(pi4 * s.y / s.x));
            else disk = s.y * glm::vec2(std::cos(2.0f * pi4 - pi4 * s.x / s.y), std::sin(2.0f * pi4 - pi4 * s.x / s.y));
        }
        o = glm::vec3(disk * apertureRadius, 0.0f);
        d = d * focusDistance - o;               //kroz tocku ostre ravnine
    }
    origin = glm::vec3(cameraToWorld * glm::vec4(o, 1.0f));
    direction = glm::normalize(glm::vec3(cameraToWorld * glm::vec4(d, 0.0f)));
}

bool Camera::project(const glm::vec3& world, glm::vec2& pixel) const{
    const glm::vec3 p = glm::vec3(glm::inverse(cameraToWorld) * glm::vec4(world, 1.0f));
    if(p.z >= 0.0f) return false;
    pixel = pixelOf(p);
    return true;
}

uint32_t Scene::addTexture(Texture texture){
    textures.push_back(std::move(texture));
    return uint32_t(textures.size() - 1);
}

uint32_t Scene::addMaterial(Material material){
    materials.push_back(std::move(material));
    return uint32_t(materials.size() - 1);
}

uint32_t Scene::addMesh(const MeshData& mesh, const glm::mat4& world, uint32_t material,
                        const std::string& name, ObjectFlags flags){
    Object object;
    object.name = name;
    object.flags = flags;
    object.firstTriangle = uint32_t(triangles.size());
    const uint32_t objectIndex = uint32_t(objects.size());

    const size_t count = mesh.positions.size();
    const uint32_t base = uint32_t(positions.size());
    //Normale se nose inverznom transponiranom - inace ih nejednoliko mjerilo iskrivi
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
    const bool mirrored = glm::determinant(glm::mat3(world)) < 0.0f;

    std::vector<glm::vec3> objectNormals = mesh.normals;
    if(objectNormals.size() != count){
        //Glatke normale: zbroj normala susjednih lica, tezinski po povrsini
        objectNormals.assign(count, glm::vec3(0.0f));
        for(size_t t = 0; t + 2 < mesh.indices.size(); t += 3){
            const uint32_t a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
            if(a >= count || b >= count || c >= count) continue;
            const glm::vec3 n = glm::cross(mesh.positions[b] - mesh.positions[a], mesh.positions[c] - mesh.positions[a]);
            objectNormals[a] += n; objectNormals[b] += n; objectNormals[c] += n;
        }
    }

    const bool hasUv = mesh.uvs.size() == count;
    //TANGENTE iz UV-a (Lengyel): smjer u kojem u raste, i bitangenta u smjeru u kojem slika ide
    //GORE. glTF ima v = 0 u prvom retku slike, pa "gore" znaci da v PADA - zato -v
    std::vector<glm::vec3> tangentSum(hasUv ? count : 0, glm::vec3(0.0f)), bitangentSum(hasUv ? count : 0, glm::vec3(0.0f));
    if(hasUv){
        for(size_t t = 0; t + 2 < mesh.indices.size(); t += 3){
            const uint32_t a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
            if(a >= count || b >= count || c >= count) continue;
            const glm::vec3 e1 = mesh.positions[b] - mesh.positions[a], e2 = mesh.positions[c] - mesh.positions[a];
            const glm::vec2 d1(mesh.uvs[b].x - mesh.uvs[a].x, -(mesh.uvs[b].y - mesh.uvs[a].y));
            const glm::vec2 d2(mesh.uvs[c].x - mesh.uvs[a].x, -(mesh.uvs[c].y - mesh.uvs[a].y));
            const float det = d1.x * d2.y - d2.x * d1.y;
            if(std::abs(det) < 1e-20f) continue;
            const float r = 1.0f / det;
            const glm::vec3 tangent = (e1 * d2.y - e2 * d1.y) * r;
            const glm::vec3 bitangent = (e2 * d1.x - e1 * d2.x) * r;
            for(uint32_t v : {a, b, c}){ tangentSum[v] += tangent; bitangentSum[v] += bitangent; }
        }
    }

    for(size_t i = 0; i < count; ++i){
        positions.push_back(glm::vec3(world * glm::vec4(mesh.positions[i], 1.0f)));
        glm::vec3 n = normalMatrix * objectNormals[i];
        n = glm::dot(n, n) > 1e-30f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
        normals.push_back(n);
        uvs.push_back(hasUv ? mesh.uvs[i] : glm::vec2(0.0f));
        glm::vec4 tangent(0.0f, 0.0f, 0.0f, 1.0f);
        if(hasUv){
            glm::vec3 t = glm::mat3(world) * tangentSum[i];
            const glm::vec3 b = glm::mat3(world) * bitangentSum[i];
            t -= n * glm::dot(n, t);
            if(glm::dot(t, t) > 1e-30f){
                t = glm::normalize(t);
                tangent = glm::vec4(t, glm::dot(glm::cross(n, t), b) < 0.0f ? -1.0f : 1.0f);
            }
        }
        tangents.push_back(tangent);
    }

    for(size_t t = 0; t + 2 < mesh.indices.size(); t += 3){
        uint32_t a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
        if(a >= count || b >= count || c >= count) continue;
        const glm::vec3& pa = positions[base + a];
        if(glm::length(glm::cross(positions[base + b] - pa, positions[base + c] - pa)) <= 0.0f) continue;
        //Zrcaljenje (negativno mjerilo) okrece redoslijed vrhova; geometrijska normala mora ostati
        //na strani na kojoj su normale vrhova
        if(mirrored) std::swap(b, c);
        Triangle triangle;
        triangle.v[0] = base + a; triangle.v[1] = base + b; triangle.v[2] = base + c;
        triangle.material = material;
        triangle.object = objectIndex;
        triangles.push_back(triangle);
    }
    object.triangleCount = uint32_t(triangles.size()) - object.firstTriangle;
    objects.push_back(object);
    return objectIndex;
}

MeshData unitCube(){
    MeshData m;
    auto quad = [&](glm::vec3 centre, glm::vec3 u, glm::vec3 v){
        const glm::vec3 n = glm::normalize(glm::cross(u, v));
        const uint32_t start = uint32_t(m.positions.size());
        const glm::vec3 corners[4] = {centre - u - v, centre + u - v, centre + u + v, centre - u + v};
        const glm::vec2 uv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for(int k = 0; k < 4; ++k){ m.positions.push_back(corners[k]); m.normals.push_back(n); m.uvs.push_back(uv[k]); }
        m.indices.insert(m.indices.end(), {start, start + 1, start + 2, start, start + 2, start + 3});
    };
    const float h = 0.5f;
    quad({0, 0, h}, {h, 0, 0}, {0, h, 0});
    quad({0, 0, -h}, {-h, 0, 0}, {0, h, 0});
    quad({h, 0, 0}, {0, 0, -h}, {0, h, 0});
    quad({-h, 0, 0}, {0, 0, h}, {0, h, 0});
    quad({0, h, 0}, {h, 0, 0}, {0, 0, -h});
    quad({0, -h, 0}, {h, 0, 0}, {0, 0, h});
    return m;
}

MeshData unitPlane(){
    MeshData m;
    const glm::vec3 u(0.0f, 0.0f, 0.5f), v(0.5f, 0.0f, 0.0f);
    const glm::vec3 corners[4] = {-u - v, u - v, u + v, -u + v};
    const glm::vec2 uv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for(int k = 0; k < 4; ++k){ m.positions.push_back(corners[k]); m.normals.push_back({0, 1, 0}); m.uvs.push_back(uv[k]); }
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}

MeshData uvSphere(float radius, uint32_t segments, uint32_t rings){
    MeshData m;
    segments = std::max(3u, segments);
    rings = std::max(2u, rings);
    for(uint32_t r = 0; r <= rings; ++r){
        const float theta = glm::pi<float>() * float(r) / float(rings);
        for(uint32_t s = 0; s <= segments; ++s){
            const float phi = 2.0f * glm::pi<float>() * float(s) / float(segments);
            const glm::vec3 n(std::sin(theta) * std::sin(phi), std::cos(theta), std::sin(theta) * std::cos(phi));
            m.positions.push_back(n * radius);
            m.normals.push_back(n);
            m.uvs.push_back({float(s) / float(segments), float(r) / float(rings)});
        }
    }
    const uint32_t row = segments + 1;
    for(uint32_t r = 0; r < rings; ++r){
        for(uint32_t s = 0; s < segments; ++s){
            const uint32_t a = r * row + s, b = a + row;
            if(r != 0) m.indices.insert(m.indices.end(), {a, b, a + 1});
            if(r + 1 != rings) m.indices.insert(m.indices.end(), {a + 1, b, b + 1});
        }
    }
    return m;
}

//---------------------------------------------------------------------------------------------
// PREETHAMOVO NEBO
//---------------------------------------------------------------------------------------------
Texture makeSky(const glm::vec3& towardSunIn, float turbidity, float zenith, const glm::vec3& ground,
                uint32_t width, uint32_t height){
    Texture sky;
    sky.width = width;
    sky.height = height;
    sky.floats.assign(size_t(width) * height * 4, 0.0f);
    sky.srgb = false;

    const glm::vec3 towardSun = glm::normalize(towardSunIn);
    const float T = std::clamp(turbidity, 1.7f, 10.0f);
    //Sunce ispod horizonta: model za to ne vrijedi. Racuna se kao da je tik iznad, a sjaj neba
    //pada s dubinom sumraka - noc nije crna rupa, ali nije ni dan
    const float sunElevation = std::asin(std::clamp(towardSun.y, -1.0f, 1.0f));
    const float thetaS = glm::half_pi<float>() - std::max(sunElevation, 0.02f);
    const float dusk = sunElevation >= 0.0f ? 1.0f : std::max(0.02f, 1.0f + sunElevation / 0.2f);
    glm::vec3 sunDir = towardSun;
    if(sunElevation < 0.02f){
        const glm::vec2 flat = glm::length(glm::vec2(towardSun.x, towardSun.z)) > 1e-6f
            ? glm::normalize(glm::vec2(towardSun.x, towardSun.z)) : glm::vec2(1.0f, 0.0f);
        sunDir = glm::vec3(flat.x * std::cos(0.02f), std::sin(0.02f), flat.y * std::cos(0.02f));
    }

    struct Perez{ float A, B, C, D, E; };
    const Perez pY{0.1787f * T - 1.4630f, -0.3554f * T + 0.4275f, -0.0227f * T + 5.3251f, 0.1206f * T - 2.5771f, -0.0670f * T + 0.3703f};
    const Perez px{-0.0193f * T - 0.2592f, -0.0665f * T + 0.0008f, -0.0004f * T + 0.2125f, -0.0641f * T - 0.8989f, -0.0033f * T + 0.0452f};
    const Perez py{-0.0167f * T - 0.2608f, -0.0950f * T + 0.0092f, -0.0079f * T + 0.2102f, -0.0441f * T - 1.6537f, -0.0109f * T + 0.0529f};
    auto F = [](const Perez& p, float theta, float gamma){
        const float cosTheta = std::max(0.01f, std::cos(theta));
        const float cosGamma = std::cos(gamma);
        return (1.0f + p.A * std::exp(p.B / cosTheta)) * (1.0f + p.C * std::exp(p.D * gamma) + p.E * cosGamma * cosGamma);
    };
    const float t2 = thetaS * thetaS, t3 = t2 * thetaS;
    const float xz = T * T * (0.00166f * t3 - 0.00375f * t2 + 0.00209f * thetaS) +
                     T * (-0.02903f * t3 + 0.06377f * t2 - 0.03202f * thetaS + 0.00394f) +
                     (0.11693f * t3 - 0.21196f * t2 + 0.06052f * thetaS + 0.25886f);
    const float yz = T * T * (0.00275f * t3 - 0.00610f * t2 + 0.00317f * thetaS) +
                     T * (-0.04214f * t3 + 0.08970f * t2 - 0.04153f * thetaS + 0.00516f) +
                     (0.15346f * t3 - 0.26756f * t2 + 0.06670f * thetaS + 0.26688f);
    const float normY = F(pY, 0.0f, thetaS), normx = F(px, 0.0f, thetaS), normy = F(py, 0.0f, thetaS);

    for(uint32_t row = 0; row < height; ++row){
        const float theta = glm::pi<float>() * (float(row) + 0.5f) / float(height);
        for(uint32_t col = 0; col < width; ++col){
            const float phi = 2.0f * glm::pi<float>() * (float(col) + 0.5f) / float(width);
            const glm::vec3 d(std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi));
            glm::vec3 rgb;
            //Nebo se racuna i malo ispod horizonta, pa se u uskom pojasu stopi s tlom
            const float skyTheta = std::min(theta, glm::half_pi<float>() - 0.001f);
            const glm::vec3 skyDir(std::sin(skyTheta) * std::sin(phi), std::cos(skyTheta), -std::sin(skyTheta) * std::cos(phi));
            const float gamma = std::acos(std::clamp(glm::dot(skyDir, sunDir), -1.0f, 1.0f));
            const float Y = zenith * F(pY, skyTheta, gamma) / normY * dusk;
            const float x = xz * F(px, skyTheta, gamma) / normx;
            const float y = yz * F(py, skyTheta, gamma) / normy;
            const float X = x / std::max(1e-4f, y) * Y, Z = (1.0f - x - y) / std::max(1e-4f, y) * Y;
            rgb = glm::max(glm::vec3(0.0f), glm::vec3(3.2406f * X - 1.5372f * Y - 0.4986f * Z,
                                                      -0.9689f * X + 1.8758f * Y + 0.0415f * Z,
                                                      0.0557f * X - 0.2040f * Y + 1.0570f * Z));
            //Tlo: jednoliko, a horizont ima pojas od dva stupnja u kojem se nebo pretopi u njega
            const float below = std::clamp((-d.y) / 0.035f, 0.0f, 1.0f);
            rgb = glm::mix(rgb, ground * dusk, below);
            float* out = sky.floats.data() + (size_t(row) * width + col) * 4;
            out[0] = rgb.r; out[1] = rgb.g; out[2] = rgb.b; out[3] = 1.0f;
        }
    }
    return sky;
}

}
