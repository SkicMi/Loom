#pragma once
#include "Engine/Bundle.h"

#include <string>

namespace Engine{

//=============================================================================================
// KOLIKO TRAJE CITANJE SENZORA, izmjereno iz same snimke, i poze za to.
//
// Bundle ima model rolling shuttera (BundleConfig::rowTime), ali ne zna koliko citanje traje -
// to je svojstvo kamere i nacina snimanja, i nigdje ne pise. Ovdje se izmjeri: za niz vremena
// citanja pusti se bundle, a sudac je medijan ostatka na IZDVOJENIM opazanjima (svako osmo), koja
// bundle ne vidi. Model koji samo prenauci smanji ostatak na onome iz cega racuna, a na
// izdvojenima ne.
//
// ZASTO SE ISPLATI, izmjereno na C0257 (Sony ZV-E10 II, 4K 50p, iz ruke): citanje ~0.6 kadra,
// izdvojena opazanja 3.00 -> 2.78 px, a splat treniran s tim pozama i crtan redak po redak
// (gsplat 3DGUT) na izdvojenim kadrovima 25.52 dB naspram 23.43 bez toga - vise od dva decibela.
//
// Rezultat su poze za tri trenutka: sredina kadra (kao dosad), gornji i donji redak. Trener
// splatova iz gornjeg i donjeg zna pozu svakog retka.
//=============================================================================================

struct RollingShutterConfig{
    double huberPixels = 2.0;
    uint32_t iterations = 20;
    uint32_t holdEvery = 8;            //svako N-to opazanje je izdvojeno za suca

    //Grubi niz, pa fino oko najboljeg. U KADROVIMA: 1.0 = citanje traje cijeli razmak izmedju
    //kadrova, negativno = senzor cita odozdo prema gore
    double coarseFrom = -1.2, coarseTo = 1.2, coarseStep = 0.2, fineStep = 0.05;

    //Koliko izdvojeni ostatak mora pasti da bi se model uzeo. Ispod toga je razlika sum, a
    //rolling shutter koji ne postoji samo bi dodao nepoznanicu
    double minimumGain = 0.02;
};

struct RollingShutterResult{
    bool used = false;
    double readout = 0.0;              //u kadrovima; 0 kad nije uzet
    double heldOutGlobal = 0.0;        //medijan izdvojenih bez modela, px
    double heldOutBest = 0.0;          //i s najboljim vremenom citanja
    uint32_t bundles = 0;

    //Zavrsni bundle na SVIM opazanjima uz izmjereno vrijeme (samo kad je used)
    std::vector<Pose> centre, top, bottom;
    std::vector<glm::vec3> points;
};

//times: redni broj kadra snimke za svaku kameru; linear/angular: brzine po kameri (vidi
//rollingShutterVelocities) - najbolje iz SUSJEDNIH VIDEO KADROVA, ne kljucnih
RollingShutterResult estimateRollingShutter(const std::vector<Observation>& observations,
                                            const std::vector<Pose>& poses,
                                            const std::vector<glm::vec3>& points,
                                            const Intrinsics& intrinsics,
                                            const std::vector<glm::vec3>& linear,
                                            const std::vector<glm::vec3>& angular,
                                            const RollingShutterConfig& config = {});

//=============================================================================================
// ROLLING SHUTTER NAD GOTOVIM REZULTATOM (mapa s cameras/images/points3D.txt i kamera.usda).
//
// Cita ono sto je VideoSolve zapisao - model i kameru za SVAKI kadar iz kamera.usda (brzine iz
// susjednih video kadrova) - izmjeri vrijeme citanja i zapise rs_top, rs_bottom (poze gornjeg i
// donjeg retka, s tockama iz istog bundlea) i rolling_shutter.txt. Kad model ne donese dobitak,
// ne pise nista i brise stare. Radi i nad starim rezultatom, bez novog solvea.
//
// Zasto nad zapisanim, a ne u memoriji solvea: ovo je put koji je izmjeren (C0257: citanje 0.6
// kadra, splat +2.1 dB), a prva inacica koja je radila nad podacima u memoriji nije nasla nista
//=============================================================================================
bool rollingShutterForResult(const std::string& directory, RollingShutterResult& result, std::string& report,
                             const RollingShutterConfig& config = {});

}
