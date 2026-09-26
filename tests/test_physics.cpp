// Collideri (Engine/Physics.h, Jolt): drska od trokuta i kapsule clanaka prsta.
//
// Drska: kutija 3 x 20 x 3 cm oko osi Y (12 trokuta). Kapsula polumjera 1 cm:
//   - uz drsku (razmak 1.5 cm od povrsine) ne dira; kroz drsku dira; tocno na povrsini dira
//   - udaljenost tocke od povrsine
//   - prazan mesh nema collidera
#include "TestHarness.h"

#include <Engine/Physics.h>

#include <cmath>

namespace{

Engine::Physics::TriangleMesh box(glm::vec3 half){
    Engine::Physics::TriangleMesh mesh;
    for(int i = 0; i < 8; ++i)
        mesh.vertices.push_back(glm::vec3((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z));
    const uint32_t faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for(const auto& f : faces) mesh.indices.insert(mesh.indices.end(), {f[0], f[1], f[2], f[0], f[2], f[3]});
    return mesh;
}

}

int main(){
    TestReport report("physics");
    const Engine::Physics::Collider handle = Engine::Physics::Collider::fromMesh(box({0.015f, 0.10f, 0.015f}));
    report.check("drska od 12 trokuta", !handle.empty() && handle.triangles() == 12, fmt("%zu trokuta", handle.triangles()));

    //Kapsula uz os X, 4 cm od osi drske: povrsina je na 1.5 cm, kapsula do 3 cm - razmak 1.5 cm
    report.check("kapsula uz drsku ne dira", !handle.capsuleHits({0.04f, 0.0f, -0.02f}, {0.04f, 0.0f, 0.02f}, 0.01f), "");
    report.check("kapsula kroz drsku dira", handle.capsuleHits({-0.05f, 0.0f, 0.0f}, {0.05f, 0.0f, 0.0f}, 0.01f), "");
    report.check("kapsula na povrsini dira", handle.capsuleHits({0.0249f, 0.0f, -0.02f}, {0.0249f, 0.0f, 0.02f}, 0.01f), "");
    report.check("kugla (a = b) dira kad je u drsci", handle.capsuleHits({0.004f, 0.05f, 0.003f}, {0.004f, 0.05f, 0.003f}, 0.005f), "");

    const float d = handle.distance({0.05f, 0.0f, 0.0f}, 0.2f);
    report.check("udaljenost tocke od povrsine 3.5 cm", std::fabs(d - 0.035f) < 1e-3f, fmt("%.4f m", d));
    report.check("udaljenost dalje od max je max", handle.distance({1.0f, 0.0f, 0.0f}, 0.2f) == 0.2f, "");

    report.check("prazan mesh nema collidera", Engine::Physics::Collider::fromMesh({}).empty(), "");
    return report.result();
}
