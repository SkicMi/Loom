#include "Tracer/Compiled.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Tracer{

namespace{
constexpr float Pi = glm::pi<float>();
float luminance(const glm::vec3& c){ return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
float oneMinusCosFromSin2(float sin2){
    const float cosMax = std::sqrt(std::max(0.0f, 1.0f - sin2));
    return sin2 / (1.0f + cosMax);
}
}

std::shared_ptr<const CompiledScene> compile(Scene scene){
    auto compiled = std::make_shared<CompiledScene>();
    CompiledScene& c = *compiled;
    c.world = std::move(scene);
    Scene& world = c.world;
    Bvh& tree = c.tree;
    EnvironmentSampler& sky = c.sky;
    std::vector<LightRecord>& lights = c.lights;
    std::vector<int>& emitterOfTriangle = c.emitterOfTriangle;
    std::vector<uint8_t>& triangleFlags = c.triangleFlags;
    float& sceneRadius = c.sceneRadius;
    const auto start = std::chrono::steady_clock::now();
    if(world.materials.empty()) world.materials.push_back(Material{});
    for(Texture& t : world.textures) if(t.mips.empty()) t.buildMips();
    for(const Volume& v : world.volumes) c.volumeInverse.push_back(glm::inverse(v.toWorld));
    for(Triangle& t : world.triangles) if(t.material >= world.materials.size()) t.material = 0;
    //Teksture koje ne postoje se odspoje - materijal radi s faktorima
    for(Material& m : world.materials){
        for(int* slot : {&m.baseColorTexture, &m.metallicRoughnessTexture, &m.normalTexture, &m.emissionTexture}){
            if(*slot >= int(world.textures.size()) || (*slot >= 0 && !world.textures[size_t(*slot)].valid())) *slot = -1;
        }
    }
    tree.build(world.positions, world.triangles);
    c.cameraInverse = glm::inverse(world.camera.cameraToWorld);
    const glm::vec3 extent = tree.boundsMax() - tree.boundsMin();
    sceneRadius = world.triangles.empty() ? 1.0f : std::max(1e-4f, 0.5f * glm::length(extent));

    triangleFlags.assign(world.triangles.size(), 0);
    for(size_t i = 0; i < world.triangles.size(); ++i){
        const Triangle& t = world.triangles[i];
        const Material& m = world.materials[t.material];
        uint8_t flags = 0;
        if(m.alphaMode != Material::Alpha::Opaque) flags |= AlphaTested;
        if(m.transmission > 0.0f && m.metallic < 1.0f) flags |= Transmissive;
        if(t.object < world.objects.size()){
            const ObjectFlags& o = world.objects[t.object].flags;
            if(o.shadowCatcher) flags |= Catcher;
            if(!o.cameraVisible) flags |= NoCamera;
            if(!o.castsShadows) flags |= NoShadow;
        }
        triangleFlags[i] = flags;
    }

    sky.build(world.environment);

    //SVJETLA I VJEROJATNOST IZBORA. Tezina svakog je procjena ozracenosti koju daje na tipicnoj
    //udaljenosti (pola scene) - nebo pi*L, sunce svoja ozracenost, kugla I/d^2, trokut L*A/d^2.
    //Tezina ne mijenja ocekivanje, samo sum: svjetlo koje malo daje rjedje se bira
    const float reference = std::max(1e-3f, sceneRadius * 0.5f);
    const float reference2 = reference * reference;
    std::vector<float> weights;
    auto push = [&](LightRecord light, float weight){
        light.index = int(lights.size());
        lights.push_back(light);
        weights.push_back(std::max(weight, 0.0f));
    };
    for(const Tracer::Light& source : world.lights){
        const glm::vec3 power = source.color * source.intensity;
        if(luminance(power) <= 0.0f) continue;
        LightRecord light;
        if(source.type == Tracer::Light::Type::Distant){
            light.kind = LightRecord::Sun;
            light.axis = -glm::normalize(source.direction);
            const float half = std::clamp(source.angle * 0.5f, 0.0f, 0.5f * Pi);
            light.delta = half < 1e-5f;
            if(light.delta){
                light.radiance = power;
            }else{
                const float sin2 = std::sin(half) * std::sin(half);
                light.oneMinusCos = oneMinusCosFromSin2(sin2);
                light.cosMax = 1.0f - light.oneMinusCos;
                //Disk kutnog polumjera theta daje okomitoj plohi ozracenost L * pi * sin^2(theta)
                light.radiance = power / (Pi * sin2);
            }
            push(light, luminance(power));
        }else{
            light.kind = LightRecord::Sphere;
            light.position = source.position;
            light.radius = std::max(0.0f, source.radius);
            light.delta = light.radius <= 0.0f;
            //Kugla polumjera R i radijancije L ima intenzitet L * pi * R^2 u svakom smjeru
            light.radiance = light.delta ? power : power / (Pi * light.radius * light.radius);
            if(source.type == Tracer::Light::Type::Spot){
                light.spot = true;
                light.axis = glm::normalize(source.direction);
                const float outer = std::clamp(source.spotAngle, 1e-3f, Pi);
                light.cosOuter = std::cos(outer);
                light.cosInner = std::cos(outer * (1.0f - std::clamp(source.spotBlend, 0.0f, 1.0f)));
            }
            push(light, luminance(power) / reference2);
        }
    }
    emitterOfTriangle.assign(world.triangles.size(), -1);
    for(size_t i = 0; i < world.triangles.size(); ++i){
        const Triangle& t = world.triangles[i];
        const Material& m = world.materials[t.material];
        const glm::vec3 emitted = m.emission * m.emissionStrength;
        if(luminance(emitted) <= 0.0f) continue;
        const glm::vec3& a = world.positions[t.v[0]];
        const float area = 0.5f * glm::length(glm::cross(world.positions[t.v[1]] - a, world.positions[t.v[2]] - a));
        if(area <= 0.0f) continue;
        LightRecord light;
        light.kind = LightRecord::Triangle;
        light.triangle = uint32_t(i);
        light.area = area;
        emitterOfTriangle[i] = int(lights.size());
        push(light, luminance(emitted) * (m.emissionTexture >= 0 ? 0.5f : 1.0f) * area / reference2);
    }
    if(sky.active()){
        LightRecord light;
        light.kind = LightRecord::Sky;
        push(light, sky.averageLuminance() * Pi);
    }
    float total = 0.0f;
    for(float w : weights) total += w;
    c.lightPick.assign(weights.size(), 0.0f);
    c.lightCumulative.assign(weights.size(), 0.0f);
    float running = 0.0f;
    for(size_t i = 0; i < weights.size(); ++i){
        c.lightPick[i] = total > 0.0f ? weights[i] / total : 1.0f / float(weights.size());
        running += c.lightPick[i];
        c.lightCumulative[i] = running;
    }
    if(!c.lightCumulative.empty()) c.lightCumulative.back() = 1.0f;
    for(size_t i = 0; i < lights.size(); ++i){
        if(lights[i].kind == LightRecord::Sun && !lights[i].delta) c.suns.push_back(uint32_t(i));
        if(lights[i].kind == LightRecord::Sphere && !lights[i].delta) c.spheres.push_back(uint32_t(i));
    }
    c.buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return compiled;
}

uint32_t CompiledScene::pickLight(float choice) const{
    //Prvo svjetlo ciji zbroj prelazi izbor. Binarno: scena sa svijetlecim modelom ima tisuce
    //trokuta-svjetala, a linearni prolaz po uzorku bio bi vec vidljiv u profilu
    const auto at = std::upper_bound(lightCumulative.begin(), lightCumulative.end(), choice);
    return uint32_t(std::min<ptrdiff_t>(at - lightCumulative.begin(), ptrdiff_t(lightCumulative.size()) - 1));
}


}
