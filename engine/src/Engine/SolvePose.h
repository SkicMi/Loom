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

    //HUBEROVA KAZNA, u pikselima. Nula znaci iskljuceno - cisti najmanji kvadrati, kao dosad.
    //Iznad praga kazna raste samo linearno umjesto kvadratno, pa jedno promaseno poklapanje
    //prestane povlaciti cijelo rjesenje. Na pravoj snimci su promasaji pravilo, ne iznimka
    double huberPixels = 0.0;
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


//=============================================================================================
// ISTO, ALI S ODBACIVANJEM PROMASAJA.
//
// Zasto uopce: gornji solvePose uzima SVA opazanja s punom tezinom, a Huber promasaj samo
// pritegne umjesto da ga izbaci. Tocke koje kamera vidi nisu sve dobre - one su triangulirane iz
// dosad rijesenih poza, pa je dio njih na krivoj dubini. Kad ih je petina, poza se povuce za
// njima, a medijan preko SVIH tocaka onda tu kameru i odbije - iako je poza mozda bila dobra.
//
// Ovdje se umjesto toga trazi NAJVECI SKUP KOJI SE SLAZE: nasumicni mali uzorak da pretpostavku,
// prebroji se koliko je opazanja unutar praga, i najbolji skup se na kraju dotjera sam za sebe.
// Odluka o kameri se onda donosi po TOM skupu, a ne po svemu sto je kamera vidjela.
//
// UZORAK SE DOTJERUJE, NE RJESAVA IZ NICEGA. Prava minimalna rjesavacica (P3P) daje pozu iz tri
// tocke bez ikakve pretpostavke; ovdje se krece od zadane poze, sto je u nizu kadrova susjedni
// kadar i time blizu. Za neuredjenu zbirku fotografija to ne bi bilo dovoljno i P3P bi trebao;
// ovdje se najprije mjeri koliko donosi samo odbacivanje promasaja.
//
// NASUMICNOST JE ODREDJENA: isti ulaz daje isti izlaz, jer rekonstrukcija koja se mijenja izmedju
// dva pokretanja nema se s cime usporediti
//=============================================================================================
struct PoseRansacConfig{
    //Koliko piksela smije promasiti opazanje da bi se racunalo kao slaganje. Siroko namjerno:
    //ovo razlucuje promasaj od suma, a ne dobru pozu od lose. COLMAP ovdje drzi 12 px
    double maxError = 12.0;

    //Najmanji broj i udio opazanja koja se slazu, da bi se poza uopce prihvatila
    uint32_t minInliers = 12;
    double minInlierRatio = 0.25;

    //Koliko opazanja ide u jednu pretpostavku. Sest za sest nepoznanica bio bi minimum; osam daje
    //malo zaliha protiv suma, a i dalje je uzorak koji vjerojatno nema promasaj
    uint32_t sampleSize = 8;

    //Gornja granica pokusaja. Stvarni broj se skracuje cim se nadje velik skup koji se slaze
    uint32_t maxTrials = 100;

    //Sigurnost s kojom se zeli bar jedan uzorak bez promasaja
    double confidence = 0.99;

    PoseSolveConfig solve;
};

struct PoseRansacResult{
    Pose pose;
    std::vector<uint8_t> inlier;   //po opazanju, istim redom kojim su dosla
    uint32_t inliers = 0;
    uint32_t trials = 0;
    double inlierMedian = 0.0;     //medijan reprojekcije PO SKUPU KOJI SE SLAZE
    bool solved = false;
};

PoseRansacResult solvePoseRansac(const std::vector<glm::vec3>& points,
                                 const std::vector<PointObservation>& observations,
                                 const Intrinsics& intrinsics,
                                 const Pose& initial,
                                 const PoseRansacConfig& config = {});

}
