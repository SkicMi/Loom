#pragma once
#include "Engine/Bundle.h"
#include "Engine/Reconstruct.h"
#include "Engine/SyntheticScene.h"

namespace Engine{

enum class ViewGraphCalibrationStatus{
    Determined,
    InsufficientData,
    DegenerateGeometry,
    FlatObjective
};

const char* viewGraphCalibrationStatusName(ViewGraphCalibrationStatus status);

struct ViewGraphCalibrationConfig{
    uint32_t minimumSharedPoints = 30;
    uint32_t minimumPairs = 3;
    double minimumFocalInImageWidths = 0.15;
    double maximumFocalInImageWidths = 5.0;
    double maximumPairFocalDisagreement = 0.25;
    double maximumRelativeMad = 0.15;
    double minimumHomographyResidualPixels = 0.05;
    double minimumBougnouxScale = 1e-4;
    uint32_t radialSamples = 31;
    double maximumAbsoluteImageRadial = 0.3;
};

struct ViewGraphCalibrationResult{
    double focalPixels = 0.0;
    double k1 = 0.0;
    double medianErrorPixels = 0.0;
    uint32_t usedPairs = 0;
    ViewGraphCalibrationStatus status = ViewGraphCalibrationStatus::InsufficientData;
    bool determined = false;
};

//Zajednicko zariste samo iz 2D veza kroz vise pogleda. Ne prima ni poze ni 3D tocke: za svaki
//kandidat procijeni relativne poze, triangulira veze i mjeri njihovu reprojekciju. templateCamera
//daje velicinu slike i glavnu tocku; njegovo fx/fy se namjerno ne koristi kao odgovor.
ViewGraphCalibrationResult estimateViewGraphFocal(const std::vector<Observation>& observations,
                                                  uint32_t cameraCount,
                                                  uint32_t pointCount,
                                                  const Intrinsics& templateCamera,
                                                  const ViewGraphCalibrationConfig& config = {});

enum class SelfCalibrationStatus{
    Determined,
    InsufficientData,
    DegenerateGeometry,
    IllConditioned,
    OutOfRange
};

const char* selfCalibrationStatusName(SelfCalibrationStatus status);

struct SelfCalibrationConfig{
    uint32_t minimumObservations = 30;
    uint32_t minimumCameras = 3;
    double minimumBaselineToDepth = 0.005;
    double huberPixels = 3.0;
    uint32_t robustIterations = 8;
    double minimumFocalInImageWidths = 0.15;
    double maximumFocalInImageWidths = 5.0;
    double maximumAbsoluteK1 = 0.5;
};

struct SelfCalibrationResult{
    Intrinsics intrinsics{};
    SelfCalibrationStatus status = SelfCalibrationStatus::InsufficientData;
    bool determined = false;
    uint32_t usedObservations = 0;
    double baselineToDepth = 0.0;
    double startRms = 0.0;
    double endRms = 0.0;
};

//Dotjeruje zajednicko zariste i prvi radijalni koeficijent nad svim kadrovima. Glavna tocka
//ostaje u sredistu koje je dano, a pikseli se NE smiju unaprijed undistortati: k1 se procjenjuje
//iz sirovih mjerenja. Poze i tocke moraju vec opisivati istu geometriju; zajednicko dotjerivanje
//njih i intrinzika radi visi sloj rekonstrukcije.
SelfCalibrationResult refineSharedIntrinsics(const std::vector<Observation>& observations,
                                             const std::vector<Pose>& poses,
                                             const std::vector<glm::vec3>& points,
                                             const Intrinsics& initial,
                                             const SelfCalibrationConfig& config = {});

struct JointSelfCalibrationConfig{
    ViewGraphCalibrationConfig viewGraph{};
    SelfCalibrationConfig intrinsics{};
    BundleConfig bundle{};
    uint32_t maxIterations = 8;
    double updateFraction = 0.6;
    double focalConvergence = 1e-4;
};

struct JointSelfCalibrationResult{
    Intrinsics intrinsics{};
    std::vector<Pose> poses;
    std::vector<glm::vec3> points;
    SelfCalibrationStatus status = SelfCalibrationStatus::InsufficientData;
    bool determined = false;
    uint32_t iterations = 0;
    double graphFocalPixels = 0.0;
    double startRms = 0.0;
    double endRms = 0.0;
};

JointSelfCalibrationResult selfCalibrateBundle(const std::vector<Observation>& rawObservations,
                                               const std::vector<Pose>& poses,
                                               const std::vector<glm::vec3>& points,
                                               const Intrinsics& initial,
                                               const JointSelfCalibrationConfig& config = {});

//Cijeli genericki put koji koristi VideoSolve kada kamera nema poznatu kalibraciju. Ulaz su sirova
//2D opazanja; izlaz cuva oba oblika intrinzika namjerno:
//  measuredIntrinsics - fizicka leca, ukljucujuci procijenjeni k1
//  flatIntrinsics     - pinhole kamera za ispravljena opazanja i ispravljene izlazne slike
//Razdvajanje sprjecava najopasniju tihu gresku: zakrivljene slike uz PINHOLE cameras.txt.
struct SelfCalibratedReconstruction{
    Reconstruction reconstruction;
    Intrinsics measuredIntrinsics{};
    Intrinsics flatIntrinsics{};
    std::vector<Observation> flatObservations;
    ViewGraphCalibrationStatus graphStatus = ViewGraphCalibrationStatus::InsufficientData;
    SelfCalibrationStatus status = SelfCalibrationStatus::InsufficientData;
    double graphFocalPixels = 0.0;
    bool determined = false;
    //Samo za ispis: koliko je trajao koji korak
    double graphSeconds = 0.0, reconstructSeconds = 0.0, bundleSeconds = 0.0;
};

SelfCalibratedReconstruction reconstructSelfCalibrated(
    const std::vector<Observation>& rawObservations,
    uint32_t cameraCount,
    uint32_t pointCount,
    const Intrinsics& templateCamera,
    const ReconstructConfig& reconstructConfig = {},
    const JointSelfCalibrationConfig& calibrationConfig = {});

}
