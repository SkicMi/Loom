// Izvoz i uvoz moraju biti isto preslikavanje u dva smjera.
//
// Konvencija je jedino sto ovdje moze TIHO promasiti. COLMAP rotaciju i pomak vodi iz svijeta u
// kameru, u klasicnoj konvenciji (+Z naprijed, +Y dolje); nasa Pose je obrnuta (kamera u svijet,
// -Z naprijed), a nas fy je negativan. Svaka od tih triju razlika je prilika da se predznak izgubi
// - i nijedna se ne vidi kao greska, nego kao rekonstrukcija koja je "nekako kriva".
//
// Zato se ovdje ne provjerava racun nego KRUG: poznata rekonstrukcija se izveze, procita natrag i
// mora se vratiti ista. Ako se predznak izgubi u jednom smjeru, krug se ne zatvori; ako se izgubi
// u OBA smjera jednako, zatvorio bi se lazno - pa se zato posebno provjerava i da procitane poze
// projiciraju opazanja na ista mjesta.
//
// Sto se brani:
//
//   poze          polozaj i orijentacija se vrate isti kroz izvoz pa uvoz
//   intrinsike    zarista i glavna tocka prezive, ukljucujuci nas negativan fy
//   tocke         3D polozaji se vrate isti
//   projekcija    procitani model projicira opazanja tamo gdje su i bila - provjera koja ne ovisi
//                 o tome je li ista greska napravljena dvaput
#include "TestHarness.h"

#include <Engine/ColmapExport.h>
#include <Engine/ColmapImport.h>
#include <Engine/SyntheticScene.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

int main(){
    TestReport report("COLMAP izvoz i uvoz zatvaraju krug");

    // Poznata scena: kamere po luku, tocke razasute
    Engine::SyntheticConfig sceneConfig;
    sceneConfig.cameraCount = 8;
    sceneConfig.pointCount = 120;
    const Engine::SyntheticScene scene = Engine::makeSyntheticScene(sceneConfig);

    Engine::Reconstruction original;
    original.poses = scene.poses;
    original.posed.assign(scene.poses.size(), 1);
    original.points = scene.points;
    original.solved.assign(scene.points.size(), 1);
    original.posedCameras = uint32_t(scene.poses.size());
    original.solvedPoints = uint32_t(scene.points.size());
    original.ok = true;

    const std::string directory = "colmap_roundtrip";
    std::string command = "rm -rf " + directory + " && mkdir -p " + directory;
    if(std::system(command.c_str()) != 0){
        std::printf("   FAIL priprema mape nije uspjela\n");
        return 1;
    }

    const bool written = Engine::writeColmapText(directory, original, scene.intrinsics, scene.observations);

    Engine::ColmapModel back;
    const bool read = written && Engine::readColmapText(directory, back);

    report.check("zapis i citanje uspiju", written && read,
        fmt("zapisano %s, procitano %s", written ? "da" : "ne", read ? "da" : "ne"));

    if(!read){
        return report.result();
    }

    report.check("broj kamera i tocaka se poklapa",
        back.reconstruction.poses.size() == original.poses.size() &&
        back.reconstruction.points.size() == original.points.size(),
        fmt("%zu kamera (bilo %zu), %zu tocaka (bilo %zu)",
            back.reconstruction.poses.size(), original.poses.size(),
            back.reconstruction.points.size(), original.points.size()));

    //Prag je u jedinicama scene: zapis ide kroz tekst, pa se gubi na znamenkama a ne na racunu
    double worstPosition = 0.0, worstAngle = 0.0;
    const size_t cameras = std::min(back.reconstruction.poses.size(), original.poses.size());
    for(size_t camera = 0; camera < cameras; ++camera){
        const Engine::Pose& was = original.poses[camera];
        const Engine::Pose& now = back.reconstruction.poses[camera];
        worstPosition = std::max(worstPosition, double(glm::length(now.position - was.position)));

        const double dot = std::fabs(double(glm::dot(glm::normalize(now.orientation), glm::normalize(was.orientation))));
        worstAngle = std::max(worstAngle, glm::degrees(2.0 * std::acos(std::min(1.0, dot))));
    }

    report.check("poze prezive krug",
        worstPosition < 1e-3 && worstAngle < 1e-2,
        fmt("najgori polozaj %.2e, najgori kut %.2e st", worstPosition, worstAngle));

    double worstPoint = 0.0;
    const size_t points = std::min(back.reconstruction.points.size(), original.points.size());
    for(size_t point = 0; point < points; ++point){
        worstPoint = std::max(worstPoint, double(glm::length(back.reconstruction.points[point] - original.points[point])));
    }
    report.check("tocke prezive krug", worstPoint < 1e-3,
        fmt("najgora razlika %.2e", worstPoint));

    //Nas fy je negativan; COLMAP-ov je pozitivan. Ako se predznak izgubi, ovo je jedino mjesto
    //gdje se to vidi kao broj, a ne kao cudna rekonstrukcija
    report.check("intrinsike prezive krug, ukljucujuci predznak fy",
        std::fabs(double(back.intrinsics.fx - scene.intrinsics.fx)) < 1e-2 &&
        std::fabs(double(back.intrinsics.fy - scene.intrinsics.fy)) < 1e-2 &&
        std::fabs(double(back.intrinsics.cx - scene.intrinsics.cx)) < 1e-2 &&
        std::fabs(double(back.intrinsics.cy - scene.intrinsics.cy)) < 1e-2,
        fmt("fx %.2f (bilo %.2f), fy %.2f (bilo %.2f)",
            double(back.intrinsics.fx), double(scene.intrinsics.fx),
            double(back.intrinsics.fy), double(scene.intrinsics.fy)));

    //NEZAVISNA PROVJERA: ista greska napravljena u oba smjera bi gornje testove prosla. Ovo ne -
    //ovdje se procitani model suoci s opazanjima koja nikad nisu prosla kroz pretvorbu
    report.check("procitani model projicira opazanja na njihova mjesta",
        back.reconstruction.medianReprojection < 0.01,
        fmt("medijan reprojekcije %.4f px kroz %zu opazanja",
            back.reconstruction.medianReprojection, back.observations.size()));

    std::system(("rm -rf " + directory).c_str());
    return report.result();
}
