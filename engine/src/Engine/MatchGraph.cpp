#include "Engine/Bands.h"
#include "Engine/MatchGraph.h"
#include "Engine/Track.h"

#include <algorithm>
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

MatchGraphResult buildMatchGraph(const std::vector<GrayImage>& images,
                                 const Intrinsics& intrinsics,
                                 const MatchGraphConfig& config){
    MatchGraphResult result;
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
    if(config.workingWidth > 0 && images[0].width > config.workingWidth){
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

    for(uint32_t frame = 0; frame < frames; ++frame){
        points[frame] = detectCorners(working[frame], detect);
        if(config.useSift) siftSignatures[frame] = describeSiftAll(working[frame], points[frame], sift);
        else               signatures[frame] = describeAll(working[frame], points[frame], describe);
        offset[frame + 1] = offset[frame] + uint32_t(points[frame].size());
    }
    result.localizationPixels = float(shrink);
    result.featuresTotal = offset[frames];
    if(result.featuresTotal == 0) return result;

    // ---------------------------------------------------------------------------------
    // Parovi kadrova
    // ---------------------------------------------------------------------------------

    Groups groups(result.featuresTotal);
    std::vector<Edge> edges;
    std::vector<double> perPair;
    const float radius = config.searchFraction * float(working[0].width);

    for(uint32_t a = 0; a < frames; ++a){
        for(uint32_t step = 1; step <= config.window; ++step){
            const uint32_t b = a + step;
            if(b >= frames) break;
            if(config.useSift){ if(siftSignatures[a].empty() || siftSignatures[b].empty()) continue; }
            else              { if(signatures[a].empty() || signatures[b].empty()) continue; }

            ++result.comparedFrames;

            //Dva potpisa daju dva razlicita zapisa poklapanja; dalje ih zanima samo tko s kim
            std::vector<std::pair<uint32_t, uint32_t>> matches;
            std::vector<uint32_t> distances;
            if(config.useSift){
                for(const SiftMatch& one : matchSiftNear(siftSignatures[a], points[a],
                                                          siftSignatures[b], points[b], radius, sift)){
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
            if(matches.size() < config.minInliers) continue;

            std::vector<glm::vec2> here, there;
            here.reserve(matches.size()); there.reserve(matches.size());
            for(const auto& match : matches){
                here.push_back(points[a][match.first]);
                there.push_back(points[b][match.second]);
            }

            const TwoViewResult pose = relativePoseRobust(here, there, small, config.ransac);
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

            if(shared >= config.minTriangleSupport) witnessed.push_back(edge);
            else ++result.unwitnessedEdges;
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
    // Komponente u tocke
    // ---------------------------------------------------------------------------------
    //
    // Komponenta koja dva puta dodirne isti kadar je greska u poklapanju: jedna tocka ne moze biti
    // na dva mjesta u istoj slici. Zadrzava se prvo vidjeno, jer ostalo bi triangulaciju vuklo
    // prema mjestu izmedju dva - dakle nikamo

    std::unordered_map<uint32_t, uint32_t> numbering;
    std::vector<Observation> collected;
    collected.reserve(result.featuresTotal);

    std::vector<uint64_t> takenKeys;
    std::unordered_map<uint64_t, uint8_t> taken;

    //Koje su komponente sukobljene - zna se tek kad se sve prodje, pa se najprije samo biljezi
    std::unordered_map<uint32_t, uint8_t> conflicted;

    for(uint32_t frame = 0; frame < frames; ++frame){
        for(uint32_t i = 0; i < uint32_t(points[frame].size()); ++i){
            const uint32_t root = groups.find(offset[frame] + i);

            auto found = numbering.find(root);
            if(found == numbering.end()){
                found = numbering.emplace(root, uint32_t(numbering.size())).first;
            }
            const uint32_t point = found->second;

            const uint64_t key = (uint64_t(frame) << 32) | uint64_t(point);
            if(!taken.emplace(key, 1).second){
                conflicted[point] = 1;
                continue;
            }

            //NATRAG U KOORDINATE POZIVATELJEVE SLIKE. Prosjek po kvadratu od f piksela stavlja
            //srediste bloka na x*f + (f-1)/2, pa se tim istim izrazom vraca - bez pola piksela
            //pomaka svaka bi tocka bila sustavno pomaknuta prema gore lijevo
            glm::vec2 pixel = points[frame][i];
            if(shrink > 1) pixel = pixel * float(shrink) + glm::vec2(0.5f * float(shrink - 1));

            collected.push_back(Observation{frame, point, pixel});
        }
    }

    // ---------------------------------------------------------------------------------
    // Tocke vidjene iz premalo kadrova ne ulaze
    // ---------------------------------------------------------------------------------
    //
    // Znacajka koju nijedan drugi kadar nije potvrdio nije tocka nego pojedinacno opazanje, a
    // takvih je vecina - detektor ih nadje na tisuce po kadru. Brojevi se zatim zbiju, jer
    // reconstruct polja indeksira brojem tocke

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

    return result;
}

}
