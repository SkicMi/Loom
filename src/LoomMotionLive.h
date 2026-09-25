#pragma once

#include <Engine/WeaverMotion.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>

namespace Loom{

struct MotionBricksLiveSession{
    bool active = false;
    bool stopRequested = false;
    bool ready = false;
    bool footContactIK = true;
    int seed = 42;
    float rigFitScale = 1.0f;
    bool pathDriven = false;
    std::vector<glm::vec3> pathSamples;
    std::vector<bool> pathSampleValid;
    bool failed = false;
    bool complete = false;
    std::filesystem::path directory;
    std::filesystem::path controlFile;
    std::filesystem::path poseFile;
    std::filesystem::path sequenceFile;
    std::filesystem::path statusFile;
    std::filesystem::path outputFile;
    std::filesystem::path logFile;
    Warp::Id character = Warp::None;
    size_t animationIndex = 0;
    double startFrame = 1.0;
    long lastSequence = -1;
    size_t recordedFrames = 0;
    std::string message;
};

inline bool writeMotionBricksLiveControl(const MotionBricksLiveSession& session, bool stop,
                                         int frame, const std::string& style,
                                         float directionX, float directionZ, float speed,
                                         float heading, int seed){
    std::error_code error;
    std::filesystem::create_directories(session.directory, error);
    if(error) return false;
    const std::filesystem::path temporary = session.controlFile.string() + ".tmp";
    std::ofstream file(temporary, std::ios::trunc);
    if(!file) return false;
    file << std::fixed << std::setprecision(7)
         << "{\"stop\":" << (stop ? "true" : "false")
         << ",\"record\":true"
         << ",\"frame\":" << frame
         << ",\"style\":\"" << style << "\""
         << ",\"direction_x\":" << directionX
         << ",\"direction_z\":" << directionZ
         << ",\"speed\":" << speed
         << ",\"heading\":" << heading
         << ",\"seed\":" << seed << "}\n";
    file.close();
    if(!file) return false;
    std::filesystem::rename(temporary, session.controlFile, error);
    if(error){
        std::filesystem::remove(session.controlFile, error);
        error.clear();
        std::filesystem::rename(temporary, session.controlFile, error);
    }
    return !error;
}

inline void refreshMotionBricksLiveStatus(MotionBricksLiveSession& session){
    std::ifstream file(session.statusFile);
    if(!file) return;
    std::string first;
    std::getline(file, first);
    if(first.empty()) return;
    session.ready = first == "READY" || first == "DONE";
    session.complete = first == "DONE";
    session.failed = first.rfind("ERROR:", 0) == 0 || first == "ERROR";
    session.message = session.failed ? first : session.stopRequested ? "Saving recorded BVH..." :
                      first == "READY" ? "MotionBricks G1 is live." :
                      first == "DONE" ? "Final BVH saved." : first;
    std::string line;
    while(std::getline(file, line)){
        if(line.rfind("frames=", 0) == 0){
            try{ session.recordedFrames = size_t(std::stoull(line.substr(7))); }catch(...){ }
        }else if(line.rfind("output=", 0) == 0){
            session.outputFile = line.substr(7);
        }else if(session.failed && !line.empty()){
            session.message = line;
            break;
        }
    }
}

inline bool readMotionBricksLivePose(MotionBricksLiveSession& session,
                                     Engine::WeaverMotion::Clip& clip,
                                     long& sequence, std::string& problem){
    std::ifstream seqFile(session.sequenceFile);
    long next = -1;
    if(!(seqFile >> next) || next <= session.lastSequence) return false;
    if(!Engine::WeaverMotion::readKimodoBvh(session.poseFile.string(), clip, problem)) return false;
    if(clip.frames.size() < 2){ problem = "live MotionBricks pose needs its start and current frame"; return false; }
    sequence = next;
    session.lastSequence = next;
    return true;
}

} // namespace Loom
