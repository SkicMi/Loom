#include "Engine/MergeTracks.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace Engine{
namespace{

//Union-find nad tragovima. Spajanje je relacija ekvivalencije - ako je A isto sto i B, a B isto
//sto i C, onda su sva tri jedna tocka - i ovo je struktura koja to drzi bez da se ista prepisuje
//dok traje
struct Groups{
    std::vector<uint32_t> parent;

    explicit Groups(uint32_t count) : parent(count){
        std::iota(parent.begin(), parent.end(), 0u);
    }

    uint32_t find(uint32_t a){
        while(parent[a] != a){
            parent[a] = parent[parent[a]];   //put se usput skracuje
            a = parent[a];
        }
        return a;
    }

    bool join(uint32_t a, uint32_t b){
        a = find(a); b = find(b);
        if(a == b) return false;
        parent[b] = a;
        return true;
    }
};

}

MergeResult mergeTracks(const std::vector<Observation>& observations,
                        const std::vector<GrayImage>& images,
                        uint32_t pointCount,
                        const Intrinsics& intrinsics,
                        const MergeConfig& config){
    MergeResult result;
    result.observations = observations;
    result.pointCount = pointCount;
    if(images.empty() || observations.empty()) return result;

    const uint32_t frames = uint32_t(images.size());

    // ---------------------------------------------------------------------------------
    // Potpisi po kadru
    // ---------------------------------------------------------------------------------
    //
    // Racunaju se JEDNOM po kadru, za sve tocke tog kadra odjednom - inace bi se slika zagladjivala
    // po tocki. Ista pogreska je u trackeru piramidu gradila po tragu i kostala 934 ms po kadru

    std::vector<std::vector<uint32_t>> tracksIn(frames);
    std::vector<std::vector<glm::vec2>> pixelsIn(frames);
    for(const Observation& one : observations){
        if(one.camera >= frames) continue;
        tracksIn[one.camera].push_back(one.point);
        pixelsIn[one.camera].push_back(one.pixel);
    }

    std::vector<std::vector<Descriptor>> signatures(frames);
    for(uint32_t frame = 0; frame < frames; ++frame){
        if(pixelsIn[frame].empty()) continue;
        signatures[frame] = describeAll(images[frame], pixelsIn[frame], config.describe);
    }

    // ---------------------------------------------------------------------------------
    // Parovi kadrova
    // ---------------------------------------------------------------------------------

    Groups groups(pointCount);

    for(uint32_t a = 0; a < frames; ++a){
        for(uint32_t step = config.nearest; step <= config.window; ++step){
            const uint32_t b = a + step;
            if(b >= frames) break;
            if(signatures[a].empty() || signatures[b].empty()) continue;

            ++result.comparedFrames;
            const std::vector<Match> matches = matchDescriptors(signatures[a], signatures[b], config.describe);
            if(matches.size() < config.minInliers) continue;

            //GEOMETRIJSKA PROVJERA. Potpis kaze da dva mjesta izgledaju isto; geometrija kaze mogu
            //li SVA poklapanja biti istina istovremeno. Slucajno poklapanje izgleda uvjerljivo samo
            //dok se gleda samo - cim se trazi jedna poza koja objasnjava sve, ono ispadne van
            std::vector<glm::vec2> here, there;
            here.reserve(matches.size()); there.reserve(matches.size());
            for(const Match& match : matches){
                here.push_back(pixelsIn[a][match.from]);
                there.push_back(pixelsIn[b][match.to]);
            }

            const TwoViewResult pose = relativePoseRobust(here, there, intrinsics, config.ransac);
            if(!pose.solved || pose.inlierCount < config.minInliers) continue;
            ++result.acceptedFrames;

            for(size_t i = 0; i < matches.size(); ++i){
                if(i < pose.inliers.size() && !pose.inliers[i]) continue;
                const uint32_t left = tracksIn[a][matches[i].from];
                const uint32_t right = tracksIn[b][matches[i].to];
                if(groups.join(left, right)) ++result.mergedPairs;
            }
        }
    }

    // ---------------------------------------------------------------------------------
    // Prepisivanje brojeva tocaka
    // ---------------------------------------------------------------------------------
    //
    // Brojevi moraju ostati gusti - reconstruct polja indeksira brojem tocke, pa bi rupe znacile
    // prazna polja kroz cijeli racun

    std::vector<uint32_t> renumbered(pointCount, UINT32_MAX);
    uint32_t next = 0;
    for(uint32_t point = 0; point < pointCount; ++point){
        const uint32_t root = groups.find(point);
        if(renumbered[root] == UINT32_MAX) renumbered[root] = next++;
        renumbered[point] = renumbered[root];
    }

    for(Observation& one : result.observations){
        if(one.point < pointCount) one.point = renumbered[one.point];
    }
    result.pointCount = next;

    //JEDNA TOCKA, JEDNO OPAZANJE PO KADRU. Spajanje moze dovesti dva traga iz istog kadra u istu
    //tocku - a tada bi ista kamera tvrdila da tocku vidi na dva mjesta. Triangulacija bi to
    //pomirila tako da tocku stavi izmedju, dakle nigdje. Zadrzava se prvo.
    //
    //Preko skupa, ne pretragom po listi: opazanja ima stotine tisuca, pa je kvadratna provjera
    //skuplja od cijelog ostatka funkcije zajedno
    std::unordered_set<uint64_t> used;
    used.reserve(result.observations.size() * 2);

    std::vector<Observation> kept;
    kept.reserve(result.observations.size());
    for(const Observation& one : result.observations){
        const uint64_t key = (uint64_t(one.camera) << 32) | uint64_t(one.point);
        if(!used.insert(key).second) continue;
        kept.push_back(one);
    }
    result.observations = std::move(kept);

    return result;
}

}
