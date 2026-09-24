#include "Engine/Bands.h"
#include "Engine/MatchGraph.h"
#include "Engine/Track.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <numeric>
#include <unordered_map>

namespace Engine{
namespace{

struct Groups{
    std::vector<uint32_t> parent;
    explicit Groups(size_t count) : parent(count){ std::iota(parent.begin(), parent.end(), 0u); }

    uint32_t find(uint32_t a){
        while(parent[a] != a){ parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    }
    void join(uint32_t a, uint32_t b){
        a = find(a); b = find(b);
        if(a != b) parent[b] = a;
    }
};

//Jedan provjereni brid: dvije znacajke koje su isto, i koliko su im potpisi bili blizu
struct Edge{
    uint32_t a = 0, b = 0;
    uint32_t distance = 0;
    uint32_t support = 0;   //koliko ga je trecih kadrova potvrdilo - vidi minTriangleSupport
};

//Koje kadrove komponenta vec drzi. Trag je kratak - nekoliko kadrova - pa je obicno polje brze od
//svakog skupa, a i cuva poredak, sto ovdje nije svejedno
struct FrameSets{
    std::vector<std::vector<uint32_t>> frames;
    explicit FrameSets(size_t count) : frames(count){}

    bool wouldClash(const std::vector<uint32_t>& one, const std::vector<uint32_t>& two) const {
        for(uint32_t frame : one){
            for(uint32_t other : two){
                if(frame == other) return true;
            }
        }
        return false;
    }
};

}

MatchGraphResult mergeGraphs(const MatchGraphResult& first, const MatchGraphResult& second){
    MatchGraphResult out;
    out.observations = first.observations;
    out.observations.reserve(first.observations.size() + second.observations.size());
    for(const Observation& one : second.observations){
        out.observations.push_back(Observation{one.camera, one.point + first.pointCount, one.pixel});
    }
    out.pointCount = first.pointCount + second.pointCount;

    //Tocnost je ona grublja - vidi zaglavlje
    out.localizationPixels = std::max(first.localizationPixels, second.localizationPixels);

    //Brojaci se zbrajaju, jer opisuju posao koji je stvarno napravljen u oba
    out.comparedFrames = first.comparedFrames + second.comparedFrames;
    out.acceptedFrames = first.acceptedFrames + second.acceptedFrames;
    out.featuresTotal = first.featuresTotal + second.featuresTotal;
    out.conflictingPoints = first.conflictingPoints + second.conflictingPoints;
    out.conflictingObservations = first.conflictingObservations + second.conflictingObservations;
    out.mergedObservations = first.mergedObservations + second.mergedObservations;
    out.refusedEdges = first.refusedEdges + second.refusedEdges;
    out.unwitnessedEdges = first.unwitnessedEdges + second.unwitnessedEdges;
    out.splitPoints = first.splitPoints + second.splitPoints;
    out.droppedWeakEdges = first.droppedWeakEdges + second.droppedWeakEdges;
    out.refinedObservations = first.refinedObservations + second.refinedObservations;
    out.unrefinedObservations = first.unrefinedObservations + second.unrefinedObservations;
    out.unrefinedTracks = first.unrefinedTracks + second.unrefinedTracks;
    out.preprocessSeconds = first.preprocessSeconds + second.preprocessSeconds;
    out.featureSeconds = first.featureSeconds + second.featureSeconds;
    out.detectionSeconds = first.detectionSeconds + second.detectionSeconds;
    out.descriptorSeconds = first.descriptorSeconds + second.descriptorSeconds;
    out.descriptorSmoothingSeconds = first.descriptorSmoothingSeconds + second.descriptorSmoothingSeconds;
    out.descriptorGradientSeconds = first.descriptorGradientSeconds + second.descriptorGradientSeconds;
    out.descriptorBuildSeconds = first.descriptorBuildSeconds + second.descriptorBuildSeconds;
    out.matchingSeconds = first.matchingSeconds + second.matchingSeconds;
    out.geometrySeconds = first.geometrySeconds + second.geometrySeconds;
    out.assemblySeconds = first.assemblySeconds + second.assemblySeconds;

    //Medijan dvaju medijana nije medijan, pa se uzima veci od dvoje - to je barem broj koji je
    //negdje stvarno izmjeren
    out.medianMatchesPerPair = std::max(first.medianMatchesPerPair, second.medianMatchesPerPair);
    return out;
}

MatchGraphResult buildMatchGraph(const std::vector<GrayImage>& images,
                                 const Intrinsics& intrinsics,
                                 const MatchGraphConfig& config){
    MatchGraphResult result;
    using Clock = std::chrono::steady_clock;
    auto secondsSince = [](Clock::time_point from){
        return std::chrono::duration<double>(Clock::now() - from).count();
    };
    const auto preprocessStarted = Clock::now();
    const uint32_t frames = uint32_t(images.size());
    if(frames < 2) return result;

    // ---------------------------------------------------------------------------------
    // Znacajke po kadru, neovisno
    // ---------------------------------------------------------------------------------

    std::vector<std::vector<glm::vec2>> points(frames);
    std::vector<std::vector<Descriptor>> signatures(frames);
    std::vector<std::vector<SiftDescriptor>> siftSignatures(frames);
    std::vector<uint32_t> offset(frames + 1, 0);

    //ZAKRPA PRATI RAZLUCIVOST, kao i prozor pracenja i iz istog razloga. 256 bitova treba strukturu
    //pod sobom; na 4K je zakrpa od 16 px 0.4 posto sirine slike i potpis ondje mjeri sum.
    //
    //Izmjereno na dva prava kadra, 4000 uglova, koliko parova prezivi geometrijsku provjeru:
    //
    //   zakrpa    16    24    32    48    64    96   128
    //   dobrih    14    29    60   131   172   194   178
    //
    //Cetrnaest puta razlike izmedju 16 i 96. Vrh je na 96, sto je 2.5 posto sirine - isti odnos
    //koji na 480x360 daje 12 px, koliko je ondje i bilo dobro
    //SIRINA NA KOJOJ SE RADI - vidi MatchGraphConfig::workingWidth. Cijeli djelitelj, jer prosjek
    //po kvadratu ne trazi interpolaciju ni odluku o njoj
    uint32_t shrink = 1;
    //PROSTOR MJERILA RADI NA PUNOJ SLICI - vidi MatchGraphConfig::useScaleSpace. Smanjenje ovdje
    //nema smisla: ono postoji zato da detektor ne hvata sum, a detektor mjerila to rjesava sam,
    //time sto znacajku trazi na mjerilu na kojem ona postoji
    if(config.useScaleSpace){
        // shrink ostaje 1
    }else if(config.workingWidth > 0 && images[0].width > config.workingWidth){
        shrink = images[0].width / config.workingWidth;
        if(shrink < 1) shrink = 1;
    }

    std::vector<std::vector<uint8_t>> smallPixels;
    std::vector<GrayImage> working(images);
    if(shrink > 1){
        smallPixels.resize(frames);
        for(uint32_t frame = 0; frame < frames; ++frame){
            const GrayImage& source = images[frame];
            const uint32_t stride = source.stride ? source.stride : source.width;
            const uint32_t width = source.width / shrink;
            const uint32_t height = source.height / shrink;

            smallPixels[frame].assign(size_t(width) * height, 0);
            for(uint32_t y = 0; y < height; ++y){
                for(uint32_t x = 0; x < width; ++x){
                    uint32_t sum = 0;
                    for(uint32_t dy = 0; dy < shrink; ++dy){
                        const uint8_t* row = source.pixels + size_t(y * shrink + dy) * stride;
                        for(uint32_t dx = 0; dx < shrink; ++dx) sum += row[x * shrink + dx];
                    }
                    smallPixels[frame][size_t(y) * width + x] = uint8_t(sum / (shrink * shrink));
                }
            }
            working[frame] = GrayImage{smallPixels[frame].data(), width, height, width};
        }
    }

    //Kamera na smanjenoj slici: zariste i glavna tocka dijele se istim brojem. Dvoprizorna poza
    //se racuna nad tim koordinatama, pa mora dobiti i njima pripadnu kameru
    Intrinsics small = intrinsics;
    if(shrink > 1){
        small.width = working[0].width;
        small.height = working[0].height;
        small.fx = intrinsics.fx / float(shrink);
        small.fy = intrinsics.fy / float(shrink);
        small.cx = (intrinsics.cx - 0.5f * float(shrink - 1)) / float(shrink);
        small.cy = (intrinsics.cy - 0.5f * float(shrink - 1)) / float(shrink);
    }
    result.preprocessSeconds = secondsSince(preprocessStarted);

    DescribeConfig describe = config.describe;
    if(config.patchFromWidth) describe.patch = std::max(12u, working[0].width / 40u);

    //I ZAGLADJIVANJE PRATI ZAKRPU. Zakrpa od 96 px uzorkovana uz sigmu 1.5 uzima sirove piksele
    //sest puta rjedje nego zakrpa od 16 - dakle uzorkuje sum umjesto strukture. Izmjereno na
    //istom paru kadrova pri zakrpi 96: sigma 1.5 daje 255 poklapanja, sigma 8 daje 633
    if(config.patchFromWidth){
        describe.smoothing = std::max(1.5f, float(describe.patch) / 12.0f);
    }

    TrackConfig detect = config.detect;
    if(shrink > 1) detect.minDistance = std::max(2.0f, config.detect.minDistance / float(shrink));

    //Zakrpa i zagladjivanje SIFT-ovog potpisa prate istu sirinu; zagladjivanje se izvodi iz zakrpe
    SiftConfig sift = config.sift;
    if(config.patchFromWidth) sift.patch = describe.patch;

    //=====================================================================================
    // KADROVI USPOREDO. Unutar kadra su zamucivanja i potpisi vec po pojasevima, ali izmedju njih
    // ostaje mnogo serijskog (pretvorba, razlike zagladjenja, prepolovljenje oktave), pa je 28
    // dretvi na 4K kadru radilo kao desetak. Ovdje svaka dretva uzme cijeli kadar i racuna ga u
    // jednom komadu (SerialBands). Kadrovi su medjusobno neovisni i svaki pise samo u svoje mjesto,
    // pa je izlaz isti do bita.
    //
    // NAJVISE DVANAEST ODJEDNOM. Jedan 4K kadar u prostoru mjerila drzi oko 0.4 GB ploha
    //=====================================================================================
    const auto featuresStarted = Clock::now();
    std::vector<double> detectionOf(frames, 0.0), descriptorOf(frames, 0.0);
    std::vector<SiftTiming> timingOf(frames);
    auto featuresOf = [&](uint32_t frame){
        const auto detectionStarted = Clock::now();
        if(config.useScaleSpace){
            const std::vector<Keypoint> keys = detectScaleSpace(working[frame], config.scaleSpace);
            detectionOf[frame] = secondsSince(detectionStarted);
            points[frame].reserve(keys.size());
            std::vector<float> scales;
            scales.reserve(keys.size());
            for(const Keypoint& one : keys){ points[frame].push_back(one.pixel); scales.push_back(one.scale); }
            const auto descriptorStarted = Clock::now();
            siftSignatures[frame] = describeSiftScaled(working[frame], points[frame], scales, sift, &timingOf[frame]);
            descriptorOf[frame] = secondsSince(descriptorStarted);
        }else{
            points[frame] = detectCorners(working[frame], detect);
            detectionOf[frame] = secondsSince(detectionStarted);
            const auto descriptorStarted = Clock::now();
            if(config.useSift) siftSignatures[frame] = describeSiftAll(working[frame], points[frame], sift);
            else               signatures[frame] = describeAll(working[frame], points[frame], describe);
            descriptorOf[frame] = secondsSince(descriptorStarted);
        }
    };
    const uint32_t workerCount = std::min({frames, std::max(1u, std::thread::hardware_concurrency()), 12u});
    if(workerCount <= 1){
        for(uint32_t frame = 0; frame < frames; ++frame) featuresOf(frame);
    }else{
        std::atomic<uint32_t> nextFrame{0};
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for(uint32_t worker = 0; worker < workerCount; ++worker){
            workers.emplace_back([&]{
                SerialBands serial;
                for(uint32_t frame = nextFrame++; frame < frames; frame = nextFrame++) featuresOf(frame);
            });
        }
        for(std::thread& worker : workers) worker.join();
    }
    for(uint32_t frame = 0; frame < frames; ++frame) offset[frame + 1] = offset[frame] + uint32_t(points[frame].size());

    //Faze su izmjerene po dretvi; ovdje se svedu na zidno vrijeme u istom omjeru, da se ispis
    //zbraja kao i prije (detekcija + potpisi = znacajke)
    double threadSeconds = 0.0;
    for(uint32_t frame = 0; frame < frames; ++frame) threadSeconds += detectionOf[frame] + descriptorOf[frame];
    const double toWall = threadSeconds > 0.0 ? secondsSince(featuresStarted) / threadSeconds : 0.0;
    for(uint32_t frame = 0; frame < frames; ++frame){
        result.detectionSeconds += detectionOf[frame] * toWall;
        result.descriptorSeconds += descriptorOf[frame] * toWall;
        result.descriptorSmoothingSeconds += timingOf[frame].smoothingSeconds * toWall;
        result.descriptorGradientSeconds += timingOf[frame].gradientSeconds * toWall;
        result.descriptorBuildSeconds += timingOf[frame].descriptorSeconds * toWall;
    }
    result.localizationPixels = config.useScaleSpace ? 1.0f : float(shrink);
    result.featuresTotal = offset[frames];
    result.featureSeconds = secondsSince(featuresStarted);
    if(result.featuresTotal == 0) return result;

    // ---------------------------------------------------------------------------------
    // Parovi kadrova
    // ---------------------------------------------------------------------------------

    Groups groups(result.featuresTotal);
    std::vector<Edge> edges;
    std::vector<double> perPair;
    const float radius = config.searchFraction * float(working[0].width);
    const bool bySift = config.useSift || config.useScaleSpace;

    //Isti kadar sudjeluje u do config.window parova, a njegova prostorna mreza ovisi samo o
    //znacajkama i stalnom radijusu. Pripremi je jednom umjesto da je ponovno gradi svaki par.
    std::vector<SiftMatchGrid> siftGrids;
    if(bySift && !config.siftPairMatcher){
        const auto matchingStarted = Clock::now();
        siftGrids.reserve(frames);
        for(uint32_t frame = 0; frame < frames; ++frame){
            siftGrids.push_back(prepareSiftMatchGrid(siftSignatures[frame], points[frame], radius));
        }
        result.matchingSeconds += secondsSince(matchingStarted);
    }

    //VANJSKO POKLAPANJE (config.siftPairMatcher): svi parovi odjednom, istim redom kao petlja nize
    std::vector<std::vector<SiftMatch>> external;
    size_t externalNext = 0;
    if(bySift && config.siftPairMatcher){
        std::vector<std::pair<uint32_t, uint32_t>> pairList;
        for(uint32_t a = 0; a < frames; ++a){
            for(uint32_t step = 1; step <= config.window; ++step){
                const uint32_t b = a + step;
                if(b >= frames) break;
                if(siftSignatures[a].empty() || siftSignatures[b].empty()) continue;
                pairList.push_back({a, b});
            }
        }
        const auto matchingStarted = Clock::now();
        external = config.siftPairMatcher(siftSignatures, points, pairList, radius, sift);
        result.matchingSeconds += secondsSince(matchingStarted);
        if(external.size() != pairList.size()) external.assign(pairList.size(), {});
    }

    for(uint32_t a = 0; a < frames; ++a){
        for(uint32_t step = 1; step <= config.window; ++step){
            const uint32_t b = a + step;
            if(b >= frames) break;
            if(bySift){ if(siftSignatures[a].empty() || siftSignatures[b].empty()) continue; }
            else      { if(signatures[a].empty() || signatures[b].empty()) continue; }

            ++result.comparedFrames;

            //Dva potpisa daju dva razlicita zapisa poklapanja; dalje ih zanima samo tko s kim
            std::vector<std::pair<uint32_t, uint32_t>> matches;
            std::vector<uint32_t> distances;
            const auto matchingStarted = Clock::now();
            if(bySift && config.siftPairMatcher){
                for(const SiftMatch& one : external[externalNext++]){
                    matches.push_back({one.from, one.to});
                    distances.push_back(uint32_t(one.distance));
                }
            }else if(bySift){
                for(const SiftMatch& one : matchSiftNear(siftSignatures[a], points[a],
                                                          siftGrids[a], siftSignatures[b], points[b],
                                                          siftGrids[b], radius, sift)){
                    matches.push_back({one.from, one.to});
                    distances.push_back(uint32_t(one.distance));
                }
            }else{
                for(const Match& one : matchDescriptorsNear(signatures[a], points[a],
                                                            signatures[b], points[b], radius, describe)){
                    matches.push_back({one.from, one.to});
                    distances.push_back(one.distance);
                }
            }
            result.matchingSeconds += secondsSince(matchingStarted);
            if(matches.size() < config.minInliers) continue;

            std::vector<glm::vec2> here, there;
            here.reserve(matches.size()); there.reserve(matches.size());
            for(const auto& match : matches){
                here.push_back(points[a][match.first]);
                there.push_back(points[b][match.second]);
            }

            const auto geometryStarted = Clock::now();
            const TwoViewResult pose = relativePoseRobust(here, there, small, config.ransac);
            result.geometrySeconds += secondsSince(geometryStarted);
            if(!pose.solved || pose.inlierCount < config.minInliers) continue;

            ++result.acceptedFrames;
            perPair.push_back(double(pose.inlierCount));

            for(size_t i = 0; i < matches.size(); ++i){
                if(i < pose.inliers.size() && !pose.inliers[i]) continue;
                edges.push_back(Edge{offset[a] + matches[i].first, offset[b] + matches[i].second,
                                     distances[i]});
            }
        }
    }

    if(!perPair.empty()){
        std::sort(perPair.begin(), perPair.end());
        result.medianMatchesPerPair = perPair[perPair.size() / 2];
    }
    const auto assemblyStarted = Clock::now();

    // ---------------------------------------------------------------------------------
    // Bridovi u komponente
    // ---------------------------------------------------------------------------------

    //Kojem kadru pripada koja znacajka - treba spajanju koje odbija sukob
    std::vector<uint32_t> frameOf(result.featuresTotal, 0);
    for(uint32_t frame = 0; frame < frames; ++frame){
        for(uint32_t i = offset[frame]; i < offset[frame + 1]; ++i) frameOf[i] = frame;
    }

    //SUGLASNOST TROJKI. Brid bez svjedoka u trecem kadru nije potvrdjen nicim osim dvoprizornom
    //geometrijom, a ona propusta sve sto lezi na epipolarnoj crti - ukljucujuci krivo poklapanje
    //TROJKA MORA BITI MOGUCA. Svjedok je znacajka u TRECEM kadru, pa je za nju potrebno i da
    //kadrova ima barem tri i da se usporedjuju parovi preko susjednog - uz prozor 1 postoje samo
    //bridovi (i, i+1), a njima trojka ne moze nastati. Bez ove ograde filtar na dva kadra pobrise
    //sve i graf tiho izadje prazan
    const bool trianglesPossible = frames >= 3 && config.window >= 2;

    if(config.minTriangleSupport > 0 && trianglesPossible && !edges.empty()){
        //Susjedi po znacajki. Znacajka ih ima malo - najvise onoliko koliko ima parova u prozoru -
        //pa je presjek dvaju sortiranih popisa jeftin
        std::vector<std::vector<uint32_t>> neighbours(result.featuresTotal);
        for(const Edge& edge : edges){
            neighbours[edge.a].push_back(edge.b);
            neighbours[edge.b].push_back(edge.a);
        }
        for(std::vector<uint32_t>& list : neighbours) std::sort(list.begin(), list.end());

        std::vector<Edge> witnessed;
        witnessed.reserve(edges.size());
        for(const Edge& edge : edges){
            const std::vector<uint32_t>& first = neighbours[edge.a];
            const std::vector<uint32_t>& second = neighbours[edge.b];

            uint32_t shared = 0;
            size_t i = 0, j = 0;
            while(i < first.size() && j < second.size()){
                if(first[i] < second[j]) ++i;
                else if(second[j] < first[i]) ++j;
                else { ++shared; ++i; ++j; }
            }

            if(shared >= config.minTriangleSupport){
                Edge kept = edge;
                kept.support = shared;
                witnessed.push_back(kept);
            }else ++result.unwitnessedEdges;
        }
        edges.swap(witnessed);
    }

    if(config.conflictFreeMerge){
        //NAJPOUZDANIJI BRID PRVI. Kad dva traga ne smiju u isti, pobjeduje onaj koji je stigao
        //prvi - pa je vazno da to bude bolji brid, a ne slucajni. Poredak je potpuno zadan:
        //po udaljenosti potpisa, a kod izjednacenja po rednim brojevima znacajki
        std::stable_sort(edges.begin(), edges.end(), [](const Edge& one, const Edge& two){
            if(one.distance != two.distance) return one.distance < two.distance;
            if(one.a != two.a) return one.a < two.a;
            return one.b < two.b;
        });

        FrameSets sets(result.featuresTotal);
        for(uint32_t i = 0; i < result.featuresTotal; ++i) sets.frames[i] = {frameOf[i]};

        for(const Edge& edge : edges){
            const uint32_t rootA = groups.find(edge.a);
            const uint32_t rootB = groups.find(edge.b);
            if(rootA == rootB) continue;

            if(sets.wouldClash(sets.frames[rootA], sets.frames[rootB])){
                ++result.refusedEdges;
                continue;
            }

            groups.join(rootA, rootB);
            const uint32_t root = groups.find(rootA);
            const uint32_t other = root == rootA ? rootB : rootA;

            sets.frames[root].insert(sets.frames[root].end(),
                                     sets.frames[other].begin(), sets.frames[other].end());
            sets.frames[other].clear();
            sets.frames[other].shrink_to_fit();
        }
    }else{
        for(const Edge& edge : edges) groups.join(edge.a, edge.b);
    }

    // ---------------------------------------------------------------------------------
    // Popravak sukobljene komponente umjesto bacanja
    // ---------------------------------------------------------------------------------
    //
    // Komponenta koja isti kadar dodirne dvaput sadrzi bar jedan krivi brid. Dosad se cijela
    // bacala - a s njom i sve sto je u njoj bilo tocno. Ovdje se ona RASTAVLJA: njezini se bridovi
    // prolaze ponovno, najpouzdaniji prvi, i spoj koji bi opet doveo dva opazanja u isti kadar se
    // ne izvede. Umjesto jednog bacenog traga ostane vise ispravnih.
    //
    // ZASTO OVDJE RADI, A GLOBALNO (conflictFreeMerge) NIJE: ondje je isto pravilo vrijedilo za sve
    // komponente, pa je krivi brid s malom udaljenoscu potpisa znao zauzeti mjesto i odbiti pravi -
    // u zdravoj komponenti koja to nije trebala. Ovdje se dira samo ono sto je vec dokazano
    // pokvareno; zdrave komponente prolaze nedirnute.
    //
    // I SUDAC JE BOLJI: prvo broj svjedoka (koliko je trecih kadrova potvrdilo brid), pa tek onda
    // udaljenost potpisa. Svjedok je neovisna potvrda, udaljenost potpisa nije
    if(config.splitConflicting && !config.conflictFreeMerge && !edges.empty()){
        //Koja komponenta dodiruje isti kadar dvaput. Znacajke su poredane po kadrovima, pa je
        //dovoljno pamtiti zadnji vidjeni kadar po korijenu
        std::vector<uint32_t> lastFrame(result.featuresTotal, UINT32_MAX);
        std::vector<uint8_t> clashes(result.featuresTotal, 0);
        for(uint32_t frame = 0; frame < frames; ++frame){
            for(uint32_t i = offset[frame]; i < offset[frame + 1]; ++i){
                const uint32_t root = groups.find(i);
                if(lastFrame[root] == frame) clashes[root] = 1;
                lastFrame[root] = frame;
            }
        }

        //Zdravo se prepisuje kako jest, pokvareno ide na ponovno slaganje. Oba kraja brida imaju
        //isti korijen, pa je dovoljno pitati jedan
        Groups repaired(result.featuresTotal);
        std::vector<Edge> broken;
        for(const Edge& edge : edges){
            if(clashes[groups.find(edge.a)]) broken.push_back(edge);
            else repaired.join(edge.a, edge.b);
        }

        //JACI DOKAZ UNUTAR SUMNJIVE KOMPONENTE. Komponenta je vec dokazano pokvarena, pa se u njoj
        //ne vjeruje bridu koji je prosao samo najnizi prag svjedoka. Brid ispod ovog praga se ne
        //odgadja nego BACA - ono sto se raspadne, raspalo se jer ga nista nije drzalo
        if(config.splitSupport > 0){
            std::vector<Edge> strong;
            strong.reserve(broken.size());
            for(const Edge& edge : broken){
                if(edge.support >= config.splitSupport) strong.push_back(edge);
                else ++result.droppedWeakEdges;
            }
            broken.swap(strong);
        }

        if(!broken.empty()){
            //POREDAK JE POTPUNO ZADAN: vise svjedoka prvo, pa manja udaljenost potpisa, pa redni
            //brojevi - inace bi rezultat ovisio o poretku parova kadrova
            std::stable_sort(broken.begin(), broken.end(), [](const Edge& one, const Edge& two){
                if(one.support != two.support) return one.support > two.support;
                if(one.distance != two.distance) return one.distance < two.distance;
                if(one.a != two.a) return one.a < two.a;
                return one.b < two.b;
            });

            FrameSets sets(result.featuresTotal);
            for(const Edge& edge : broken){
                sets.frames[edge.a] = {frameOf[edge.a]};
                sets.frames[edge.b] = {frameOf[edge.b]};
            }

            for(const Edge& edge : broken){
                const uint32_t rootA = repaired.find(edge.a);
                const uint32_t rootB = repaired.find(edge.b);
                if(rootA == rootB) continue;

                if(sets.wouldClash(sets.frames[rootA], sets.frames[rootB])){
                    ++result.refusedEdges;
                    continue;
                }

                repaired.join(rootA, rootB);
                const uint32_t root = repaired.find(rootA);
                const uint32_t other = root == rootA ? rootB : rootA;

                sets.frames[root].insert(sets.frames[root].end(),
                                         sets.frames[other].begin(), sets.frames[other].end());
                sets.frames[other].clear();
                sets.frames[other].shrink_to_fit();
            }

            for(uint32_t root = 0; root < result.featuresTotal; ++root){
                if(clashes[root]) ++result.splitPoints;
            }
        }

        groups.parent.swap(repaired.parent);
    }

    // ---------------------------------------------------------------------------------
    // Komponente u tocke
    // ---------------------------------------------------------------------------------
    //
    // Komponenta koja dva puta dodirne isti kadar NIJE nuzno greska: uglovi su razmaknuti najmanje
    // minDistance, pa su ta dva opazanja dva SUSJEDNA UGLA cije se zakrpe preklapaju - dvostruko
    // uzorkovanje iste tocke. Bliska se stapaju u jedno, daleka ostaju sukob. Vidi mergeWithin

    std::unordered_map<uint32_t, uint32_t> numbering;
    std::vector<Observation> collected;
    collected.reserve(result.featuresTotal);

    //Sve sto je pala u isti (kadar, tocka) - tek kad se skupi zna se je li to jedno ili dvoje
    std::unordered_map<uint64_t, std::vector<glm::vec2>> gathered;
    std::vector<uint64_t> order;   //poredak prvog pojavljivanja, da izlaz ne ovisi o tablici

    for(uint32_t frame = 0; frame < frames; ++frame){
        for(uint32_t i = 0; i < uint32_t(points[frame].size()); ++i){
            const uint32_t root = groups.find(offset[frame] + i);

            auto found = numbering.find(root);
            if(found == numbering.end()){
                found = numbering.emplace(root, uint32_t(numbering.size())).first;
            }
            const uint32_t point = found->second;

            //NATRAG U KOORDINATE POZIVATELJEVE SLIKE. Prosjek po kvadratu od f piksela stavlja
            //srediste bloka na x*f + (f-1)/2, pa se tim istim izrazom vraca - bez pola piksela
            //pomaka svaka bi tocka bila sustavno pomaknuta prema gore lijevo
            glm::vec2 pixel = points[frame][i];
            if(shrink > 1) pixel = pixel * float(shrink) + glm::vec2(0.5f * float(shrink - 1));

            const uint64_t key = (uint64_t(frame) << 32) | uint64_t(point);
            auto& list = gathered[key];
            if(list.empty()) order.push_back(key);
            list.push_back(pixel);
        }
    }

    //Koliko daleko smiju biti da bi bila isti detalj. Izvedeno: razmak uglova u koordinatama
    //pozivateljeve slike, s malo zaliha - dva susjedna ugla su tocno toliko razmaknuta
    const float mergeWithin = config.mergeWithin > 0.0f
        ? config.mergeWithin
        : 1.5f * detect.minDistance * float(shrink);

    std::unordered_map<uint32_t, uint8_t> conflicted;

    for(uint64_t key : order){
        const std::vector<glm::vec2>& list = gathered[key];
        const uint32_t frame = uint32_t(key >> 32);
        const uint32_t point = uint32_t(key & 0xffffffffu);

        if(list.size() == 1){
            collected.push_back(Observation{frame, point, list[0]});
            continue;
        }

        //Koliko su medjusobno daleko. Ako stanu u mergeWithin, to je jedan detalj uzorkovan vise
        //puta i stapa se u srediste; ako ne, komponenta je stvarno spojila dvije razlicite tocke
        float widest = 0.0f;
        for(size_t a = 0; a < list.size(); ++a){
            for(size_t b = a + 1; b < list.size(); ++b){
                widest = std::max(widest, glm::length(list[a] - list[b]));
            }
        }

        if(config.mergeDuplicates && widest <= mergeWithin){
            //UZIMA SE PRVI, NE SREDINA. Sredina dvaju uglova nije ugao - to je mjesto izmedju njih,
            //gdje detektor nije nista nasao, pa tocka triangulirana iz nje sjedi malo pokraj.
            //Izmjereno: sa sredinom 26.27 dB, s prvim 26.81 na istim postavkama.
            //
            //Prvi je uvijek onaj s manjim rednim brojem ugla, a detektor ih vraca po jacini - pa
            //je to ujedno i jaci od njih dvaju
            collected.push_back(Observation{frame, point, list[0]});
            ++result.mergedObservations;
        }else{
            conflicted[point] = 1;
        }
    }

    //Sukobljene komponente: prebrojati ih, pa po postavci i izbaciti
    std::vector<uint32_t> views(numbering.size(), 0);
    for(const Observation& one : collected) ++views[one.point];

    for(const auto& entry : conflicted){
        if(entry.first >= views.size()) continue;
        ++result.conflictingPoints;
        result.conflictingObservations += views[entry.first];
    }

    std::vector<uint32_t> renumbered(numbering.size(), UINT32_MAX);
    uint32_t next = 0;
    for(uint32_t point = 0; point < uint32_t(views.size()); ++point){
        if(config.dropConflicting && conflicted.count(point)) continue;
        if(views[point] >= config.minViews) renumbered[point] = next++;
    }

    //DOTJERIVANJE NA PUNOJ SLICI, tek sada - samo za ono sto je proslo sva sita. Prije sortiranja
    //u izlaz, jer se time mijenja samo polozaj a ne pripadnost
    if(config.refineAtFullResolution && shrink > 1){
        const uint32_t reach = std::max(3u, shrink + 1);
        const float allowed = config.refineMaxShift > 0.0f ? config.refineMaxShift : float(shrink);

        inBands(0, int(collected.size()), [&](uint32_t, int firstItem, int lastItem){
            for(int index = firstItem; index < lastItem; ++index){
                Observation& one = collected[size_t(index)];
                one.pixel = refineCorner(images[one.camera], one.pixel, reach, allowed);
            }
        });
    }

    result.observations.reserve(collected.size());
    for(const Observation& one : collected){
        if(renumbered[one.point] == UINT32_MAX) continue;
        result.observations.push_back(Observation{one.camera, renumbered[one.point], one.pixel});
    }
    result.pointCount = next;

    // ---------------------------------------------------------------------------------
    // Dotjerivanje prema referentnom opazanju
    // ---------------------------------------------------------------------------------
    //
    // Tek sada, nad onim sto je prezivjelo sva sita, i nad punom slikom. Mijenja se samo polozaj,
    // ne pripadnost - pa poredak i brojevi tocaka ostaju kakvi jesu

    if(config.refineToReference && shrink > 1 && result.pointCount > 0){
        const float allowed = config.referenceMaxShift > 0.0f
            ? config.referenceMaxShift : float(shrink);

        //Opazanja jedne tocke zajedno. Poredak je onaj u kojem su izasla - po kadrovima rastuce -
        //pa je prvo ujedno i ono iz najranijeg kadra, i izbor reference je time zadan
        std::vector<std::vector<uint32_t>> ofPoint(result.pointCount);
        for(uint32_t i = 0; i < uint32_t(result.observations.size()); ++i){
            ofPoint[result.observations[i].point].push_back(i);
        }

        //Razlika izgleda je ovdje veca nego pri pracenju - usporedjuju se kadrovi udaljeni do
        //cijelog prozora, ne susjedni
        TrackConfig follow = config.detect;
        if(config.referenceMaxResidual > 0.0f) follow.maxResidual = config.referenceMaxResidual;

        std::vector<uint32_t> done(result.pointCount, 0), failed(result.pointCount, 0);
        std::vector<uint8_t> gaveUp(result.pointCount, 0);

        inBands(0, int(result.pointCount), [&](uint32_t, int firstItem, int lastItem){
            std::vector<glm::vec2> found;
            for(int index = firstItem; index < lastItem; ++index){
                const std::vector<uint32_t>& list = ofPoint[size_t(index)];
                if(list.size() < 2) continue;

                //REFERENCA JE SREDNJI KADAR TRAGA, ne prvi. Iz prvog je najdalji clan udaljen
                //cijelu duljinu traga, iz srednjeg polovicu - a sto su kadrovi dalji, to se zakrpa
                //vise promijenila i to je cesci razlog da dotjerivanje odustane
                const Observation reference = result.observations[list[list.size() / 2]];

                found.assign(list.size(), glm::vec2(0.0f));
                uint32_t ok = 0, missed = 0;
                for(size_t k = 0; k < list.size(); ++k){
                    const Observation& one = result.observations[list[k]];
                    found[k] = one.pixel;
                    if(one.camera == reference.camera) continue;

                    if(refineToward(images[reference.camera], images[one.camera],
                                    reference.pixel, found[k], allowed, follow)) ++ok;
                    else ++missed;
                }

                if(missed > 0 && config.refineWholeTracks){
                    gaveUp[size_t(index)] = 1;
                    failed[size_t(index)] = uint32_t(list.size());
                    continue;
                }

                for(size_t k = 0; k < list.size(); ++k) result.observations[list[k]].pixel = found[k];
                done[size_t(index)] = ok;
                failed[size_t(index)] = missed;
            }
        });

        for(uint32_t point = 0; point < result.pointCount; ++point){
            result.refinedObservations += done[point];
            result.unrefinedObservations += failed[point];
            result.unrefinedTracks += gaveUp[point];
        }
    }

    result.assemblySeconds = secondsSince(assemblyStarted);
    return result;
}

}
