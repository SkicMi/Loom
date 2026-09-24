// Metapodaci kamere iz snimke (Spool/CameraMetadata.h).
//
// ZASTO SE OVO TESTIRA. Oznake Sonyjevog 'rtmd' traga nisu iz specifikacije nego izmjerene na
// jednoj kameri, pa test drzi ono sto se zna: snimka bez traga daje present == false (i ne
// baca), a kad je prava Sony snimka pri ruci (LOOM_SONY_CLIP), procita se ono sto kamera kaze.
#include "TestHarness.h"

#include <Spool/CameraMetadata.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>

int main(){
    TestReport report("M1 metapodaci kamere");

    const Spool::CameraMetadata none = Spool::readCameraMetadata("/nema/takve/snimke.mp4");
    report.check("snimka koje nema: nista, bez iznimke", !none.present && none.frames == 0, "prazno");

    const char* clip = std::getenv("LOOM_SONY_CLIP");
    if(clip && std::filesystem::exists(clip)){
        const Spool::CameraMetadata m = Spool::readCameraMetadata(clip);
        double mean[3] = {0, 0, 0};
        for(const auto& g : m.gyro) for(int a = 0; a < 3; ++a) mean[a] += double(g[size_t(a)]);
        report.check("Sony snimka: fps, ekspozicija, citanje, ziroskop",
            m.present && std::fabs(m.framesPerSecond - 50.0) < 0.01 && m.exposureSeconds > 0.0 &&
            m.readoutFrames > 0.3 && m.readoutFrames < 1.0 && m.gyroRate == 2000 &&
            m.gyro.size() == size_t(m.frames) * 40 && m.isoPerFrame.size() == m.frames,
            fmt("%u kadrova @ %.2f fps, ekspozicija 1/%.0f s, citanje %.3f kadra, ziroskop %u Hz (%zu uzoraka, %.1f jed. po st/s)",
                m.frames, m.framesPerSecond, m.exposureSeconds > 0 ? 1.0 / m.exposureSeconds : 0.0, m.readoutFrames,
                m.gyroRate, m.gyro.size(), double(m.gyroUnitsPerDegreePerSecond)));
    }
    return report.result();
}
