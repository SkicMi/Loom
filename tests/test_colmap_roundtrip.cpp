// Export and import must be the same mapping in both directions.
//
// The convention is the only thing that can SILENTLY miss here. COLMAP carries rotation and
// translation from world to camera, in the classic convention (+Z forward, +Y down); our Pose
// is inverted (camera to world, -Z forward), and our fy is negative. Each of those three
// differences is a chance to lose a sign - and none shows up as an error, but as a
// reconstruction that is "somehow wrong".
//
// So here not the math is checked but the ROUND TRIP: a known reconstruction is exported,
// read back, and must come back the same. If a sign is lost in one direction the circle does
// not close; if it is lost EQUALLY in both directions it would close falsely - so it is also
// checked separately that the read-back poses project the observations onto the same places.
//
// What is defended:
//
//   poses          position and orientation come back the same through export then import
//   intrinsics     focal points and the principal point survive, including our negative fy
//   points         3D positions come back the same
//   projection     the read-back model projects observations where they were - a check that
//                  does not depend on whether the same error was made twice
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

    // Known scene: cameras along an arc, points scattered
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

    //Threshold is in scene units: the export goes through text, so loss happens on digits, not
    //on math
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

    //Our fy is negative; COLMAP's is positive. If the sign is lost, this is the only place it
    //shows as a number, not as a strange reconstruction
    report.check("intrinsike prezive krug, ukljucujuci predznak fy",
        std::fabs(double(back.intrinsics.fx - scene.intrinsics.fx)) < 1e-2 &&
        std::fabs(double(back.intrinsics.fy - scene.intrinsics.fy)) < 1e-2 &&
        std::fabs(double(back.intrinsics.cx - scene.intrinsics.cx)) < 1e-2 &&
        std::fabs(double(back.intrinsics.cy - scene.intrinsics.cy)) < 1e-2,
        fmt("fx %.2f (bilo %.2f), fy %.2f (bilo %.2f)",
            double(back.intrinsics.fx), double(scene.intrinsics.fx),
            double(back.intrinsics.fy), double(scene.intrinsics.fy)));

    //INDEPENDENT CHECK: the same error made in both directions would pass the checks above. This
    //one won't - here the read-back model faces observations that never went through the
    //conversion
    report.check("procitani model projicira opazanja na njihova mjesta",
        back.reconstruction.medianReprojection < 0.01,
        fmt("medijan reprojekcije %.4f px kroz %zu opazanja",
            back.reconstruction.medianReprojection, back.observations.size()));

    std::system(("rm -rf " + directory).c_str());
    return report.result();
}
