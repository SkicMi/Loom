#include "Engine/MatchGraph.h"

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

    for(uint32_t frame = 0; frame < frames; ++frame){
        points[frame] = detectCorners(working[frame], detect);
        signatures[frame] = describeAll(working[frame], points[frame], describe);
        offset[frame + 1] = offset[frame] + uint32_t(points[frame].size());
    }
    result.localizationPixels = float(shrink);
    result.featuresTotal = offset[frames];
    if(result.featuresTotal == 0) return result;

    // ---------------------------------------------------------------------------------
    // Parovi kadrova
    // ---------------------------------------------------------------------------------

    Groups groups(result.featuresTotal);
    std::vector<double> perPair;
    const float radius = config.searchFraction * float(working[0].width);

    for(uint32_t a = 0; a < frames; ++a){
        for(uint32_t step = 1; step <= config.window; ++step){
            const uint32_t b = a + step;
            if(b >= frames) break;
            if(signatures[a].empty() || signatures[b].empty()) continue;

            ++result.comparedFrames;
            const std::vector<Match> matches = matchDescriptorsNear(
                signatures[a], points[a], signatures[b], points[b], radius, describe);
            if(matches.size() < config.minInliers) continue;

            std::vector<glm::vec2> here, there;
            here.reserve(matches.size()); there.reserve(matches.size());
            for(const Match& match : matches){
                here.push_back(points[a][match.from]);
                there.push_back(points[b][match.to]);
            }

            const TwoViewResult pose = relativePoseRobust(here, there, small, config.ransac);
            if(!pose.solved || pose.inlierCount < config.minInliers) continue;

            ++result.acceptedFrames;
            perPair.push_back(double(pose.inlierCount));

            for(size_t i = 0; i < matches.size(); ++i){
                if(i < pose.inliers.size() && !pose.inliers[i]) continue;
                groups.join(offset[a] + matches[i].from, offset[b] + matches[i].to);
            }
        }
    }

    if(!perPair.empty()){
        std::sort(perPair.begin(), perPair.end());
        result.medianMatchesPerPair = perPair[perPair.size() / 2];
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

    result.observations.reserve(collected.size());
    for(const Observation& one : collected){
        if(renumbered[one.point] == UINT32_MAX) continue;
        result.observations.push_back(Observation{one.camera, renumbered[one.point], one.pixel});
    }
    result.pointCount = next;

    return result;
}

}
