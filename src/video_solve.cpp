// Camera solve na PRAVOJ snimci: video -> tragovi -> poze i tocke -> COLMAP.
//
//   ./VideoSolve snimka.mp4 [korak] [kadrova] [vidno polje u stupnjevima] [izlazna mapa]
//
// Sve dosad je mjereno protiv poznate istine. Ovdje istine nema, pa ostaju dvije mjere koje se
// same brane: REPROJEKCIJA (koliko dobro rjesenje objasnjava opazanja) i BROJ RIJESENIH KAMERA.
// Nijedna ne kaze da je rekonstrukcija tocna - kazu da je konzistentna, i to je sve sto se bez
// istine moze tvrditi.
//
// VIDNO POLJE SE NE ZNA. Snimka ne nosi zarisnu duljinu, a kriva zarisna se trguje s geometrijom -
// izmjereno u S9: rjesenje pobjegne od istine i pritom SMANJI reprojekciju. Zato se, kad se vidno
// polje ne zada, proba nekoliko vrijednosti i uzme ona s najmanjom reprojekcijom. To je gruba
// samokalibracija: mjerljiva, ali ne i dokaz - ravna scena zna dati nisku reprojekciju uz krivu
// zarisnu.
#include <Spool/VideoFile.h>

#include <Engine/CameraHints.h>
#include <Engine/ColmapExport.h>
#include <Engine/Reconstruct.h>
#include <Engine/Track.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace{

std::vector<uint8_t> toGray(const Spool::Image& image){
    std::vector<uint8_t> gray(size_t(image.width) * image.height);
    for(size_t i = 0; i < gray.size(); ++i){
        gray[i] = uint8_t(0.299f * float(image.pixels[i * 4 + 0]) +
                          0.587f * float(image.pixels[i * 4 + 1]) +
                          0.114f * float(image.pixels[i * 4 + 2]));
    }
    return gray;
}

}

int main(int argc, char** argv){
    if(argc < 2){
        std::printf("Upotreba: VideoSolve snimka.mp4 [korak] [kadrova] [vidno polje] [izlazna mapa]\n");
        return 1;
    }

    const std::string path = argv[1];
    const uint32_t step = argc > 2 ? uint32_t(std::atoi(argv[2])) : 5;
    const uint32_t wanted = argc > 3 ? uint32_t(std::atoi(argv[3])) : 24;
    const double fieldOfView = argc > 4 ? std::atof(argv[4]) : 0.0;
    const std::string outputDirectory = argc > 5 ? std::string(argv[5]) : std::string();

    Spool::VideoReader reader(path);
    const Spool::VideoInfo& info = reader.info();
    std::printf("Snimka %ux%u, %.2f fps, %s\n", info.width, info.height, info.frameRate(), info.codec.c_str());
    std::printf("  uzimam svaki %u. kadar, najvise %u\n", step, wanted);

    // -------------------------------------------------------------------------------
    // Kadrovi -> tragovi
    // -------------------------------------------------------------------------------

    Engine::TrackConfig trackConfig;
    trackConfig.window = 6;
    trackConfig.minDistance = 14.0f;
    trackConfig.maxCorners = 800;
    trackConfig.minTracks = 400;
    Engine::Tracker tracker(trackConfig);

    const auto started = std::chrono::steady_clock::now();
    uint32_t used = 0;
    uint32_t index = 0;

    while(!reader.atEnd() && used < wanted){
        const Spool::Image frame = reader.readNext();
        if(frame.pixels.empty()) break;
        if(index++ % step != 0) continue;

        const std::vector<uint8_t> gray = toGray(frame);
        tracker.addFrame(Engine::GrayImage{gray.data(), frame.width, frame.height, frame.width});
        ++used;
        std::printf("\r  kadar %u, tragova zivo %u  ", used, tracker.activeTracks());
        std::fflush(stdout);
    }
    std::printf("\n  %u kadrova, %u tragova, %zu opazanja, %.1f s\n", used, tracker.trackCount(),
                tracker.observations().size(),
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());

    if(used < 3){
        std::printf("Premalo kadrova.\n");
        return 1;
    }

    //Koliko se dugo tragovi drze - kratki tragovi znace da rekonstrukcija nema sto povezati
    std::vector<uint32_t> length(tracker.trackCount(), 0);
    for(const Engine::Observation& observation : tracker.observations()) ++length[observation.point];
    std::vector<uint32_t> sorted = length;
    std::sort(sorted.begin(), sorted.end());
    std::printf("  duljina traga: medijan %u kadrova, najdulji %u\n",
                sorted[sorted.size() / 2], sorted.back());

    // -------------------------------------------------------------------------------
    // Rekonstrukcija, uz probanje vidnog polja kad nije zadano
    // -------------------------------------------------------------------------------

    auto solveWith = [&](double fov){
        Engine::Intrinsics intrinsics;
        intrinsics.width = info.width;
        intrinsics.height = info.height;
        intrinsics.cx = 0.5f * float(info.width);
        intrinsics.cy = 0.5f * float(info.height);
        //Vodoravno vidno polje: fx = (sirina/2) / tan(fov/2)
        const double focal = (0.5 * double(info.width)) / std::tan(0.5 * fov * 3.14159265358979 / 180.0);
        intrinsics.fx = float(focal);
        intrinsics.fy = float(focal);

        Engine::ReconstructConfig config;
        config.huberPixels = 2.0;
        config.acceptPixels = 6.0;
        config.minPointsForPose = 20;
        return std::make_pair(Engine::reconstruct(tracker.observations(), used, tracker.trackCount(),
                                                  intrinsics, config), intrinsics);
    };

    //STO SNIMKA KAZE O SEBI. Kad je kamera prepoznata, ne pogadja se od nule nego se provjeri uski
    //pojas oko onoga sto pise - a kad nije, sirok raspon i uz to jasno receno da je to pogadjanje
    Engine::SourceFacts facts;
    facts.width = info.width;
    facts.height = info.height;
    facts.frameRate = info.frameRate();
    facts.rotation = info.rotation;
    facts.pixelAspect = info.pixelAspect;
    facts.codec = info.codec;
    for(const auto& entry : info.metadata) facts.metadata.push_back(Engine::MetadataEntry{entry.first, entry.second});
    for(const Spool::VideoInfo::StreamNote& stream : info.streams){
        facts.streams.push_back(stream.kind + " " + stream.codec + " (" + stream.handler + ")");
    }
    const Engine::CameraHints hints = Engine::hintsFrom(facts);

    std::printf("  kamera: %s%s, vidno polje %.1f st (%s)\n",
                hints.make.empty() ? "nepoznata" : hints.make.c_str(),
                hints.model.empty() ? "" : (" " + hints.model).c_str(),
                hints.horizontalFieldOfView, Engine::sourceName(hints.focalSource));
    if(hints.hasTelemetry) std::printf("  telemetrija postoji (%s) - jos je ne citamo\n", hints.telemetryNote.c_str());

    std::vector<double> candidates;
    if(fieldOfView > 0.0){
        candidates.push_back(fieldOfView);
    }else if(hints.focalSource == Engine::HintSource::ModelTable || hints.focalSource == Engine::HintSource::Metadata){
        const double centre = hints.horizontalFieldOfView;
        candidates = {centre * 0.85, centre * 0.93, centre, centre * 1.07, centre * 1.15};
    }else{
        candidates = {50.0, 60.0, 70.0, 78.0, 86.0, 94.0};
    }

    Engine::Reconstruction best;
    Engine::Intrinsics bestIntrinsics;
    double bestFov = 0.0;

    for(double fov : candidates){
        const auto result = solveWith(fov);
        const Engine::Reconstruction& state = result.first;
        std::printf("  vidno polje %5.1f st (f = %6.1f px): %2u/%u kamera, %4u tocaka, reprojekcija %6.3f px\n",
                    fov, double(result.second.fx), state.posedCameras, used, state.solvedPoints, state.medianReprojection);

        const bool better = state.posedCameras > best.posedCameras ||
                            (state.posedCameras == best.posedCameras && state.medianReprojection < best.medianReprojection);
        if(!best.ok || better){
            best = state;
            bestIntrinsics = result.second;
            bestFov = fov;
        }
    }

    std::printf("\nNajbolje: vidno polje %.1f st, %u od %u kamera, %u tocaka, reprojekcija %.3f px\n",
                bestFov, best.posedCameras, used, best.solvedPoints, best.medianReprojection);

    //DVIJE PROVJERE KOJE RADE BEZ POZNATE ISTINE.
    //
    //1. Putanja drona je glatka. Rjesenje koje je promasilo obicno trza - kut izmedju uzastopnih
    //   koraka skace i brzina se mijenja iz kadra u kadar. Ovo ne dokazuje tocnost, ali neglatka
    //   putanja je dokaz da nesto NE valja
    //2. Oblik rjesenja ne smije ovisiti o pretpostavljenoj zarisnoj. Ako se s drugom zarisnom
    //   dobije druga putanja, onda snimka ne odredjuje geometriju i nijedan broj gore ne vrijedi
    auto pathShape = [&](const Engine::Reconstruction& state){
        std::vector<glm::vec3> centres;
        for(size_t i = 0; i < state.poses.size(); ++i) if(state.posed[i]) centres.push_back(state.poses[i].position);

        std::vector<double> angles, speeds;
        for(size_t i = 2; i < centres.size(); ++i){
            const glm::vec3 a = centres[i - 1] - centres[i - 2];
            const glm::vec3 b = centres[i] - centres[i - 1];
            const double lengthA = double(glm::length(a)), lengthB = double(glm::length(b));
            if(lengthA <= 0.0 || lengthB <= 0.0) continue;
            angles.push_back(glm::degrees(std::acos(std::max(-1.0, std::min(1.0,
                double(glm::dot(a, b)) / (lengthA * lengthB))))));
            speeds.push_back(lengthB / lengthA);
        }
        std::sort(angles.begin(), angles.end());
        std::sort(speeds.begin(), speeds.end());
        return std::make_pair(angles.empty() ? 0.0 : angles[angles.size() / 2],
                              speeds.empty() ? 0.0 : speeds[speeds.size() / 2]);
    };

    const auto shape = pathShape(best);
    std::printf("  putanja: medijan skretanja %.2f st po kadru, omjer brzina %.3f (glatko = malo i oko 1)\n",
                shape.first, shape.second);

    //Isto rjesenje s zarisnom +-25 %: koliko se promijeni SMJER putanje
    for(double factor : {0.75, 1.25}){
        const auto other = solveWith(bestFov * factor > 120.0 ? 120.0 : bestFov * factor);
        const auto otherShape = pathShape(other.first);
        std::printf("  zarisna x%.2f: %u kamera, reprojekcija %.3f px, skretanje %.2f st\n",
                    factor, other.first.posedCameras, other.first.medianReprojection, otherShape.first);
    }

    if(!outputDirectory.empty() && best.ok){
        std::filesystem::create_directories(outputDirectory);
        if(Engine::writeColmapText(outputDirectory, best, bestIntrinsics, tracker.observations())){
            std::printf("Zapisano u %s (cameras.txt, images.txt, points3D.txt)\n", outputDirectory.c_str());
        }
    }
    return 0;
}
