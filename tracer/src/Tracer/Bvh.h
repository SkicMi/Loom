#pragma once
//=============================================================================================
// BVH - hijerarhija omeđujucih kutija nad trokutima scene.
//
// Gradi se po SAH-u (surface area heuristic) s binovima: na svakom cvoru se po svakoj osi
// isproba 16 rezova po sredistima trokuta i uzme onaj koji minimizira ocekivani trosak
// (povrsina djeteta / povrsina roditelja * broj trokuta). Tako se gradi milijun trokuta u
// sekundi, a obilazak je unutar desetak posto od punog SAH-a.
//
// Cvor je 32 bajta (dvije tocke i dva broja): list drzi raspon trokuta, unutarnji cvor indeks
// lijevog djeteta; desno je odmah iza njega. Trokuti su preslozeni redom listova i spremljeni kao
// (v0, e1, e2) - Moller-Trumbore bez ijednog oduzimanja pri obilasku.
//
// FILTER. Presjek se moze odbiti: alfa maska (list kroz koji se vidi), objekt koji ne baca sjenu,
// objekt koji kamera ne vidi. Filter dobije indeks trokuta i baricentricne koordinate i kaze
// prihvaca li pogodak - zato je obilazak predlozak, da se provjera ugradi u petlju
//=============================================================================================
#include "Tracer/Scene.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace Tracer{

struct Ray{
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::infinity();
};

struct Hit{
    float t = std::numeric_limits<float>::infinity();
    uint32_t triangle = ~0u;            //indeks u Scene::triangles
    float u = 0.0f, v = 0.0f;           //baricentricne: tocka = (1-u-v)*v0 + u*v1 + v*v2
    bool valid() const {return triangle != ~0u;}
};

struct AcceptAll{
    bool operator()(uint32_t, float, float) const {return true;}
};

class Bvh{
public:
    void build(const std::vector<glm::vec3>& positions, const std::vector<Triangle>& triangles);

    //Najblizi prihvaceni pogodak unutar [tMin, tMax]. Pogodak skrati ray.tMax
    template<class Filter = AcceptAll>
    bool intersect(Ray& ray, Hit& hit, const Filter& accept = Filter()) const;

    //Postoji li ikakav prihvaceni pogodak - za zrake sjene, prekida na prvom
    template<class Filter = AcceptAll>
    bool occluded(const Ray& ray, const Filter& accept = Filter()) const;

    size_t nodeCount() const {return nodes.size();}
    int depth() const {return maxDepth;}
    glm::vec3 boundsMin() const {return nodes.empty() ? glm::vec3(0.0f) : nodes[0].min;}
    glm::vec3 boundsMax() const {return nodes.empty() ? glm::vec3(0.0f) : nodes[0].max;}

private:
    struct Node{
        glm::vec3 min;
        uint32_t leftOrFirst;           //list: prvi trokut; unutarnji: lijevo dijete
        glm::vec3 max;
        uint32_t count;                 //0: unutarnji cvor
    };
    struct Prepared{
        glm::vec3 v0, e1, e2;
    };

    std::vector<Node> nodes;
    std::vector<Prepared> prepared;     //redom listova
    std::vector<uint32_t> order;        //redom listova -> indeks u Scene::triangles
    int maxDepth = 0;

    static bool slab(const Node& node, const glm::vec3& origin, const glm::vec3& inverse, float tMin, float tMax, float& entry){
        const glm::vec3 t0 = (node.min - origin) * inverse;
        const glm::vec3 t1 = (node.max - origin) * inverse;
        const glm::vec3 near = glm::min(t0, t1), far = glm::max(t0, t1);
        entry = std::max(std::max(near.x, near.y), std::max(near.z, tMin));
        const float exit = std::min(std::min(far.x, far.y), std::min(far.z, tMax));
        return entry <= exit;
    }

    static bool triangle(const Prepared& p, const Ray& ray, float tMax, float& t, float& u, float& v){
        const glm::vec3 pv = glm::cross(ray.direction, p.e2);
        const float det = glm::dot(p.e1, pv);
        if(std::abs(det) < 1e-12f) return false;
        const float inv = 1.0f / det;
        const glm::vec3 tv = ray.origin - p.v0;
        u = glm::dot(tv, pv) * inv;
        if(u < 0.0f || u > 1.0f) return false;
        const glm::vec3 qv = glm::cross(tv, p.e1);
        v = glm::dot(ray.direction, qv) * inv;
        if(v < 0.0f || u + v > 1.0f) return false;
        t = glm::dot(p.e2, qv) * inv;
        return t > ray.tMin && t < tMax;
    }

    template<bool AnyHit, class Filter>
    bool traverse(Ray& ray, Hit* hit, const Filter& accept) const;
};

template<bool AnyHit, class Filter>
bool Bvh::traverse(Ray& ray, Hit* hit, const Filter& accept) const{
    if(nodes.empty()) return false;
    //Os s nultim smjerom: beskonacnost s predznakom, ne 0*inf = NaN na rubu kutije
    auto safe = [](float d){ return std::abs(d) > 1e-20f ? d : (d < 0.0f ? -1e-20f : 1e-20f); };
    const glm::vec3 inverse(1.0f / safe(ray.direction.x), 1.0f / safe(ray.direction.y), 1.0f / safe(ray.direction.z));
    uint32_t stack[96];
    int top = 0;
    float entry;
    if(!slab(nodes[0], ray.origin, inverse, ray.tMin, ray.tMax, entry)) return false;
    stack[top++] = 0;
    bool found = false;
    while(top > 0){
        const Node& node = nodes[stack[--top]];
        if(node.count > 0){
            for(uint32_t i = 0; i < node.count; ++i){
                const uint32_t at = node.leftOrFirst + i;
                float t, u, v;
                if(!triangle(prepared[at], ray, ray.tMax, t, u, v)) continue;
                if(!accept(order[at], u, v)) continue;
                if(AnyHit) return true;
                ray.tMax = t;
                hit->t = t; hit->triangle = order[at]; hit->u = u; hit->v = v;
                found = true;
            }
            continue;
        }
        const uint32_t left = node.leftOrFirst, right = left + 1;
        float entryLeft, entryRight;
        const bool hitLeft = slab(nodes[left], ray.origin, inverse, ray.tMin, ray.tMax, entryLeft);
        const bool hitRight = slab(nodes[right], ray.origin, inverse, ray.tMin, ray.tMax, entryRight);
        //Blizi na vrh stoga: on se obidje prvi, pa daleki cesto otpadne jer je tMax vec kraci
        if(hitLeft && hitRight){
            if(entryLeft <= entryRight){ stack[top++] = right; stack[top++] = left; }
            else{ stack[top++] = left; stack[top++] = right; }
        }else if(hitLeft) stack[top++] = left;
        else if(hitRight) stack[top++] = right;
    }
    return found;
}

template<class Filter>
bool Bvh::intersect(Ray& ray, Hit& hit, const Filter& accept) const{
    return traverse<false>(ray, &hit, accept);
}

template<class Filter>
bool Bvh::occluded(const Ray& ray, const Filter& accept) const{
    Ray copy = ray;
    return traverse<true>(copy, nullptr, accept);
}

}
