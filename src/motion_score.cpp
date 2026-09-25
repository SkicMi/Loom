//=============================================================================================
// MotionScore: ocjena BVH pokreta bez gledanja (Engine/MotionQuality.h).
//
//   MotionScore a.bvh b.bvh ...      jedan redak po datoteci, najbolja oznacena zvjezdicom
//   MotionScore mapa/                svi .bvh u mapi - varijante jednog generiranja
//
// Redak je dovoljan za dnevnik mjerenja: ista mjera koju editor koristi za odabir varijante.
//=============================================================================================
#include <Engine/MotionQuality.h>
#include <Engine/WeaverMotion.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv){
    std::vector<fs::path> files;
    for(int i = 1; i < argc; ++i){
        const fs::path path(argv[i]);
        if(fs::is_directory(path)){
            for(const auto& entry : fs::directory_iterator(path))
                if(entry.path().extension() == ".bvh") files.push_back(entry.path());
        }else files.push_back(path);
    }
    std::sort(files.begin(), files.end());
    if(files.empty()){
        std::fprintf(stderr, "upotreba: MotionScore <datoteka.bvh | mapa> ...\n");
        return 2;
    }

    std::vector<Engine::MotionQuality::Report> reports;
    for(const fs::path& file : files){
        //Zadana putanja lezi uz generiranje: motion_X.constraints.json pokraj mape motion_X/
        //(vise varijanti) ili pokraj motion_X.bvh (jedna)
        Engine::MotionQuality::Options options;
        for(const fs::path& candidate : {fs::path(file).replace_extension(".constraints.json"),
                                         file.parent_path().parent_path() / (file.parent_path().filename().string() + ".constraints.json")}){
            if(fs::is_regular_file(candidate)){ options.route = Engine::MotionQuality::readRouteConstraints(candidate.string()); break; }
        }
        Engine::WeaverMotion::Clip clip;
        std::string error;
        Engine::MotionQuality::Report report;
        if(!Engine::WeaverMotion::readKimodoBvh(file.string(), clip, error)) report.problem = error;
        else report = Engine::MotionQuality::evaluate(clip, options);
        reports.push_back(report);
    }
    const int best = Engine::MotionQuality::bestOf(reports);
    std::printf("%-44s %6s %8s %8s %8s %8s %6s %8s  %s\n", "datoteka", "ocjena", "skate", "sink", "jitter",
                "lebdi", "dodir", "ruta_cm", "najgore");
    for(size_t i = 0; i < files.size(); ++i){
        const Engine::MotionQuality::Report& r = reports[i];
        if(!r.valid){
            std::printf("%-44s  %s\n", files[i].filename().string().c_str(), r.problem.c_str());
            continue;
        }
        std::printf("%-44s %6.2f %8.2f %8.2f %8.1f %8.2f %6.2f %8.1f  %s%s\n",
                    files[i].filename().string().c_str(), double(r.score), double(r.footSkateCmPerSecond),
                    double(r.penetrationCm), double(r.jerkMetersPerSecond3), double(r.floatingCm),
                    double(r.contactShare), double(r.routeErrorCm),
                    r.worst.c_str(), int(i) == best ? "  *" : "");
    }
    return best >= 0 ? 0 : 1;
}
