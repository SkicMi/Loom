// Camera solve na PRAVOJ snimci: video -> tragovi -> poze i tocke -> COLMAP.
//
//   ./VideoSolve snimka.mp4 [korak] [kadrova] [vidno polje u stupnjevima] [izlazna mapa]
//
// Sve dosad je mjereno protiv poznate istine. Ovdje istine nema, pa ostaju dvije mjere koje se
// same brane: REPROJEKCIJA (koliko dobro rjesenje objasnjava opazanja) i BROJ RIJESENIH KAMERA.
// Nijedna ne kaze da je rekonstrukcija tocna - kazu da je konzistentna, i to je sve sto se bez
// istine moze tvrditi.
//
// VIDNO POLJE SE NE ZNA. Kad korisnik ne preda ni FOV ni cameras.txt, zajednicki f+k1 prvo se
// procijene iz view-grapha pa zajedno s pozama i tockama. Ako geometrija to ne moze odrediti,
// rezultat se NE izmisli: ispisuje se razlog i tek tada se koristi stari FOV sweep kao fallback.
#include <Spool/ImageFile.h>
#include <Spool/CameraMetadata.h>
#include <Spool/VideoFile.h>

#include <Engine/CameraHints.h>
#include <Engine/ResidualField.h>
#include <Engine/ColmapExport.h>
#include <Engine/Upright.h>
#include <Engine/RollingShutter.h>
#include <Engine/UsdExport.h>

#include "LoomSnapshot.h"
#include <Engine/ColmapImport.h>
#include <Engine/MatchGraph.h>
#include <Engine/MergeTracks.h>
#include <Engine/Keyframes.h>
#include <Engine/Reconstruct.h>
#include <Engine/SelfCalibration.h>
#include <Engine/SolveCache.h>
#include <Engine/Bands.h>
#include <Engine/SolvePose.h>
#include <Engine/Track.h>

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Vulkan/DescriptorMatcher.h"
#include "Vulkan/SiftDescriber.h"

#include <optional>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <filesystem>
#include <future>
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

//=============================================================================================
// PUNE SLICICE: poza za SVAKI kadar, ne samo za kljucne.
//
// ZASTO. Solver uzima svaki step-ti kadar, pa od 2304 kadra snimke izadje 229 poza. Za splat je to
// dosta. Za match-move nije: CG objekt tada skace pet puta u sekundi, a izmedju uzoraka USD
// linearno interpolira - sto kamera iz ruke sigurno ne radi.
//
// NE REKONSTRUIRA SE IZNOVA. Struktura je vec rijesena, pa se medjukadar samo LOKALIZIRA: tocke se
// pratiteljem prenesu iz najblizeg kljucnog kadra, a poza izadje iz PnP-a. Red velicine jeftinije
// od solvea, jer se u 3D nista ne trazi.
//
// KRECE SE OD KLJUCNOG KADRA, NE OD PRETHODNOG MEDJUKADRA. Lanac pracenja koji se ne resetira
// nakuplja pomak, pa bi zadnji kadar prije sljedeceg kljucnog bio najlosiji. Ovako je svaki
// medjukadar udaljen najvise step-1 kadrova od mjesta s tocnom pozom
//=============================================================================================
struct DenseTrack{
    std::vector<Engine::Pose> poses;      //po IZVORNOM kadru snimke
    std::vector<uint8_t> posed;
    uint32_t localised = 0;
    uint32_t failed = 0;
    uint32_t medianKept = 0;   //koliko je tocaka prezivjelo prijenos, medijan
};

DenseTrack localiseEveryFrame(const std::string& path, uint32_t step,
                              const Engine::Reconstruction& solved,
                              const std::vector<Engine::Observation>& observations,
                              const std::vector<uint32_t>& keyframeFrames,
                              const Engine::Intrinsics& intrinsics){
    DenseTrack out;
    if(step <= 1 || keyframeFrames.empty()) return out;

    //Koja opazanja kljucnog kadra gledaju RIJESENU tocku - samo ona nose 3D koji PnP treba
    std::vector<std::vector<Engine::PointObservation>> perKeyframe(keyframeFrames.size());
    for(const Engine::Observation& observation : observations){
        if(observation.camera >= keyframeFrames.size()) continue;
        if(observation.point >= solved.solved.size() || !solved.solved[observation.point]) continue;
        if(observation.camera >= solved.posed.size() || !solved.posed[observation.camera]) continue;
        perKeyframe[observation.camera].push_back(
            Engine::PointObservation{observation.point, observation.pixel});
    }

    //Od rednog broja KORISTENOG kadra do kljucnog kadra
    std::vector<int> keyframeOfUsed;
    for(size_t k = 0; k < keyframeFrames.size(); ++k){
        if(keyframeFrames[k] >= keyframeOfUsed.size()) keyframeOfUsed.resize(keyframeFrames[k] + 1, -1);
        keyframeOfUsed[keyframeFrames[k]] = int(k);
    }

    Engine::TrackConfig trackConfig;
    trackConfig.levels = 4;          //medjukadar je blizu, ali kamera iz ruke zna skociti
    trackConfig.window = 10;

    Engine::PoseSolveConfig poseConfig;
    poseConfig.huberPixels = 3.0;

    //SAMO UNUTAR RIJESENOG OPSEGA. Iza zadnjeg kljucnog kadra rekonstrukcija ne pokriva nista, pa
    //bi se poze ondje EKSTRAPOLIRALE - pracenje bi jos neko vrijeme uspijevalo i davalo brojke koje
    //izgledaju uredno a ne stoje ni na cemu. Prvi pokusaj je bas to radio: 780 od 1088 kadrova
    //"lokalizirano" dok je rjesenje pokrivalo njih dvjesto cetrdeset
    const uint32_t lastSolvedSource = keyframeFrames.back() * step;

    //=====================================================================================
    // ODSJECCI PARALELNO. Svaki lanac krece od svog kljucnog kadra (poza i tocke su ondje vec
    // rijesene) i ide do sljedeceg, pa odsjecci ne ovise jedan o drugome - samo dekodiranje mora
    // ici redom. Jedna nit dekodira i slaze kadrove po odsjecima, a gotov odsjecak ide radniku.
    // Svaki radnik pise samo u svoje kadrove, pa je rezultat bit po bit isti kao kad se islo
    // redom. Bilo je 425 s na C0257 (2072 medjukadra), od cega je dekodiranje manji dio.
    //
    // Najvise 'inFlight' odsjecaka u memoriji: kadar je 8 MB sive slike na 4K, odsjecak desetak
    //=====================================================================================
    struct Segment{
        int keyframe = -1;
        std::vector<uint32_t> sources;
        std::vector<std::vector<uint8_t>> grays;
    };
    struct SegmentResult{
        uint32_t localised = 0, failed = 0;
        std::vector<uint32_t> inliers;
    };
    uint32_t width = 0, height = 0;
    out.poses.resize(size_t(lastSolvedSource) + 1);
    out.posed.resize(size_t(lastSolvedSource) + 1, uint8_t(0));

    auto runSegment = [&](Segment segment){
        SegmentResult result;
        Engine::Pyramid previous;
        Engine::Pose lastPose = solved.poses[size_t(segment.keyframe)];
        std::vector<Engine::PointObservation> active = perKeyframe[size_t(segment.keyframe)];
        for(size_t i = 0; i < segment.sources.size(); ++i){
            const Engine::GrayImage image{segment.grays[i].data(), width, height, width};
            Engine::Pyramid pyramid(image, trackConfig.levels);
            const uint32_t source = segment.sources[i];
            if(i == 0){
                out.poses[source] = lastPose;
                out.posed[source] = 1;
            }else if(!active.empty()){
                std::vector<Engine::PointObservation> moved(active.size());
                std::vector<uint8_t> kept(active.size(), 0);
                Engine::inBands(0, int(active.size()), [&](uint32_t, int first, int last){
                    for(int k = first; k < last; ++k){
                        glm::vec2 landed;
                        if(Engine::trackPoint(previous, pyramid, active[size_t(k)].pixel, landed, trackConfig)){
                            moved[size_t(k)] = Engine::PointObservation{active[size_t(k)].point, landed};
                            kept[size_t(k)] = 1;
                        }
                    }
                });
                std::vector<Engine::PointObservation> survived;
                survived.reserve(moved.size());
                for(size_t k = 0; k < moved.size(); ++k) if(kept[k]) survived.push_back(moved[k]);

                if(survived.size() >= 12){
                    const Engine::PoseSolveResult solvedPose =
                        Engine::solvePose(solved.points, survived, intrinsics, lastPose, poseConfig);
                    if(solvedPose.solved){
                        out.poses[source] = solvedPose.pose;
                        out.posed[source] = 1;
                        lastPose = solvedPose.pose;
                        ++result.localised;
                        result.inliers.push_back(uint32_t(survived.size()));
                    }else{
                        ++result.failed;
                    }
                }else{
                    ++result.failed;
                }
                //Pracenje ide dalje od ONOGA STO JE NADJENO, da se sljedeci kadar ne trazi iz starog mjesta
                active = survived;
            }
            previous = std::move(pyramid);
            segment.grays[i].clear();
            segment.grays[i].shrink_to_fit();
        }
        return result;
    };

    //Pojasevi UNUTAR odsjecka ostaju: odsjecaka je malo, a svaki prati tisuce tocaka po kadru.
    //Izmjereno: odsjecci serijski iznutra i 12 u letu - 166 s umjesto 94 s na 532 medjukadra
    const size_t inFlight = 6;
    std::vector<std::future<SegmentResult>> running;
    std::vector<SegmentResult> finished;
    auto launch = [&](Segment&& segment){
        if(segment.keyframe < 0 || segment.sources.empty()) return;
        while(running.size() >= inFlight){
            finished.push_back(running.front().get());
            running.erase(running.begin());
        }
        running.push_back(std::async(std::launch::async, runSegment, std::move(segment)));
    };

    Spool::VideoReader reader(path);
    uint32_t sourceIndex = 0, usedIndex = 0;
    Segment current;
    while(!reader.atEnd()){
        const Spool::Image frame = reader.readNext();
        if(frame.pixels.empty()) break;
        width = frame.width;
        height = frame.height;

        const bool isUsed = (step <= 1) || (sourceIndex % step == 0);
        int keyframe = -1;
        if(isUsed && usedIndex < keyframeOfUsed.size()) keyframe = keyframeOfUsed[usedIndex];

        if(keyframe >= 0){
            launch(std::move(current));
            current = Segment{};
            current.keyframe = keyframe;
        }
        //Kadrovi prije prvog kljucnog nemaju odakle krenuti - kao i prije, ostaju bez poze
        if(current.keyframe >= 0){
            current.sources.push_back(sourceIndex);
            current.grays.push_back(toGray(frame));
        }

        if(isUsed) ++usedIndex;
        ++sourceIndex;
        if(sourceIndex > lastSolvedSource) break;
    }
    launch(std::move(current));
    for(auto& one : running) finished.push_back(one.get());

    std::vector<uint32_t> inlierCounts;
    for(const SegmentResult& one : finished){
        out.localised += one.localised;
        out.failed += one.failed;
        inlierCounts.insert(inlierCounts.end(), one.inliers.begin(), one.inliers.end());
    }

    if(!inlierCounts.empty()){
        std::sort(inlierCounts.begin(), inlierCounts.end());
        out.medianKept = inlierCounts[inlierCounts.size() / 2];
    }
    return out;
}


void printReconstructTiming(const Engine::Reconstruction& state, const char* indent = "    "){
    const Engine::ReconstructTiming& time = state.timing;
    const double classified = time.initialPairSeconds + time.poseSeconds +
        time.triangulationSeconds + time.bundleSeconds + time.filteringSeconds +
        time.diagnosticsSeconds;
    const double other = std::max(0.0, time.totalSeconds - classified);
    std::printf("%sfaze rekonstrukcije: par %.1f, PnP %.1f (%u), triangulacija %.1f (%u), "
                "bundle %.1f (%u), filter %.1f, dijagnoza %.1f, ostalo %.1f; ukupno %.1f s\n",
                indent, time.initialPairSeconds, time.poseSeconds, time.poseCalls,
                time.triangulationSeconds, time.triangulationCalls,
                time.bundleSeconds, time.bundleCalls, time.filteringSeconds,
                time.diagnosticsSeconds, other, time.totalSeconds);
    const double bundleClassified = time.bundleCostSeconds + time.bundleLinearizeSeconds +
        time.bundleSchurSeconds + time.bundleDenseSolveSeconds +
        time.bundleBackSubstituteSeconds;
    std::printf("%s  bundle: linearizacija %.1f, Schur %.1f, dense solve %.1f, "
                "uvrstavanje %.1f, trosak %.1f, ostalo %.1f s\n",
                indent, time.bundleLinearizeSeconds, time.bundleSchurSeconds,
                time.bundleDenseSolveSeconds, time.bundleBackSubstituteSeconds,
                time.bundleCostSeconds, std::max(0.0, time.bundleSeconds - bundleClassified));
}

uint64_t cacheSignature(const std::string& mode, double fieldOfView){
    //FNV nad stvarnim ulazima u gradnju grafa. Prazan mode i "graf" namjerno su isti jer vode
    //istim kodom; promjena FOV-a nije ista jer dvoprizorno geometrijsko sito koristi tu zarisnu.
    const std::string normalized = mode.empty() ? "graf" : mode;
    uint64_t hash = 1469598103934665603ull;
    auto add = [&](const void* data, size_t size){
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for(size_t i = 0; i < size; ++i){ hash ^= bytes[i]; hash *= 1099511628211ull; }
    };
    add(normalized.data(), normalized.size());
    uint64_t fovBits = 0;
    std::memcpy(&fovBits, &fieldOfView, sizeof(fovBits));
    add(&fovBits, sizeof(fovBits));
    return hash;
}


//=============================================================================================
// NEOPTIMIZIRAN BUILD SE MORA JAVITI, glasno.
//
// ZASTO OVO POSTOJI. Cijeli je projekt od 17. do 22. rujna stajao u build mapi postavljenoj na
// Debug - dakle bez ijedne -O zastavice. Nijedno mjerenje u tom razdoblju nije vrijedilo, a to se
// nije vidjelo jer alat radi jednako, samo cetiri puta sporije. Dan je potrosen na trazenje uskog
// grla u kodu koji prevoditelj nije ni pokusao optimizirati.
//
// CMakeLists vec brani od toga - postavlja RelWithDebInfo kad build type nije zadan - ali ta se
// zastita ne aktivira kad je Debug VEC u predmemoriji. Jedino sto pouzdano zna je li prevodjeno s
// optimizacijom je sam prevoditelj, pa se pita njega
//=============================================================================================
void warnIfUnoptimised(){
#ifndef __OPTIMIZE__
    std::printf("\n  !! NEOPTIMIZIRAN BUILD - mjerenja ne vrijede, a alat je oko cetiri puta "
                "sporiji.\n     Popravak: cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo\n\n");
#endif
}
}

//Zidno vrijeme po fazama, za ispis na kraju: bez njega su se vidjele samo faze koje same javljaju
//vrijeme, a dvadeset minuta solvea nije imalo kamo otici osim u "ostalo"
struct PhaseClock{
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    std::vector<std::pair<const char*, double>> phases;
    void mark(const char* name){
        const auto now = std::chrono::steady_clock::now();
        phases.emplace_back(name, std::chrono::duration<double>(now - last).count());
        last = now;
    }
    void print() const {
        double total = 0.0;
        for(const auto& phase : phases) total += phase.second;
        std::printf("  vrijeme po fazama:");
        for(size_t i = 0; i < phases.size(); ++i) std::printf("%s %s %.1f", i ? "," : "", phases[i].first, phases[i].second);
        std::printf("; ukupno %.1f s\n", total);
    }
};
PhaseClock phaseClock;

int main(int realArgc, char** realArgv){
    warnIfUnoptimised();

    //=====================================================================================
    // SAMO KAMERA, za matchmove: --samo-kamera bilo gdje u naredbi.
    //
    // Splat trazi slike kadrova u punoj razlucivosti (images/), a matchmove samo pozu - kameru na
    // svakom kadru i tocke za orijentaciju. Zapis stotina 4K PNG-ova je 18 % cijelog solvea
    // (izmjereno na kamenom zidu: 0.46 -> 0.64 kumulativno), pa se u ovom nacinu preskace. Sve
    // ostalo ostaje: pune slicice (kamera na SVAKOM kadru) su ono sto matchmove i trazi, a boje
    // tocaka se i dalje skupljaju iz istih kadrova - bez njih je oblak u editoru siv.
    //
    // Zastavica, ne jos jedan pozicijski argument: ima ih vec osam, a deveti bi znacio da se za
    // matchmove moraju napisati i svi prije njega
    //=====================================================================================
    bool cameraOnly = false;
    //--subpixel-corners: uglovi grafa poklapanja dobiju subpikselni polozaj (TrackConfig::subpixel,
    //Foerstner na smanjenoj slici). Bez njega su na 4K tocni na cetiri piksela
    bool subpixelCorners = false;
    //--rolling-shutter: izmjeri vrijeme citanja i zapisi rs_top/rs_bottom (Engine/RollingShutter.h).
    //ZADANO ISKLJUCENO: solve bolji (izdvojeni -7 %), ali splat na izdvojenim kadrovima nije -
    //parno po kadru -0.08 +- 0.14 dB (evaluate_splat.py) - a kosta ~80 s
    bool rollingShutter = false;
    //--focal-diagnostic: jos dva solvea sa zarisnom x0.75 i x1.25, samo za ispis koliko se putanja
    //pri tome zakrene. Nista ne mijenja u rezultatu, a na C0257 su to dvije pune rekonstrukcije
    bool focalDiagnostic = false;
    //--track-scale N: pracenje na slici smanjenoj N puta. Uz graf poklapanja pracenje sluzi SAMO za
    //izbor kljucnih kadrova (solve koristi opazanja grafa), pa mu puna 4K slika ne treba
    uint32_t trackScale = 1;
    //--keyframes-only: bez poze za svaki kadar (pune slicice) - kamera samo na kljucnim kadrovima
    bool keyframesOnly = false;
    //Poklapanje opisnika grafa na kartici (Loomov DescriptorMatcher, ista poklapanja kao procesor -
    //test_gpu_match; na 60 kadrova C0257 izlaz isti do bita, poklapanje 78 -> 25 s). ZADANO UKLJUCENO;
    //--cpu-match vraca procesor, a bez kartice se na nj prelazi samo
    bool gpuMatch = true;
    //Potpisi prostora mjerila na kartici (Loomov SiftDescriber - test_gpu_sift: 0.27 % vrijednosti
    //potpisa razlicito za jedan-dva, poklapanja ista). ZADANO UKLJUCENO; --cpu-features vraca procesor
    bool gpuFeatures = true;
    //--initial-pairs N: koliko pocetnih parova puna obrada gradi do kraja (ReconstructConfig::initialPairTrials)
    uint32_t initialPairs = 0;
    //--measure-held-out: kad puna obrada zamijeni brzi kandidat, jos jedna rekonstrukcija s istim
    //pocetnim parom i izdvojenim opazanjima - samo da provjera bez istine mjeri ISPORUCENO rjesenje.
    //Bez nje ta provjera ostaje s brzog kandidata i tako se i ispise
    bool measureHeldOut = false;
    std::vector<char*> positional;
    for(int i = 0; i < realArgc; ++i){
        if(std::string(realArgv[i]) == "--samo-kamera") cameraOnly = true;
        else if(std::string(realArgv[i]) == "--subpixel-corners") subpixelCorners = true;
        else if(std::string(realArgv[i]) == "--rolling-shutter") rollingShutter = true;
        else if(std::string(realArgv[i]) == "--focal-diagnostic") focalDiagnostic = true;
        else if(std::string(realArgv[i]) == "--keyframes-only") keyframesOnly = true;
        else if(std::string(realArgv[i]) == "--gpu-match") gpuMatch = true;
        else if(std::string(realArgv[i]) == "--cpu-match") gpuMatch = false;
        else if(std::string(realArgv[i]) == "--cpu-features") gpuFeatures = false;
        else if(std::string(realArgv[i]) == "--measure-held-out") measureHeldOut = true;
        else if(std::string(realArgv[i]) == "--initial-pairs" && i + 1 < realArgc) initialPairs = uint32_t(std::max(1, std::atoi(realArgv[++i])));
        else if(std::string(realArgv[i]) == "--track-scale" && i + 1 < realArgc) trackScale = std::max(1, std::atoi(realArgv[++i]));
        else positional.push_back(realArgv[i]);
    }
    const int argc = int(positional.size());
    char** argv = positional.data();

    if(argc < 2){
        std::printf("Upotreba: VideoSolve snimka.mp4 [korak] [kadrova] [vidno polje] [izlazna mapa] [cameras.txt] [graf|graf-bez-mjerila|spajanje|bez-spajanja] [graph-cache.bin]\n");
        std::printf("  zadano je graf: uglovi za pokrivenost i prostor mjerila za tocnost, spojeni\n");
        std::printf("  --samo-kamera: za matchmove - bez slika kadrova za splat, oko petine brze\n");
        std::printf("  cameras.txt: COLMAP-ova kalibracija. Kad je zadana, zarista i distorzija se\n");
        std::printf("               NE pogadjaju nego citaju, a opazanja se isprave prije solvea\n");
        return 1;
    }

    const std::string path = argv[1];
    //Svaki kadar se prati; koji ulaze u rekonstrukciju bira chooseKeyframes. Preskakanje pri
    //citanju je ostalo samo za brzo probavanje na dugackim snimkama
    const uint32_t step = argc > 2 ? uint32_t(std::atoi(argv[2])) : 1;
    const uint32_t wanted = argc > 3 ? uint32_t(std::atoi(argv[3])) : 100000;
    const double fieldOfView = argc > 4 ? std::atof(argv[4]) : 0.0;
    const std::string outputDirectory = argc > 5 ? std::string(argv[5]) : std::string();
    const std::string calibrationFile = argc > 6 ? std::string(argv[6]) : std::string();

    //=====================================================================================
    // KAKO NASTAJU KORESPONDENCIJE. Sedmi argument, i ZADANO JE GRAF POKLAPANJA.
    //
    // Dugo je zadano bilo spajanje vec pracenih tragova, a graf se dobivao samo uz izricit
    // argument - pa je sav rad na njemu bio nedostupan onome tko alat naprosto pokrene. Graf je
    // mjereno bolji u redu velicine: pracenje je davalo oko 300 opazanja po kljucnom kadru, graf
    // ih daje preko 3000, a i sve sto je poslije napravljeno (rastavljanje sukobljenih komponenti,
    // vise pocetnih parova, savovi) vrijedi samo za njega.
    //
    //   graf           znacajke se u svakom kadru nadju NEOVISNO pa povezu - zadano
    //   spajanje       stari put: prate se tragovi pa se naknadno spajaju
    //   bez-spajanja   golo pracenje, za usporedbu
    //=====================================================================================
    const std::string how = argc > 7 ? std::string(argv[7]) : std::string();
    const bool mergeTracks = how == "spajanje";
    const bool matchGraph = how.empty() || how == "graf" || how == "graf-bez-mjerila";

    //Prostor mjerila uz uglove - zadano. "graf-bez-mjerila" ga gasi, za usporedbu
    const bool scaleSpace = how.empty() || how == "graf";
    const std::filesystem::path cacheFile = argc > 8 ? std::filesystem::path(argv[8]) : std::filesystem::path();

    Spool::VideoReader reader(path);
    const Spool::VideoInfo& info = reader.info();
    std::printf("Snimka %ux%u, %.2f fps, %s\n", info.width, info.height, info.frameRate(), info.codec.c_str());
    std::printf("  uzimam svaki %u. kadar, najvise %u\n", step, wanted);

    std::error_code fileError;
    const uint64_t sourceBytes = std::filesystem::file_size(path, fileError);
    const int64_t sourceWriteTime = fileError ? 0 : int64_t(
        std::filesystem::last_write_time(path, fileError).time_since_epoch().count());
    const uint64_t buildSignature = cacheSignature(subpixelCorners ? how + "+subpixel" : how, fieldOfView);

    uint32_t used = 0;
    std::vector<uint32_t> keyframeFrames;
    uint32_t cameraCount = 0;
    float featurePixels = 1.0f;
    std::vector<Engine::Observation> observations;
    uint32_t pointCount = 0;
    bool cacheLoaded = false;

    if(!cacheFile.empty() && std::filesystem::exists(cacheFile)){
        Engine::SolveCache cache;
        std::string error;
        if(Engine::readSolveCache(cacheFile, cache, &error)){
            const bool sameInput = cache.width == info.width && cache.height == info.height &&
                cache.step == step && cache.requestedFrames == wanted &&
                cache.sourceBytes == sourceBytes && cache.sourceWriteTime == sourceWriteTime &&
                cache.buildSignature == buildSignature;
            if(sameInput){
                used = cache.usedFrames;
                keyframeFrames = std::move(cache.keyframes);
                cameraCount = uint32_t(keyframeFrames.size());
                observations = std::move(cache.observations);
                pointCount = cache.pointCount;
                featurePixels = cache.localizationPixels;
                cacheLoaded = true;
                std::printf("  cache grafa ucitan: %u kljucnih kadrova, %u tocaka, %zu opazanja\n",
                            cameraCount, pointCount, observations.size());
            }else{
                std::printf("  cache grafa ne pripada ovoj snimci ili postavkama; gradim ga ponovo\n");
            }
        }else{
            std::printf("  cache grafa nije valjan (%s); gradim ga ponovo\n", error.c_str());
        }
    }

    // -------------------------------------------------------------------------------
    // Kadrovi -> tragovi
    // -------------------------------------------------------------------------------

    if(!cacheLoaded){

    Engine::TrackConfig trackConfig;

    //PROZOR MORA PRATITI RAZLUCIVOST. Fiksnih 6 je bilo podeseno na 480x360, gdje prozor od 13 px
    //pokriva 2.7 posto sirine slike i u njemu ima strukture. Na 4K isti prozor pokriva 0.34 posto
    //i gotovo ravnu mrlju - iz koje se dva parametra jos i daju odrediti, ali sest ne. Afini warp
    //tada vodi sum, probije ogradu na rastezanje i trag umre u kadru u kojem je rodjen.
    //
    //Izmjereno na snimci 3840x2160, 12 kadrova, afino pracenje:
    //
    //   window   tragova   opazanja   medijan duljine traga   vrijeme
    //      6       3738       7816             1 kadar         34.0 s
    //     12        800       7000            12 kadrova       18.9 s
    //     24        800       8658            12 kadrova       66.1 s
    //     32        800       8835            12 kadrova      112.9 s
    //
    //Prozor 12 je i NAJBRZI, jer uz njega tragovi zive pa nema lavine ponovnog trazenja uglova -
    //ista ta lavina je ono sto prozor 6 cini sporijim unatoc manjem prozoru. Veci od 12 kupi vise
    //opazanja, ali cijena raste s kvadratom: 24 je 3.5x sporiji za 24 posto vise opazanja.
    //
    //Mjereno je samo na 4K; omjer je odatle prenesen, a donja granica je stara vrijednost koja se
    //na malim slikama pokazala dobrom
    trackConfig.window = std::max(6u, uint32_t(info.width) / trackScale / 320u);

    //AFINO JE ISKLJUCENO NA PRAVOJ SNIMCI, i to je mjereno a ne pretpostavka. Na sintetici je afino
    //pracenje 15 do 19 puta tocnije, ali na snimci iz kamere ne prezivi. Isti isjecak, 40 kadrova
    //iz dijela gdje se kamera giba:
    //
    //   afino        21631 tragova, 31832 opazanja, medijan duljine traga 1 kadar,   30.7 s
    //   samo pomak    1219 tragova, 23338 opazanja, medijan duljine traga 20 kadrova, 4.9 s
    //
    //Sest puta sporije i prakticki nijedan trag ne prezivi kadar u kojem je rodjen. Razlika prema
    //sintetici nije u sumu - izmjeren je i jednak je na obje strane (0.247) - nego u tome sto
    //renderirana slika nema ni rolling shutter, ni motion blur, ni kompresiju. Dok se ne nadje sto
    //tocno afini dio zavodi, prava snimka ide na pomak. Sinteticki testovi ostaju na afinom, gdje
    //je mjerljivo bolje - zato se ovo postavlja OVDJE, a ne mijenja u TrackConfigu
    trackConfig.affine = false;
    trackConfig.minDistance = 14.0f;
    trackConfig.maxCorners = 800;
    trackConfig.minTracks = 400;
    Engine::Tracker tracker(trackConfig);

    const auto started = std::chrono::steady_clock::now();
    uint32_t index = 0;

    while(!reader.atEnd() && used < wanted){
        const Spool::Image frame = reader.readNext();
        if(frame.pixels.empty()) break;
        if(index++ % step != 0) continue;

        const std::vector<uint8_t> gray = toGray(frame);
        if(trackScale > 1){
            const uint32_t w = frame.width / trackScale, h = frame.height / trackScale;
            std::vector<uint8_t> small(size_t(w) * h);
            for(uint32_t y = 0; y < h; ++y) for(uint32_t x = 0; x < w; ++x){
                uint32_t sum = 0;
                for(uint32_t dy = 0; dy < trackScale; ++dy)
                    for(uint32_t dx = 0; dx < trackScale; ++dx) sum += gray[size_t(y * trackScale + dy) * frame.width + x * trackScale + dx];
                small[size_t(y) * w + x] = uint8_t(sum / (trackScale * trackScale));
            }
            tracker.addFrame(Engine::GrayImage{small.data(), w, h, w});
        }else{
            tracker.addFrame(Engine::GrayImage{gray.data(), frame.width, frame.height, frame.width});
        }
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

    //Opazanja pracenja u pikselima PUNE slike (vidi --track-scale)
    std::vector<Engine::Observation> tracked = tracker.observations();
    if(trackScale > 1){
        for(Engine::Observation& one : tracked) one.pixel = (one.pixel + glm::vec2(0.5f)) * float(trackScale) - glm::vec2(0.5f);
    }

    //Koliko se dugo tragovi drze - kratki tragovi znace da rekonstrukcija nema sto povezati
    std::vector<uint32_t> length(tracker.trackCount(), 0);
    for(const Engine::Observation& observation : tracked) ++length[observation.point];
    std::vector<uint32_t> sorted = length;
    std::sort(sorted.begin(), sorted.end());
    std::printf("  duljina traga: medijan %u kadrova, najdulji %u\n",
                sorted[sorted.size() / 2], sorted.back());

    // -------------------------------------------------------------------------------
    // Rekonstrukcija, uz probanje vidnog polja kad nije zadano
    // -------------------------------------------------------------------------------

    //Kljucni kadrovi - vidi Engine/Keyframes.h. Izbor ne ovisi o zarisnoj pa se radi jednom
    const Engine::KeyframeSelection keys = Engine::chooseKeyframes(tracked, used, info.width);
    keyframeFrames = keys.frames;
    std::printf("  kljucnih kadrova %zu od %u (medijan paralakse %.1f px)\n",
                keyframeFrames.size(), used, keys.medianParallaxPixels);
    if(keyframeFrames.size() < 3){
        std::printf("Premalo kljucnih kadrova.\n");
        return 1;
    }
    cameraCount = uint32_t(keyframeFrames.size());

    // ---------------------------------------------------------------------------------
    // Spajanje tragova preko potpisa
    // ---------------------------------------------------------------------------------
    //
    // Pracenje umire cim ugao izadje iz kadra; kad se kamera vrati, isti kut se nadje kao NOVI trag.
    // Izmjereno na ovoj snimci: kadar 0 dijeli 236 tocaka s kljucnim kadrom 5 i NIJEDNU s kadrom 10,
    // pa siroka baza nije postojala nigdje. Vidi Engine/MergeTracks.h.
    //
    // ZASTO SE SNIMKA CITA DRUGI PUT. Kljucni kadrovi se znaju tek nakon sto je sve ispraceno, a
    // drzati sve kadrove u memoriji nije opcija - 3384 kadra na 4K je 28 GB. Ponovno citanje kosta
    // dekodiranje, oko 0.06 s po kadru, i to je jeftinije od svake druge mogucnosti
    //Koliko su polozaji znacajki tocni, u pikselima izvorne slike. Pracenje radi na punoj slici,
    //graf poklapanja na smanjenoj - a prag prihvacanja kamere mora znati o kojem se od to dvoje
    //radi, inace odbija kamere koje su tocne koliko podatak dopusta
    observations = keys.observations;
    pointCount = tracker.trackCount();

    //GRAF POKLAPANJA umjesto spajanja vec pracenih tragova. Spajanje popravlja tocke, ovo stvara
    //nove veze - znacajke se u svakom kljucnom kadru nadju NEOVISNO pa se povezu. Vidi
    //Engine/MatchGraph.h
    if(matchGraph || mergeTracks){
        std::vector<std::vector<uint8_t>> keyframeGray(cameraCount);
        std::vector<Engine::GrayImage> keyframeImages(cameraCount);

        Spool::VideoReader again(path);
        uint32_t index = 0, taken = 0, seen = 0;
        while(!again.atEnd() && taken < cameraCount){
            const Spool::Image frame = again.readNext();
            if(frame.pixels.empty()) break;
            if(step > 1 && (index++ % step) != 0) continue;

            //keyframeFrames nosi redne brojeve KORISTENIH kadrova, istim redom kojim su prosli kroz
            //tracker - pa se broji isto kako se brojalo tada
            if(taken < cameraCount && keyframeFrames[taken] == seen){
                keyframeGray[taken] = toGray(frame);
                keyframeImages[taken] = Engine::GrayImage{keyframeGray[taken].data(),
                                                          frame.width, frame.height, frame.width};
                ++taken;
            }
            ++seen;
        }

        if(taken == cameraCount){
            Engine::Intrinsics guess;
            guess.width = info.width; guess.height = info.height;
            guess.cx = 0.5f * float(info.width); guess.cy = 0.5f * float(info.height);
            //Priblizna zarisna je ovdje dovoljna: dvoprizorna poza sluzi samo kao SITO, a sito ne
            //mora biti kalibrirano da bi odbacilo poklapanje koje se ni s jednim rjesenjem ne slaze
            const double assumed = fieldOfView > 0.0 ? fieldOfView : 60.0;
            guess.fx = guess.fy = float((0.5 * double(info.width)) / std::tan(0.5 * assumed * 3.14159265358979 / 180.0));

            if(matchGraph){
                Engine::MatchGraphConfig graphConfig;
                graphConfig.detect = trackConfig;

                //UGLOVA KOLIKO IH IMA, I GUSTO. Trazenje je neovisno po kadru, pa ih smije biti
                //puno vise nego pri pracenju; a razmak se u MatchGraphu jos dijeli smanjenjem, pa
                //12 na 4K postane 3 na radnoj sirini od 960. Izmjereno na dva prava kadra: razmak
                //8 daje 598 provjerenih parova, razmak 3 daje 2928
                //=================================================================
                // KOLIKO ZNACAJKI PO KADRU, IZVEDENO IZ POVRSINE SLIKE.
                //
                // Dvadeset tisuca je bio apsolutan broj i na 4K se pokazao kao gornja granica koja
                // se STVARNO dosegne: na kamenom zidu su oba grafa udarila u svoj strop i dala
                // 15 822 opazanja po kadru, dok COLMAP na slicnom materijalu radi s oko 3500.
                //
                // Cijena nije linearna - poklapanje je na toj snimci uzelo 2914 CPU-sekundi, vise
                // od polovice cijelog posla. A konstanta koja je razumna na jednoj razlucivosti je
                // besmislena na drugoj; ista pouka kao kod zakrpe potpisa i prozora pracenja.
                //
                // Jedna znacajka na otprilike tisucu piksela: na 4K to je oko 8000 po kadru, na
                // 1080p oko 2000. Strop od 20 000 ostaje kao gornja ograda
                //=================================================================
                const double megapixels = double(info.width) * double(info.height) / 1.0e6;
                const uint32_t perFrame = std::min(20000u, uint32_t(megapixels * 1000.0));

                graphConfig.detect.maxCorners = perFrame;

                //Poklapanje na kartici: Loom bez prozora, samo za compute
                std::optional<LoomInitializer> gpu;
                std::optional<DescriptorMatcher> matcher;
                std::optional<SiftDescriber> describer;
                std::mutex describerLock;
                if(gpuMatch || gpuFeatures){
                    LoomConfig gpuConfig;
                    gpuConfig.width = 64; gpuConfig.height = 64;
                    gpuConfig.appName = "VideoSolve"; gpuConfig.engineName = "Loom";
                    gpuConfig.headless = true;
                    gpuConfig.maxDescriptorSets = 64;
                    try{
                        gpu.emplace(gpuConfig);
                        if(gpuMatch) matcher.emplace(*gpu);
                        if(gpuFeatures) describer.emplace(*gpu);
                    }catch(const std::exception& error){
                        std::printf("  kartica nedostupna (%s) - potpisi i poklapanje na procesoru\n", error.what());
                        describer.reset();
                        matcher.reset();
                        gpu.reset();
                    }
                }
                if(describer){
                    //Graf zove iz vise dretvi (po kadru); kartica je jedna, pa jedan po jedan -
                    //detekcija ostalih kadrova za to vrijeme tece dalje
                    graphConfig.siftScaledDescriber = [&](const Engine::GrayImage& image, const std::vector<glm::vec2>& points,
                                                          const std::vector<float>& scales, const Engine::SiftConfig& sift){
                        SiftDescriber::Settings settings;
                        settings.scaleBands = sift.scaleBands; settings.patchPerScale = sift.patchPerScale;
                        settings.clamp = sift.clamp; settings.orient = sift.orient;
                        SiftDescriber::Output found;
                        {
                            std::lock_guard<std::mutex> hold(describerLock);
                            found = describer->describe(image.pixels, image.width, image.height, image.stride,
                                                        reinterpret_cast<const float*>(points.data()), scales.data(),
                                                        uint32_t(points.size()), settings);
                        }
                        std::vector<Engine::SiftDescriptor> out(points.size());
                        for(size_t i = 0; i < points.size(); ++i){
                            out[i].valid = found.valid[i] != 0;
                            if(!out[i].valid) continue;
                            out[i].angle = found.angles[i];
                            std::memcpy(out[i].values.data(), found.values.data() + i * Engine::siftLength, Engine::siftLength);
                        }
                        return out;
                    };
                }
                if(matcher){
                    graphConfig.siftPairMatcher = [&](const std::vector<std::vector<Engine::SiftDescriptor>>& signatures,
                                                      const std::vector<std::vector<glm::vec2>>& pixels,
                                                      const std::vector<std::pair<uint32_t, uint32_t>>& pairs,
                                                      float radius, const Engine::SiftConfig& sift){
                        std::vector<std::vector<uint8_t>> flat(signatures.size()), valid(signatures.size());
                        std::vector<DescriptorMatcher::Frame> frames(signatures.size());
                        for(size_t f = 0; f < signatures.size(); ++f){
                            flat[f].reserve(signatures[f].size() * Engine::siftLength);
                            for(const Engine::SiftDescriptor& d : signatures[f]){
                                flat[f].insert(flat[f].end(), d.values.begin(), d.values.end());
                                valid[f].push_back(d.valid ? 1 : 0);
                            }
                            frames[f] = DescriptorMatcher::Frame{flat[f].data(), reinterpret_cast<const float*>(pixels[f].data()),
                                                                 valid[f].data(), uint32_t(signatures[f].size())};
                        }
                        DescriptorMatcher::Rules rules;
                        rules.radius = radius; rules.maxDistance = sift.maxDistance;
                        rules.ratio = sift.ratio; rules.secondBestApart = sift.secondBestApart;
                        const auto found = matcher->match(frames, pairs, rules);
                        std::vector<std::vector<Engine::SiftMatch>> out(found.size());
                        for(size_t p = 0; p < found.size(); ++p){
                            out[p].reserve(found[p].size());
                            for(const auto& m : found[p]) out[p].push_back(Engine::SiftMatch{m.from, m.to, m.distance});
                        }
                        return out;
                    };
                }
                graphConfig.detect.subpixel = subpixelCorners;
                graphConfig.detect.minDistance = 12.0f;
                graphConfig.describe.ratio = 0.9f;        //vidi mjerenje u MatchGraph.cpp
                graphConfig.describe.maxDistance = 96;

                const auto started = std::chrono::steady_clock::now();
                Engine::MatchGraphResult graph = Engine::buildMatchGraph(keyframeImages, guess, graphConfig);

                //=================================================================
                // I ZNACAJKE IZ PROSTORA MJERILA, uz uglove - vidi Engine::mergeGraphs.
                //
                // Uglovi na smanjenoj slici daju POKRIVENOST: 101 kameru i 5213 opazanja po kadru.
                // Prostor mjerila daje TOCNOST: tocke na 0.933 posto opsega putanje od najblize
                // COLMAP-ove, ondje gdje uglovi daju 7.891 - ali samo 64 kamere.
                //
                // Spojeno drzi punu pokrivenost i bolje je od samih uglova po svakoj mjeri
                // tocnosti: reprojekcija 1.635 -> 1.289 px, baza 4.72 -> 5.01 st, smjer koraka
                // 1.87 -> 1.27 st, a zaokret iz kadra u kadar se vise ne razlikuje od COLMAP-ovog
                //=================================================================
                if(scaleSpace){
                    Engine::MatchGraphConfig fineConfig = graphConfig;
                    fineConfig.useScaleSpace = true;
                    fineConfig.scaleSpace.minDistance = 4.0f;
                    fineConfig.scaleSpace.maxKeypoints = perFrame;

                    const Engine::MatchGraphResult fine =
                        Engine::buildMatchGraph(keyframeImages, guess, fineConfig);
                    std::printf("  prostor mjerila: %u znacajki, %u tocaka, %zu opazanja "
                                "(priprema %.1f, znacajke %.1f [detekcija %.1f, potpisi %.1f: blur %.1f, gradijenti %.1f, gradnja %.1f], poklapanje %.1f, "
                                "geometrija %.1f, slaganje %.1f s)\n",
                                fine.featuresTotal, fine.pointCount, fine.observations.size(),
                                fine.preprocessSeconds, fine.featureSeconds,
                                fine.detectionSeconds, fine.descriptorSeconds,
                                fine.descriptorSmoothingSeconds, fine.descriptorGradientSeconds,
                                fine.descriptorBuildSeconds, fine.matchingSeconds,
                                fine.geometrySeconds, fine.assemblySeconds);

                    if(fine.pointCount > 0) graph = Engine::mergeGraphs(graph, fine);
                }

                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

                std::printf("  graf poklapanja: %u znacajki, %u tocaka, %zu opazanja (%.0f po kadru), "
                            "%u od %u parova proslo geometriju, medijan %.0f parova, %.1f s\n"
                            "    faze: priprema %.1f, znacajke %.1f [detekcija %.1f, potpisi %.1f: blur %.1f, gradijenti %.1f, gradnja %.1f], poklapanje %.1f, "
                            "geometrija %.1f, slaganje %.1f s\n",
                            graph.featuresTotal, graph.pointCount, graph.observations.size(),
                            double(graph.observations.size()) / double(cameraCount),
                            graph.acceptedFrames, graph.comparedFrames, graph.medianMatchesPerPair, seconds,
                            graph.preprocessSeconds, graph.featureSeconds,
                            graph.detectionSeconds, graph.descriptorSeconds,
                            graph.descriptorSmoothingSeconds, graph.descriptorGradientSeconds,
                            graph.descriptorBuildSeconds, graph.matchingSeconds,
                            graph.geometrySeconds, graph.assemblySeconds);

                if(graph.pointCount > 0){
                    observations = graph.observations;
                    pointCount = graph.pointCount;
                    featurePixels = graph.localizationPixels;
                }
            }else{
                Engine::MergeConfig mergeConfig;
                const Engine::MergeResult merged = Engine::mergeTracks(
                    observations, keyframeImages, pointCount, guess, mergeConfig);

                std::printf("  spajanje: %u -> %u tocaka, %u spajanja, %u od %u parova kadrova proslo geometriju\n",
                            pointCount, merged.pointCount, merged.mergedPairs,
                            merged.acceptedFrames, merged.comparedFrames);
                observations = merged.observations;
                pointCount = merged.pointCount;
            }
        }else{
            std::printf("  spajanje preskoceno: naslo se %u od %u kljucnih kadrova\n", taken, cameraCount);
        }
    }

    if(!cacheFile.empty()){
        Engine::SolveCache cache;
        cache.width = info.width;
        cache.height = info.height;
        cache.step = step;
        cache.requestedFrames = wanted;
        cache.usedFrames = used;
        cache.pointCount = pointCount;
        cache.localizationPixels = featurePixels;
        cache.sourceBytes = sourceBytes;
        cache.sourceWriteTime = sourceWriteTime;
        cache.buildSignature = buildSignature;
        cache.keyframes = keyframeFrames;
        cache.observations = observations;
        std::string error;
        if(Engine::writeSolveCache(cacheFile, cache, &error)){
            std::printf("  cache grafa zapisan u %s (%zu opazanja)\n",
                        cacheFile.string().c_str(), observations.size());
        }else{
            std::printf("  UPOZORENJE: cache grafa nije zapisan (%s)\n", error.c_str());
        }
    }
    }

    //KALIBRACIJA, kad je zadana. Jednom izmjerena za tijelo i objektiv vrijedi za svaku sljedecu
    //snimku istom kamerom, pa je nema smisla pogadjati iz vidnog polja u svakoj
    Engine::Intrinsics measured;
    std::string measuredModel;
    phaseClock.mark("pracenje i graf");
    const bool calibrated = !calibrationFile.empty() &&
                            Engine::readColmapCamera(calibrationFile, measured, measuredModel);
    if(!calibrationFile.empty() && !calibrated){
        std::printf("Ne mogu procitati kalibraciju iz %s\n", calibrationFile.c_str());
        return 1;
    }

    auto reconstructionConfig = [&](bool thorough){
        Engine::ReconstructConfig config;
        config.huberPixels = 2.0;
        config.acceptPixels = std::max(6.0, 2.0 * double(featurePixels));
        config.minPointsForPose = 20;

        //NAJVECI ZDRAVI ODSJECAK UMJESTO SVE-ILI-NISTA. Rjesenje koje se proteze preko loma je
        //tiho krivo: dva dijela snimke koja se medjusobno ne slazu, a svaki je u sebi uredan.
        //Pedeset ispravnih kamera je upotrebljivo, osamdeset preko loma nije.
        //
        //Knjiznica ovo ne radi sama od sebe - ovdje se pali jer alat isporucuje rezultat, a ne
        //samo racuna
        config.keepLargestHealthySegment = true;

        //LOKALNI BUNDLE U RASTU. Bundle se i dalje zove nakon svake kamere, ali mice samo prozor
        //oko zadnje dodane; daleke kamere stoje i drze tocke koje vide. Refine ostaje globalan, pa
        //se nakupljeni drift i dalje ispravlja.
        //
        //Izmjereno, isti graf, ista konfiguracija:
        //   kameni zid (229 kamera)  3107 s -> 626 s, baza 6.82 -> 6.82 st, omjer 2.52 -> 2.53
        //   joystick    (75 kamera)    95 s ->  37 s, baza 8.57 -> 8.46 st, omjer 4.08 -> 4.03
        //Dakle isto rjesenje, pet puta jeftinije - dobitak raste s brojem kamera
        config.localBundleWindow = 10;

        //=================================================================================
        // ZIVI SNIMAK ZA SUICELJE. Rekonstrukcija traje minutama i dosad se o njoj nije znalo
        // nista dok ne zavrsi; ovako `loom` moze pokazati kako scena nastaje.
        //
        // PISE SE ATOMSKI - prvo u .tmp pa preimenovanje - jer citatelj gleda isti taj fajl i ne
        // smije uhvatiti polovicu. Bez toga bi povremeno ucitao besmislice i nitko ne bi znao zasto
        //=================================================================================
        if(!outputDirectory.empty()){
            const std::string progressPath = outputDirectory + "/napredak.bin";
            config.progressEvery = 5;
            config.onProgress = [progressPath](const Engine::Reconstruction& state){
                //Isti pisac kao na kraju (LoomSnapshot.h), pa pisac i citac ne mogu razici
                Loom::Snapshot snapshot;
                for(size_t c = 0; c < state.poses.size(); ++c){
                    if(c < state.posed.size() && !state.posed[c]) continue;
                    snapshot.cameras.push_back(state.poses[c].position);
                    snapshot.orientations.push_back(state.poses[c].orientation);
                }
                for(size_t p = 0; p < state.points.size(); ++p){
                    if(p < state.solved.size() && state.solved[p]) snapshot.points.push_back(state.points[p]);
                }
                Loom::writeSnapshot(progressPath, snapshot);
            };
        }

        if(thorough && initialPairs > 0) config.initialPairTrials = initialPairs;
        if(!thorough){
            config.initialPairTrials = 1;
            config.seamFactor = 0.0;
            config.holdOutEvery = 10;
        }
        return config;
    };

    //Kad korisnik nije dao ni kalibraciju ni FOV, genericki put prvo mjeri f+k1 samo iz 2D veza,
    //zatim ih zajedno dotjera s pozama i tockama. Poznata kalibracija i rucno zadani FOV imaju
    //prednost: eksplicitna informacija se ne smije tiho zamijeniti procjenom.
    Engine::SelfCalibratedReconstruction automatic;
    if(!calibrated && fieldOfView <= 0.0){
        Engine::Intrinsics unknown;
        unknown.width = info.width; unknown.height = info.height;
        unknown.cx = 0.5f * float(info.width); unknown.cy = 0.5f * float(info.height);
        unknown.fx = unknown.fy = 0.5f * float(info.width); //samo predlozak; odgovor dolazi iz grafa
        automatic = Engine::reconstructSelfCalibrated(
            observations, cameraCount, pointCount, unknown, reconstructionConfig(false));
        if(automatic.determined){
            const double fov = 2.0 * std::atan(0.5 * double(info.width) /
                                               double(automatic.measuredIntrinsics.fx)) *
                               180.0 / 3.14159265358979;
            std::printf("  samokalibracija: f %.2f px, k1 %.5f, vidno polje %.2f st; "
                        "%u/%u kamera, reprojekcija %.3f px\n",
                        double(automatic.measuredIntrinsics.fx),
                        double(automatic.measuredIntrinsics.k1), fov,
                        automatic.reconstruction.posedCameras, cameraCount,
                        automatic.reconstruction.medianReprojection);

            //=================================================================
            // DISTORZIJA U PIKSELIMA, NE U k1.
            //
            // "k1 = 0.326" covjeku ne znaci nista, a "kut se pomice 116 px" znaci sve. Izmjereno
            // na kamenom zidu: procjena je dala bas tih 5.26 posto pomaka u kutu, a Sigma 18-50
            // f/2.8 na tom zaristu ima daleko ispod jedan posto. Dakle u k1 je upijeno nesto drugo
            // - ondje je scena bila gotovo ravna, baza 2.05 st, sto je degeneriran slucaj za
            // samokalibraciju.
            //
            // Prag se NE postavlja jer bi bio pogodjen: objektivi se razlikuju, a riblje oko je
            // izvan ovog modela ionako. Broj se ispisuje da se o njemu dade suditi
            //=================================================================
            const double halfDiagonal = std::hypot(0.5 * double(info.width), 0.5 * double(info.height));
            const double normalised = halfDiagonal / double(automatic.measuredIntrinsics.fx);
            const double shift = halfDiagonal * double(automatic.measuredIntrinsics.k1) * normalised * normalised;
            std::printf("    distorzija pomice kut za %.1f px (%.2f %% polumjera); "
                        "ispravljeni objektiv je obicno ispod jednog posto\n",
                        shift, 100.0 * shift / halfDiagonal);
        }else{
            std::printf("  samokalibracija nije odredjena (%s; view-graph %s). "
                        "Koristim stari FOV sweep kao fallback.\n",
                        Engine::selfCalibrationStatusName(automatic.status),
                        Engine::viewGraphCalibrationStatusName(automatic.graphStatus));
        }
        printReconstructTiming(automatic.reconstruction);
    }

    Engine::SourceFacts facts;
    facts.width = info.width;
    facts.height = info.height;
    facts.frameRate = info.frameRate();
    facts.rotation = info.rotation;

    //PRATECA DATOTEKA. Sonyjev XAVC ne pise ime kamere ni objektiva u samu snimku - provjereno na
    //tri klipa sa ZV-E10M2: kontejner nosi samo major_brand=XAVC, vrijeme i timecode. Sve je u
    //XML-u koji lezi pokraj, i ondje pise i uredjaj i objektiv
    for(const auto& entry : Spool::readSidecarMetadata(path)){
        facts.metadata.push_back(Engine::MetadataEntry{entry.first, entry.second});
    }
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


    //=====================================================================================
    // OGRADA OBJEKTIVA STOJI NA PUTU KOJIM REZULTAT PROLAZI, ne samo na zamjenskom.
    //
    // Ogradu sam prvo primijenio samo na zamjensku pretragu vidnog polja - a kad samokalibracija
    // uspije, ta se pretraga preskoci i ograda s njom. Na snimci joysticka je samokalibracija dala
    // 75.4 st, dok objektiv 18-50 mm na APS-C senzoru fizicki ne moze preko 66. Rezultat je izasao
    // izvan fizike, a nitko ga nije zaustavio.
    //
    // Procjena izvan onoga sto objektiv moze NIJE procjena nego dijagnoza: znaci da je geometrija
    // bila preslaba da ju odredi. Zato se odbija i ide se na ogradjenu pretragu
    //=====================================================================================
    bool focalOutsideLens = false;
    if(automatic.determined && hints.hasFieldOfViewRange){
        const double estimated = 2.0 * std::atan(0.5 * double(info.width) /
                                                 double(automatic.measuredIntrinsics.fx)) *
                                 180.0 / 3.14159265358979;
        const double slack = 1.05;   //senzor se u videu izrezuje, pa ograda ima malo zraka
        if(estimated > hints.widestFieldOfView * slack ||
           estimated < hints.narrowestFieldOfView / slack){
            focalOutsideLens = true;
            std::printf("  SAMOKALIBRACIJA ODBIJENA: procijenjeno vidno polje %.2f st je izvan "
                        "onoga sto objektiv moze (%.0f do %.0f st).\n",
                        estimated, hints.narrowestFieldOfView, hints.widestFieldOfView);
            std::printf("             To nije procjena nego dijagnoza: geometrija je bila "
                        "preslaba da odredi zariste.\n");
        }
    }

    //OPAZANJA SE ISPRAVE JEDNOM, na ulazu. Ista fizicka leca kasnije ispravlja i izlazne slike;
    //inace bi cameras.txt tvrdio PINHOLE dok bi PNG-ovi ostali zakrivljeni.
    const bool automaticallyCalibrated = automatic.determined && !focalOutsideLens;
    const bool havePhysicalLens = calibrated || automaticallyCalibrated;
    const Engine::Intrinsics physicalLens = calibrated ? measured : automatic.measuredIntrinsics;
    std::vector<Engine::Observation> solveObservations = automaticallyCalibrated
        ? automatic.flatObservations : observations;
    if(!automaticallyCalibrated && havePhysicalLens &&
       (physicalLens.k1 != 0.0f || physicalLens.k2 != 0.0f)){
        double worst = 0.0;
        for(Engine::Observation& one : solveObservations){
            const glm::vec2 fixed = Engine::undistort(physicalLens, one.pixel);
            worst = std::max(worst, double(glm::length(fixed - one.pixel)));
            one.pixel = fixed;
        }
        std::printf("  ispravljena distorzija k1 %.5f k2 %.5f - najveci pomak %.2f px\n",
                    double(physicalLens.k1), double(physicalLens.k2), worst);
    }

    //=====================================================================================
    // TRAZENJE ZARISNE JE IZBOR, NE POLIRANJE.
    //
    // Sest kandidata puta puna rekonstrukcija znaci sest puta cetiri pocetna para i sest popravaka
    // sava - tridesetak izgradnji scene da bi se izabrao JEDAN broj. Izmjereno: na 80 kadrova 4K
    // snimke to je prelo sat vremena i nije doslo do kraja.
    //
    // A za izbor zarisne to nije potrebno: kriva zarisna se vidi po tome sto rjesenje ima manje
    // kamera i vecu reprojekciju, a to se vidi i iz jednog pokusaja. Puna obrada ide tek na
    // pobjednika
    //=====================================================================================
    auto solveWith = [&](double fov, bool thorough){
        Engine::Intrinsics intrinsics;
        if(calibrated){
            intrinsics = measured;
            //Distorzija je vec izvadjena iz opazanja; solver od sada gleda ravnu lecu
            intrinsics.k1 = 0.0f;
            intrinsics.k2 = 0.0f;
        }else{
            intrinsics.width = info.width;
            intrinsics.height = info.height;
            intrinsics.cx = 0.5f * float(info.width);
            intrinsics.cy = 0.5f * float(info.height);
            //Vodoravno vidno polje: fx = (sirina/2) / tan(fov/2)
            const double focal = (0.5 * double(info.width)) / std::tan(0.5 * fov * 3.14159265358979 / 180.0);
            intrinsics.fx = float(focal);
            intrinsics.fy = float(focal);
        }

        const Engine::ReconstructConfig config = reconstructionConfig(thorough);
        return std::make_pair(Engine::reconstruct(solveObservations, cameraCount, pointCount,
                                                  intrinsics, config), intrinsics);
    };

    //STO SNIMKA KAZE O SEBI. Kad je kamera prepoznata, ne pogadja se od nule nego se provjeri uski
    //pojas oko onoga sto pise - a kad nije, sirok raspon i uz to jasno receno da je to pogadjanje
    std::vector<double> candidates;
    if(calibrated){
        //cameras.txt zadaje zarisnu, a solveWith tada vidno polje ne gleda - svaki kandidat bio bi
        //isti solve ponovljen. Na 60 kadrova C0257 to je bilo sedam puta 17 s istog rezultata
        candidates.push_back(2.0 * std::atan(0.5 * double(measured.width) / double(measured.fx)) *
                             180.0 / 3.14159265358979);
    }else if(fieldOfView > 0.0){
        candidates.push_back(fieldOfView);
    }else if(automaticallyCalibrated){
        candidates.push_back(2.0 * std::atan(0.5 * double(info.width) /
                                             double(automatic.measuredIntrinsics.fx)) *
                             180.0 / 3.14159265358979);
    }else if(hints.focalSource == Engine::HintSource::ModelTable || hints.focalSource == Engine::HintSource::Metadata){
        const double centre = hints.horizontalFieldOfView;
        candidates = {centre * 0.85, centre * 0.93, centre, centre * 1.07, centre * 1.15};
    }else if(hints.hasFieldOfViewRange){
        //OGRADA IZ OBJEKTIVA - vidi CameraHints::hasFieldOfViewRange. Kandidati izvan onoga sto
        //objektiv fizicki moze dati nisu kandidati. Bez ove ograde je ista kamera na prijasnjoj
        //snimci dala plato na 94-110 st, dakle birala je iz sest tisucinki piksela izvan fizike
        const double lowest = hints.narrowestFieldOfView;
        const double highest = hints.widestFieldOfView;
        for(int step = 0; step < 7; ++step){
            candidates.push_back(lowest + (highest - lowest) * double(step) / 6.0);
        }
        std::printf("  vidno polje ograniceno objektivom na %.0f-%.0f st\n", lowest, highest);
    }else{
        candidates = {40.0, 50.0, 60.0, 70.0, 78.0, 86.0, 94.0, 102.0, 110.0};
    }

    Engine::Reconstruction best;
    Engine::Intrinsics bestIntrinsics;
    double bestFov = 0.0;

    std::vector<double> reprojectionOf;
    uint32_t heldOutCount = 0;
    double heldOutError = 0.0, heldOutAgainst = 0.0;
    bool heldOutFromFast = false;     //provjera bez istine je s brzog kandidata, a isporucuje se puna obrada
    if(automaticallyCalibrated){
        //Samokalibracija je vec izgradila tocno onaj brzi kandidat koji bi solveWith ovdje ponovno
        //gradio. Nakon joint bundlea osvjezila je i reprojekciju, held-out mjeru i bazu, pa ga se
        //moze izravno preuzeti. Puna obrada ispod i dalje krece iz nule s istim konacnim FOV-om.
        best = automatic.reconstruction;
        bestIntrinsics = automatic.flatIntrinsics;
        bestFov = candidates.front();
        reprojectionOf.push_back(best.medianReprojection);
        heldOutCount = best.heldOutObservations;
        heldOutError = best.heldOutReprojection;
        heldOutAgainst = best.medianReprojection;
        std::printf("  vidno polje %5.1f st (f = %6.1f px): %2u/%u kamera, %4u tocaka, reprojekcija %6.3f px\n",
                    bestFov, double(bestIntrinsics.fx), best.posedCameras, cameraCount,
                    best.solvedPoints, best.medianReprojection);
        std::printf("    ponovno koristen samokalibracijski kandidat; nema duplog solvea\n");
    }else{
        for(double fov : candidates){
            const auto result = solveWith(fov, false);
            if(result.first.ok) reprojectionOf.push_back(result.first.medianReprojection);
            const Engine::Reconstruction& state = result.first;
            std::printf("  vidno polje %5.1f st (f = %6.1f px): %2u/%u kamera, %4u tocaka, reprojekcija %6.3f px\n",
                        fov, double(result.second.fx), state.posedCameras, cameraCount,
                        state.solvedPoints, state.medianReprojection);
            printReconstructTiming(state);

            const bool better = state.posedCameras > best.posedCameras ||
                                (state.posedCameras == best.posedCameras &&
                                 state.medianReprojection < best.medianReprojection);
            if(!best.ok || better){
                best = state;
                bestIntrinsics = result.second;
                bestFov = fov;
                heldOutCount = state.heldOutObservations;
                heldOutError = state.heldOutReprojection;
                heldOutAgainst = state.medianReprojection;
            }
        }
    }

    std::printf("\nNajbolje: vidno polje %.1f st, %u od %u kamera, %u tocaka, reprojekcija %.3f px\n",
                bestFov, best.posedCameras, cameraCount, best.solvedPoints, best.medianReprojection);

    //=====================================================================================
    // POBJEDNIK NA RUBU RASPONA ZNACI DA ZARISNA NIJE ODREDJENA.
    //
    // Mjera po kojoj se bira je reprojekcija, a kriva zarisna se s njom TRGUJE: rjesenje pobjegne
    // od istine i pritom smanji reprojekciju, jer scenu izobliči tako da se opazanja i dalje
    // objasnjavaju. Kad najbolji ispadne bas na kraju raspona, to je znak da se mjera nije okrenula
    // - dakle da minimum nije nadjen nego da smo stali na ogradi.
    //
    // Izmjereno na drugoj snimci: raspon je zavrsavao na 94 st i pobjednik je bio 94 st, a i
    // zarisna x1.25 od nje je davala jednaku reprojekciju i GLATKIJU putanju. Rjesenje se zato ne
    // smije citati kao izmjerena zarisna
    //=====================================================================================
    //NE TRAZI SE SAMO RUB NEGO I PLATO. Na drugoj snimci je raspon prosiren do 110 st i pobjednik
    //vise nije bio na rubu - ali 94, 102 i 110 st dali su 1.311, 1.312 i 1.312 px. Mjera je ondje
    //RAVNA, pa je pobjednik izabran iz sest tisucinki piksela. Rub je poseban slucaj plato
    if(fieldOfView <= 0.0 && !automaticallyCalibrated && reprojectionOf.size() > 2){
        double lowest = reprojectionOf.front(), highest = reprojectionOf.front();
        for(double one : reprojectionOf){
            lowest = std::min(lowest, one);
            highest = std::max(highest, one);
        }

        //Koliko ih je unutar jednog postotka od najboljeg - ako ih je vise, izbor nije mjerenje
        size_t tied = 0;
        for(double one : reprojectionOf) if(one <= 1.01 * lowest) ++tied;

        const bool atEdge = bestFov <= candidates.front() + 1e-6 || bestFov >= candidates.back() - 1e-6;
        if(atEdge || tied > 1){
            std::printf("  UPOZORENJE: vidno polje NIJE ODREDJENO.\n");
            if(tied > 1){
                std::printf("             %zu kandidata je unutar jednog postotka od najboljeg "
                            "(%.3f do %.3f px) - mjera je ondje ravna.\n",
                            tied, lowest, highest);
            }
            if(atEdge){
                std::printf("             Najbolji je na rubu raspona (%.0f do %.0f st).\n",
                            candidates.front(), candidates.back());
            }
            std::printf("             Kriva zarisna se s reprojekcijom trguje: scena se izoblici "
                        "tako da opazanja i dalje pasu.\n");
            std::printf("             Zadaj vidno polje cetvrtim argumentom ili predaj cameras.txt.\n");
        }
    }

    phaseClock.mark("brzi kandidat");
    //Pobjednik dobiva punu obradu: vise pocetnih parova i popravak sava. Tek se tu placa ono sto
    //bi puta sest kandidata bilo neupotrebljivo
    if(best.ok){
        const auto polished = solveWith(bestFov, true);
        //PUNA OBRADA NIJE NUZNO BOLJA. Gradi iz drugih pocetnih parova, i na 60 kadrova C0257 je
        //brzi kandidat (79807 tocaka, 1.19 px) zamijenila krivim rjesenjem od 30525 tocaka. Mjerilo
        //je isto kao medju pokusajima u Engine::reconstruct: kamere, pa objasnjene tocke
        const bool polishedBetter = polished.first.ok &&
            (polished.first.posedCameras > best.posedCameras ||
             (polished.first.posedCameras == best.posedCameras &&
              double(polished.first.solvedPoints) >= 0.9 * double(best.solvedPoints)));
        if(polished.first.ok && !polishedBetter){
            std::printf("  puna obrada odbacena: %u kamera, %u tocaka, reprojekcija %.3f px - brzi kandidat ostaje\n",
                        polished.first.posedCameras, polished.first.solvedPoints, polished.first.medianReprojection);
            for(const Engine::Reconstruction::Trial& trial : polished.first.trials){
                std::printf("    pocetni par %u-%u (%u tocaka pod %.2f st): %u kamera, %u tocaka, baza %.2f st, reprojekcija %.3f px\n",
                            trial.initialA, trial.initialB, trial.initialPoints, trial.initialAngle,
                            trial.posedCameras, trial.solvedPoints, trial.medianTriangulationAngle, trial.medianReprojection);
            }
        }
        if(polishedBetter){
            best = polished.first;
            bestIntrinsics = polished.second;
            heldOutFromFast = true;
            if(measureHeldOut){
                Engine::ReconstructConfig measure = reconstructionConfig(true);
                measure.forceInitialA = best.initialA;
                measure.forceInitialB = best.initialB;
                measure.initialPairTrials = 1;
                measure.holdOutEvery = 10;
                const Engine::Reconstruction measured = Engine::reconstruct(solveObservations, cameraCount, pointCount,
                                                                            bestIntrinsics, measure);
                if(measured.ok && measured.heldOutObservations > 0){
                    heldOutCount = measured.heldOutObservations;
                    heldOutError = measured.heldOutReprojection;
                    heldOutAgainst = measured.medianReprojection;
                    heldOutFromFast = false;
                }
            }
            std::printf("  nakon pune obrade: %u od %u kamera, %u tocaka, reprojekcija %.3f px, baza %.2f st\n",
                        best.posedCameras, cameraCount, best.solvedPoints,
                        best.medianReprojection, best.medianTriangulationAngle);
            printReconstructTiming(best);
            for(const Engine::Reconstruction::Trial& trial : best.trials){
                std::printf("    pocetni par %u-%u (%u tocaka pod %.2f st): %u kamera, %u tocaka, baza %.2f st, reprojekcija %.3f px%s\n",
                            trial.initialA, trial.initialB, trial.initialPoints, trial.initialAngle,
                            trial.posedCameras, trial.solvedPoints,
                            trial.medianTriangulationAngle, trial.medianReprojection,
                            trial.initialA == best.initialA && trial.initialB == best.initialB ? "  <- izabran" : "");
            }
            if(best.seamsFound){
                std::printf("  savova nadjeno %u, prvi kod kadra %u, rastavljanje %s\n",
                            best.seamsFound, best.seamAt, best.seamRepaired ? "pomoglo" : "nije pomoglo");
            }
        }
    }

    phaseClock.mark("puna obrada");
    //=====================================================================================
    // USPRAVNO, prije svega sto ide van - vidi Engine/Upright.h.
    //
    // Rekonstrukcija je dosad bila u sustavu prve kamere: nagnuta koliko god je prva kamera bila
    // nagnuta, u svakom alatu nizvodno. Ovdje se okrene tako da je prosjecni "gore" kamera +Y, prva
    // kamera gleda niz -Z, a ishodiste je u sredistu scene. Transformacija je kruta, pa se nijedna
    // reprojekcija ne mijenja - a pune slicice, USD, COLMAP tekst i splat dobiju isti sustav
    //=====================================================================================
    if(best.ok){
        const Engine::UprightFrame upright =
            Engine::uprightFrame(best.poses, best.posed, best.points, best.solved);
        if(upright.applied){
            Engine::applyUpright(upright, best);
            std::printf("  uspravno: scena je bila nagnuta %.1f st, sada je prosjecni gore kamera +Y "
                        "(sloznost %.3f)\n", upright.tiltDegrees, upright.coherence);
        }else{
            std::printf("  uspravno: NIJE primijenjeno - kamere se ne slazu oko smjera gore "
                        "(sloznost %.3f, prag 0.5); scena ostaje u sustavu prve kamere\n",
                        upright.coherence);
        }
    }

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

    //=====================================================================================
    // KOLIKO SE RJESENJU SMIJE VJEROVATI, bez ikakve istine.
    //
    // Reprojekcija nad opazanjima koja su rjesenje GRADILA kaze samo koliko se ono slaze sa sobom,
    // a to je vise puta bila laz - krivo rjesenje se sa sobom slaze jednako dobro kao ispravno, i
    // bundle je ta ista opazanja duzan objasniti jer ih minimizira.
    //
    // Izdvojena opazanja nisu usla ni u triangulaciju, ni u bundle, ni u ciscenje. Omjer je
    // referentni broj.
    //
    // Izmjereno na nacrtanim snimkama gdje je istina poznata (TruthBench):
    //
    //   luk, ispravno rjesenje          omjer 1.42
    //   prolaz, ispravno rjesenje       omjer 1.52
    //   zaokret u mjestu, bez paralakse omjer 3.33, i samo 4 izdvojena opazanja prezive
    //
    // Dakle zdravo je oko jedan i pol; bitno vise znaci da je bundle upio sum umjesto scene
    //=====================================================================================
    if(best.camerasDroppedBySeam > 0){
        std::printf("  ODREZANO NA ZDRAVI ODSJECAK: zadrzani kadrovi %u do %u, odbaceno %u kamera.\n",
                    best.healthyFrom, best.healthyTo, best.camerasDroppedBySeam);
        std::printf("             Lanac se ondje presidrio i popravak ga nije pomirio, pa bi "
                    "rjesenje preko loma bilo tiho krivo.\n");
    }

    if(heldOutCount > 0 && heldOutAgainst > 0.0){
        const double ratio = heldOutError / heldOutAgainst;
        std::printf("  provjera bez istine%s: %u izdvojenih opazanja, reprojekcija %.3f px "
                    "naspram %.3f na koristenima - omjer %.2f\n",
                    heldOutFromFast ? " (BRZOG KANDIDATA, ne isporucenog - vidi --measure-held-out)" : "",
                    heldOutCount, heldOutError, heldOutAgainst, ratio);
        if(ratio > 2.5){
            std::printf("             UPOZORENJE: rjesenje se bitno slabije slaze s onim sto nije "
                        "vidjelo (zdravo je oko 1.5).\n");
        }
    }

    //PROSTORNI OSTACI. Jedan medijan reprojekcije ne vidi razliku izmedju bijelog suma i polja
    //koje pinhole kamera nikako ne moze objasniti. Stabilizacija i rolling shutter daju polje
    //koje se mijenja po kadru; neispravljena leca daje isto polje kroz cijelu snimku.
    if(best.ok){
        const Engine::ResidualFieldResult field = Engine::analyzeResidualField(
            solveObservations, best.poses, best.points, bestIntrinsics, {}, best.observationUsed);
        std::printf("  polje ostataka: %s; prostorni signal %.3f px, staticko %.3f px, "
                    "promjenjivi signal %.3f px (sum sredine %.3f, prag %.3f; %u/%u kadrova)\n",
                    Engine::residualDiagnosisName(field.diagnosis), field.spatialSignalRms,
                    field.staticRms, field.temporalSignalRms, field.meanNoiseRms,
                    field.decisionThreshold, field.evaluatedFrames, cameraCount);
        if(field.diagnosis == Engine::ResidualDiagnosis::Static){
            std::printf("             UPOZORENJE: ostatak ovisi o mjestu u slici i ponavlja se "
                        "kroz kadrove. Provjeri distorziju objektiva.\n");
        }else if(field.diagnosis == Engine::ResidualDiagnosis::Changing){
            std::printf("             UPOZORENJE: polje ostataka mijenja se po kadru. "
                        "Pinhole model ne vrijedi; moguca stabilizacija ili rolling shutter.\n");
        }else if(field.diagnosis == Engine::ResidualDiagnosis::InsufficientData){
            std::printf("             Polje nema dovoljno popunjenih celija za dijagnozu.\n");
        }
    }

    //Isto rjesenje s zarisnom +-25 %: koliko se promijeni SMJER putanje. Dva solvea nemaju
    //zajednicko promjenjivo stanje: samo citaju opazanja, a svaki RANSAC ima svoj lokalni RNG s
    //fiksnim seedem. Zato krecu istodobno, ali se rezultati uzimaju i ispisuju starim redom.
    auto diagnosticSolve = [&](double factor){
        return std::async(std::launch::async, [&, factor]{
            const double fov = std::min(120.0, bestFov * factor);
            return solveWith(fov, false);
        });
    };
    if(focalDiagnostic){
        auto lowerDiagnostic = diagnosticSolve(0.75);
        auto upperDiagnostic = diagnosticSolve(1.25);

        auto printDiagnostic = [&](double factor, const auto& other){
            const auto otherShape = pathShape(other.first);
            std::printf("  zarisna x%.2f: %u kamera, reprojekcija %.3f px, skretanje %.2f st\n",
                        factor, other.first.posedCameras, other.first.medianReprojection, otherShape.first);
        };
        {
            const auto lower = lowerDiagnostic.get();
            printDiagnostic(0.75, lower);
        }
        {
            const auto upper = upperDiagnostic.get();
            printDiagnostic(1.25, upper);
        }
    }

    // -------------------------------------------------------------------------------
    // Izvoz: COLMAP i SLIKE
    //
    // Trener splatova treba oboje - poze bez piksela nemaju sto optimizirati. Slike se pisu iz
    // drugog prolaza kroz snimku, a ne iz memorije: stotinu kadrova u 4K je gigabajt i pol, a
    // ponovno dekodiranje traje sekunde. Imena moraju biti tocno ona koja je ColmapExport upisao
    // u images.txt, inace trener trazi datoteke kojih nema
    // -------------------------------------------------------------------------------

    if(!outputDirectory.empty() && best.ok){
        std::filesystem::create_directories(outputDirectory);

        const std::filesystem::path imageDirectory = std::filesystem::path(outputDirectory) / "images";
        if(!cameraOnly) std::filesystem::create_directories(imageDirectory);

        //Koji kadar snimke odgovara kojoj kameri. Ime mora biti ono koje je ColmapExport upisao u
        //images.txt, a on numerira po INDEKSU KAMERE - ne po redu rijesenih. Kad jedna kamera
        //padne, ta se dva reda raziđu i trener bi ucio krive slike uz prave poze
        std::vector<uint32_t> placeOfFrame(used, uint32_t(-1));
        for(uint32_t place = 0; place < cameraCount; ++place){
            if(best.posed[place]) placeOfFrame[keyframeFrames[place]] = place;
        }

        //BOJE TOCAKA SE SKUPLJAJU U ISTOM PROLAZU. Trener iz points3D.txt cita i boju, a ona
        //postaje pocetna boja gaussiane - bez nje scena krece jednolicno siva i trener ju mora
        //cijelu prebojiti. Kadrovi se pamte smanjeni cetiri puta: boja ne treba punu razlucivost,
        //a stotinu 4K kadrova u boji je gigabajta i pol
        constexpr uint32_t shrinkColour = 4;
        std::vector<std::vector<uint8_t>> colourStore(cameraCount);
        std::vector<Engine::ColourImage> colourImages(cameraCount);

        //=================================================================================
        // ZAPIS USPOREDO. Jedan 4K PNG je 1.4 s kodiranja (stb, razina 6), a kadrova je 229 - pa
        // je zapis slika bio vise minuta na jednoj dretvi. Kadar se preda dretvi i citanje ide
        // dalje; najvise dvanaest ih je u letu (po 33 MB). Datoteke su iste do bajta
        //=================================================================================
        std::vector<std::future<void>> writing;
        auto waitForSlot = [&](size_t most){
            while(writing.size() > most){
                writing.front().get();
                writing.erase(writing.begin());
            }
        };

        phaseClock.mark("provjere");
        const auto exportStarted = std::chrono::steady_clock::now();
        Spool::VideoReader again(path);
        uint32_t fileIndex = 0, trackedIndex = 0, written = 0;
        while(!again.atEnd() && trackedIndex < used){
            const Spool::Image frame = again.readNext();
            if(frame.pixels.empty()) break;
            if(fileIndex++ % step != 0) continue;
            const uint32_t tracked = trackedIndex++;

            const uint32_t place = placeOfFrame[tracked];
            if(place == uint32_t(-1)) continue;

            char name[64];
            std::snprintf(name, sizeof(name), "frame_%04u.png", place);
            std::vector<uint8_t> flatPixels;
            const uint8_t* exportPixels = frame.pixels.data();
            if(havePhysicalLens && (physicalLens.k1 != 0.0f || physicalLens.k2 != 0.0f)){
                flatPixels = Engine::undistortRgba(frame.pixels.data(), frame.width, frame.height,
                                                   frame.width, physicalLens);
                if(flatPixels.empty()){
                    std::printf("Ne mogu ispraviti distorziju izlazne slike %s\n", name);
                    return 1;
                }
                exportPixels = flatPixels.data();
            }

            const uint32_t w = frame.width / shrinkColour, h = frame.height / shrinkColour;
            colourStore[place].assign(size_t(w) * h * 4, 0);
            for(uint32_t y = 0; y < h; ++y){
                for(uint32_t x = 0; x < w; ++x){
                    const uint8_t* from = exportPixels
                        + (size_t(y * shrinkColour) * frame.width + size_t(x * shrinkColour)) * 4;
                    uint8_t* to = colourStore[place].data() + (size_t(y) * w + x) * 4;
                    to[0] = from[0]; to[1] = from[1]; to[2] = from[2]; to[3] = 255;
                }
            }
            colourImages[place] = Engine::ColourImage{colourStore[place].data(), w, h, w};
            //Boje su vec izvadjene, pa pikseli mogu otici dretvi koja pise
            if(!cameraOnly){
                waitForSlot(11);
                std::vector<uint8_t> owned = flatPixels.empty() ? frame.pixels : std::move(flatPixels);
                writing.push_back(std::async(std::launch::async,
                    [target = (imageDirectory / name).string(), pixels = std::move(owned), width = frame.width, height = frame.height]{
                        Spool::savePng(target, Spool::imageFromPixels(pixels.data(), width, height));
                    }));
                ++written;
            }
        }
        waitForSlot(0);
        if(cameraOnly) std::printf("Samo kamera: slike kadrova se ne zapisuju (splat ih trazi, matchmove ne)\n");
        else std::printf("Zapisano %u slika u %s (%.1f s s citanjem snimke)\n", written, imageDirectory.string().c_str(),
                         std::chrono::duration<double>(std::chrono::steady_clock::now() - exportStarted).count());

        //=================================================================================
        // IZVOZE SE OPAZANJA S KOJIMA JE RIJESENO, ne ona iz trackera.
        //
        // Ovdje je stajalo keys.observations - tragovi iz pracenja - dok je rjesenje nastalo iz
        // GRAFA POKLAPANJA, a on ima posve druge brojeve tocaka. points3D.txt se pise tako da se
        // za svaku tocku skupe kadrovi koji ju vide; s krivim brojevima to znaci da se polozaj
        // jedne tocke spoji s tragom druge.
        //
        // Ne vidi se u ispisu solvea, jer on javlja svoju reprojekciju - a ta je tocna. Vidi se
        // tek kad se izvezeni model procita natrag: ModelInfo je na ovoj snimci javio reprojekciju
        // od 1314 px ondje gdje je solver javio 1.312, i duljinu traga 1.0 kadar.
        //
        // Distorzija: izvozi se pinhole, pa idu ISPRAVLJENA opazanja - ista ona s kojima se racunalo
        //=================================================================================
        const std::vector<glm::u8vec3> colours =
            Engine::pointColours(best, solveObservations, colourImages, shrinkColour);


        //=================================================================================
        // I USD, ZA VFX ALAT. COLMAP tekst ide treneru splatova, ali u Nuke, Houdini ili Blender
        // ne ide nista - a bez toga rjesenje ne izlazi iz naseg lanca u alat u kojem se radi
        // kompozit. USD to rjesava bez ijedne nove ovisnosti: punu matricu nosi sam tekst, pa
        // nema Eulerovog redoslijeda koji se moze promasiti.
        //
        // Zarisna se pise u STVARNIM milimetrima kad se zna senzor - a zna se otkad se cita
        // pratece XML uz snimku
        //=================================================================================
        {
            phaseClock.mark("slike kadrova");
            //=============================================================================
            // PUNE SLICICE - vidi localiseEveryFrame. Solve daje pozu svakog step-tog kadra;
            // match-move trazi svaki. Medjukadrovi se lokaliziraju, ne rekonstruiraju.
            //=============================================================================
            Engine::Reconstruction forExport = best;
            int exportStep = int(step);


            if(step > 1 && !keyframesOnly){
                const auto denseStarted = std::chrono::steady_clock::now();
                const DenseTrack dense = localiseEveryFrame(path, step, best, solveObservations,
                                                            keyframeFrames, bestIntrinsics);
                const double denseSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - denseStarted).count();

                const uint32_t between = dense.localised + dense.failed;
                if(dense.localised > 0 && between > 0){
                    std::printf("  pune slicice: %u od %u medjukadrova lokalizirano (%.1f %%), "
                                "medijan %u prenesenih tocaka, %.1f s\n",
                                dense.localised, between,
                                100.0 * double(dense.localised) / double(between),
                                dense.medianKept, denseSeconds);

                    forExport.poses = dense.poses;
                    forExport.posed = dense.posed;
                    exportStep = 1;

                }else{
                    std::printf("  pune slicice nisu uspjele (%u medjukadrova), izvozi se svaki %u. kadar\n",
                                between, step);
                }
            }

            Engine::UsdExportConfig usdConfig;
            usdConfig.firstFrame = 1;
            usdConfig.frameStep = exportStep;
            usdConfig.framesPerSecond = info.frameRate() > 0.0 ? info.frameRate() : 25.0;
            usdConfig.sensorWidthMillimetres = hints.sensorWidthMillimetres;

            std::vector<glm::vec3> usdColours;
            usdColours.reserve(colours.size());
            for(const glm::u8vec3& colour : colours){
                usdColours.push_back(glm::vec3(colour) / 255.0f);
            }

            //KONACNI SNIMAK za loom: uspravan, i ovaj put s bojama. Tijekom rasta boje nije bilo
            //jer ona trazi slike, a tek su sada sve procitane
            {
                Loom::Snapshot finalSnapshot;
                for(size_t c = 0; c < best.poses.size(); ++c){
                    if(c < best.posed.size() && !best.posed[c]) continue;
                    finalSnapshot.cameras.push_back(best.poses[c].position);
                    finalSnapshot.orientations.push_back(best.poses[c].orientation);
                }
                for(size_t p = 0; p < best.points.size(); ++p){
                    if(p < best.solved.size() && !best.solved[p]) continue;
                    finalSnapshot.points.push_back(best.points[p]);
                    if(p < colours.size()) finalSnapshot.colours.push_back(colours[p]);
                }
                if(finalSnapshot.colours.size() != finalSnapshot.points.size()) finalSnapshot.colours.clear();
                Loom::writeSnapshot(outputDirectory + "/napredak.bin", finalSnapshot);
            }

            const std::string usdPath = outputDirectory + "/kamera.usda";
            if(Engine::writeUsdScene(usdPath, forExport, bestIntrinsics, info.width, info.height,
                                     usdColours, usdConfig)){
                std::printf("Zapisan %s (kamera i tocke za Nuke/Houdini/Blender%s)\n",
                            usdPath.c_str(),
                            hints.sensorWidthMillimetres > 0.0 ? ", zarisna u pravim mm" : "");
            }
        }

        phaseClock.mark("pune slicice i USD");
        if(Engine::writeColmapText(outputDirectory, best, bestIntrinsics, solveObservations, {}, colours)){
            std::printf("Zapisano u %s (cameras.txt, images.txt, points3D.txt)\n", outputDirectory.c_str());

            //=============================================================================
            // ROLLING SHUTTER: koliko traje citanje senzora i poze gornjeg i donjeg retka za
            // trener - nad upravo zapisanim modelom i kamera.usda (Engine/RollingShutter.h)
            //=============================================================================
            //METAPODACI KAMERE (Spool/CameraMetadata.h): vrijeme ekspozicije i citanja za trener -
            //zamucenje pokretom i rolling shutter se modeliraju tek kad se zna koliko traju
            {
                //Iz koje snimke je model - dnevnik mjerenja (tools/bench/mjerenja.py) po tome zna na cemu se mjerilo
                std::ofstream(std::filesystem::path(outputDirectory) / "izvor.txt") << path << "\n";

                const Spool::CameraMetadata camera = Spool::readCameraMetadata(path);
                if(camera.present){
                    std::ofstream note(std::filesystem::path(outputDirectory) / "camera_metadata.txt");
                    note << "source " << camera.source << "\nframes_per_second " << camera.framesPerSecond
                         << "\nexposure_seconds " << camera.exposureSeconds << "\nexposure_frames " << camera.exposureFrames()
                         << "\nreadout_frames_metadata " << camera.readoutFrames << "\n";
                    std::printf("  metapodaci kamere (%s): ekspozicija 1/%.0f s = %.2f kadra, citanje %.3f kadra\n",
                                camera.source.c_str(), camera.exposureSeconds > 0.0 ? 1.0 / camera.exposureSeconds : 0.0,
                                camera.exposureFrames(), camera.readoutFrames);
                }
            }
            if(rollingShutter){
                const auto rollingStarted = std::chrono::steady_clock::now();
                Engine::RollingShutterResult rolling;
                std::string rollingReport;
                const bool rollingOk = Engine::rollingShutterForResult(outputDirectory, rolling, rollingReport);
                std::printf("  rolling shutter: %s, %.1f s%s\n", rollingReport.c_str(),
                            std::chrono::duration<double>(std::chrono::steady_clock::now() - rollingStarted).count(),
                            rollingOk && rolling.used ? " - rs_top/rs_bottom zapisani" : "");
            }

            //=============================================================================
            // IZLAZ SE CITA NATRAG I PROVJERAVA.
            //
            // Solver javlja SVOJU reprojekciju, i ona je tocna ma sto se poslije zapisalo. Kad je
            // izvoz pisao opazanja iz trackera umjesto onih s kojima je rijeseno, ispis solvea je
            // i dalje pokazivao 1.312 px dok je zapisani model imao 1314 px - i to se vidjelo tek
            // kad je netko procitao izlaz natrag.
            //
            // Zato ovo stoji ovdje, a ne u testu: greska nije bila u knjiznici nego u tome STO joj
            // je predano, a to nijedan test knjiznice ne vidi
            //=============================================================================
            Engine::ColmapModel written;
            if(Engine::readColmapText(outputDirectory, written)){
                std::vector<double> errors;
                errors.reserve(written.observations.size());
                for(const Engine::Observation& one : written.observations){
                    if(one.camera >= written.reconstruction.poses.size()) continue;
                    if(one.point >= written.reconstruction.points.size()) continue;
                    if(!written.reconstruction.posed[one.camera]) continue;
                    if(!written.reconstruction.solved[one.point]) continue;

                    glm::vec2 pixel;
                    if(!Engine::project(written.reconstruction.poses[one.camera], written.intrinsics,
                                        written.reconstruction.points[one.point], pixel)) continue;
                    errors.push_back(double(glm::length(pixel - one.pixel)));
                }
                std::sort(errors.begin(), errors.end());
                const double readBack = errors.empty() ? 0.0 : errors[errors.size() / 2];

                std::printf("  provjera zapisanog: %zu opazanja, reprojekcija %.3f px "
                            "(solver je javio %.3f)\n",
                            errors.size(), readBack, best.medianReprojection);

                if(errors.empty() || readBack > 2.0 * std::max(1.0, best.medianReprojection)){
                    std::printf("  UPOZORENJE: zapisani model se ne slaze sa solverom. "
                                "Poze su vjerojatno u redu, oblak tocaka nije.\n");
                }
            }
        }
    }
    phaseClock.mark("COLMAP i provjera");
    phaseClock.print();
    return 0;
}
