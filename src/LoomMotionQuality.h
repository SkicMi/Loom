#pragma once
//=============================================================================================
// OCJENA VARIJANTI U EDITORU: koja je od Kimodovih varijanti najbolja, bez gledanja svake.
//
// Mjera je Engine::MotionQuality (klizanje stopala, propadanje, lebdenje, trzaji, skok korijena,
// promasaj zadane putanje). Ovdje je samo ono sto editor treba oko nje:
//
//   PREDMEMORIJA   povijest se osvjezava svake dvije sekunde, a citanje i FK jednog BVH-a traje
//                  milisekunde - dvanaest takeova puta trideset kadrova u sekundi je osjetno. Ocjena
//                  se racuna jednom po datoteci i vremenu zapisa
//   PUTANJA        motion_X.constraints.json lezi uz mapu varijanti; ako postoji, ocjena zna i
//                  koliko je koja varijanta promasila zadanu rutu
//=============================================================================================
#include <Engine/MotionQuality.h>
#include <Engine/WeaverMotion.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Loom{

inline std::filesystem::path motionConstraintsFor(const std::filesystem::path& bvh){
    std::filesystem::path single = bvh;
    single.replace_extension(".constraints.json");
    if(std::filesystem::is_regular_file(single)) return single;
    const std::filesystem::path grouped = bvh.parent_path().parent_path() /
                                          (bvh.parent_path().filename().string() + ".constraints.json");
    if(std::filesystem::is_regular_file(grouped)) return grouped;
    return {};
}

inline Engine::MotionQuality::Report scoreMotionFile(const std::filesystem::path& bvh){
    Engine::MotionQuality::Report report;
    Engine::WeaverMotion::Clip clip;
    std::string error;
    if(!Engine::WeaverMotion::readKimodoBvh(bvh.string(), clip, error)){
        report.problem = error;
        return report;
    }
    Engine::MotionQuality::Options options;
    const std::filesystem::path constraints = motionConstraintsFor(bvh);
    if(!constraints.empty()) options.route = Engine::MotionQuality::readRouteConstraints(constraints.string());
    return Engine::MotionQuality::evaluate(clip, options);
}

struct MotionQualityCache{
    struct Entry{
        std::filesystem::file_time_type written{};
        Engine::MotionQuality::Report report;
    };
    std::map<std::filesystem::path, Entry> entries;

    const Engine::MotionQuality::Report& get(const std::filesystem::path& bvh){
        std::error_code error;
        const auto written = std::filesystem::last_write_time(bvh, error);
        auto found = entries.find(bvh);
        if(found == entries.end() || found->second.written != written){
            Entry entry;
            entry.written = written;
            entry.report = scoreMotionFile(bvh);
            found = entries.insert_or_assign(bvh, std::move(entry)).first;
        }
        return found->second.report;
    }
};

//Najbolja od varijanti; -1 kad nijedna nije ocijenjena (npr. NPZ bez BVH-a)
inline int bestMotionVariant(const std::vector<std::filesystem::path>& variants, MotionQualityCache& cache){
    std::vector<Engine::MotionQuality::Report> reports;
    for(const std::filesystem::path& bvh : variants) reports.push_back(cache.get(bvh));
    return Engine::MotionQuality::bestOf(reports);
}

}
