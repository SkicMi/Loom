#include "Tracer/Compiled.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <functional>
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

void buildLightTree(CompiledScene& c);

std::shared_ptr<const CompiledScene> compile(Scene scene){
    return compile(std::move(scene), nullptr);
}

std::shared_ptr<const CompiledScene> compile(Scene scene, const CompiledScene* previous, uint32_t rebuildEvery){
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
    //Pomak: kljucevi moraju imati isto vrhova kao scena, inace ih nema (i kamera ostaje)
    for(std::vector<std::vector<glm::vec3>>* keys : {&world.motion.positions, &world.motion.normals})
        for(const std::vector<glm::vec3>& key : *keys) if(key.size() != world.positions.size()){ keys->clear(); break; }
    if(world.motion.normals.size() != world.motion.positions.size()) world.motion.normals.clear();
    const std::vector<std::vector<glm::vec3>>* keys = world.motion.geometry() ? &world.motion.positions : nullptr;
    const bool sameTopology = previous && previous->refits + 1 < rebuildEvery &&
        previous->world.triangles.size() == world.triangles.size() && previous->world.positions.size() == world.positions.size() &&
        std::equal(world.triangles.begin(), world.triangles.end(), previous->world.triangles.begin(), [](const Triangle& a, const Triangle& b){
            return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2] && a.material == b.material && a.object == b.object; });
    if(sameTopology){
        tree = previous->tree;
        tree.refit(world.positions, world.triangles, keys);
        c.refits = previous->refits + 1;
    }else tree.build(world.positions, world.triangles, keys);
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
    buildLightTree(c);
    c.buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return compiled;
}

namespace{

struct Cone{ glm::vec3 axis{0.0f, 0.0f, 1.0f}; float thetaO = 0.0f, thetaE = 0.0f; };

//Najmanji stozac koji obuhvati oba (Conty & Kulla 2018, alg. 1)
Cone unite(Cone a, Cone b){
    if(b.thetaO > a.thetaO) std::swap(a, b);
    const float thetaD = std::acos(std::clamp(glm::dot(a.axis, b.axis), -1.0f, 1.0f));
    const float thetaE = std::max(a.thetaE, b.thetaE);
    if(std::min(thetaD + b.thetaO, Pi) <= a.thetaO) return {a.axis, a.thetaO, thetaE};
    const float thetaO = 0.5f * (a.thetaO + thetaD + b.thetaO);
    if(thetaO >= Pi) return {a.axis, Pi, thetaE};
    const float thetaR = thetaO - a.thetaO;
    glm::vec3 w = glm::cross(a.axis, b.axis);
    if(glm::dot(w, w) < 1e-12f) return {a.axis, Pi, thetaE};
    w = glm::normalize(w);
    //Rodrigues: a.axis zakrenut za thetaR prema b.axis
    const glm::vec3 axis = a.axis * std::cos(thetaR) + glm::cross(w, a.axis) * std::sin(thetaR) + w * glm::dot(w, a.axis) * (1.0f - std::cos(thetaR));
    return {glm::normalize(axis), thetaO, thetaE};
}

}

void buildLightTree(CompiledScene& c){
    const Scene& world = c.world;
    c.lightTree.clear();
    c.lightTreeParent.clear();
    c.lightTreeLeaf.assign(c.lights.size(), ~0u);
    c.infiniteLights.clear();
    c.infiniteCumulative.clear();
    c.localPick = 0.0f;
    struct Item{ LightTreeNode node; glm::vec3 centre; };
    std::vector<Item> items;
    float infinite = 0.0f;
    for(size_t i = 0; i < c.lights.size(); ++i){
        const LightRecord& light = c.lights[i];
        if(light.kind == LightRecord::Sun || light.kind == LightRecord::Sky){
            c.infiniteLights.push_back(uint32_t(i));
            infinite += c.lightPick[i];
            c.infiniteCumulative.push_back(infinite);
            continue;
        }
        c.localPick += c.lightPick[i];
        LightTreeNode node;
        node.leaf = true;
        node.left = uint32_t(i);
        if(light.kind == LightRecord::Sphere){
            node.min = light.position - glm::vec3(light.radius);
            node.max = light.position + glm::vec3(light.radius);
            //Kugla: intenzitet = radijancija * pi R^2 (tocka: sam intenzitet)
            node.power = luminance(light.radiance) * (light.delta ? 1.0f : Pi * light.radius * light.radius);
            if(light.spot){ node.axis = light.axis; node.thetaO = 0.0f; node.thetaE = std::acos(std::clamp(light.cosOuter, -1.0f, 1.0f)); }
            else{ node.thetaO = Pi; node.thetaE = 0.5f * Pi; }
        }else{
            const Triangle& t = world.triangles[light.triangle];
            const glm::vec3& a = world.positions[t.v[0]];
            const glm::vec3& b = world.positions[t.v[1]];
            const glm::vec3& d = world.positions[t.v[2]];
            node.min = glm::min(a, glm::min(b, d));
            node.max = glm::max(a, glm::max(b, d));
            const Material& m = world.materials[t.material];
            //Intenzitet plohe prema normali: radijancija * povrsina (kosinus je u stoscu)
            node.power = luminance(m.emission * m.emissionStrength) * light.area;
            node.axis = glm::normalize(glm::cross(b - a, d - a));
            node.thetaO = m.emissionTwoSided ? Pi : 0.0f;
            node.thetaE = 0.5f * Pi;
        }
        if(!(node.power > 0.0f)) node.power = 1e-12f;
        items.push_back({node, 0.5f * (node.min + node.max)});
    }
    for(float& v : c.infiniteCumulative) v = infinite > 0.0f ? v / infinite : 1.0f;
    if(!c.infiniteCumulative.empty()) c.infiniteCumulative.back() = 1.0f;
    if(items.empty()){ c.localPick = 0.0f; return; }
    if(c.infiniteLights.empty()) c.localPick = 1.0f;
    c.lightTree.reserve(items.size() * 2);
    c.lightTreeParent.reserve(items.size() * 2);
    //Rekurzivno: po najduljoj osi sredista, na medijanu; list je jedno svjetlo
    std::function<uint32_t(size_t, size_t, uint32_t)> build = [&](size_t first, size_t last, uint32_t parent){
        const uint32_t index = uint32_t(c.lightTree.size());
        c.lightTree.emplace_back();
        c.lightTreeParent.push_back(parent);
        if(last - first == 1){
            c.lightTree[index] = items[first].node;
            c.lightTreeLeaf[items[first].node.left] = index;
            return index;
        }
        glm::vec3 lo(std::numeric_limits<float>::infinity()), hi(-std::numeric_limits<float>::infinity());
        for(size_t i = first; i < last; ++i){ lo = glm::min(lo, items[i].centre); hi = glm::max(hi, items[i].centre); }
        const glm::vec3 extent = hi - lo;
        const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
        const size_t middle = (first + last) / 2;
        std::nth_element(items.begin() + ptrdiff_t(first), items.begin() + ptrdiff_t(middle), items.begin() + ptrdiff_t(last),
                         [axis](const Item& a, const Item& b){ return a.centre[axis] < b.centre[axis]; });
        const uint32_t left = build(first, middle, index);
        const uint32_t right = build(middle, last, index);
        LightTreeNode node;
        const LightTreeNode& l = c.lightTree[left];
        const LightTreeNode& r = c.lightTree[right];
        node.min = glm::min(l.min, r.min);
        node.max = glm::max(l.max, r.max);
        node.power = l.power + r.power;
        const Cone cone = unite({l.axis, l.thetaO, l.thetaE}, {r.axis, r.thetaO, r.thetaE});
        node.axis = cone.axis; node.thetaO = cone.thetaO; node.thetaE = cone.thetaE;
        node.left = left; node.right = right;
        c.lightTree[index] = node;
        return index;
    };
    build(0, items.size(), ~0u);
}

float CompiledScene::lightTreeImportance(uint32_t index, const glm::vec3& p, const glm::vec3& n) const{
    const LightTreeNode& node = lightTree[index];
    const glm::vec3 centre = 0.5f * (node.min + node.max);
    const float r = 0.5f * glm::length(node.max - node.min);
    const glm::vec3 v = centre - p;
    const float d2 = glm::dot(v, v);
    const float dist = std::sqrt(d2);
    const float thetaU = dist > r ? std::asin(std::min(1.0f, r / dist)) : Pi;
    const glm::vec3 toP = dist > 0.0f ? -v / dist : glm::vec3(0.0f, 0.0f, 1.0f);
    //Emisija: kut izmedju osi i smjera prema p, umanjen za raspon osi i kutiju
    const float thetaI = std::acos(std::clamp(glm::dot(node.axis, toP), -1.0f, 1.0f));
    const float thetaP = std::max(0.0f, thetaI - node.thetaO - thetaU);
    if(thetaP >= node.thetaE) return 0.0f;
    float importance = node.power * std::cos(thetaP) / std::max(d2, std::max(r * r, 1e-8f));
    //Primatelj: kutija mora biti bar djelomicno iznad plohe
    if(glm::dot(n, n) > 0.0f){
        const float thetaN = std::acos(std::clamp(glm::dot(n, -toP), -1.0f, 1.0f));
        const float thetaNP = std::max(0.0f, thetaN - thetaU);
        if(thetaNP >= 0.5f * Pi) return 0.0f;
        importance *= std::cos(thetaNP);
    }
    return std::max(importance, 0.0f);
}

bool CompiledScene::chooseLight(float choice, const glm::vec3& p, const glm::vec3& n, uint32_t& light, float& probability, bool tree) const{
    if(!tree){
        if(lights.empty()) return false;
        light = pickLight(choice);
        probability = lightPick[light];
        return probability > 0.0f;
    }
    if(choice < localPick && !lightTree.empty()){
        float u = std::min(choice / localPick, 0.99999994f);
        float prob = localPick;
        uint32_t node = 0;
        while(!lightTree[node].leaf){
            const float a = lightTreeImportance(lightTree[node].left, p, n), b = lightTreeImportance(lightTree[node].right, p, n);
            const float total = a + b;
            if(!(total > 0.0f)) return false;
            const float pl = a / total;
            if(u < pl){ u = std::min(u / pl, 0.99999994f); prob *= pl; node = lightTree[node].left; }
            else{ u = std::min((u - pl) / (1.0f - pl), 0.99999994f); prob *= 1.0f - pl; node = lightTree[node].right; }
        }
        light = lightTree[node].left;
        probability = prob;
        return prob > 0.0f;
    }
    if(infiniteLights.empty()) return false;
    const float u = localPick < 1.0f ? (choice - localPick) / (1.0f - localPick) : 0.0f;
    const auto at = std::upper_bound(infiniteCumulative.begin(), infiniteCumulative.end(), u);
    light = infiniteLights[size_t(std::min<ptrdiff_t>(at - infiniteCumulative.begin(), ptrdiff_t(infiniteLights.size()) - 1))];
    probability = lightPick[light];
    return probability > 0.0f;
}

float CompiledScene::choiceProbability(uint32_t light, const glm::vec3& p, const glm::vec3& n, bool tree) const{
    const uint32_t leaf = light < lightTreeLeaf.size() ? lightTreeLeaf[light] : ~0u;
    if(!tree) return lightPick[light];
    if(leaf == ~0u) return lightPick[light];
    float prob = localPick;
    for(uint32_t node = leaf; lightTreeParent[node] != ~0u; node = lightTreeParent[node]){
        const LightTreeNode& parent = lightTree[lightTreeParent[node]];
        const uint32_t sibling = parent.left == node ? parent.right : parent.left;
        const float mine = lightTreeImportance(node, p, n), other = lightTreeImportance(sibling, p, n);
        const float total = mine + other;
        if(!(total > 0.0f)) return 0.0f;
        prob *= mine / total;
    }
    return prob;
}

uint32_t CompiledScene::pickLight(float choice) const{
    //Prvo svjetlo ciji zbroj prelazi izbor. Binarno: scena sa svijetlecim modelom ima tisuce
    //trokuta-svjetala, a linearni prolaz po uzorku bio bi vec vidljiv u profilu
    const auto at = std::upper_bound(lightCumulative.begin(), lightCumulative.end(), choice);
    return uint32_t(std::min<ptrdiff_t>(at - lightCumulative.begin(), ptrdiff_t(lightCumulative.size()) - 1));
}


}
