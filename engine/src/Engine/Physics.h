#pragma once
//=============================================================================================
// COLLIDERI (Jolt Physics): oblik predmeta od njegovih trokuta i pitanje "dira li ga ova kost".
//
// Zasto: prsti oko drske. Preset savijanja ne zna koliko je drska debela - prsti bi prolazili kroz
// tanki mac ili lebdjeli oko debele drske. S colliderom se svaki clanak prsta savija dok kapsula
// clanka ne dotakne predmet (Loom, LoomHandPose.h) - prsti se omotaju oko stvarnog oblika.
//
// Jolt je skriven u Physics.cpp: ovaj header ne ovisi o njegovim definicijama, pa ga Loom moze
// ukljuciti bilo gdje. Koordinate su u sustavu collidera (lokalno predmetu); pretvorba iz svijeta
// je na pozivatelju.
//=============================================================================================
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Engine::Physics{

struct TriangleMesh{
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;       //po tri na trokut
};

class Collider{
public:
    //Collider od trokuta (Jolt MeshShape). Prazan kad trokuta nema ili su svi degenerirani
    static Collider fromMesh(const TriangleMesh& mesh);

    bool empty() const { return !impl; }
    size_t triangles() const;

    //Dira li kapsula od a do b polumjera radius collider (dodir ili prodor)
    bool capsuleHits(const glm::vec3& a, const glm::vec3& b, float radius) const;

    //Najmanja udaljenost tocke od povrsine (do maxDistance; vise od toga vraca maxDistance)
    float distance(const glm::vec3& point, float maxDistance) const;

private:
    struct Impl;
    std::shared_ptr<const Impl> impl;
};

}
