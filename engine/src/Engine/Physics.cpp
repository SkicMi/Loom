#include "Engine/Physics.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace Engine::Physics{

namespace{

//Jolt treba alokator, tvornicu tipova i registrirane oblike prije prvog oblika - jednom po procesu
void ensureJolt(){
    static std::once_flag once;
    std::call_once(once, []{
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

JPH::Vec3 vec(const glm::vec3& v){ return JPH::Vec3(v.x, v.y, v.z); }

}

struct Collider::Impl{
    JPH::RefConst<JPH::Shape> shape;
    size_t triangles = 0;
};

Collider Collider::fromMesh(const TriangleMesh& mesh){
    ensureJolt();
    Collider collider;
    JPH::VertexList vertices;
    vertices.reserve(mesh.vertices.size());
    for(const glm::vec3& v : mesh.vertices) vertices.push_back(JPH::Float3(v.x, v.y, v.z));
    JPH::IndexedTriangleList triangles;
    for(size_t i = 0; i + 2 < mesh.indices.size(); i += 3){
        const uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if(a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size()) continue;
        //Degenerirani trokut Jolt odbaci sam, ali cijeli oblik moze pasti ako ostane prazan
        if(glm::length(glm::cross(mesh.vertices[b] - mesh.vertices[a], mesh.vertices[c] - mesh.vertices[a])) < 1e-12f) continue;
        triangles.push_back(JPH::IndexedTriangle(a, b, c));
    }
    if(triangles.empty()) return collider;
    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    const JPH::ShapeSettings::ShapeResult result = settings.Create();
    if(result.HasError()) return collider;
    auto impl = std::make_shared<Impl>();
    impl->shape = result.Get();
    impl->triangles = settings.mIndexedTriangles.size();
    collider.impl = impl;
    return collider;
}

size_t Collider::triangles() const { return impl ? impl->triangles : 0; }

bool Collider::capsuleHits(const glm::vec3& a, const glm::vec3& b, float radius) const{
    if(!impl || radius <= 0.0f) return false;
    const glm::vec3 axis = b - a;
    const float length = glm::length(axis);
    //Kapsula u Joltu lezi uz os Y sa sredistem u ishodistu; okrene se na smjer a -> b
    JPH::RefConst<JPH::Shape> probe;
    JPH::Quat rotation = JPH::Quat::sIdentity();
    if(length < 1e-6f){
        probe = new JPH::SphereShape(radius);
    }else{
        probe = new JPH::CapsuleShape(0.5f * length, radius);
        rotation = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), vec(axis / length));
    }
    //Mesh je samo povrsina: kapsula cijela unutar predmeta ne sijece nijedan trokut. Krajevi unutar
    //zatvorenog mesha (Jolt broji presjeke zrake) znace da je clanak vec u predmetu
    for(const glm::vec3& end : {a, b}){
        JPH::AnyHitCollisionCollector<JPH::CollidePointCollector> inside;
        impl->shape->CollidePoint(vec(end) - impl->shape->GetCenterOfMass(), JPH::SubShapeIDCreator(), inside);
        if(inside.HadHit()) return true;
    }
    const JPH::Mat44 probeTransform = JPH::Mat44::sRotationTranslation(rotation, vec(0.5f * (a + b)));
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = 0.0f;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;     //trokuti tudjih modela nisu uvijek okrenuti prema van
    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
    JPH::CollisionDispatch::sCollideShapeVsShape(probe, impl->shape, JPH::Vec3::sReplicate(1.0f), JPH::Vec3::sReplicate(1.0f),
                                                 probeTransform, JPH::Mat44::sIdentity(),
                                                 JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(), settings, collector);
    return collector.HadHit();
}

float Collider::distance(const glm::vec3& point, float maxDistance) const{
    if(!impl) return maxDistance;
    //Mala kugla u tocki; Jolt javlja i razmak do maxDistance (negativna dubina prodora)
    const float probeRadius = std::max(1e-5f, maxDistance * 1e-3f);
    JPH::RefConst<JPH::Shape> probe = new JPH::SphereShape(probeRadius);
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = maxDistance;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::ClosestHitCollisionCollector<JPH::CollideShapeCollector> collector;
    JPH::CollisionDispatch::sCollideShapeVsShape(probe, impl->shape, JPH::Vec3::sReplicate(1.0f), JPH::Vec3::sReplicate(1.0f),
                                                 JPH::Mat44::sTranslation(vec(point)), JPH::Mat44::sIdentity(),
                                                 JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(), settings, collector);
    if(!collector.HadHit()) return maxDistance;
    return std::clamp(-collector.mHit.mPenetrationDepth + probeRadius, 0.0f, maxDistance);
}

}
