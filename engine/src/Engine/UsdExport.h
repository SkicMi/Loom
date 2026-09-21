#pragma once
#include "Engine/Reconstruct.h"

#include <string>
#include <vector>

namespace Engine{

//=============================================================================================
// Zapis rijesene kamere i oblaka tocaka u USD, za VFX alate.
//
// ZASTO USD, a ne Nukeov .chan ili FBX. Rjesenje mora izaci iz naseg lanca u alat u kojem se
// stvarno radi kompozit, i ondje postoje tri prepreke koje USD sve tri uklanja odjednom:
//
//   - EULEROV REDOSLIJED. .chan i FBX nose rotaciju kao tri kuta, a paketi se ne slazu kojim se
//     redom mnoze. Promasen redoslijed ne pada nego samo tise krivo izgleda. USD nosi PUNU
//     MATRICU, pa pitanja nema.
//   - OVISNOSTI. Alembic i FBX trazie biblioteku; USD u tekstualnom obliku je obican tekst.
//   - SPLAT I KAMERA ZAJEDNO. Od OpenUSD 26.03 splat je prvorazredni tip prima
//     (UsdVolParticleField3DGaussianSplat), pa scena i kamera stanu u istu datoteku.
//
// Nuke 17 cita USD preko GeoImporta, Houdini 21 i Blender takodjer.
//
// KONVENCIJA - ovdje je, za razliku od COLMAP-a, gotovo nema. Nasa Pose gleda niz -Z s +Y gore
// (vidi projectUnbounded), a USD kamera gleda isto tako. Nema zrcaljenja osi. Ostaje samo jedna
// stvarna razlika:
//
//   USD MATRICA JE PO RETCIMA, s vektorom retkom: v' = v * M. Nasa glm matrica je po stupcima,
//   v' = M * v. Zato se u USD pise TRANSPONIRANO: prva tri retka su osi kamere u svijetu, a
//   CETVRTI redak je polozaj kamere. Tko to zamijeni, dobije kameru koja se vrti oko ishodista
//   umjesto da stoji gdje je stajala.
//
// MJERILO JE SLOBODNO i to se ne da sakriti. Rekonstrukcija iz same snimke ne zna metre; sto je
// izaslo je tocno do jedne skale. U USD se pise metersPerUnit = 1 i time se ne tvrdi nista -
// umjetnik skalira scenu u alatu. Bas to rade i u produkciji: Framestore je na Supermanu pratio
// kameru s plate-a pa ju SKALIRAO I POMAKNUO u prostor splata, a ne obrnuto.
//
// NEPOSTAVLJENE KAMERE SE PRESKACU. Kadar koji solver nije rijesio nema vremenski uzorak, pa USD
// izmedju susjednih interpolira. To je bolje nego zapisati pozu koja ne znaci nista, ali znaci da
// izvoz moze imati manje uzoraka nego sto snimka ima kadrova - i to se vidi u samoj datoteci
//=============================================================================================

struct UsdExportConfig{
    //Kadar na kojem pocinje prva kamera i razmak medju njima, u kadrovima izvorne snimke.
    //VideoSolve uzima svaki n-ti kadar, pa je ovo bas taj korak - bez toga bi izvoz tvrdio da je
    //snimka n puta kraca nego sto jest, i sve bi se gibalo n puta prebrzo
    int firstFrame = 1;
    int frameStep = 1;
    double framesPerSecond = 25.0;

    //Sirina senzora u milimetrima, kad se zna. Tada je zarisna duljina u zapisu STVARNA zarisna
    //u milimetrima, onakva kakvu umjetnik ocekuje vidjeti (18 mm, 35 mm). Nula znaci "ne znam",
    //pa se uzima 36 mm kao dogovor i zarisna je tada samo dosljedna, ne i stvarna
    double sensorWidthMillimetres = 0.0;

    //Pise li se i oblak tocaka. Tocke su matchmove umjetniku ono na sto lijepi objekt, pa se
    //zadano pisu
    bool writePoints = true;
};

//Zapisuje jednu .usda datoteku sa kamerom kroz vrijeme i oblakom tocaka.
//colours smije biti prazan; tada tocke nemaju boju.
//Vraca false ako se datoteka ne da otvoriti ili ako nijedna kamera nije rijesena.
bool writeUsdScene(const std::string& path,
                   const Reconstruction& reconstruction,
                   const Intrinsics& intrinsics,
                   uint32_t imageWidth,
                   uint32_t imageHeight,
                   const std::vector<glm::vec3>& colours = {},
                   const UsdExportConfig& config = {});

}
