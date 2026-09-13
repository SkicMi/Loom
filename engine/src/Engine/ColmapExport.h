#pragma once
#include "Engine/Reconstruct.h"

#include <string>

namespace Engine{

//=============================================================================================
// Zapis rekonstrukcije u COLMAP-ov tekstualni format.
//
// To je tocka na kojoj nas solve prelazi dalje: treneri gaussian splattinga ocekuju bas taj
// format (cameras.txt, images.txt, points3D.txt), pa se nasa rekonstrukcija moze trenirati bez
// ijedne promjene u njihovom alatu.
//
// KONVENCIJA JE OVDJE JEDINO STO MOZE TIHO PROMASITI. COLMAP zapisuje rotaciju i pomak iz SVIJETA
// U KAMERU, u klasicnoj konvenciji (+Z naprijed, +Y dolje); nasa Pose je obrnuta (kamera u svijet,
// -Z naprijed). Izvod:
//
//   x_kamera_nasa      = R'(x - c)
//   x_kamera_klasicna  = M x_kamera_nasa = (M R') x - (M R') c
//   dakle R_colmap = M R',  t_colmap = -R_colmap c
//
// Test to i provjerava tako da procita natrag vlastiti zapis i projicira po COLMAP-ovim
// pravilima: ako konvencija promasi, pikseli se ne poklope - a trening bi inace dao kasu i
// krivnja bi se trazila drugdje.
//=============================================================================================

//Zapisuje cameras.txt, images.txt i points3D.txt u zadanu mapu. Mapa mora postojati.
//Imena slika su frame_0000.png, frame_0001.png ... - onim redom kojim kamere idu
bool writeColmapText(const std::string& directory,
                     const Reconstruction& reconstruction,
                     const Intrinsics& intrinsics,
                     const std::vector<Observation>& observations);

}
