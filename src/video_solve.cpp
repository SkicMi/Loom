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
#include <Spool/ImageFile.h>
#include <Spool/VideoFile.h>

#include <Engine/CameraHints.h>
#include <Engine/ColmapExport.h>
#include <Engine/ColmapImport.h>
#include <Engine/MatchGraph.h>
#include <Engine/MergeTracks.h>
#include <Engine/Keyframes.h>
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
        std::printf("Upotreba: VideoSolve snimka.mp4 [korak] [kadrova] [vidno polje] [izlazna mapa] [cameras.txt] [graf|graf-bez-mjerila|spajanje|bez-spajanja]\n");
        std::printf("  zadano je graf: uglovi za pokrivenost i prostor mjerila za tocnost, spojeni\n");
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

    Spool::VideoReader reader(path);
    const Spool::VideoInfo& info = reader.info();
    std::printf("Snimka %ux%u, %.2f fps, %s\n", info.width, info.height, info.frameRate(), info.codec.c_str());
    std::printf("  uzimam svaki %u. kadar, najvise %u\n", step, wanted);

    // -------------------------------------------------------------------------------
    // Kadrovi -> tragovi
    // -------------------------------------------------------------------------------

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
    trackConfig.window = std::max(6u, uint32_t(info.width) / 320u);

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

    //Kljucni kadrovi - vidi Engine/Keyframes.h. Izbor ne ovisi o zarisnoj pa se radi jednom
    const Engine::KeyframeSelection keys = Engine::chooseKeyframes(tracker.observations(), used, info.width);
    std::printf("  kljucnih kadrova %zu od %u (medijan paralakse %.1f px)\n",
                keys.frames.size(), used, keys.medianParallaxPixels);
    if(keys.frames.size() < 3){
        std::printf("Premalo kljucnih kadrova.\n");
        return 1;
    }
    const uint32_t cameraCount = uint32_t(keys.frames.size());

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
    float featurePixels = 1.0f;

    std::vector<Engine::Observation> observations = keys.observations;
    uint32_t pointCount = tracker.trackCount();

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

            //keys.frames nosi redne brojeve KORISTENIH kadrova, istim redom kojim su prosli kroz
            //tracker - pa se broji isto kako se brojalo tada
            if(taken < cameraCount && keys.frames[taken] == seen){
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
                graphConfig.detect.maxCorners = 20000;
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

                    const Engine::MatchGraphResult fine =
                        Engine::buildMatchGraph(keyframeImages, guess, fineConfig);
                    std::printf("  prostor mjerila: %u znacajki, %u tocaka, %zu opazanja\n",
                                fine.featuresTotal, fine.pointCount, fine.observations.size());

                    if(fine.pointCount > 0) graph = Engine::mergeGraphs(graph, fine);
                }

                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

                std::printf("  graf poklapanja: %u znacajki, %u tocaka, %zu opazanja (%.0f po kadru), "
                            "%u od %u parova proslo geometriju, medijan %.0f parova, %.1f s\n",
                            graph.featuresTotal, graph.pointCount, graph.observations.size(),
                            double(graph.observations.size()) / double(cameraCount),
                            graph.acceptedFrames, graph.comparedFrames, graph.medianMatchesPerPair, seconds);

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

    //KALIBRACIJA, kad je zadana. Jednom izmjerena za tijelo i objektiv vrijedi za svaku sljedecu
    //snimku istom kamerom, pa je nema smisla pogadjati iz vidnog polja u svakoj
    Engine::Intrinsics measured;
    std::string measuredModel;
    const bool calibrated = !calibrationFile.empty() &&
                            Engine::readColmapCamera(calibrationFile, measured, measuredModel);
    if(!calibrationFile.empty() && !calibrated){
        std::printf("Ne mogu procitati kalibraciju iz %s\n", calibrationFile.c_str());
        return 1;
    }

    //OPAZANJA SE ISPRAVE JEDNOM, na ulazu. Solver racuna s ravnom lecom - triangulacija, PnP i
    //bundle svi pretpostavljaju ravnu zraku - pa je jedino mjesto gdje distorzija smije postojati
    //ovdje, izmedju mjerenja i racuna. Da se nosi kroz svaki korak, svaki bi ju morao znati
    std::vector<Engine::Observation> solveObservations = observations;
    if(calibrated && (measured.k1 != 0.0f || measured.k2 != 0.0f)){
        double worst = 0.0;
        for(Engine::Observation& one : solveObservations){
            const glm::vec2 fixed = Engine::undistort(measured, one.pixel);
            worst = std::max(worst, double(glm::length(fixed - one.pixel)));
            one.pixel = fixed;
        }
        std::printf("  ispravljena distorzija k1 %.5f k2 %.5f - najveci pomak %.2f px\n",
                    double(measured.k1), double(measured.k2), worst);
    }

    auto solveWith = [&](double fov){
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

        Engine::ReconstructConfig config;
        config.huberPixels = 2.0;

        //PRAG PRATI TOCNOST ZNACAJKE. Graf poklapanja radi na smanjenoj slici, pa su polozaji
        //tocni na onoliko piksela koliko je smanjenje - i prag mora biti veci od toga, inace se
        //odbijaju kamere koje su tocne koliko podatak uopce dopusta. Izmjereno na 30 kadrova
        //prave snimke uz tocnost od 4 px: prag 4 daje 5 od 30 kamera, prag 8 daje 30 od 30
        config.acceptPixels = std::max(6.0, 2.0 * double(featurePixels));
        config.minPointsForPose = 20;
        return std::make_pair(Engine::reconstruct(solveObservations, cameraCount, pointCount,
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
                    fov, double(result.second.fx), state.posedCameras, cameraCount, state.solvedPoints, state.medianReprojection);

        const bool better = state.posedCameras > best.posedCameras ||
                            (state.posedCameras == best.posedCameras && state.medianReprojection < best.medianReprojection);
        if(!best.ok || better){
            best = state;
            bestIntrinsics = result.second;
            bestFov = fov;
        }
    }

    std::printf("\nNajbolje: vidno polje %.1f st, %u od %u kamera, %u tocaka, reprojekcija %.3f px\n",
                bestFov, best.posedCameras, cameraCount, best.solvedPoints, best.medianReprojection);

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
        if(Engine::writeColmapText(outputDirectory, best, bestIntrinsics, keys.observations)){
            std::printf("Zapisano u %s (cameras.txt, images.txt, points3D.txt)\n", outputDirectory.c_str());
        }

        const std::filesystem::path imageDirectory = std::filesystem::path(outputDirectory) / "images";
        std::filesystem::create_directories(imageDirectory);

        //Koji kadar snimke odgovara kojoj kameri. Ime mora biti ono koje je ColmapExport upisao u
        //images.txt, a on numerira po INDEKSU KAMERE - ne po redu rijesenih. Kad jedna kamera
        //padne, ta se dva reda raziđu i trener bi ucio krive slike uz prave poze
        std::vector<uint32_t> placeOfFrame(used, uint32_t(-1));
        for(uint32_t place = 0; place < cameraCount; ++place){
            if(best.posed[place]) placeOfFrame[keys.frames[place]] = place;
        }

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
            Spool::savePng((imageDirectory / name).string(),
                           Spool::imageFromPixels(frame.pixels.data(), frame.width, frame.height));
            ++written;
        }
        std::printf("Zapisano %u slika u %s\n", written, imageDirectory.string().c_str());
    }
    return 0;
}
