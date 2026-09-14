#pragma once
#include "Engine/Reconstruct.h"

#include <string>

namespace Engine{

//=============================================================================================
// Citanje COLMAP-ovog tekstualnog modela natrag u nasu rekonstrukciju.
//
// ZASTO OVO POSTOJI. Dok se nas solver razvija, treba nam mjerilo - broj koji kaze koliko je
// dobro "dobro". COLMAP na istoj snimci daje 0.91 px reprojekcije i 65 od 101 kamere; bez toga
// smo mjerili sami sebe i nismo znali je li nasih 4.4 px katastrofa ili granica snimke.
//
// Kad model udje u nas tip, dalje je svejedno tko ga je izracunao: isti prizor, isti izvoz, ista
// kocka nacrtana u splat sceni. To je i put do proizvoda dok solver jos nije spreman, i alat za
// razvoj kad bude.
//
// KONVENCIJA JE TOCAN INVERZ IZVOZA, i to je jedino sto ovdje moze tiho promasiti. Iz
// R_colmap = M R' i t_colmap = -R_colmap c slijedi, jer je M samoj sebi inverz:
//
//     R' = M R_colmap    =>    nasa R = (M R_colmap)'
//     c  = -R_colmap' t_colmap
//
// Test to ne provjerava racunom nego krugom: nasa rekonstrukcija se izveze, procita natrag i mora
// se vratiti ista. Ako konvencija promasi u bilo kojem smjeru, krug se ne zatvori - i tako je
// odmah pao prvi pokusaj, u kojem sam fy okrenuo jer ga Loomov renderer vodi negativnim.
// Engineova Intrinsics je pozitivna u oba zarista; ta se dva svijeta ne smiju mijesati.
//
// STO SE NE CITA: boje tocaka i pogreska po tocki. Nasa Reconstruction ih nema, a izmisljati polja
// zato sto ih tudji format nosi znaci pustiti tudji format da oblikuje nas tip.
//=============================================================================================

struct ColmapModel{
    Reconstruction reconstruction;
    Intrinsics intrinsics;

    //Opazanja onako kako ih COLMAP vodi: kamera je redni broj slike po IMENU, tocka je redni broj
    //tocke. Isti oblik koji reconstruct prima, pa se dade i ponovno solvati
    std::vector<Observation> observations;

    //Imena slika, poredana isto kao kamere - trening ih treba da zna koji piksel ide uz koju pozu
    std::vector<std::string> imageNames;

    //Distorzija koju je COLMAP rijesio. Nasa Intrinsics ju jos ne nosi, pa stoji ovdje kao
    //IZVJESTAJ: k bitno veci od nule znaci da slika nije ravna, a nas solver to ne modelira
    double radialK1 = 0.0;
    double radialK2 = 0.0;
    std::string cameraModel;
};

//Cita cameras.txt, images.txt i points3D.txt iz zadane mape. False kad ijedna fali ili je necitljiva
bool readColmapText(const std::string& directory, ColmapModel& model);

}
