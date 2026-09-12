#pragma once
#include "Engine/SolvePose.h"

namespace Engine{

//=============================================================================================
// Bundle adjustment: poze i tocke se popravljaju ZAJEDNO.
//
// PnP popravlja pozu uz nepomicne tocke, triangulacija tocke uz nepomicne poze. Svaka od njih
// vjeruje onome sto joj je dano, pa se greska seli s jedne strane na drugu i nikad ne nestane.
// Ovdje su nepoznanice i jedno i drugo: 6 brojeva po kameri i 3 po tocki.
//
// ZASTO SCHUR. Za ovu scenu to je 48 + 900 nepoznanica, a sustav bi bio 948x948. Ali blok po
// TOCKAMA je blok-dijagonalan - tocke se medjusobno ne diraju, svaka vidi samo svoje kamere - pa
// se svaka moze izbaciti iz sustava u zatvorenoj formi. Ostane 48x48 za kamere, a tocke se poslije
// vrate uvrstavanjem. To je cijeli trik, i on je razlog zasto je bundle adjustment izvediv.
//
// GAUGE: cijelo rjesenje smije kliziti. Pomakni sve kamere i sve tocke za isti metar i nijedno
// opazanje se ne promijeni - reprojekcija je slijepa za to. Zato se prva kamera drzi fiksnom;
// bez toga je sustav singularan i korak odlazi u smjeru koji ne znaci nista.
//=============================================================================================

struct BundleConfig{
    uint32_t maxIterations = 20;
    double lambda = 1e-4;
    double minStep = 1e-12;

    //Sidro za gauge. Bez njega bi rjesenje klizilo, a s njim su ostale kamere i tocke izrazene
    //prema prvoj - sto je i ono sto se poslije predaje Loomu
    bool fixFirstCamera = true;
};

struct BundleResult{
    std::vector<Pose> poses;
    std::vector<glm::vec3> points;
    uint32_t iterations = 0;
    double startMedian = 0.0;
    double endMedian = 0.0;
    bool solved = false;
};

//Jakobijan reprojekcije po TOCKI: 2 retka (u, v), 3 stupca (x, y, z u svijetu).
//False kad je tocka iza kamere
bool pointJacobian(const Pose& pose,
                   const Intrinsics& intrinsics,
                   const glm::vec3& point,
                   const glm::vec2& observed,
                   double residual[2],
                   double jacobian[2][3]);

//Poze i tocke koje zajedno najbolje objasnjavaju opazanja
BundleResult bundleAdjust(const std::vector<Observation>& observations,
                          const std::vector<Pose>& poses,
                          const std::vector<glm::vec3>& points,
                          const Intrinsics& intrinsics,
                          const BundleConfig& config = {});

}
