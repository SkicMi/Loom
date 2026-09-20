#include "Engine/ResidualField.h"

#include <algorithm>
#include <cmath>

namespace Engine{
namespace{

struct Accumulator{
    glm::dvec2 sum{0.0};
    glm::dvec2 squared{0.0};
    uint32_t samples = 0;
};

double lengthSquared(const glm::vec2& value){
    return double(value.x) * double(value.x) + double(value.y) * double(value.y);
}

}

const char* residualDiagnosisName(ResidualDiagnosis diagnosis){
    switch(diagnosis){
        case ResidualDiagnosis::White:            return "bijelo";
        case ResidualDiagnosis::Static:           return "staticko polje";
        case ResidualDiagnosis::Changing:         return "polje se mijenja po kadru";
        case ResidualDiagnosis::InsufficientData: return "nedovoljno podataka";
    }
    return "nepoznato";
}

ResidualFieldResult analyzeResidualField(const std::vector<Observation>& observations,
                                         const std::vector<Pose>& poses,
                                         const std::vector<glm::vec3>& points,
                                         const Intrinsics& intrinsics,
                                         const ResidualFieldConfig& config,
                                         const std::vector<uint8_t>& observationUsed){
    ResidualFieldResult result;
    if(config.columns == 0 || config.rows == 0 || config.minSamplesPerCell < 2 ||
       intrinsics.width == 0 || intrinsics.height == 0 || poses.empty()){
        return result;
    }

    const size_t cellsPerFrame = size_t(config.columns) * config.rows;
    result.frames.resize(poses.size());
    for(ResidualFrame& frame : result.frames) frame.cells.resize(cellsPerFrame);
    std::vector<Accumulator> sums(poses.size() * cellsPerFrame);

    for(size_t i = 0; i < observations.size(); ++i){
        if(!observationUsed.empty() && (i >= observationUsed.size() || !observationUsed[i])) continue;
        const Observation& observation = observations[i];
        if(observation.camera >= poses.size() || observation.point >= points.size()) continue;
        if(observation.pixel.x < 0.0f || observation.pixel.y < 0.0f ||
           observation.pixel.x >= float(intrinsics.width) ||
           observation.pixel.y >= float(intrinsics.height)) continue;

        glm::vec2 predicted;
        if(!project(poses[observation.camera], intrinsics, points[observation.point], predicted)) continue;

        const uint32_t column = std::min(config.columns - 1,
            uint32_t(observation.pixel.x * float(config.columns) / float(intrinsics.width)));
        const uint32_t row = std::min(config.rows - 1,
            uint32_t(observation.pixel.y * float(config.rows) / float(intrinsics.height)));
        Accumulator& cell = sums[size_t(observation.camera) * cellsPerFrame +
                                 size_t(row) * config.columns + column];
        const glm::dvec2 residual = glm::dvec2(predicted) - glm::dvec2(observation.pixel);
        cell.sum += residual;
        cell.squared += residual * residual;
        ++cell.samples;
    }

    for(size_t camera = 0; camera < poses.size(); ++camera){
        ResidualFrame& frame = result.frames[camera];
        glm::dvec2 frameSum(0.0);
        uint64_t frameSamples = 0;

        for(size_t cellIndex = 0; cellIndex < cellsPerFrame; ++cellIndex){
            const Accumulator& source = sums[camera * cellsPerFrame + cellIndex];
            ResidualCell& cell = frame.cells[cellIndex];
            cell.samples = source.samples;
            if(source.samples < config.minSamplesPerCell) continue;

            const glm::dvec2 mean = source.sum / double(source.samples);
            const glm::dvec2 variance = (source.squared - double(source.samples) * mean * mean) /
                                        double(source.samples - 1);
            cell.mean = glm::vec2(mean);
            cell.meanNoise = std::sqrt(std::max(0.0, variance.x + variance.y) /
                                       double(source.samples));
            cell.valid = true;
            ++frame.validCells;
            frameSum += double(source.samples) * mean;
            frameSamples += source.samples;
        }

        if(frame.validCells < config.minCellsPerFrame || frameSamples == 0) continue;
        frame.valid = true;
        ++result.evaluatedFrames;
        frame.mean = glm::vec2(frameSum / double(frameSamples));

        double frameEnergy = 0.0;
        for(ResidualCell& cell : frame.cells){
            if(!cell.valid) continue;
            cell.centredMean = cell.mean - frame.mean;
            frameEnergy += lengthSquared(cell.centredMean);
            ++result.evaluatedCells;
        }
        frame.spatialRms = std::sqrt(frameEnergy / double(frame.validCells));
    }

    if(result.evaluatedFrames < config.minFrames || result.evaluatedCells == 0) return result;

    double spatialEnergy = 0.0;
    double noiseEnergy = 0.0;
    double noiseFourth = 0.0;
    for(const ResidualFrame& frame : result.frames){
        if(!frame.valid) continue;
        for(const ResidualCell& cell : frame.cells){
            if(!cell.valid) continue;
            spatialEnergy += lengthSquared(cell.centredMean);
            noiseEnergy += cell.meanNoise * cell.meanNoise;
            noiseFourth += cell.meanNoise * cell.meanNoise * cell.meanNoise * cell.meanNoise;
        }
    }
    result.spatialRms = std::sqrt(spatialEnergy / double(result.evaluatedCells));
    result.meanNoiseRms = std::sqrt(noiseEnergy / double(result.evaluatedCells));
    const double spatialExcessEnergy = std::max(0.0,
        result.spatialRms * result.spatialRms - result.meanNoiseRms * result.meanNoiseRms);
    result.spatialSignalRms = std::sqrt(spatialExcessEnergy);
    const double spatialEnergyError = std::sqrt(2.0 * noiseFourth) /
                                      double(result.evaluatedCells);

    double staticEnergy = 0.0;
    double temporalEnergy = 0.0;
    uint32_t staticCells = 0;
    uint32_t temporalSamples = 0;
    for(size_t cellIndex = 0; cellIndex < cellsPerFrame; ++cellIndex){
        glm::dvec2 mean(0.0);
        uint32_t frames = 0;
        for(const ResidualFrame& frame : result.frames){
            if(!frame.valid || !frame.cells[cellIndex].valid) continue;
            mean += glm::dvec2(frame.cells[cellIndex].centredMean);
            ++frames;
        }
        if(frames < config.minFrames) continue;
        mean /= double(frames);
        staticEnergy += glm::dot(mean, mean);
        ++staticCells;

        for(const ResidualFrame& frame : result.frames){
            if(!frame.valid || !frame.cells[cellIndex].valid) continue;
            const glm::dvec2 difference = glm::dvec2(frame.cells[cellIndex].centredMean) - mean;
            temporalEnergy += glm::dot(difference, difference);
            ++temporalSamples;
        }
    }

    if(staticCells == 0 || temporalSamples == 0) return result;
    result.staticRms = std::sqrt(staticEnergy / double(staticCells));
    result.temporalRms = std::sqrt(temporalEnergy / double(temporalSamples));
    result.decisionThreshold = config.minimumMagnitudePixels;

    //Varijanca sredine celije vec je izmjerena iz opazanja u njoj. Ona se ODUZIMA od energije
    //polja, umjesto da se njezin RMS mnozi pragom kao da odluku donosi jedna celija. Dokaz se
    //skuplja preko svih celija: standardna pogreska srednje energije pada s njihovim brojem.
    const double temporalNoiseEnergy = result.meanNoiseRms * result.meanNoiseRms *
        (1.0 - double(staticCells) / double(temporalSamples));
    const double temporalExcessEnergy = std::max(0.0,
        result.temporalRms * result.temporalRms - temporalNoiseEnergy);
    result.temporalSignalRms = std::sqrt(temporalExcessEnergy);
    const double temporalEnergyError = std::sqrt(2.0 / double(temporalSamples)) *
                                       temporalNoiseEnergy;

    const bool spatialSignificant = spatialExcessEnergy > config.significance * spatialEnergyError;
    const bool temporalSignificant = temporalExcessEnergy > config.significance * temporalEnergyError;

    if(result.spatialSignalRms <= result.decisionThreshold || !spatialSignificant){
        result.diagnosis = ResidualDiagnosis::White;
    }else if(result.temporalSignalRms <= result.decisionThreshold || !temporalSignificant){
        result.diagnosis = ResidualDiagnosis::Static;
    }else{
        result.diagnosis = ResidualDiagnosis::Changing;
    }
    return result;
}

}
