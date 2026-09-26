#include "Tracer/Renderer.h"
#include "Tracer/Bsdf.h"
#include "Tracer/Denoise.h"
#include "Tracer/Sampler.h"
#include "Tracer/Volume.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace Tracer{

namespace{

constexpr float Pi = glm::pi<float>();
constexpr float Infinity = std::numeric_limits<float>::infinity();



float luminance(const glm::vec3& c){ return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

//Ortonormirana baza oko n (Duff i sur. 2017) - bez grananja i bez rupe na polu
void basis(const glm::vec3& n, glm::vec3& t, glm::vec3& b){
    const float sign = std::copysign(1.0f, n.z);
    const float a = -1.0f / (sign + n.z);
    const float c = n.x * n.y * a;
    t = glm::vec3(1.0f + sign * n.x * n.x * a, sign * c, -sign * n.x);
    b = glm::vec3(c, sign + n.y * n.y * a, -n.y);
}

float powerHeuristic(float a, float b){
    const float a2 = a * a, b2 = b * b;
    return a2 + b2 > 0.0f ? a2 / (a2 + b2) : 0.0f;
}
//Tri strategije (magla: slobodni put + svjetlo, slobodni put + faza, ekviangularno + svjetlo).
//Mjerilo po najvecoj da kvadrati ne preliju
float powerHeuristic(float mine, float a, float b){
    const float top = std::max({mine, a, b});
    if(!(top > 0.0f) || !std::isfinite(top)) return std::isinf(mine) ? 1.0f : 0.0f;
    mine /= top; a /= top; b /= top;
    return mine * mine / (mine * mine + a * a + b * b);
}

//Odsjecak zrake kroz medij: o + t d, medij je u [a, b]; svjetlo se bira za [a, far] (b ili
//konacni kraj kad je b beskonacan - magla po visini do neba)
struct MediumSegment{ glm::vec3 o{0.0f}, d{0.0f}; float a = 0.0f, b = 0.0f, far = 0.0f; };

//Jednoliko u stoscu oko osi `axis` s kosinusom polukuta cosMax
glm::vec3 sampleCone(const glm::vec3& axis, float cosMax, const glm::vec2& u){
    const float cosTheta = 1.0f - u.x * (1.0f - cosMax);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * Pi * u.y;
    glm::vec3 t, b;
    basis(axis, t, b);
    return glm::normalize(t * (sinTheta * std::cos(phi)) + b * (sinTheta * std::sin(phi)) + axis * cosTheta);
}

//1 - cos, tocno i za vrlo male kuteve (sunce ima 1 - cos ~ 1e-5)
float oneMinusCosFromSin2(float sin2){
    const float cosMax = std::sqrt(std::max(0.0f, 1.0f - sin2));
    return sin2 / (1.0f + cosMax);
}

bool hitSphere(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& centre, float radius,
               float tMax, float& t){
    const glm::vec3 oc = origin - centre;
    const float b = glm::dot(oc, direction);
    const float c = glm::dot(oc, oc) - radius * radius;
    const float disc = b * b - c;
    if(disc < 0.0f) return false;
    const float s = std::sqrt(disc);
    t = -b - s;
    if(t <= 1e-7f) t = -b + s;
    return t > 1e-7f && t < tMax;
}

float spotFalloff(float cosAngle, float cosOuter, float cosInner){
    if(cosAngle <= cosOuter) return 0.0f;
    if(cosAngle >= cosInner) return 1.0f;
    const float t = (cosAngle - cosOuter) / std::max(1e-6f, cosInner - cosOuter);
    return t * t * (3.0f - 2.0f * t);
}

}

//---------------------------------------------------------------------------------------------
// Svjetlo spremno za uzorkovanje
//---------------------------------------------------------------------------------------------
struct Renderer::Accumulator{
    glm::vec3 cg{0.0f};
    float coverage = 0.0f;
    glm::vec3 background{0.0f};
    glm::vec3 catcherLit{0.0f}, catcherShadowed{0.0f};
    uint32_t catcherSamples = 0, missSamples = 0;
    glm::vec3 albedo{0.0f}, normal{0.0f};
    float depth = NoDepth, depthOffset = Infinity;
    double luminance = 0.0, luminance2 = 0.0;
    uint32_t samples = 0;
};

struct Renderer::PathResult{
    glm::vec3 radiance{0.0f};           //cg, kad je pogodjen objekt
    glm::vec3 background{0.0f};
    //Svjetlo magle rasprseno s kamerine zrake (ekviangularno) - ide u cg bez pokrivenosti: dodaje
    //se i uzorku koji prode kroz maglu do neba ili snimke (premultiplicirano, kao sjaj)
    glm::vec3 inscatter{0.0f};
    bool object = false, catcher = false, miss = false;
    glm::vec3 lit{0.0f}, shadowed{0.0f};
    glm::vec3 albedo{0.0f}, normal{0.0f};
    float depth = NoDepth;
};

//---------------------------------------------------------------------------------------------
// GRADNJA
//---------------------------------------------------------------------------------------------
Renderer::Renderer(Scene scene) : compiled(compile(std::move(scene))){}
Renderer::Renderer(std::shared_ptr<const CompiledScene> scene) : compiled(std::move(scene)){}

Renderer::~Renderer() = default;

//---------------------------------------------------------------------------------------------
// PUTANJA
//---------------------------------------------------------------------------------------------
Renderer::PathResult Renderer::trace(glm::vec2 pixel, uint32_t sampleIndex, uint32_t pixelSeed, float clampValue,
                                     uint32_t maxBounces, uint64_t& rays) const{
    const CompiledScene& C = *compiled;
    const Scene& world = C.world;
    const Bvh& tree = C.tree;
    const EnvironmentSampler& sky = C.sky;
    const std::vector<LightRecord>& lights = C.lights;
    const std::vector<float>& lightCdf = C.lightPick;
    const std::vector<int>& emitterOfTriangle = C.emitterOfTriangle;
    const std::vector<uint8_t>& triangleFlags = C.triangleFlags;
    const float sceneRadius = C.sceneRadius;
    const glm::mat4& cameraInverse = C.cameraInverse;
    PathResult result;
    Sampler sampler(pixelSeed, sampleIndex);
    //Trenutak uzorka u otvoru zatvaraca (Scene::motion); bez pomaka sredina
    float time = 0.5f;
    const Motion& motion = world.motion;
    //Vrh i normala u trenutku uzorka
    auto vertexAt = [&](uint32_t index){
        if(!motion.geometry()) return world.positions[index];
        size_t k;
        float f;
        motionSegment(motion.positions.size(), time, k, f);
        return glm::mix(motion.positions[k][index], motion.positions[k + 1][index], f);
    };
    auto normalAt = [&](uint32_t index){
        if(motion.normals.size() < 2) return world.normals[index];
        size_t k;
        float f;
        motionSegment(motion.normals.size(), time, k, f);
        return glm::mix(motion.normals[k][index], motion.normals[k + 1][index], f);
    };
    const Camera& camera = world.camera;
    const float epsilonScale = std::max(1e-6f * sceneRadius, 1e-7f);

    auto offset = [&](const glm::vec3& p, const glm::vec3& n, bool outward){
        const float m = std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)});
        const float eps = std::max(epsilonScale, 4e-6f * m);
        return p + n * (outward ? eps : -eps);
    };
    auto uvAt = [&](const Triangle& t, float u, float v){
        return world.uvs[t.v[0]] * (1.0f - u - v) + world.uvs[t.v[1]] * u + world.uvs[t.v[2]] * v;
    };
    //Alfa: maska po pragu, prozirnost stohasticki - zraka prode s vjerojatnoscu 1 - alfa
    auto alphaPasses = [&](uint32_t index, float u, float v, uint32_t salt){
        const Triangle& t = world.triangles[index];
        const Material& m = world.materials[t.material];
        float alpha = m.opacity;
        if(m.baseColorTexture >= 0) alpha *= world.textures[size_t(m.baseColorTexture)].sample(uvAt(t, u, v)).a;
        if(m.alphaMode == Material::Alpha::Mask) return alpha >= m.alphaCutoff;
        return sampling::toUnit(sampling::hash(pixelSeed ^ sampling::hash(index, salt + sampleIndex * 977u))) < alpha;
    };
    //Propusnost zrake sjene: 0 kad je zaklonjena, inace umnozak propusnosti stakala na putu.
    //crossed: prosla je kroz barem jedno staklo (tada izravno svjetlo nema par u BSDF strategiji)
    auto shadowTransmittance = [&](const Ray& ray, bool ignoreCatchers, uint32_t salt, bool& crossed){
        Ray shadowRay = ray;
        shadowRay.time = time;
        crossed = false;
        glm::vec3 through(1.0f);
        const bool blocked = tree.occluded(shadowRay, [&](uint32_t index, float u, float v){
            const uint8_t f = triangleFlags[index];
            if(f & NoShadow) return false;
            if(ignoreCatchers && (f & Catcher)) return false;
            if((f & AlphaTested) && !alphaPasses(index, u, v, salt)) return false;
            if(glass && (f & Transmissive)){
                const Triangle& t = world.triangles[index];
                const Material& m = world.materials[t.material];
                const glm::vec3& a = world.positions[t.v[0]];
                const glm::vec3 n = glm::normalize(glm::cross(world.positions[t.v[1]] - a, world.positions[t.v[2]] - a));
                const float F = fresnelDielectric(std::abs(glm::dot(n, shadowRay.direction)), std::max(1.0001f, m.ior));
                through *= m.baseColor * (m.transmission * (1.0f - m.metallic) * (1.0f - F));
                crossed = true;
                return false;
            }
            return true;
        });
        if(blocked) return glm::vec3(0.0f);
        if(!world.volumes.empty()){
            VolumeRng rng{sampling::hash(pixelSeed ^ sampling::hash(salt, sampleIndex * 977u + 0x5bd1e995u))};
            through *= volumeTransmittance(world.volumes, C.volumeInverse, shadowRay.origin, shadowRay.direction, shadowRay.tMax, rng);
        }
        return through;
    };

    //Uzorak svjetla iz tocke p: smjer, udaljenost, radijancija (ili ozracenost za delta), gustoca
    //po prostornom kutu UKLJUCUJUCI vjerojatnost izbora svjetla
    //light/pick: koje je svjetlo i s kojom vjerojatnoscu izabrano (pdf / pick = gustoca samog svjetla)
    struct LightSample{ glm::vec3 wi{0.0f}; float distance = Infinity; glm::vec3 value{0.0f}; float pdf = 0.0f; bool delta = false;
                        uint32_t light = 0; float pick = 1.0f; };
    //Tocka na trokutu-svjetlu za uzorak u (jednoliko po povrsini)
    auto trianglePoint = [&](const LightRecord& light, const glm::vec2& u, float& b1, float& b2){
        const Triangle& t = world.triangles[light.triangle];
        const float su = std::sqrt(u.x);
        b1 = 1.0f - su; b2 = u.y * su;
        return world.positions[t.v[0]] * (1.0f - b1 - b2) + world.positions[t.v[1]] * b1 + world.positions[t.v[2]] * b2;
    };
    //Uzorak zadanog svjetla (vec izabranog s vjerojatnoscu pick)
    auto sampleLightIndex = [&](uint32_t index, float pick, const glm::vec3& p, const glm::vec2& u, LightSample& out){
        const LightRecord& light = lights[index];
        out.light = index;
        out.pick = pick;
        switch(light.kind){
        case LightRecord::Sky:{
            float pdf = 0.0f;
            out.value = sky.sample(u, out.wi, pdf);
            if(!(pdf > 0.0f)) return false;
            out.pdf = pdf * pick;
            out.distance = Infinity;
            return true;
        }
        case LightRecord::Sun:{
            if(light.delta){
                out.wi = light.axis; out.value = light.radiance; out.pdf = pick; out.delta = true;
                return true;
            }
            out.wi = sampleCone(light.axis, light.cosMax, u);
            out.value = light.radiance;
            out.pdf = pick / (2.0f * Pi * light.oneMinusCos);
            return true;
        }
        case LightRecord::Sphere:{
            const glm::vec3 toCentre = light.position - p;
            const float d2 = glm::dot(toCentre, toCentre);
            if(d2 <= 0.0f) return false;
            const float d = std::sqrt(d2);
            const glm::vec3 axis = toCentre / d;
            const float falloff = light.spot ? spotFalloff(glm::dot(-axis, light.axis), light.cosOuter, light.cosInner) : 1.0f;
            if(falloff <= 0.0f) return false;
            if(light.delta){
                out.wi = axis; out.distance = d; out.value = light.radiance * falloff / d2; out.pdf = pick; out.delta = true;
                return true;
            }
            if(d <= light.radius * 1.0001f) return false;       //unutar svjetla
            const float sin2 = light.radius * light.radius / d2;
            const float oneMinusCos = oneMinusCosFromSin2(sin2);
            out.wi = sampleCone(axis, 1.0f - oneMinusCos, u);
            float t;
            out.distance = hitSphere(p, out.wi, light.position, light.radius, Infinity, t) ? t : d - light.radius;
            out.value = light.radiance * falloff;
            out.pdf = pick / (2.0f * Pi * oneMinusCos);
            return true;
        }
        case LightRecord::Triangle:{
            const Triangle& t = world.triangles[light.triangle];
            float b1, b2;
            const glm::vec3 q = trianglePoint(light, u, b1, b2);
            const glm::vec3& a = world.positions[t.v[0]];
            const glm::vec3& b = world.positions[t.v[1]];
            const glm::vec3& c = world.positions[t.v[2]];
            const glm::vec3 toLight = q - p;
            const float dist2 = glm::dot(toLight, toLight);
            if(dist2 <= 0.0f) return false;
            const float dist = std::sqrt(dist2);
            out.wi = toLight / dist;
            const glm::vec3 nl = glm::normalize(glm::cross(b - a, c - a));
            const Material& m = world.materials[t.material];
            //Jednostrano svjetlo (pravokutnik) svijetli samo prema svojoj prednjoj strani
            const float cosLight = m.emissionTwoSided ? std::abs(glm::dot(nl, out.wi)) : -glm::dot(nl, out.wi);
            if(cosLight < 1e-6f) return false;
            glm::vec3 emitted = m.emission * m.emissionStrength;
            if(m.emissionTexture >= 0)
                emitted *= glm::vec3(world.textures[size_t(m.emissionTexture)].sample(uvAt(t, b1, b2)));
            out.value = emitted;
            out.distance = dist;
            out.pdf = pick * dist2 / (cosLight * light.area);
            return true;
        }
        }
        return false;
    };
    //n: normala primatelja za stablo svjetala (nula: bez tog uvjeta - staklo, magla)
    auto sampleLight = [&](const glm::vec3& p, const glm::vec3& n, float choice, const glm::vec2& u, LightSample& out){
        if(lights.empty()) return false;
        uint32_t index;
        float pick;
        if(!C.chooseLight(choice, p, n, index, pick, useLightTree)) return false;
        return sampleLightIndex(index, pick, p, u, out);
    };
    //Gustoca kojom bi sampleLight izabrao smjer koji je BSDF vec izabrao - za MIS
    auto skyPdf = [&](const glm::vec3& d){
        if(!sky.active() || lights.empty() || lights.back().kind != LightRecord::Sky) return 0.0f;
        return sky.pdf(d) * lightCdf.back();
    };
    //Gustoca kojom bi ekviangularna strategija na odsjecku s dala tocku t prema svjetlu index
    //(sredisnja tocka centre) - bez gustoce samog svjetla, koja je ista kao u izravnom svjetlu
    auto equiangularDensity = [&](const MediumSegment& segment, uint32_t index, const glm::vec3& centre, float t){
        const LightRecord& light = lights[index];
        if(light.kind != LightRecord::Sphere && light.kind != LightRecord::Triangle) return 0.0f;
        return equiangularPdf(equiangular(segment.o, segment.d, centre, segment.a, segment.b), t) *
               C.choiceProbabilityOnSegment(index, segment.o, segment.d, segment.a, segment.far, useLightTree);
    };
    auto clampContribution = [&](glm::vec3 c, bool indirect){
        if(!indirect || clampValue <= 0.0f) return c;
        const float l = luminance(c);
        return l > clampValue ? c * (clampValue / l) : c;
    };
    //Sto zraka koja je pobjegla vidi: nebo, diskove sunaca i (za kameru kroz staklo) snimku
    auto escaped = [&](const glm::vec3& origin, const glm::vec3& d, float bsdfPdf, bool fromCamera, bool mirrorChain,
                       bool useMis){
        glm::vec3 total(0.0f);
        //Snimka iza scene: samo kroz zrcalne odraze i lomove od kamere
        if(!fromCamera && mirrorChain && world.backplate.valid()){
            const glm::vec3 local = glm::vec3(cameraInverse * glm::vec4(d, 0.0f));
            if(local.z < 0.0f){
                const glm::vec2 px = camera.pixelOf(local);
                const glm::vec2 uv = glm::clamp(px / glm::vec2(float(camera.width), float(camera.height)),
                                                glm::vec2(0.0f), glm::vec2(1.0f));
                return glm::vec3(world.backplate.sample(uv));
            }
        }
        if(fromCamera && !world.environment.cameraVisible) return total;
        if(sky.active()){
            const float w = useMis ? powerHeuristic(bsdfPdf, skyPdf(d)) : 1.0f;
            total += sky.radiance(d) * w;
        }
        for(uint32_t sunIndex : C.suns){
            const LightRecord& light = lights[sunIndex];
            if(glm::dot(d, light.axis) < light.cosMax) continue;
            const float lightPdf = lightCdf[size_t(light.index)] / (2.0f * Pi * light.oneMinusCos);
            total += light.radiance * (useMis ? powerHeuristic(bsdfPdf, lightPdf) : 1.0f);
        }
        (void)origin;
        return total;
    };
    //Kugle svjetla na putu zrake (nisu u BVH-u): najbliza prije tMax
    auto sphereOnRay = [&](const glm::vec3& o, const glm::vec3& d, float tMax, float& tHit, const LightRecord*& which){
        which = nullptr;
        tHit = tMax;
        for(uint32_t sphereIndex : C.spheres){
            const LightRecord& light = lights[sphereIndex];
            float t;
            if(hitSphere(o, d, light.position, light.radius, tHit, t)){ tHit = t; which = &light; }
        }
        return which != nullptr;
    };

    //-- zraka iz kamere -------------------------------------------------------------------------
    const glm::vec2 filterSample = sampler.next2D();
    const glm::vec2 lensSample = sampler.next2D();
    auto tent = [](float u){
        const float r = 0.75f;          //filtar sirine 1.5 piksela
        return u < 0.5f ? -r + r * std::sqrt(2.0f * u) : r - r * std::sqrt(2.0f - 2.0f * u);
    };
    const glm::vec2 jitter(tent(filterSample.x), tent(filterSample.y));
    //Stvarna ploha ispred ovog uzorka (holdout), duz -Z; 0 = nema je
    float realDepth = 0.0f;
    if(world.holdout.valid()){
        const glm::vec2 at = (pixel + jitter) * glm::vec2(float(world.holdout.width) / float(camera.width),
                                                           float(world.holdout.height) / float(camera.height));
        realDepth = world.holdout.fetch(int(std::floor(at.x)), int(std::floor(at.y))).r;
        if(!(realDepth > 0.0f) || realDepth > NoDepth * 0.5f) realDepth = 0.0f;
    }
    Ray ray;
    if(motion.active()) time = sampler.next2D().x;
    ray.time = time;
    if(motion.cameras.size() >= 2){
        Camera moving = camera;
        moving.cameraToWorld = cameraAt(motion.cameras, time);
        moving.ray(pixel + jitter, lensSample, ray.origin, ray.direction);
    }else camera.ray(pixel + jitter, lensSample, ray.origin, ray.direction);
    //STOZAC ZRAKE (ray cones, Akenine-Moller 2019) za razinu mipmape: sirina raste s udaljenoscu,
    //kut sirenja je kut piksela; hrapavo odbijanje ga jako rasiri
    float coneWidth = 0.0f, coneSpread = 1.0f / std::max(1.0f, camera.focalPixels);
    const glm::mat4& worldToCamera = cameraInverse;

    glm::vec3 beta(1.0f);
    glm::vec3 radiance(0.0f);
    float previousPdf = 0.0f;
    glm::vec3 previousNormal(0.0f);     //normala primatelja s kojom je prosla tocka birala svjetlo
    bool mirrorChain = true;
    glm::vec3 previousPoint = ray.origin;
    //Staklene sjene: poslije hrapave plohe putanja koja prode kroz staklo je kaustika - svjetlo
    //koje pogodi ne broji se (vec ga je donijela zraka sjene s te plohe)
    bool sawRough = false, caustic = false;
    //EKVIANGULARNO U MAGLI. Na svakom odsjecku kroz medij (dok ima odbijanja) jedno svjetlo se
    //bira stablom za cijeli odsjecak (CompiledScene::chooseLightOnSegment), a tocka rasprsenja ekviangularno prema njemu (kod trokuta
    //prema tocki na trokutu). S izravnim svjetlom u tocki slobodnog puta i fazom koja pogodi
    //svjetlo to su tri procjene istog integrala; tezine su power heuristika nad gustocama koje
    //se sve mogu izracunati u svakoj tocki, pa je zbroj nepristran. Za pogodak fazom treba
    //odsjecak s kojeg je put rasprsio - zato se pamti
    const bool equiangularOn = equiangularSampling && C.localPick > 0.0f && !lights.empty();
    bool previousMedium = false;
    MediumSegment previousSegment;
    float previousT = 0.0f, previousEvent = 0.0f;

    for(uint32_t depth = 0;; ++depth){
        Hit hit;
        ray.tMin = 0.0f;
        ray.tMax = Infinity;
        const uint32_t salt = depth * 7919u + 13u;
        ++rays;
        if(depth == 0){
            tree.intersect(ray, hit, [&](uint32_t index, float u, float v){
                const uint8_t f = triangleFlags[index];
                if(f & NoCamera) return false;
                if((f & AlphaTested) && !alphaPasses(index, u, v, salt)) return false;
                return true;
            });
        }else{
            tree.intersect(ray, hit, [&](uint32_t index, float u, float v){
                const uint8_t f = triangleFlags[index];
                if((f & AlphaTested) && !alphaPasses(index, u, v, salt)) return false;
                return true;
            });
        }

        //Kugla svjetla ispred plohe
        float sphereT;
        const LightRecord* sphere = nullptr;
        const bool sphereHit = sphereOnRay(ray.origin, ray.direction, hit.valid() ? hit.t : Infinity, sphereT, sphere);

        //-- magla: dogadjaj rasprsenja prije plohe ili svjetla -----------------------------------
        if(!world.volumes.empty()){
            const glm::vec2 free = sampler.next2D();
            float t;
            uint32_t which;
            float end = sphereHit ? sphereT : (hit.valid() ? hit.t : Infinity);
            //Holdout: iza stvarne plohe iz splata nema magle koju bi kamera vidjela (tamo je snimka)
            if(depth == 0 && realDepth > 0.0f){
                const float forward = -glm::dot(ray.direction, glm::vec3(camera.cameraToWorld[2]));
                if(forward > 1e-6f) end = std::min(end, realDepth * (1.0f + world.holdoutBias) / forward);
            }
            const MediaOnRay media = mediaOnRay(world.volumes, C.volumeInverse, ray.origin, ray.direction, end);
            MediumSegment segment;
            const bool inMedium = media.extent(end, segment.a, segment.b);
            if(inMedium){
                segment.o = ray.origin;
                segment.d = ray.direction;
                segment.far = std::isinf(segment.b) ? segment.a + 2.0f * C.sceneRadius : segment.b;
            }
            //Ekviangularno: jednostruko rasprsenje s ovog odsjecka, neovisno o tome gdje put rasprsi
            if(inMedium && equiangularOn && depth < maxBounces){
                const glm::vec2 choice = sampler.next2D(), u = sampler.next2D(), v = sampler.next2D();
                uint32_t index;
                float pick;
                if(C.chooseLightOnSegment(choice.x, segment.o, segment.d, segment.a, segment.far, index, pick, useLightTree) &&
                   (lights[index].kind == LightRecord::Sphere || lights[index].kind == LightRecord::Triangle)){
                    const LightRecord& light = lights[index];
                    float b1, b2;
                    const glm::vec3 centre = light.kind == LightRecord::Triangle ? trianglePoint(light, u, b1, b2) : light.position;
                    const Equiangular e = equiangular(segment.o, segment.d, centre, segment.a, segment.b);
                    const float te = equiangularSample(e, v.x);
                    const float pe = equiangularPdf(e, te);
                    const float event = std::isfinite(te) && te >= segment.a && te <= segment.b ? media.eventPdf(world.volumes, te) : 0.0f;
                    const glm::vec3 p = ray.origin + ray.direction * te;
                    //Stvarno sigma(t) T(t): kod nehomogene magle gustoca u tocki i ratio tracking do nje
                    float real = event;
                    uint32_t scatterer = 0;
                    if(event > 0.0f){
                        if(media.varies){
                            VolumeRng rng{sampling::hash(pixelSeed ^ sampling::hash(salt + 523u, sampleIndex * 977u))};
                            real = media.realTotal(world.volumes, C.volumeInverse, te, v.y, scatterer);
                            if(real > 0.0f) real *= media.transmittance(world.volumes, C.volumeInverse, te, rng);
                        }else scatterer = media.pick(world.volumes, te, v.y);
                    }
                    LightSample ls;
                    if(pe > 0.0f && real > 0.0f && sampleLightIndex(index, pick, p, u, ls)){
                        const Volume& medium = world.volumes[scatterer];
                        const float phase = phaseOf(medium, glm::dot(ray.direction, ls.wi));
                        if(phase > 0.0f && luminance(medium.albedo * ls.value) > 0.0f){
                            Ray shadowRay{p, ls.wi, 0.0f, std::isinf(ls.distance) ? Infinity : ls.distance * (1.0f - 1e-4f)};
                            ++rays;
                            bool crossed;
                            const glm::vec3 through = shadowTransmittance(shadowRay, false, salt + 419u, crossed);
                            if(luminance(through) > 0.0f){
                                const float own = ls.pdf / pick;
                                const float w = powerHeuristic(pe * ls.pdf,
                                                               event * C.choiceProbability(index, p, glm::vec3(0.0f), useLightTree) * own,
                                                               ls.delta || crossed ? 0.0f : event * phase);
                                const glm::vec3 c = clampContribution(beta * medium.albedo * ls.value * through * (phase * real * w / (pe * ls.pdf)),
                                                                      depth > 0);
                                //S kamerine zrake: sjaj magle i kad put ne rasprsi (ne pokriva piksel)
                                if(depth == 0) result.inscatter += c; else radiance += c;
                            }
                        }
                    }
                }
            }
            if(inMedium && sampleVolume(world.volumes, C.volumeInverse, media, end, free,
                                        sampling::hash(pixelSeed ^ sampling::hash(salt + 211u, sampleIndex * 977u)), t, which)){
                const Volume& medium = world.volumes[which];
                const float event = media.eventPdf(world.volumes, t);
                const glm::vec3 p = ray.origin + ray.direction * t;
                if(depth == 0) result.object = true;
                beta *= medium.albedo;
                if(depth >= maxBounces || !(luminance(beta) > 0.0f)) break;
                //Izravno svjetlo kroz fazu (zrake sunca u magli, pruge sjena); RIS kao na plohi
                {
                    LightSample chosen;
                    float chosenPhase = 0.0f, chosenTarget = 0.0f, weightSum = 0.0f;
                    for(uint32_t k = 0; k < candidates; ++k){
                        LightSample ls;
                        const glm::vec2 choice = sampler.next2D();
                        const glm::vec2 u = sampler.next2D();
                        if(!sampleLight(p, glm::vec3(0.0f), choice.x, u, ls)) continue;
                        const float phase = phaseOf(medium, glm::dot(ray.direction, ls.wi));
                        const float target = luminance(ls.value) * phase * (ls.delta ? 1.0f : powerHeuristic(ls.pdf, phase));
                        if(!(target > 0.0f)) continue;
                        weightSum += target / ls.pdf;
                        if(choice.y * weightSum < target / ls.pdf){ chosen = ls; chosenPhase = phase; chosenTarget = target; }
                    }
                    if(chosenTarget > 0.0f){
                        Ray shadowRay{p, chosen.wi, 0.0f, std::isinf(chosen.distance) ? Infinity : chosen.distance * (1.0f - 1e-4f)};
                        ++rays;
                        bool crossed;
                        const glm::vec3 through = shadowTransmittance(shadowRay, false, salt + 307u, crossed);
                        if(luminance(through) > 0.0f){
                            float equi = 0.0f;
                            if(equiangularOn){
                                const LightRecord& light = lights[chosen.light];
                                const glm::vec3 centre = light.kind == LightRecord::Triangle ? p + chosen.wi * chosen.distance : light.position;
                                equi = equiangularDensity(segment, chosen.light, centre, t) * (chosen.pdf / chosen.pick);
                            }
                            const float w = powerHeuristic(event * chosen.pdf, chosen.delta || crossed ? 0.0f : event * chosenPhase, equi);
                            radiance += clampContribution(beta * chosen.value * through * (chosenPhase * w * weightSum / (float(candidates) * chosenTarget)),
                                                          depth > 0);
                        }
                    }
                }
                //Novi smjer po fazi: tezina faza / pdf = 1
                float pdf;
                const glm::vec3 next = samplePhase(medium, ray.direction, sampler.next2D(), pdf);
                const glm::vec2 roulette = sampler.next2D();
                mirrorChain = false;
                previousPdf = pdf;
                previousPoint = p;
                previousMedium = true;
                previousSegment = segment;
                previousT = t;
                previousEvent = event;
                previousNormal = glm::vec3(0.0f);
                coneSpread += 0.2f;
                if(glass){ sawRough = true; caustic = false; }
                if(depth >= 3){
                    const float keep = std::clamp(std::max({beta.r, beta.g, beta.b}), 0.05f, 0.95f);
                    if(roulette.x >= keep) break;
                    beta /= keep;
                }
                ray.origin = p;
                ray.direction = next;
                continue;
            }
        }

        if(sphereHit){
            const glm::vec3 p = ray.origin + ray.direction * sphereT;
            float falloff = 1.0f;
            if(sphere->spot) falloff = spotFalloff(glm::dot(glm::normalize(p - sphere->position), sphere->axis),
                                                   sphere->cosOuter, sphere->cosInner);
            glm::vec3 le = sphere->radiance * falloff;
            if(depth > 0){
                const float d2 = glm::dot(sphere->position - previousPoint, sphere->position - previousPoint);
                const float sin2 = std::min(1.0f, sphere->radius * sphere->radius / std::max(d2, 1e-20f));
                const float lightPdf = C.choiceProbability(uint32_t(sphere->index), previousPoint, previousNormal, useLightTree) /
                                       (2.0f * Pi * oneMinusCosFromSin2(sin2));
                if(previousMedium && equiangularOn){
                    const float equi = equiangularDensity(previousSegment, uint32_t(sphere->index), sphere->position, previousT) /
                                       (2.0f * Pi * oneMinusCosFromSin2(sin2));
                    le *= powerHeuristic(previousEvent * previousPdf, previousEvent * lightPdf, equi);
                }else le *= powerHeuristic(previousPdf, lightPdf);
            }
            if(!caustic) radiance += clampContribution(beta * le, depth > 1);
            if(depth == 0){ result.object = true; result.depth = -(worldToCamera * glm::vec4(p, 1.0f)).z; }
            break;
        }

        if(!hit.valid()){
            if(depth == 0){
                result.miss = true;
                result.background = escaped(ray.origin, ray.direction, 0.0f, true, false, false);
            }else if(!caustic){
                radiance += clampContribution(beta * escaped(ray.origin, ray.direction, previousPdf, false, mirrorChain, true),
                                              depth > 1);
            }
            break;
        }

        //-- ploha -----------------------------------------------------------------------------
        const Triangle& tri = world.triangles[hit.triangle];
        const Material& material = world.materials[tri.material];
        const float b0 = 1.0f - hit.u - hit.v;
        const glm::vec3 p0 = vertexAt(tri.v[0]);
        const glm::vec3 p1 = vertexAt(tri.v[1]);
        const glm::vec3 p2 = vertexAt(tri.v[2]);
        const glm::vec3 p = p0 * b0 + p1 * hit.u + p2 * hit.v;
        if(depth == 0 && realDepth > 0.0f && -(cameraInverse * glm::vec4(p, 1.0f)).z > realDepth * (1.0f + world.holdoutBias)){
            result.miss = true;
            result.background = escaped(ray.origin, ray.direction, 0.0f, true, false, false);
            break;
        }
        glm::vec3 ng = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        glm::vec3 ns = normalAt(tri.v[0]) * b0 + normalAt(tri.v[1]) * hit.u + normalAt(tri.v[2]) * hit.v;
        ns = glm::dot(ns, ns) > 1e-20f ? glm::normalize(ns) : ng;
        //Geometrijska normala slijedi normale vrhova: one kazu gdje je "van" (smjer namotaja
        //trokuta u modelima iz raznih alata nije pouzdan)
        if(glm::dot(ng, ns) < 0.0f) ng = -ng;
        const glm::vec2 uv = uvAt(tri, hit.u, hit.v);
        //Razina mipmape: sirina stosca na plohi (koso = sire), puta gustoca teksela na trokutu
        coneWidth += coneSpread * hit.t;
        float lodBase = -Infinity;
        {
            const glm::vec2 du = world.uvs[tri.v[1]] - world.uvs[tri.v[0]], dv = world.uvs[tri.v[2]] - world.uvs[tri.v[0]];
            const float uvArea = std::abs(du.x * dv.y - du.y * dv.x);
            const float worldArea = glm::length(glm::cross(p1 - p0, p2 - p0));
            const float cosHit = std::max(0.05f, std::abs(glm::dot(ng, ray.direction)));
            if(mipmaps && uvArea > 0.0f && worldArea > 0.0f && coneWidth > 0.0f)
                lodBase = std::log2(coneWidth / cosHit) + 0.5f * std::log2(uvArea / worldArea);
        }
        auto texture = [&](int index){
            const Texture& t = world.textures[size_t(index)];
            return t.sample(uv, lodBase + 0.5f * std::log2(float(t.width) * float(t.height)));
        };

        SurfaceParameters surface;
        surface.baseColor = material.baseColor;
        surface.metallic = material.metallic;
        surface.roughness = material.roughness;
        surface.ior = std::max(1.0001f, material.ior);
        surface.specular = material.specular;
        surface.transmission = material.transmission;
        surface.clearcoat = material.clearcoat;
        surface.clearcoatRoughness = material.clearcoatRoughness;
        if(material.baseColorTexture >= 0)
            surface.baseColor *= glm::vec3(texture(material.baseColorTexture));
        if(material.metallicRoughnessTexture >= 0){
            const glm::vec4 mr = texture(material.metallicRoughnessTexture);
            surface.roughness *= mr.g;
            surface.metallic *= mr.b;
        }
        if(material.normalTexture >= 0){
            const glm::vec4 tangent = world.tangents[tri.v[0]] * b0 + world.tangents[tri.v[1]] * hit.u + world.tangents[tri.v[2]] * hit.v;
            glm::vec3 t = glm::vec3(tangent) - ns * glm::dot(ns, glm::vec3(tangent));
            if(glm::dot(t, t) > 1e-20f){
                t = glm::normalize(t);
                const glm::vec3 b = glm::cross(ns, t) * (tangent.w < 0.0f ? -1.0f : 1.0f);
                glm::vec3 m = glm::vec3(texture(material.normalTexture)) * 2.0f - 1.0f;
                m.x *= material.normalScale;
                m.y *= material.normalScale;
                const glm::vec3 bent = t * m.x + b * m.y + ns * m.z;
                if(glm::dot(bent, bent) > 1e-20f) ns = glm::normalize(bent);
            }
        }
        glm::vec3 emitted = material.emission * material.emissionStrength;
        if(material.emissionTexture >= 0) emitted *= glm::vec3(texture(material.emissionTexture));

        const glm::vec3 wo = -ray.direction;
        const bool outside = glm::dot(ng, wo) > 0.0f;
        if(!outside){ ng = -ng; ns = -ns; }
        if(glm::dot(ns, wo) < 1e-4f) ns = ng;              //normala mape okrenuta od promatraca
        const float eta = outside ? surface.ior : 1.0f / surface.ior;

        if(depth == 0){
            result.depth = -(worldToCamera * glm::vec4(p, 1.0f)).z;
            result.normal = outside ? ns : -ns;
            result.albedo = surface.baseColor;
        }

        //-- shadow catcher: kamera ga ne vidi, ali on izmjeri koliko mu objekti uzmu svjetla ------
        if(depth == 0 && (triangleFlags[hit.triangle] & Catcher)){
            result.catcher = true;
            result.background = escaped(ray.origin, ray.direction, 0.0f, true, false, false);
            const glm::vec3 origin = offset(p, ng, true);
            for(int k = 0; k < 2; ++k){
                LightSample ls;
                const glm::vec2 choice = sampler.next2D();
                if(!sampleLight(p, ng, choice.x, sampler.next2D(), ls)) continue;
                const float cosine = glm::dot(ng, ls.wi);
                if(cosine <= 0.0f) continue;
                //Bijela Lambertova ploha: omjer sa sjenom i bez nje ne ovisi o boji poda
                const glm::vec3 c = ls.value * (cosine / Pi) / ls.pdf;
                result.lit += c;
                Ray shadowRay{origin, ls.wi, 0.0f, ls.distance * (1.0f - 1e-4f)};
                ++rays;
                bool crossed;
                result.shadowed += c * shadowTransmittance(shadowRay, true, salt + 101u, crossed);
            }
            break;
        }

        //STVARNA SCENA U ODRAZU I LOMU. Catcher (pod, zid iz proxyja) je na snimci vec snimljen:
        //kroz staklo ili u zlatu se mora vidjeti PRAVI pod, a ne siva ploha. Tocka se projicira u
        //kameru i uzme se piksel snimke. Ako je put zrcalni od kamere, to je vidjena radijancija i
        //putanja staje; inace je piksel snimke procjena boje poda za svjetlo koje se od njega
        //odbije na CG (albedo ~ linearna vrijednost piksela - pretpostavka da je pod osvijetljen
        //otprilike jedinicno; priblizno, ali boja se prelije ispravno)
        if(depth > 0 && (triangleFlags[hit.triangle] & Catcher) && world.backplate.valid()){
            const glm::vec3 local = glm::vec3(worldToCamera * glm::vec4(p, 1.0f));
            if(local.z < 0.0f){
                const glm::vec2 px = camera.pixelOf(local);
                const glm::vec2 uv = px / glm::vec2(float(camera.width), float(camera.height));
                if(uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f){
                    const glm::vec3 plateColour(world.backplate.sample(uv));
                    if(mirrorChain){
                        radiance += clampContribution(beta * plateColour, depth > 1);
                        break;
                    }
                    surface.baseColor = glm::min(plateColour, glm::vec3(0.9f));
                    surface.metallic = 0.0f;
                    surface.transmission = 0.0f;
                    surface.clearcoat = 0.0f;
                    surface.roughness = std::max(surface.roughness, 0.6f);
                }
            }
        }

        //Svijetleca ploha pogodjena izravno ili odbijanjem
        if(luminance(emitted) > 0.0f && (material.emissionTwoSided || outside) && !caustic){
            float w = 1.0f;
            if(depth > 0 && emitterOfTriangle[hit.triangle] >= 0){
                const LightRecord& light = lights[size_t(emitterOfTriangle[hit.triangle])];
                const float dist2 = hit.t * hit.t;
                const float cosLight = std::abs(glm::dot(ng, ray.direction));
                const float lightPdf = cosLight > 0.0f ? C.choiceProbability(uint32_t(light.index), previousPoint, previousNormal, useLightTree) *
                                                         dist2 / (cosLight * light.area) : 0.0f;
                if(previousMedium && equiangularOn){
                    const float equi = cosLight > 0.0f ? equiangularDensity(previousSegment, uint32_t(light.index), ray.origin + ray.direction * hit.t,
                                                                            previousT) * dist2 / (cosLight * light.area) : 0.0f;
                    w = powerHeuristic(previousEvent * previousPdf, previousEvent * lightPdf, equi);
                }else w = powerHeuristic(previousPdf, lightPdf);
            }
            radiance += clampContribution(beta * emitted * w, depth > 1);
        }
        if(depth == 0) result.object = true;
        if(depth >= maxBounces) break;

        //Lokalni sustav plohe
        glm::vec3 tangent, bitangent;
        basis(ns, tangent, bitangent);
        auto toLocal = [&](const glm::vec3& v){ return glm::vec3(glm::dot(v, tangent), glm::dot(v, bitangent), glm::dot(v, ns)); };
        auto toWorld = [&](const glm::vec3& v){ return tangent * v.x + bitangent * v.y + ns * v.z; };
        const Bsdf bsdf(surface, toLocal(wo), eta);

        //-- izravno svjetlo (NEE) ---------------------------------------------------------------
        //RIS (jezgra ReSTIR-a, bez prostorne i vremenske ponovne upotrebe - nepristrano): od
        //`candidates` svjetala iz stabla ostaje jedno razmjerno doprinosu bez sjene, i samo za
        //njega ide zraka sjene. S jednim kandidatom je to obican odabir iz stabla
        const glm::vec3 receiver = surface.transmission > 0.0f ? glm::vec3(0.0f) : ng;
        {
            LightSample chosen;
            glm::vec3 chosenF(0.0f);
            float chosenBsdfPdf = 0.0f, chosenTarget = 0.0f, weightSum = 0.0f;
            bool chosenSide = false;
            for(uint32_t k = 0; k < candidates; ++k){
                LightSample ls;
                const glm::vec2 choice = sampler.next2D();
                const glm::vec2 u = sampler.next2D();
                if(!sampleLight(p, receiver, choice.x, u, ls)) continue;
                const glm::vec3 wiLocal = toLocal(ls.wi);
                //Svjetlo s druge strane GEOMETRIJE ne smije stici kroz normalu mape
                const bool geometricSide = glm::dot(ls.wi, ng) > 0.0f;
                if(geometricSide != (wiLocal.z > 0.0f)) continue;
                float bsdfPdf = 0.0f;
                const glm::vec3 f = bsdf.eval(wiLocal, bsdfPdf);
                const float target = luminance(f * ls.value) * (ls.delta ? 1.0f : powerHeuristic(ls.pdf, bsdfPdf));
                if(!(target > 0.0f)) continue;
                weightSum += target / ls.pdf;
                if(choice.y * weightSum < target / ls.pdf){
                    chosen = ls; chosenF = f; chosenBsdfPdf = bsdfPdf; chosenTarget = target; chosenSide = geometricSide;
                }
            }
            if(chosenTarget > 0.0f){
                Ray shadowRay{offset(p, ng, chosenSide), chosen.wi, 0.0f,
                              std::isinf(chosen.distance) ? Infinity : chosen.distance * (1.0f - 1e-4f)};
                ++rays;
                bool crossed;
                const glm::vec3 through = shadowTransmittance(shadowRay, false, salt + 211u, crossed);
                if(luminance(through) > 0.0f){
                    //Kroz staklo BSDF strategija ne moze pogoditi svjetlo ravno - tezina 1
                    const float w = chosen.delta || crossed ? 1.0f : powerHeuristic(chosen.pdf, chosenBsdfPdf);
                    radiance += clampContribution(beta * chosenF * chosen.value * through *
                                                  (w * weightSum / (float(candidates) * chosenTarget)), depth > 0);
                }
            }
        }

        //-- odbijanje -----------------------------------------------------------------------------
        const glm::vec2 u = sampler.next2D();
        const glm::vec2 choice = sampler.next2D();
        BsdfSample bs;
        if(!bsdf.sample(u, choice.x, bs)) break;
        const glm::vec3 wi = toWorld(bs.wi);
        const bool geometricSide = glm::dot(wi, ng) > 0.0f;
        if(geometricSide != (bs.wi.z > 0.0f)) break;
        beta *= bs.weight;
        if(!(luminance(beta) > 0.0f) || !std::isfinite(luminance(beta))) break;
        mirrorChain = mirrorChain && bs.glossy;
        if(!bs.glossy) coneSpread += 0.2f;
        if(glass){
            if(!bs.glossy){ sawRough = true; caustic = false; }
            else if(!geometricSide && sawRough && (triangleFlags[hit.triangle] & Transmissive)) caustic = true;
        }
        previousPdf = bs.pdf;
        previousMedium = false;
        previousPoint = p;
        previousNormal = receiver;

        //Ruski rulet od treceg odbijanja: putanja koja malo nosi prekine se, a ona koja prezivi
        //nosi i udio prekinutih - ocekivanje ostaje isto
        if(depth >= 3){
            const float keep = std::clamp(std::max({beta.r, beta.g, beta.b}), 0.05f, 0.95f);
            if(choice.y >= keep) break;
            beta /= keep;
        }
        ray.origin = offset(p, ng, geometricSide);
        ray.direction = wi;
    }
    result.radiance = radiance;
    return result;
}

//---------------------------------------------------------------------------------------------
// FILM
//---------------------------------------------------------------------------------------------
bool adaptiveConverged(double luminanceSum, double luminance2Sum, double samples, float threshold){
    if(threshold <= 0.0f || samples <= 0.0) return false;
    const double mean = luminanceSum / samples;
    const double var = std::max(0.0, luminance2Sum / samples - mean * mean);
    return std::sqrt(var / samples) < double(threshold) * std::sqrt(mean + 1e-4);
}

void Renderer::renderPixel(uint32_t x, uint32_t y, uint32_t firstSample, uint32_t lastSample, uint32_t seed,
                           float clampValue, uint32_t maxBounces, uint64_t& rays){
    const uint32_t width = compiled->world.camera.width;
    Accumulator& a = pixels[size_t(y) * width + x];
    const uint32_t pixelSeed = sampling::hash(sampling::hash(x * 0x9E3779B1u ^ y) ^ (y * 0x85EBCA77u)) ^ sampling::hash(seed);
    if(!adaptiveState.empty() && (adaptiveState[size_t(y) * width + x] & 4u)) return;
    for(uint32_t s = firstSample; s < lastSample; ++s){
        const PathResult r = trace(glm::vec2(float(x) + 0.5f, float(y) + 0.5f), s, pixelSeed, clampValue, maxBounces, rays);
        a.samples += 1;
        if(r.object){ a.cg += r.radiance; a.coverage += 1.0f; }
        a.cg += r.inscatter;
        if(r.miss){ a.background += r.background; a.missSamples += 1; }
        if(r.catcher){
            a.background += r.background;
            a.catcherLit += r.lit;
            a.catcherShadowed += r.shadowed;
            a.catcherSamples += 1;
        }
        a.albedo += r.albedo;
        a.normal += r.normal;
        const double lum = double(luminance(r.radiance + r.background + r.inscatter));
        a.luminance += lum;
        a.luminance2 += lum * lum;
        //Dubina se NE prosjecuje: prosjek prednje i straznje plohe na rubu je dubina na kojoj nista
        //ne stoji. Uzme se uzorak najblizi sredistu piksela
        (void)r.depth;
    }
    //Dubina iz sredista piksela, jednom (ne ovisi o uzorku osim za alfu i dubinsku ostrinu)
    if(firstSample == 0){
        Camera centreCamera = compiled->world.camera;
        Ray ray;
        centreCamera.apertureRadius = 0.0f;
        centreCamera.ray(glm::vec2(float(x) + 0.5f, float(y) + 0.5f), glm::vec2(0.5f), ray.origin, ray.direction);
        Hit hit;
        ++rays;
        if(compiled->tree.intersect(ray, hit, [&](uint32_t index, float, float){ return !(compiled->triangleFlags[index] & NoCamera); })){
            const glm::vec3 p = ray.origin + ray.direction * hit.t;
            a.depth = -(compiled->cameraInverse * glm::vec4(p, 1.0f)).z;
        }
    }
}

void Renderer::render(const RenderSettings& settings, const std::function<void(const RenderProgress&)>& onPass,
                      const std::atomic<bool>* cancel){
    const uint32_t width = compiled->world.camera.width, height = compiled->world.camera.height;
    if(width == 0 || height == 0) return;
    if(pixels.size() != size_t(width) * height){
        pixels.assign(size_t(width) * height, Accumulator{});
        done = 0;
    }
    const auto start = std::chrono::steady_clock::now();
    uint32_t threadCount = settings.threads ? settings.threads : std::max(1u, std::thread::hardware_concurrency());
    const uint32_t tile = 32;
    const uint32_t tilesX = (width + tile - 1) / tile, tilesY = (height + tile - 1) / tile;
    const uint32_t tileCount = tilesX * tilesY;
    threadCount = std::min(threadCount, tileCount);
    glass = settings.glassShadows;
    mipmaps = settings.mipmaps;
    candidates = lightCandidatesFor(settings, *compiled);
    useLightTree = settings.lightTree;
    equiangularSampling = settings.equiangular;
    adaptiveThreshold = settings.adaptiveThreshold;
    adaptiveMinSamples = settings.adaptiveMinSamples;
    if(adaptiveThreshold > 0.0f && adaptiveState.size() != pixels.size()) adaptiveState.assign(pixels.size(), 0);

    while(done < settings.samples){
        if(cancel && cancel->load()) break;
        //Prilagodljivo: prolazi od po 8 uzoraka, provjera na pocetku prolaza (kao kartica prije uzorka)
        const bool adaptive = adaptiveThreshold > 0.0f;
        if(adaptive && done % 8 == 0 && done >= std::max(8u, adaptiveMinSamples)) adaptiveCheckpoint(done);
        const uint32_t pass = adaptive ? std::clamp(done, 1u, 8u) : std::clamp(done, 1u, 32u);
        const uint32_t first = done, last = std::min(settings.samples, done + pass);
        std::atomic<uint32_t> next{0};
        std::atomic<bool> stopped{false};
        auto work = [&]{
            uint64_t rays = 0;
            for(uint32_t t = next++; t < tileCount; t = next++){
                if(cancel && cancel->load()){ stopped = true; break; }
                const uint32_t tx = t % tilesX, ty = t / tilesX;
                for(uint32_t y = ty * tile; y < std::min(height, (ty + 1) * tile); ++y){
                    for(uint32_t x = tx * tile; x < std::min(width, (tx + 1) * tile); ++x){
                        renderPixel(x, y, first, last, settings.seed, settings.indirectClamp, settings.maxBounces, rays);
                    }
                }
            }
            rayCount += rays;
        };
        std::vector<std::thread> workers;
        for(uint32_t i = 1; i < threadCount; ++i) workers.emplace_back(work);
        work();
        for(std::thread& w : workers) w.join();
        if(stopped) break;
        done = last;
        if(onPass){
            RenderProgress progress;
            progress.samplesDone = done;
            progress.samplesTotal = settings.samples;
            progress.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            progress.rays = rayCount.load();
            onPass(progress);
        }
    }
}

//Provjera na broju uzoraka `samples` (visekratnik od 8): prvo svaki piksel zapise je li gotov
//(bit ove provjere), pa stane onaj koji je gotov sad i prosli put, sa svim susjedima gotovim
//prosli put. Bit prosle provjere se ovdje ne mijenja, pa redoslijed piksela nije bitan
void Renderer::adaptiveCheckpoint(uint32_t samples){
    const uint32_t width = compiled->world.camera.width, height = compiled->world.camera.height;
    const uint8_t now = uint8_t(1u << ((samples / 8u) & 1u)), before = uint8_t(now ^ 3u);
    std::vector<uint8_t> stop(pixels.size(), 0);
    for(size_t i = 0; i < pixels.size(); ++i){
        uint8_t& s = adaptiveState[i];
        if(s & 4u) continue;
        const Accumulator& a = pixels[i];
        const bool own = adaptiveConverged(a.luminance, a.luminance2, double(a.samples), adaptiveThreshold);
        s = own ? uint8_t(s | now) : uint8_t(s & ~now);
        if(!own || !(s & before)) continue;
        const int x = int(i % width), y = int(i / width);
        bool neighbours = true;
        for(int dy = -1; dy <= 1 && neighbours; ++dy) for(int dx = -1; dx <= 1; ++dx){
            const int nx = x + dx, ny = y + dy;
            if((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height)) continue;
            if(!(adaptiveState[size_t(ny) * width + size_t(nx)] & before)){ neighbours = false; break; }
        }
        stop[i] = neighbours;
    }
    for(size_t i = 0; i < pixels.size(); ++i) if(stop[i]) adaptiveState[i] |= 4u;
}

uint32_t lightCandidatesFor(const RenderSettings& settings, const CompiledScene& scene){
    if(settings.lightCandidates > 0) return std::min(settings.lightCandidates, 64u);
    const size_t local = (scene.lightTree.size() + 1) / 2;
    return local >= 16 ? 8u : 1u;
}

double Renderer::averageSamples() const{
    if(pixels.empty()) return 0.0;
    double sum = 0.0;
    for(const Accumulator& a : pixels) sum += a.samples;
    return sum / double(pixels.size());
}

Frame Renderer::frame(bool denoise) const{
    Frame out;
    out.width = compiled->world.camera.width;
    out.height = compiled->world.camera.height;
    out.samples = done;
    const size_t n = out.pixelCount();
    out.cg.assign(n * 4, 0.0f);
    out.background.assign(n * 3, 0.0f);
    out.shadow.assign(n * 3, 1.0f);
    out.albedo.assign(n * 3, 0.0f);
    out.normal.assign(n * 3, 0.0f);
    out.depth.assign(n, NoDepth);
    out.variance.assign(n, 0.0f);
    if(pixels.size() != n) return out;
    for(size_t i = 0; i < n; ++i){
        const Accumulator& a = pixels[i];
        if(a.samples == 0) continue;
        const float inv = 1.0f / float(a.samples);
        const glm::vec3 cg = a.cg * inv;
        out.cg[i * 4] = cg.r; out.cg[i * 4 + 1] = cg.g; out.cg[i * 4 + 2] = cg.b; out.cg[i * 4 + 3] = a.coverage * inv;
        for(int k = 0; k < 3; ++k){
            out.background[i * 3 + size_t(k)] = a.background[k] * inv;
            out.albedo[i * 3 + size_t(k)] = a.albedo[k] * inv;
        }
        const glm::vec3 nrm = glm::dot(a.normal, a.normal) > 0.0f ? glm::normalize(a.normal) : glm::vec3(0.0f);
        for(int k = 0; k < 3; ++k) out.normal[i * 3 + size_t(k)] = nrm[k];
        out.depth[i] = a.depth;
        //Sjena: dio koji nije CG = promaseni uzorci (bez sjene) + catcher uzorci s omjerom
        //zasjenjeno/osvijetljeno. Omjer zbrojeva, ne zbroj omjera - pojedinacni omjer je sum/sum
        const uint32_t rest = a.missSamples + a.catcherSamples;
        if(a.catcherSamples > 0 && rest > 0){
            for(int k = 0; k < 3; ++k){
                const float ratio = a.catcherLit[k] > 0.0f ? std::clamp(a.catcherShadowed[k] / a.catcherLit[k], 0.0f, 1.0f) : 1.0f;
                out.shadow[i * 3 + size_t(k)] = (float(a.missSamples) + float(a.catcherSamples) * ratio) / float(rest);
            }
        }
        const double mean = a.luminance / a.samples;
        const double var = std::max(0.0, a.luminance2 / a.samples - mean * mean);
        out.variance[i] = float(var / a.samples);
    }
    if(denoise) denoiseFrame(out);
    return out;
}

glm::vec3 Renderer::tracePixel(glm::vec2 pixel, uint32_t sampleIndex) const{
    uint64_t rays = 0;
    const PathResult r = trace(pixel, sampleIndex, sampling::hash(uint32_t(pixel.x) * 7919u + uint32_t(pixel.y)), 0.0f, 12, rays);
    return r.radiance + r.background + r.inscatter;
}

}
