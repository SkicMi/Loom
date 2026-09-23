#pragma once
#include "Warp/Stage.h"

#include <string>

namespace Warp{

//=============================================================================================
// PROJEKT: cijela scena u jednoj .usda datoteci.
//
// ZASTO USD, a ne vlastiti format. Scena je vec USD-ova oblika (prim, djeca, xformOp,
// timeSamples), pa se zapise bez prevodjenja - i ista datoteka se otvori u Blenderu, Houdiniju i
// Nukeu: kamera iz solvea s kljucem na svakom kadru, kocka koju je umjetnik postavio, oblak tocaka.
//
//   Xform/Camera/Points/Cube/Mesh     entiteti; xformOp:translate, orient, scale (+ timeSamples)
//   custom loom:*                     ono sto USD nema: zarisna u pikselima snimke, glavna tocka,
//                                     ploca kamere, oblik ravnine, put do splata
//   Scope "LoomMedia" (loom:media)    snimke projekta - nisu dio scene, pa ih drugi alati preskoce
//   metapodaci sloja                  raspon timelinea i fps
//
// OBJEKTIV SE PISE DVAPUT. USD kamera zna zarisnu i otvor u milimetrima; solve zna piksele. Za
// druge alate se pise USD-ov oblik (otvor 36 mm po dogovoru, kao u Engineovom izvozu), a za
// povratak u Loom pikseli tocno, u loom: atributima - preracun kroz milimetre bi pri svakom
// spremanju pojeo koju znamenku.
//
// BROJEVI SE PISU S DEVET ZNAMENKI, sto je dovoljno da se float procita natrag BIT PO BIT isti.
// Spremljen pa otvoren projekt mora biti ista scena, ne "gotovo ista" - kocka koja se pri svakom
// spremanju pomakne za tisucinku je nakon mjesec dana negdje drugdje
//=============================================================================================

bool saveProject(const Stage& stage, const std::string& path, std::string& error);

//Zamjenjuje stage onim sto je u datoteci. Pri gresci stage ostaje netaknut
bool loadProject(const std::string& path, Stage& stage, std::string& error);

//Je li datoteka Loomov projekt (zapisan saveProject-om), bez citanja cijele
bool isProjectFile(const std::string& path);

}
