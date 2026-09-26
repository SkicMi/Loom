#pragma once
//=============================================================================================
// VREMENSKA STABILNOST SEKVENCE - ocisceni kadrovi ne titraju.
//
// Filtar suma (OIDN ili A-trous) svaki kadar cisti sam za sebe: preostali sum su mrlje koje se
// od kadra do kadra pomicu - u animaciji titraju iako je svaki kadar za sebe cist. Ovdje se
// prosli (vec stabilizirani) kadar PREBACI u ovaj: svaki piksel po dubini u tocku svijeta, pa
// kroz kameru proslog kadra u njegov piksel (distorzija ukljucena). Povijest se uzme samo ako je
// ondje ista ploha: dubina tocke u proslom kadru odgovara spremljenoj dubini (2 %), normala slicna
// (0.9), a razlika u sjaju je unutar suma piksela (pola sigme sirove procjene + 2 %; veca znaci da se
// svjetlo promijenilo - sjena koja putuje). Povijest se uzorkuje Catmull-Romom stegnutim na
// susjede (bilinearno bi se kroz kadrove zamutilo). Mijesa se OSVJETLJENJE (boja / albedo, kao
// SVGF): tekstura je uvijek iz ovog kadra i ostaje ostra, a svjetlo je glatko pa ga prebacivanje
// ne mekša. cg = albedo * mijesanje(osvjetljenje ovog, povijesti, strength).
//
// Svijet se smatra mirnim: objekt koji se giba u proslom kadru nije na mjestu u koje ga dubina
// ovog kadra salje - dubina i normala tamo ne odgovaraju, pa nema duhova. Rubovi objekata i pozadina
// bez dubine (nebo, magla bez plohe) ostaju kakvi jesu.
//
// Ovo je PRISTRANO izgladjivanje (eksponencijalni prosjek kroz vrijeme, strength 0.5 = zadnjih
// ~3 kadra) - zamjena malo vremenske mekoce za mirnu sliku; loom-render --stabilnost 0 je iskljuci.
//=============================================================================================
#include "Tracer/Film.h"
#include "Tracer/Scene.h"

namespace Tracer{

struct TemporalHistory{
    Frame frame;            //prosli stabilizirani kadar (cg, dubina, normale, albedo)
    Camera camera;
    bool valid = false;
};

struct TemporalStats{
    size_t reused = 0;      //pikseli s povijesti
    size_t rejected = 0;    //pikseli s dubinom kojima povijest nije odgovarala
};

//Stabilizira frame.cg (poslije filtra suma) i zapamti ga kao povijest. strength 0..1 je tezina
//povijesti (0: samo zapamti)
TemporalStats stabilize(Frame& frame, const Camera& camera, TemporalHistory& history, float strength);

}
