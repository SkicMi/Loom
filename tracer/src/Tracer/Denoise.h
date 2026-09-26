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
// A-trous je iskren, deterministican filtar bez neuronske mreze: na 64+ uzoraka cisti dobro, na
// 4-16 ostavlja mrlje. Zato, KAD GA IMA, boju CG-a cisti Intel Open Image Denoise (OIDN 2):
// neuronska mreza trenirana na path traceru, vodjena istim albedom i normalama - cista slika
// s cetvrtinom do desetinom uzoraka. OIDN se ucitava tek pri pokretanju (dlopen), pa nije
// ovisnost gradnje; trazi se u LOOM_OIDN (datoteka ili mapa), u tools/oidn/lib (fetch.sh) i u
// sustavu. Sjena catchera (glatki omjer 0..1) ostaje na A-trousu. Sirovi render je u EXR-u.
//=============================================================================================
#include "Tracer/Film.h"

#include <string>

namespace Tracer{

enum class Denoiser{ Auto, Oidn, ATrous };

//Auto: OIDN kad je ucitan, inace A-trous. Oidn bez biblioteke padne natrag na A-trous
void denoiseFrame(Frame& frame, Denoiser which = Denoiser::Auto);

//Je li OIDN ucitan (prvi poziv ga trazi); where: odakle, ili zasto nije
bool oidnAvailable(std::string* where = nullptr);

}
