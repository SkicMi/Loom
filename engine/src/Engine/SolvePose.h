#pragma once
#include "Engine/SyntheticScene.h"

namespace Engine{

//=============================================================================================
// PnP: iz poznatih tocaka i njihovih opazanja natrag do POZE kamere.
//
// Obrnuto od triangulacije, i zajedno s njom zatvara krug: poze -> tocke -> poze. Tek kad oba
// smjera rade, bundle adjustment ima sto popravljati.
//
// Rjesava se Gauss-Newtonom s prigusenjem: krene se od pretpostavljene poze, izracuna se koliko
// svako opazanje promasuje, i poza se pomakne u smjeru koji te promasaje smanjuje. Korak je
// linearan sustav 6x6 - tri broja za pomak, tri za rotaciju, oboje u KAMERINOM sustavu.
//
// TREBA POCETNU POZU, i to je posteno reci: ovo popravlja pretpostavku, ne pogadja iz nicega. U
// nizu kadrova pocetna poza je prethodni kadar, sto je i normalan slucaj. Poza iz nicega (P3P ili
// DLT) je zaseban korak i doci ce ako zatreba.
//
// JAKOBIJAN JE IZLOZEN NAMJERNO. On je najlakse mjesto za tihu gresku: kriv predznak u jednom
// stupcu i solver i dalje konvergira, samo sporije i u krivo. Test ga zato usporedjuje s
// numerickom derivacijom, clan po clan. Isti jakobijan trebat ce i bundle adjustment.
//=============================================================================================

//Jedno opazanje poznate tocke u kameri koja se trazi
struct PointObservation{
    uint32_t point = 0;
    glm::vec2 pixel{0.0f};
};

struct PoseSolveConfig{
    uint32_t maxIterations = 30;

    //Prigusenje (Levenberg): korak se skraci kad ga sustav prenapuhne, i produzi kad ide dobro.
    //Bez njega Gauss-Newton iz daleke pocetne poze zna preskociti i razletjeti se
    double lambda = 1e-4;

    //Kad korak postane manji od ovoga, dalje se nema kamo
    double minStep = 1e-12;
};

struct PoseSolveResult{
    Pose pose;
    uint32_t iterations = 0;
    double startMedian = 0.0;   //medijan reprojekcije prije, u pikselima
    double endMedian = 0.0;     //i poslije
    bool solved = false;        //false kad opazanja nema dovoljno ili sustav nije rjesiv
};

//Reziduali i jakobijan jednog opazanja: 2 retka (u, v) i 6 stupaca (pomak | rotacija).
//False kad je tocka iza kamere - tamo derivacija ne postoji
bool poseJacobian(const Pose& pose,
                  const Intrinsics& intrinsics,
                  const glm::vec3& point,
                  const glm::vec2& observed,
                  double residual[2],
                  double jacobian[2][6]);

//Poza koja najbolje objasnjava opazanja, krenuvsi od initial
PoseSolveResult solvePose(const std::vector<glm::vec3>& points,
                          const std::vector<PointObservation>& observations,
                          const Intrinsics& intrinsics,
                          const Pose& initial,
                          const PoseSolveConfig& config = {});

}
