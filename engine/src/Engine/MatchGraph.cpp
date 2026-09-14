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
    DescribeConfig describe = config.describe;
    if(config.patchFromWidth) describe.patch = std::max(12u, images[0].width / 40u);

    for(uint32_t frame = 0; frame < frames; ++frame){
        points[frame] = detectCorners(images[frame], config.detect);
        signatures[frame] = describeAll(images[frame], points[frame], describe);
        offset[frame + 1] = offset[frame] + uint32_t(points[frame].size());
    }
    result.featuresTotal = offset[frames];
    if(result.featuresTotal == 0) return result;

    // ---------------------------------------------------------------------------------
    // Parovi kadrova
    // ---------------------------------------------------------------------------------

    Groups groups(result.featuresTotal);
    std::vector<double> perPair;
    const float radius = config.searchFraction * float(images[0].width);

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

            const TwoViewResult pose = relativePoseRobust(here, there, intrinsics, config.ransac);
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

    for(uint32_t frame = 0; frame < frames; ++frame){
        for(uint32_t i = 0; i < uint32_t(points[frame].size()); ++i){
            const uint32_t root = groups.find(offset[frame] + i);

            auto found = numbering.find(root);
            if(found == numbering.end()){
                found = numbering.emplace(root, uint32_t(numbering.size())).first;
            }
            const uint32_t point = found->second;

            const uint64_t key = (uint64_t(frame) << 32) | uint64_t(point);
            if(!taken.emplace(key, 1).second) continue;

            collected.push_back(Observation{frame, point, points[frame][i]});
        }
    }

    // ---------------------------------------------------------------------------------
    // Tocke vidjene iz premalo kadrova ne ulaze
    // ---------------------------------------------------------------------------------
    //
    // Znacajka koju nijedan drugi kadar nije potvrdio nije tocka nego pojedinacno opazanje, a
    // takvih je vecina - detektor ih nadje na tisuce po kadru. Brojevi se zatim zbiju, jer
    // reconstruct polja indeksira brojem tocke

    std::vector<uint32_t> views(numbering.size(), 0);
    for(const Observation& one : collected) ++views[one.point];

    std::vector<uint32_t> renumbered(numbering.size(), UINT32_MAX);
    uint32_t next = 0;
    for(uint32_t point = 0; point < uint32_t(views.size()); ++point){
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
