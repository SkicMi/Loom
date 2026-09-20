// Polje ostataka: model krute kamere ne smije sakriti prostorno uredjenu gresku.
//
// Ovaj test namjerno ne prolazi kroz rekonstrukciju. Ovdje su poze, tocke i projekcija tocno
// poznate, pa se ispituje samo pitanje koje detektor mora odgovoriti:
//
//   - bijeli sum nema polje
//   - jednolik pomak cijelog kadra pripada pozi, ne izoblicenju
//   - isto polje u svakom kadru je staticki kvar (npr. leca)
//   - polje koje se mijenja po kadru je dinamicki kvar (stabilizacija / rolling shutter)
//
// Svaka celija ima 4x4 opazanja. Detektor zato ne moze proci tako da samo vidi jedan veliki
// ostatak: mora izracunati sredinu celije i odvojiti je od rasapa opazanja unutar nje.
#include "TestHarness.h"

#include <Engine/ResidualField.h>
#include <Engine/SyntheticScene.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace{

constexpr uint32_t grid = 8;
constexpr uint32_t samplesPerSide = 4;
constexpr uint32_t cameraCount = 12;
constexpr float pi = 3.14159265358979323846f;

struct Fixture{
    Engine::Intrinsics intrinsics;
    std::vector<Engine::Pose> poses;
    std::vector<glm::vec3> points;
    std::vector<Engine::Observation> observations;
};

Fixture makeFixture(){
    Fixture out;
    out.intrinsics = Engine::Intrinsics{800.0f, 800.0f, 640.0f, 360.0f, 1280, 720};
    out.poses.resize(cameraCount);

    const float cellWidth = float(out.intrinsics.width) / float(grid);
    const float cellHeight = float(out.intrinsics.height) / float(grid);
    const float depth = 5.0f;

    for(uint32_t row = 0; row < grid; ++row){
        for(uint32_t column = 0; column < grid; ++column){
            for(uint32_t sy = 0; sy < samplesPerSide; ++sy){
                for(uint32_t sx = 0; sx < samplesPerSide; ++sx){
                    const float u = (float(column) + (float(sx) + 0.5f) / float(samplesPerSide)) * cellWidth;
                    const float v = (float(row) + (float(sy) + 0.5f) / float(samplesPerSide)) * cellHeight;
                    const glm::vec3 point{
                        (u - out.intrinsics.cx) * depth / out.intrinsics.fx,
                        -(v - out.intrinsics.cy) * depth / out.intrinsics.fy,
                        -depth
                    };
                    const uint32_t pointIndex = uint32_t(out.points.size());
                    out.points.push_back(point);

                    for(uint32_t camera = 0; camera < cameraCount; ++camera){
                        out.observations.push_back(Engine::Observation{camera, pointIndex, {u, v}});
                    }
                }
            }
        }
    }
    return out;
}

glm::vec2 cellField(const Engine::Observation& observation,
                    const Engine::Intrinsics& intrinsics,
                    float phase){
    const float x = observation.pixel.x / float(intrinsics.width);
    const float y = observation.pixel.y / float(intrinsics.height);
    return {
        2.0f * std::sin(2.0f * pi * y + phase),
        2.0f * std::cos(2.0f * pi * x + 1.3f * phase)
    };
}

void addWhiteNoise(std::vector<Engine::Observation>& observations, float amplitude){
    uint32_t state = 0x5eeda11u;
    for(Engine::Observation& observation : observations){
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const float x = 2.0f * (float(state & 0xffffu) / 65535.0f) - 1.0f;
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const float y = 2.0f * (float(state & 0xffffu) / 65535.0f) - 1.0f;
        observation.pixel += amplitude * glm::vec2(x, y);
    }
}

Engine::ResidualFieldConfig detectorConfig(){
    Engine::ResidualFieldConfig config;
    config.columns = grid;
    config.rows = grid;
    config.minSamplesPerCell = 8;
    config.minCellsPerFrame = 48;
    config.minFrames = 6;
    config.minimumMagnitudePixels = 0.25;
    config.significance = 4.0;
    return config;
}

}

int main(){
    TestReport report("polje ostataka");
    const Fixture clean = makeFixture();
    const Engine::ResidualFieldConfig config = detectorConfig();

    {
        const Engine::ResidualFieldResult result = Engine::analyzeResidualField(
            {}, clean.poses, clean.points, clean.intrinsics, config);
        report.check("bez dovoljno opazanja nema dijagnoze",
            result.diagnosis == Engine::ResidualDiagnosis::InsufficientData,
            fmt("dijagnoza %s", Engine::residualDiagnosisName(result.diagnosis)));
    }

    {
        const Engine::ResidualFieldResult result = Engine::analyzeResidualField(
            clean.observations, clean.poses, clean.points, clean.intrinsics, config);
        report.check("tocna projekcija nema polje",
            result.diagnosis == Engine::ResidualDiagnosis::White &&
            result.spatialRms < 1e-4 && result.temporalRms < 1e-4,
            fmt("prostorno %.6f px, vremenski %.6f px, sum sredine %.6f px",
                result.spatialRms, result.temporalRms, result.meanNoiseRms));
        report.check("racuna svih 8x8 celija po kadru",
            result.frames.size() == cameraCount &&
            result.frames[0].cells.size() == size_t(grid * grid) &&
            result.evaluatedCells == cameraCount * grid * grid,
            fmt("%zu kadrova, %zu celija u prvom, %u vrednovanih",
                result.frames.size(), result.frames.empty() ? 0u : result.frames[0].cells.size(),
                result.evaluatedCells));
    }

    {
        Fixture noisy = clean;
        addWhiteNoise(noisy.observations, 0.8f);
        const Engine::ResidualFieldResult result = Engine::analyzeResidualField(
            noisy.observations, noisy.poses, noisy.points, noisy.intrinsics, config);
        report.check("bijeli sum ne postaje polje",
            result.diagnosis == Engine::ResidualDiagnosis::White &&
            result.meanNoiseRms > 0.05,
            fmt("dijagnoza %s, prostorno %.3f, vremenski %.3f, sum sredine %.3f px",
                Engine::residualDiagnosisName(result.diagnosis), result.spatialRms,
                result.temporalRms, result.meanNoiseRms));
    }

    {
        Fixture shifted = clean;
        for(Engine::Observation& observation : shifted.observations){
            const float phase = 0.7f * float(observation.camera);
            observation.pixel += glm::vec2(3.0f * std::sin(phase), 2.0f * std::cos(phase));
        }
        const Engine::ResidualFieldResult result = Engine::analyzeResidualField(
            shifted.observations, shifted.poses, shifted.points, shifted.intrinsics, config);
        report.check("jednolik pomak kadra nije izoblicenje",
            result.diagnosis == Engine::ResidualDiagnosis::White && result.spatialRms < 1e-4,
            fmt("dijagnoza %s, prostorno %.6f px",
                Engine::residualDiagnosisName(result.diagnosis), result.spatialRms));
    }

    {
        //Rekonstrukcija vraca masku opazanja koja su je stvarno gradila. Odbaceni promasaji ne
        //smiju se vratiti kroz dijagnostiku i glumiti kvar modela kamere.
        Fixture filtered = clean;
        const size_t cleanCount = filtered.observations.size();
        std::vector<Engine::Observation> rejected = filtered.observations;
        for(Engine::Observation& observation : rejected){
            const float phase = 0.7f * float(observation.camera);
            observation.pixel += cellField(observation, filtered.intrinsics, phase);
        }
        filtered.observations.insert(filtered.observations.end(), rejected.begin(), rejected.end());
        std::vector<uint8_t> used(filtered.observations.size(), 0);
        std::fill(used.begin(), used.begin() + cleanCount, uint8_t(1));

        const Engine::ResidualFieldResult result = Engine::analyzeResidualField(
            filtered.observations, filtered.poses, filtered.points, filtered.intrinsics,
            config, used);
        report.check("odbacena opazanja ne stvaraju lazno polje",
            result.diagnosis == Engine::ResidualDiagnosis::White &&
            result.evaluatedCells == cameraCount * grid * grid,
            fmt("dijagnoza %s iz %u celija; %zu strukturiranih promasaja je odbaceno",
                Engine::residualDiagnosisName(result.diagnosis), result.evaluatedCells,
                rejected.size()));
    }

    {
        Fixture distorted = clean;
        for(Engine::Observation& observation : distorted.observations){
            observation.pixel += cellField(observation, distorted.intrinsics, 0.0f);
        }
        addWhiteNoise(distorted.observations, 0.4f);
        const Engine::ResidualFieldResult result = Engine::analyzeResidualField(
            distorted.observations, distorted.poses, distorted.points, distorted.intrinsics, config);
        report.check("isto polje kroz kadrove je staticko",
            result.diagnosis == Engine::ResidualDiagnosis::Static &&
            result.staticRms > 1.0 && result.temporalRms < 0.25,
            fmt("dijagnoza %s, staticki %.3f, vremenski %.3f, sum sredine %.3f px",
                Engine::residualDiagnosisName(result.diagnosis), result.staticRms,
                result.temporalRms, result.meanNoiseRms));

        const Engine::ResidualCell& topLeft = result.frames[0].cells[0];
        report.check("sredina celije nosi vektor ostatka",
            topLeft.samples == samplesPerSide * samplesPerSide &&
            glm::length(topLeft.mean) > 1.0f,
            fmt("%u uzoraka, sredina (%.3f, %.3f) px",
                topLeft.samples, double(topLeft.mean.x), double(topLeft.mean.y)));
    }

    {
        Fixture stabilized = clean;
        for(Engine::Observation& observation : stabilized.observations){
            const float phase = 0.7f * float(observation.camera);
            observation.pixel += cellField(observation, stabilized.intrinsics, phase);
        }
        addWhiteNoise(stabilized.observations, 0.4f);
        const Engine::ResidualFieldResult result = Engine::analyzeResidualField(
            stabilized.observations, stabilized.poses, stabilized.points, stabilized.intrinsics, config);
        report.check("polje koje se mijenja po kadru je dinamicko",
            result.diagnosis == Engine::ResidualDiagnosis::Changing &&
            result.temporalRms > 1.0,
            fmt("dijagnoza %s, staticki %.3f, vremenski %.3f, sum sredine %.3f px",
                Engine::residualDiagnosisName(result.diagnosis), result.staticRms,
                result.temporalRms, result.meanNoiseRms));
    }

    {
        //Svaka pojedina celija ima preslab signal za pravilo "4 x sum te celije", ali isti se
        //uzorak ponavlja kroz svih 768 celija. Odluka mora skupiti taj dokaz, ne zahtijevati da
        //svaka celija sama prijedje prag. Upravo je ta greska sakrila warp od 5 px u punom
        //TruthBenchu: polje 1.087 px, sum sredine 0.301 px, pa je 4 x sum bio pogresnih 1.205 px.
        Fixture weakButCoherent = clean;
        for(Engine::Observation& observation : weakButCoherent.observations){
            const float phase = 0.7f * float(observation.camera);
            observation.pixel += 0.5f * cellField(observation, weakButCoherent.intrinsics, phase);
        }
        addWhiteNoise(weakButCoherent.observations, 1.2f);
        const Engine::ResidualFieldResult result = Engine::analyzeResidualField(
            weakButCoherent.observations, weakButCoherent.poses, weakButCoherent.points,
            weakButCoherent.intrinsics, config);
        report.check("dokaz se skuplja preko svih celija",
            result.diagnosis == Engine::ResidualDiagnosis::Changing &&
            result.temporalSignalRms > 0.7,
            fmt("dijagnoza %s, promjena %.3f px (signal %.3f), sum sredine %.3f px kroz %u celija",
                Engine::residualDiagnosisName(result.diagnosis), result.temporalRms,
                result.temporalSignalRms,
                result.meanNoiseRms, result.evaluatedCells));
    }

    return report.result();
}
