#pragma once
//=============================================================================================
// UKLANJANJE SUMA - A-trous valicni filtar vodjen pomocnim slojevima (Dammertz 2010, SVGF 2017
// bez vremenske komponente).
//
// Sum Monte Carla je u OSVJETLJENJU, ne u teksturi. Zato se boja prvo podijeli s albedom (ono
// sto ostane je svjetlo koje je palo na plohu), to se zagladi, pa se pomnozi natrag - tekstura
// ostane ostra piksel po piksel. Susjedi se mijesaju samo ako lezi na istoj plohi (slicna normala
// i dubina) i ako je razlika u sjaju unutar suma koji je izmjeren u tom pikselu. Pet prolaza s
// razmakom 1, 2, 4, 8, 16 daje doseg od 64 piksela uz 25 citanja po pikselu po prolazu.
//
// NIJE OIDN. Ovo je iskren, deterministican filtar bez neuronske mreze: na 64+ uzoraka cisti
// dobro, na 4 ostavlja mrlje. Sirovi render (bez filtra) ostaje dostupan u EXR-u
//=============================================================================================
#include "Tracer/Film.h"

namespace Tracer{

void denoiseFrame(Frame& frame);

}
