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

    //HUBEROVA KAZNA, u pikselima; nula znaci iskljuceno. Isto kao u PoseSolveConfig - na pravoj
    //snimci jedno krivo poklapanje inace savije cijelu rekonstrukciju oko sebe
    double huberPixels = 0.0;

    //Sidro za gauge. Bez njega bi rjesenje klizilo, a s njim su ostale kamere i tocke izrazene
    //prema prvoj - sto je i ono sto se poslije predaje Loomu
    bool fixFirstCamera = true;

    //=========================================================================================
    // TEZINA PO IZVORU OPAZANJA. Graf spaja uglove nadjene na slici smanjenoj cetiri puta i
    // znacajke prostora mjerila na punoj: na C0257 uglovi imaju medijan ostatka 1.97 px, a prostor
    // mjerila 0.88 px - a uglova je dvaput vise, pa bez tezina upravo oni odredjuju poze. Tocke s
    // indeksom ispod precisePointsFrom (mergeGraphs ih slaze prve) mnoze ostatak s coarseWeight,
    // kao izbijeljeni ostatak (r / sigma). Pokrivenost ostaje od uglova, a tocnost od finih
    //=========================================================================================
    uint32_t precisePointsFrom = 0;
    double coarseWeight = 1.0;

    //=========================================================================================
    // KOJE SE KAMERE NE MICU, osim prve. Prazno znaci "sve su slobodne".
    //
    // ZASTO POSTOJI. U inkrementalnom rastu se bundle zove nakon svake prihvacene kamere, i svaki
    // put iznova optimizira CIJELU rekonstrukciju - i onih dvjesto kamera koje su odavno
    // konvergirale. Izmjereno na kamenom zidu: 229 poziva, ukupno 3107 s, dakle 13.6 s po pozivu,
    // a jedan poziv na kompletan graf traje 14.55 s. Svaki poziv placa cijeli graf.
    //
    // Rjedja kadenca to ne rjesava - izmjereno i odbaceno: na 1.10 vrijeme padne na 575 s ali
    // omjer izdvojenih poraste s 2.52 na 3.09 i baza padne s 6.82 na 5.40 st; na 1.25 je 244 s uz
    // omjer 5.23. Zanimljivo je da 1.25 ima NAJBOLJU reprojekciju (0.870 px) uz NAJGORI omjer -
    // ucebnicki primjer prenaucenosti: manje bundlea znaci da manje tocaka prezivi filtar, pa
    // preostanu lake, a reprojekcija na njima izgleda odlicno dok poopcavanje propada.
    //
    // Ono sto radi je ogranicavanje na PROZOR: nove kamere se micu, daleke stoje ali i dalje drze
    // tocke koje vide. Time se gustoca S-a i cijena gustog rjesavanja urusavaju s brojem
    // slobodnih kamera, a tocnost ostaje jer se lokalna geometrija i dalje ispravlja svaki korak
    //=========================================================================================
    std::vector<uint8_t> fixedCameras;

    //=========================================================================================
    // ROLLING SHUTTER. Nula znaci globalni zatvarac - tada se ne racuna nista od ovoga i bundle
    // je bit po bit isti kao prije.
    //
    // ZASTO. CMOS senzor cita sliku redak po redak: gornji red je snimljen prije donjeg. Dok se
    // kamera mice, svaki redak ima svoju pozu, a jedna poza po kadru to ne moze objasniti. Na
    // C0257 je promjenjivi dio polja ostataka 7 puta veci nego na sintetici (0.535 naspram 0.077
    // px), a staticni samo 2.8 puta - dakle ono sto kvari je nesto sto se mijenja s gibanjem.
    //
    // MODEL. Opazanje u retku v snimljeno je u trenutku s = (v - cy) * rowTime (u razmacima
    // izmedju kadrova, sredina slike je trenutak kadra). Poza tada je poza kadra pomaknuta
    // brzinom kamere: polozaj + s * linearVelocity, orijentacija * exp(s * angularVelocity).
    // Brzine daje pozivatelj (iz susjednih kadrova); ovdje su stalne tijekom optimizacije.
    //
    // rowTime je jedan broj za cijelu snimku - vrijeme citanja senzora podijeljeno visinom
    // slike i razmakom kadrova - jer je to svojstvo kamere, a ne kadra
    //=========================================================================================
    double rowTime = 0.0;
    std::vector<glm::vec3> linearVelocity;     //po kameri, jedinica svijeta po razmaku kadrova
    std::vector<glm::vec3> angularVelocity;    //po kameri, radijani po razmaku kadrova, u osima kamere
};

//Poza kamere u trenutku kad je snimljen redak 'row' (vidi BundleConfig::rowTime). Bez rolling
//shuttera ili bez brzine za tu kameru vraca pozu kadra nepromijenjenu
Pose rollingShutterPose(const Pose& pose, const BundleConfig& config, size_t camera, float row, float centreRow);

//Brzine kamera iz susjednih kadrova (sredisnja razlika; na krajevima jednostrana). times su
//redni brojevi kadrova snimke, pa su brzine po JEDNOM razmaku kadrova, kao sto rowTime trazi.
//Kutna brzina je u osima kamere
void rollingShutterVelocities(const std::vector<Pose>& poses, const std::vector<double>& times,
                              std::vector<glm::vec3>& linear, std::vector<glm::vec3>& angular);

struct BundleTiming{
    double totalSeconds = 0.0;
    double costSeconds = 0.0;
    double linearizeSeconds = 0.0;
    double schurSeconds = 0.0;
    double denseSolveSeconds = 0.0;
    double backSubstituteSeconds = 0.0;
};

struct BundleResult{
    std::vector<Pose> poses;
    std::vector<glm::vec3> points;
    uint32_t iterations = 0;
    double startMedian = 0.0;
    double endMedian = 0.0;
    BundleTiming timing;
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
