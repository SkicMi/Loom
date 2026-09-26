// Interior rules for WeaverProcedura (RoomSplit, Interior). The plan is made of rules, not a
// drawing: zones get a corridor chosen by their depth, arms connect to the main corridor, the
// front door opens into a hall or the corridor, a two-flight staircase sits at the same place on
// every floor, rooms fill the rest by program, and every room gets a door along an access tree.
// Every rule that cannot be met ends with an error that says which one.
#include "WeaverProcedura.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>

namespace Engine::WeaverProcedura{
namespace{

constexpr float eps = 1e-4f;

glm::vec3 at(const glm::vec2& xz, float y){return {xz.x, y, xz.y};}

// Small deterministic generator, so a seed always gives the same plan on every platform.
struct Random{
    uint64_t state;
    explicit Random(uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull){}
    uint64_t next(){ state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
    float uniform(float low, float high){ return low + (high - low) * float(next() % 1000000) / 999999.0f; }
    bool coin(){ return next() & 1; }
};

// A strip of a zone: rows hold rooms, the corridor is one open room. u runs along the
// corridor axis, v across it.
struct Band{
    std::size_t zone;
    bool alongX;          // u is local x
    float u0, u1, v0, v1;
    bool corridor;
    bool alcove = false;
};

struct Slot{ float u0, u1; RoomType type; bool groundOnly; };
// Band.alcove: a zone too small for rooms, kept as one storage room (a niche or closet).

LocalRect bandRect(const Band& band, float u0, float u1, float v0, float v1){
    return band.alongX ? LocalRect{{u0, v0}, {u1, v1}} : LocalRect{{v0, u0}, {v1, u1}};
}

bool isOpen(RoomType type){ return type == RoomType::Hall || type == RoomType::Corridor || type == RoomType::Stairs; }

float area(const LocalRect& r){ return (r.max.x - r.min.x) * (r.max.y - r.min.y); }

// Shared boundary of two rectangles, as a segment; false when they only touch at a corner.
bool sharedSide(const LocalRect& a, const LocalRect& b, glm::vec2& from, glm::vec2& to){
    const float z0 = std::max(a.min.y, b.min.y), z1 = std::min(a.max.y, b.max.y);
    const float x0 = std::max(a.min.x, b.min.x), x1 = std::min(a.max.x, b.max.x);
    if(z1 - z0 > 0.01f){
        if(std::abs(a.max.x - b.min.x) < eps){ from = {a.max.x, z0}; to = {a.max.x, z1}; return true; }
        if(std::abs(a.min.x - b.max.x) < eps){ from = {a.min.x, z0}; to = {a.min.x, z1}; return true; }
    }
    if(x1 - x0 > 0.01f){
        if(std::abs(a.max.y - b.min.y) < eps){ from = {x0, a.max.y}; to = {x1, a.max.y}; return true; }
        if(std::abs(a.min.y - b.max.y) < eps){ from = {x0, a.min.y}; to = {x1, a.min.y}; return true; }
    }
    return false;
}

glm::vec2 toLocal(const Footprint& footprint, const glm::vec2& world){
    const glm::vec2 d = world - footprint.frameCenter;
    const glm::vec2 across{-footprint.frameAxis.y, footprint.frameAxis.x};
    return {glm::dot(d, footprint.frameAxis), glm::dot(d, across)};
}

}  // namespace

const std::vector<std::string>& roomTypeNames(){
    static const std::vector<std::string> names = {"hall", "corridor", "stairs", "living", "kitchen", "bedroom",
                                                   "bathroom", "office", "meeting", "storage"};
    return names;
}

bool planInterior(const Footprint& footprint, const RoomSplitNode& settings, Footprint& output, std::string& error){
    if(footprint.zones.empty()){
        error = "RoomSplit needs a right-angled footprint: a Footprint node, or Footprint from Curve with rectify on";
        return false;
    }
    Random random(settings.seed);
    const float cw = settings.corridorWidth;
    const std::size_t zoneCount = footprint.zones.size();

    // 0. Zone tree: zone 0 is the main block; every other zone hangs off the first zone it shares
    //    at least 1 m of boundary with. Its corridor runs away from that boundary.
    struct Joint{ int parent = -1; bool lineAlongX = true; float line = 0.0f; };
    std::vector<Joint> joints(zoneCount);
    std::vector<std::size_t> order{0};
    std::vector<bool> placed(zoneCount, false);
    placed[0] = true;
    for(std::size_t k = 0; k < order.size(); ++k){
        const LocalRect& p = footprint.zones[order[k]];
        for(std::size_t z = 0; z < zoneCount; ++z){
            if(placed[z]) continue;
            const LocalRect& r = footprint.zones[z];
            const float ox = std::min(p.max.x, r.max.x) - std::max(p.min.x, r.min.x);
            const float oz = std::min(p.max.y, r.max.y) - std::max(p.min.y, r.min.y);
            Joint j;
            if(ox >= 1.0f && (std::abs(p.max.y - r.min.y) < eps || std::abs(p.min.y - r.max.y) < eps)){
                j = {int(order[k]), true, std::abs(p.max.y - r.min.y) < eps ? r.min.y : r.max.y};
            }else if(oz >= 1.0f && (std::abs(p.max.x - r.min.x) < eps || std::abs(p.min.x - r.max.x) < eps)){
                j = {int(order[k]), false, std::abs(p.max.x - r.min.x) < eps ? r.min.x : r.max.x};
            }else continue;
            joints[z] = j; placed[z] = true; order.push_back(z);
        }
    }
    if(order.size() != zoneCount){ error = "footprint zones are not connected by at least 1 m of shared wall"; return false; }

    // 1. Bands: every zone gets a corridor by its depth across the corridor axis.
    std::vector<Band> bands;
    std::vector<int> zoneCorridor(zoneCount, -1);
    for(const std::size_t z : order){
        const LocalRect& r = footprint.zones[z];
        const float sx = r.max.x - r.min.x, sz = r.max.y - r.min.y;
        // The main block runs along its long side; a hanging zone away from its joint (a joint
        // line along x means the zone runs along z).
        const bool alongX = joints[z].parent < 0 ? sx >= sz : !joints[z].lineAlongX;
        const float u0 = alongX ? r.min.x : r.min.y, u1 = alongX ? r.max.x : r.max.y;
        const float v0 = alongX ? r.min.y : r.min.x, v1 = alongX ? r.max.y : r.max.x;
        const float depth = v1 - v0;
        if(depth < 2.4f || u1 - u0 < 2.0f){
            // Too small for rooms: a niche or closet, reached from a neighbour, if 1.2 m either way.
            if(std::min(depth, u1 - u0) < 1.2f){ error = "zone " + std::to_string(z) + " is narrower than 1.2 m"; return false; }
            Band band{z, alongX, u0, u1, v0, v1, false};
            band.alcove = true;
            bands.push_back(band);
            continue;
        }
        auto addCorridor = [&](float a, float b){
            zoneCorridor[z] = int(bands.size());
            bands.push_back({z, alongX, u0, u1, a, b, true});
        };
        if(depth >= 8.0f){
            const float c = (v0 + v1) * 0.5f + random.uniform(-0.3f, 0.3f);
            bands.push_back({z, alongX, u0, u1, v0, c - cw * 0.5f, false});
            addCorridor(c - cw * 0.5f, c + cw * 0.5f);
            bands.push_back({z, alongX, u0, u1, c + cw * 0.5f, v1, false});
        }else if(depth >= 2.0f * cw + 2.0f && depth >= 4.4f){
            // Side corridor. The main block puts it on the side most hanging zones attach to,
            // so their corridors meet it; a hanging zone on the side toward its parent's middle
            // (the courtyard of an L or U); a lone rectangle by seed.
            bool high;
            if(joints[z].parent < 0){
                int score = 0;
                for(std::size_t c = 0; c < zoneCount; ++c){
                    if(joints[c].parent != int(z) || joints[c].lineAlongX != alongX) continue;
                    score += std::abs(joints[c].line - v1) < eps ? 1 : std::abs(joints[c].line - v0) < eps ? -1 : 0;
                }
                high = score != 0 ? score > 0 : random.coin();
            }else{
                const LocalRect& p = footprint.zones[std::size_t(joints[z].parent)];
                const float middle = alongX ? (p.min.y + p.max.y) * 0.5f : (p.min.x + p.max.x) * 0.5f;
                high = std::abs(v1 - middle) < std::abs(v0 - middle);
            }
            if(high){ bands.push_back({z, alongX, u0, u1, v0, v1 - cw, false}); addCorridor(v1 - cw, v1); }
            else{ addCorridor(v0, v0 + cw); bands.push_back({z, alongX, u0, u1, v0 + cw, v1, false}); }
        }else{
            bands.push_back({z, alongX, u0, u1, v0, v1, false});
        }
    }

    // Facade of a room: its longest stretch on one outline edge (a window needs it on one wall).
    std::vector<glm::vec2> localOutline;
    for(const glm::vec2& p : footprint.outline) localOutline.push_back(toLocal(footprint, p));
    auto facade = [&](const LocalRect& r){
        float total = 0.0f;
        for(std::size_t i = 0; i < localOutline.size(); ++i){
            const glm::vec2 a = localOutline[i], b = localOutline[(i + 1) % localOutline.size()];
            if(std::abs(a.y - b.y) < 1e-3f){
                if(std::abs(a.y - r.min.y) > 1e-3f && std::abs(a.y - r.max.y) > 1e-3f) continue;
                total = std::max(total, std::min(std::max(a.x, b.x), r.max.x) - std::max(std::min(a.x, b.x), r.min.x));
            }else if(std::abs(a.x - b.x) < 1e-3f){
                if(std::abs(a.x - r.min.x) > 1e-3f && std::abs(a.x - r.max.x) > 1e-3f) continue;
                total = std::max(total, std::min(std::max(a.y, b.y), r.max.y) - std::max(std::min(a.y, b.y), r.min.y));
            }
        }
        return total;
    };

    std::vector<std::vector<Slot>> slots(bands.size());
    auto slotFree = [&](std::size_t band, float a, float b){
        for(const Slot& s : slots[band]) if(a < s.u1 - eps && b > s.u0 + eps) return false;
        return a >= bands[band].u0 - eps && b <= bands[band].u1 + eps;
    };

    // +1 when a corridor of the same zone runs along the band's v1 side, -1 along v0, else 0.
    auto corridorSideOf = [&](const Band& band){
        for(const Band& other : bands){
            if(!other.corridor || other.zone != band.zone) continue;
            if(std::abs(other.v0 - band.v1) < eps) return 1;
            if(std::abs(other.v1 - band.v0) < eps) return -1;
        }
        return 0;
    };

    // 2. Connectors: a hanging zone's corridor that ends at a row of its parent continues
    //    through that row to the parent's corridor.
    for(std::size_t z = 0; z < zoneCount; ++z){
        const int parent = joints[z].parent;
        if(parent < 0 || zoneCorridor[z] < 0 || zoneCorridor[std::size_t(parent)] < 0) continue;
        const Band& arm = bands[std::size_t(zoneCorridor[z])];
        for(std::size_t b = 0; b < bands.size(); ++b){
            const Band& row = bands[b];
            if(row.zone != std::size_t(parent) || row.corridor || row.alongX == arm.alongX) continue;
            if(std::abs(row.v1 - joints[z].line) > eps && std::abs(row.v0 - joints[z].line) > eps) continue;
            if(slotFree(b, arm.v0, arm.v1)) slots[b].push_back({arm.v0, arm.v1, RoomType::Corridor, false});
        }
    }

    // 3. Entrance: the door goes where it opens into a corridor, or a hall is cut into the row
    //    behind the chosen outline edge.
    const std::size_t edgeCount = footprint.outline.size();
    const uint32_t entranceEdge = settings.entranceEdge % uint32_t(edgeCount);
    const glm::vec2 worldA = footprint.outline[entranceEdge], worldB = footprint.outline[(entranceEdge + 1) % edgeCount];
    const glm::vec2 la = toLocal(footprint, worldA), lb = toLocal(footprint, worldB);
    const float edgeLength = glm::length(worldB - worldA);
    const bool edgeAlongX = std::abs(la.y - lb.y) < 1e-3f;
    const float edgeLine = edgeAlongX ? la.y : la.x;
    const float edgeLow = edgeAlongX ? std::min(la.x, lb.x) : std::min(la.y, lb.y);
    const float edgeHigh = edgeAlongX ? std::max(la.x, lb.x) : std::max(la.y, lb.y);
    float entranceLocal = std::numeric_limits<float>::quiet_NaN();   // along the edge, local coordinate
    std::size_t hallBand = bands.size();
    float hallU0 = 0.0f, hallU1 = 0.0f;
    const float hallWidth = std::max(2.2f, settings.doorWidth + 1.3f);
    // Candidates: bands that touch the edge line.
    for(std::size_t b = 0; b < bands.size() && std::isnan(entranceLocal); ++b){
        const Band& band = bands[b];
        const LocalRect r = bandRect(band, band.u0, band.u1, band.v0, band.v1);
        const float low = edgeAlongX ? r.min.x : r.min.y, high = edgeAlongX ? r.max.x : r.max.y;
        const float sideA = edgeAlongX ? r.min.y : r.min.x, sideB = edgeAlongX ? r.max.y : r.max.x;
        if(std::abs(sideA - edgeLine) > 1e-3f && std::abs(sideB - edgeLine) > 1e-3f) continue;
        const float o0 = std::max(low, edgeLow), o1 = std::min(high, edgeHigh);
        if(o1 - o0 < settings.doorWidth + 0.8f) continue;
        if(band.corridor){ entranceLocal = (o0 + o1) * 0.5f; continue; }
        // A row touching the edge along its length: cut a hall at the middle of the overlap.
        if(band.alongX == edgeAlongX && o1 - o0 >= hallWidth + 0.1f){
            const float c = (o0 + o1) * 0.5f;
            if(slotFree(b, c - hallWidth * 0.5f, c + hallWidth * 0.5f)){
                hallBand = b; hallU0 = c - hallWidth * 0.5f; hallU1 = c + hallWidth * 0.5f;
                entranceLocal = c;
            }
        }
    }
    // Corridor ends: an edge across a corridor band (corridor runs into the facade).
    if(std::isnan(entranceLocal)){
        for(const Band& band : bands){
            if(!band.corridor || band.alongX == edgeAlongX) continue;
            const float end = std::abs(band.u0 - edgeLine) < 1e-3f || std::abs(band.u1 - edgeLine) < 1e-3f;
            if(!end) continue;
            if(band.v0 >= edgeLow - eps && band.v1 <= edgeHigh + eps){ entranceLocal = (band.v0 + band.v1) * 0.5f; break; }
        }
    }
    // Row ends: an edge across a row (the head of an arm) gets a hall in the row's first bay.
    const float hallLength = 2.4f;
    for(std::size_t b = 0; b < bands.size() && std::isnan(entranceLocal); ++b){
        const Band& band = bands[b];
        if(band.corridor || band.alongX == edgeAlongX || band.u1 - band.u0 < hallLength + 2.4f) continue;
        if(band.v0 < edgeLow - eps || band.v1 > edgeHigh + eps || band.v1 - band.v0 < settings.doorWidth + 0.8f) continue;
        const bool atLow = std::abs(band.u0 - edgeLine) < 1e-3f, atHigh = std::abs(band.u1 - edgeLine) < 1e-3f;
        if(!atLow && !atHigh) continue;
        const float h0 = atLow ? band.u0 : band.u1 - hallLength;
        if(!slotFree(b, h0, h0 + hallLength)) continue;
        hallBand = b; hallU0 = h0; hallU1 = h0 + hallLength;
        entranceLocal = (band.v0 + band.v1) * 0.5f;
    }
    // Last resort: the door opens into whatever room is behind it, and that room becomes the hall.
    for(std::size_t b = 0; b < bands.size() && std::isnan(entranceLocal); ++b){
        const Band& band = bands[b];
        const LocalRect r = bandRect(band, band.u0, band.u1, band.v0, band.v1);
        const float low = edgeAlongX ? r.min.x : r.min.y, high = edgeAlongX ? r.max.x : r.max.y;
        const float sideA = edgeAlongX ? r.min.y : r.min.x, sideB = edgeAlongX ? r.max.y : r.max.x;
        if(std::abs(sideA - edgeLine) > 1e-3f && std::abs(sideB - edgeLine) > 1e-3f) continue;
        const float o0 = std::max(low, edgeLow), o1 = std::min(high, edgeHigh);
        if(o1 - o0 >= settings.doorWidth + 0.8f) entranceLocal = (o0 + o1) * 0.5f;
    }
    if(std::isnan(entranceLocal)){
        error = "no corridor or room row behind outline edge " + std::to_string(entranceEdge) + " for the front door";
        return false;
    }
    if(hallBand < bands.size()) slots[hallBand].push_back({hallU0, hallU1, RoomType::Hall, true});

    // 4. Staircase for more than one floor: a 4.4 m bay at one end of the longest free stretch
    //    of a row, the same on every floor.
    InteriorPlan plan;
    const float coreLength = 4.4f;
    if(footprint.floors > 1){
        std::size_t best = bands.size();
        float bestU = 0.0f, bestLength = 0.0f;
        const bool startEnd = random.coin();
        for(std::size_t b = 0; b < bands.size(); ++b){
            const Band& band = bands[b];
            if(band.corridor || band.v1 - band.v0 < 2.6f) continue;
            // Free stretches between slots.
            std::vector<std::pair<float, float>> taken;
            for(const Slot& s : slots[b]) taken.push_back({s.u0, s.u1});
            std::sort(taken.begin(), taken.end());
            float cursor = band.u0;
            taken.push_back({band.u1, band.u1});
            for(const auto& [t0, t1] : taken){
                const float length = t0 - cursor;
                // Prefer the main block and stretches that still leave a room beside the stairs.
                const float score = length + (band.zone == 0 ? 3.0f : 0.0f) + (length >= coreLength + 2.5f ? 2.0f : 0.0f);
                if(length >= coreLength && score > bestLength){
                    best = b; bestLength = score;
                    bestU = startEnd ? cursor : t0 - coreLength;
                }
                cursor = std::max(cursor, t1);
            }
        }
        if(best == bands.size()){ error = "no room for a staircase (needs a 4.4 m by 2.6 m bay beside the corridor)"; return false; }
        slots[best].push_back({bestU, bestU + coreLength, RoomType::Stairs, false});
        const Band& band = bands[best];
        plan.hasStairs = true;
        plan.stairsAlongX = band.alongX;
        // The core is 3 m deep on the corridor's side; a deeper row keeps a room behind it.
        const int side = corridorSideOf(band);
        const float coreDepth = band.v1 - band.v0 > 3.0f + 2.4f && side != 0 ? 3.0f : band.v1 - band.v0;
        const float cv0 = side > 0 ? band.v1 - coreDepth : band.v0, cv1 = side > 0 ? band.v1 : band.v0 + coreDepth;
        plan.stairCore = bandRect(band, bestU, bestU + coreLength, cv0, cv1);
    }

    // 5. Rooms. Free stretches of rows split into rooms of about the program's width; stretches
    //    too short for a room widen the slot next to them.
    const float target = settings.program == InteriorProgram::Office ? 5.0f : 3.4f;
    for(uint32_t floor = 0; floor < footprint.floors; ++floor){
        std::vector<std::size_t> cells;     // rooms of this floor still waiting for a type
        std::map<std::size_t, bool> cellAlongX;   // row axis of each cell, by room index
        std::vector<std::size_t> services;  // service strip rooms of layered rows
        const std::size_t firstRoom = plan.rooms.size();
        for(std::size_t b = 0; b < bands.size(); ++b){
            const Band& band = bands[b];
            if(band.corridor || band.alcove){
                plan.rooms.push_back({floor, band.corridor ? RoomType::Corridor : RoomType::Storage,
                                      bandRect(band, band.u0, band.u1, band.v0, band.v1)});
                continue;
            }
            std::vector<Slot> here;
            for(const Slot& s : slots[b]) if(!s.groundOnly || floor == 0) here.push_back(s);
            std::sort(here.begin(), here.end(), [](const Slot& x, const Slot& y){ return x.u0 < y.u0; });
            // Stretches between slots; a short one is given to its neighbouring slot.
            std::vector<std::pair<float, float>> stretches;
            float cursor = band.u0;
            for(std::size_t k = 0; k <= here.size(); ++k){
                const float end = k < here.size() ? here[k].u0 : band.u1;
                if(end - cursor > eps){
                    if(end - cursor < 1.8f){
                        if(k < here.size()) here[k].u0 = cursor;           // widen the next slot back
                        else if(!here.empty()) here.back().u1 = end;        // or the previous one forward
                        else stretches.push_back({cursor, end});
                    }else stretches.push_back({cursor, end});
                }
                if(k < here.size()) cursor = here[k].u1;
            }
            for(const Slot& s : here){
                if(s.type != RoomType::Stairs){ plan.rooms.push_back({floor, s.type, bandRect(band, s.u0, s.u1, band.v0, band.v1)}); continue; }
                // Stairs: the core, widened along the row if a short stretch was given to it, and
                // a room behind it when the row is deeper than the core.
                LocalRect core = plan.stairCore;
                const LocalRect full = bandRect(band, s.u0, s.u1, band.v0, band.v1);
                if(band.alongX){ core.min.x = full.min.x; core.max.x = full.max.x; } else{ core.min.y = full.min.y; core.max.y = full.max.y; }
                plan.rooms.push_back({floor, RoomType::Stairs, core});
                const float coreV0 = band.alongX ? core.min.y : core.min.x, coreV1 = band.alongX ? core.max.y : core.max.x;
                const float restV0 = coreV0 > band.v0 + eps ? band.v0 : coreV1, restV1 = coreV0 > band.v0 + eps ? coreV0 : band.v1;
                if(restV1 - restV0 > eps){
                    cellAlongX[plan.rooms.size()] = band.alongX;
                    cells.push_back(plan.rooms.size());
                    plan.rooms.push_back({floor, RoomType::Bedroom, bandRect(band, s.u0, s.u1, restV0, restV1)});
                }
            }
            // A row deeper than 5.2 m becomes two layers: rooms about 4 m deep at the facade, and toward
            // the corridor a service strip (bathrooms, storage) with a short passage to each room.
            int corridorSide = 0;   // +1: corridor along v1, -1: along v0
            for(const Band& other : bands){
                if(!other.corridor || other.zone != band.zone) continue;
                if(std::abs(other.v0 - band.v1) < eps) corridorSide = 1;
                if(std::abs(other.v1 - band.v0) < eps) corridorSide = -1;
            }
            const float depth = band.v1 - band.v0;
            const bool layered = corridorSide != 0 && depth > 5.2f;
            const float serviceDepth = std::clamp(depth - 4.0f, 1.6f, 3.2f);
            const float outerV0 = layered && corridorSide < 0 ? band.v0 + serviceDepth : band.v0;
            const float outerV1 = layered && corridorSide > 0 ? band.v1 - serviceDepth : band.v1;
            const float serviceV0 = corridorSide > 0 ? outerV1 : band.v0, serviceV1 = corridorSide > 0 ? band.v1 : outerV0;
            for(const auto& [s0, s1] : stretches){
                const float length = s1 - s0;
                int count = std::max(1, int(std::lround(length / (target * random.uniform(0.85f, 1.15f)))));
                while(count > 1 && length / float(count) < 2.4f) --count;
                std::vector<float> weights(static_cast<std::size_t>(count));
                float total = 0.0f;
                for(float& w : weights){ w = random.uniform(0.8f, 1.2f); total += w; }
                float u = s0;
                for(int k = 0; k < count; ++k){
                    const float u1 = k + 1 == count ? s1 : u + length * weights[std::size_t(k)] / total;
                    cellAlongX[plan.rooms.size()] = band.alongX;
                    cells.push_back(plan.rooms.size());
                    plan.rooms.push_back({floor, RoomType::Bedroom, bandRect(band, u, u1, outerV0, outerV1)});
                    if(layered){
                        const float passage = 1.3f;
                        const bool wideEnough = u1 - u - passage >= 1.6f;
                        // Passages alternate ends so neighbouring rooms share one wider opening.
                        const bool atStart = k % 2 == 0;
                        const float p0 = wideEnough ? (atStart ? u : u1 - passage) : u, p1 = wideEnough ? p0 + passage : u1;
                        plan.rooms.push_back({floor, RoomType::Corridor, bandRect(band, p0, p1, serviceV0, serviceV1)});
                        if(wideEnough){
                            const float q0 = atStart ? p1 : u, q1 = atStart ? u1 : p0;
                            services.push_back(plan.rooms.size());
                            plan.rooms.push_back({floor, RoomType::Storage, bandRect(band, q0, q1, serviceV0, serviceV1)});
                        }
                    }
                    u = u1;
                }
            }
        }
        // The room the front door opens into is the hall, before any type is handed out.
        if(floor == 0){
            for(auto it = cells.begin(); it != cells.end(); ++it){
                const LocalRect& r = plan.rooms[*it].rect;
                const float low = edgeAlongX ? r.min.x : r.min.y, high = edgeAlongX ? r.max.x : r.max.y;
                const float side0 = edgeAlongX ? r.min.y : r.min.x, side1 = edgeAlongX ? r.max.y : r.max.x;
                if((std::abs(side0 - edgeLine) < 1e-3f || std::abs(side1 - edgeLine) < 1e-3f) &&
                   entranceLocal > low + eps && entranceLocal < high - eps){
                    plan.rooms[*it].type = RoomType::Hall;
                    cells.erase(it);
                    break;
                }
            }
        }
        // Cells without 2.2 m of facade on one wall cannot have a window (1.5 m and a 0.35 m margin
        // to each partition): a bathroom (if the floor still needs one) or storage. Only the others
        // take the program's rooms.
        bool darkBathroom = false;
        for(auto it = cells.begin(); it != cells.end();){
            if(facade(plan.rooms[*it].rect) >= 2.2f){ ++it; continue; }
            const LocalRect& r = plan.rooms[*it].rect;
            const bool fits = area(r) >= 2.5f && std::min(r.max.x - r.min.x, r.max.y - r.min.y) >= 1.4f;
            plan.rooms[*it].type = !darkBathroom && fits ? RoomType::Bathroom : RoomType::Storage;
            darkBathroom |= plan.rooms[*it].type == RoomType::Bathroom;
            it = cells.erase(it);
        }
        // The living room takes in neighbouring bays of its row until it has 16 m2 (an open
        // living area), since rows are cut into bays of bedroom size.
        if(settings.program == InteriorProgram::Residential && floor == 0 && !cells.empty()){
            std::size_t living = cells.front();
            for(std::size_t c : cells) if(area(plan.rooms[c].rect) > area(plan.rooms[living].rect)) living = c;
            while(area(plan.rooms[living].rect) < 16.0f){
                LocalRect& r = plan.rooms[living].rect;
                const bool alongX = cellAlongX[living];
                std::size_t best = plan.rooms.size();
                for(std::size_t c : cells){
                    if(c == living || cellAlongX[c] != alongX) continue;
                    const LocalRect& o = plan.rooms[c].rect;
                    const bool sameRow = alongX ? std::abs(o.min.y - r.min.y) < eps && std::abs(o.max.y - r.max.y) < eps
                                                : std::abs(o.min.x - r.min.x) < eps && std::abs(o.max.x - r.max.x) < eps;
                    const bool touches = alongX ? std::abs(o.min.x - r.max.x) < eps || std::abs(o.max.x - r.min.x) < eps
                                                : std::abs(o.min.y - r.max.y) < eps || std::abs(o.max.y - r.min.y) < eps;
                    if(sameRow && touches && (best == plan.rooms.size() || area(o) > area(plan.rooms[best].rect))) best = c;
                }
                if(best == plan.rooms.size()) break;
                LocalRect& o = plan.rooms[best].rect;
                r.min = glm::min(r.min, o.min); r.max = glm::max(r.max, o.max);
                o = LocalRect{};                              // removed after all floors are planned
                plan.rooms[best].type = RoomType::Storage;
                cells.erase(std::find(cells.begin(), cells.end(), best));
            }
        }
        // Types by program, larger rooms first.
        std::sort(cells.begin(), cells.end(), [&](std::size_t a, std::size_t b){ return area(plan.rooms[a].rect) > area(plan.rooms[b].rect); });
        auto assign = [&](std::size_t index, RoomType type){ plan.rooms[index].type = type; };
        if(settings.program == InteriorProgram::Office){
            for(std::size_t k = 0; k < cells.size(); ++k) assign(cells[k], RoomType::Office);
            if(cells.size() >= 3) assign(cells.front(), RoomType::Meeting);
            if(cells.size() >= 2) assign(cells.back(), RoomType::Bathroom);
        }else{
            std::size_t k = 0;
            if(floor == 0 && k < cells.size()) assign(cells[k++], RoomType::Living);
            if(floor == 0 && k < cells.size()) assign(cells[k++], RoomType::Kitchen);
            std::size_t last = cells.size();
            if(last > k && !darkBathroom){ assign(cells[--last], RoomType::Bathroom); }
            int bedrooms = 0;
            for(; k < last; ++k){
                const LocalRect& r = plan.rooms[cells[k]].rect;
                const bool small = area(r) < 7.0f || std::min(r.max.x - r.min.x, r.max.y - r.min.y) < 2.4f;
                assign(cells[k], small ? RoomType::Storage : RoomType::Bedroom);
                if(!small && ++bedrooms % 4 == 0 && k + 1 < last) assign(cells[--last], RoomType::Bathroom);
            }
            // Too few bays: a missing kitchen or bathroom is cut off the end of a wider room, as
            // long as what is left stays usable (living 12 m2, other rooms 2.4 m wide).
            auto carve = [&](RoomType type, float width, const std::vector<RoomType>& donors){
                for(RoomType donor : donors){
                    for(std::size_t c : cells){
                        if(plan.rooms[c].type != donor) continue;
                        LocalRect& r = plan.rooms[c].rect;
                        const bool alongX = cellAlongX[c];
                        const float span = alongX ? r.max.x - r.min.x : r.max.y - r.min.y;
                        const float depth = alongX ? r.max.y - r.min.y : r.max.x - r.min.x;
                        const float keep = donor == RoomType::Living ? std::max(2.4f, 12.0f / depth) : 2.4f;
                        LocalRect cut = r;
                        const LocalRect original = r;
                        if(span - width >= keep){
                            if(alongX){ cut.max.x = r.min.x + width; r.min.x = cut.max.x; }
                            else{ cut.max.y = r.min.y + width; r.min.y = cut.max.y; }
                        }else{
                            // Or across the depth, on the corridor's side, so the room keeps its facade.
                            const float keepDepth = donor == RoomType::Living ? std::max(2.4f, 12.0f / span) : 2.4f;
                            if(depth - width < keepDepth) continue;
                            bool corridorAtMax = false;
                            for(const Room& other : plan.rooms){
                                if(other.floor != floor || !isOpen(other.type)) continue;
                                glm::vec2 from, to;
                                if(sharedSide(r, other.rect, from, to))
                                    corridorAtMax |= alongX ? std::abs(from.y - r.max.y) < eps : std::abs(from.x - r.max.x) < eps;
                            }
                            if(alongX){
                                if(corridorAtMax){ cut.min.y = r.max.y - width; r.max.y = cut.min.y; }
                                else{ cut.max.y = r.min.y + width; r.min.y = cut.max.y; }
                            }else{
                                if(corridorAtMax){ cut.min.x = r.max.x - width; r.max.x = cut.min.x; }
                                else{ cut.max.x = r.min.x + width; r.min.x = cut.max.x; }
                            }
                        }
                        // A kitchen needs its window; the donor keeps one too.
                        if((type == RoomType::Kitchen && facade(cut) < 2.2f) || facade(r) < 2.2f){ r = original; continue; }
                        cellAlongX[plan.rooms.size()] = alongX;
                        plan.rooms.push_back({floor, type, cut});
                        return true;
                    }
                }
                return false;
            };
            auto present = [&](RoomType type){
                for(std::size_t i = firstRoom; i < plan.rooms.size(); ++i) if(plan.rooms[i].type == type && area(plan.rooms[i].rect) > 0.0f) return true;
                return false;
            };
            if(floor == 0 && !present(RoomType::Kitchen)) carve(RoomType::Kitchen, 3.0f, {RoomType::Living, RoomType::Bedroom});
            if((floor > 0 || footprint.floors == 1) && !present(RoomType::Bathroom))
                carve(RoomType::Bathroom, 2.2f, {RoomType::Bedroom, RoomType::Kitchen, RoomType::Living});
        }
        // A bathroom is at most 2.6 m and a kitchen 4.2 m wide along its row; the rest of the bay
        // becomes a bedroom (office in an office), or storage when it is too small for one.
        std::vector<bool> rowAlongX(plan.rooms.size(), true);
        for(const auto& [index, alongX] : cellAlongX) rowAlongX[index] = alongX;
        const std::size_t typed = plan.rooms.size();
        for(std::size_t i = firstRoom; i < typed; ++i){
            const RoomType type = plan.rooms[i].type;
            const float limit = type == RoomType::Bathroom ? 2.6f : type == RoomType::Kitchen ? 4.2f : 0.0f;
            if(limit == 0.0f || std::find(cells.begin(), cells.end(), i) == cells.end()) continue;
            LocalRect& r = plan.rooms[i].rect;
            const bool alongX = rowAlongX[i];
            const float width = alongX ? r.max.x - r.min.x : r.max.y - r.min.y;
            if(width <= limit + 0.2f) continue;
            if(width < limit + 2.4f){
                // The rest is too narrow for a room: the neighbour in the row takes it.
                for(std::size_t j = firstRoom; j < typed; ++j){
                    if(j == i || isOpen(plan.rooms[j].type)) continue;
                    LocalRect& o = plan.rooms[j].rect;
                    const bool sameRow = alongX ? std::abs(o.min.y - r.min.y) < eps && std::abs(o.max.y - r.max.y) < eps
                                                : std::abs(o.min.x - r.min.x) < eps && std::abs(o.max.x - r.max.x) < eps;
                    if(!sameRow) continue;
                    float& rHigh = alongX ? r.max.x : r.max.y;
                    float& rLow = alongX ? r.min.x : r.min.y;
                    float& oLow = alongX ? o.min.x : o.min.y;
                    float& oHigh = alongX ? o.max.x : o.max.y;
                    if(std::abs(oLow - rHigh) < eps){ rHigh = rLow + limit; oLow = rHigh; break; }
                    if(std::abs(oHigh - rLow) < eps){ rLow = rHigh - limit; oHigh = rLow; break; }
                }
                continue;
            }
            LocalRect rest = r;
            if(alongX){ rest.min.x = r.min.x + limit; r.max.x = rest.min.x; }
            else{ rest.min.y = r.min.y + limit; r.max.y = rest.min.y; }
            const bool roomy = area(rest) >= 7.0f && std::min(rest.max.x - rest.min.x, rest.max.y - rest.min.y) >= 2.4f &&
                               facade(rest) >= 2.2f;
            const RoomType restType = !roomy ? RoomType::Storage
                                    : settings.program == InteriorProgram::Office ? RoomType::Office : RoomType::Bedroom;
            plan.rooms.push_back({floor, restType, rest});
        }
        // With a service strip the bathrooms move there, off the facade: facade bathrooms become
        // bedrooms (offices), and the strip gets one bathroom per two bedrooms, at least one.
        if(!services.empty()){
            int bedrooms = 0;
            for(std::size_t i = firstRoom; i < plan.rooms.size(); ++i){
                Room& room = plan.rooms[i];
                if(room.type == RoomType::Bathroom && std::find(services.begin(), services.end(), i) == services.end() &&
                   facade(room.rect) >= 2.2f)
                    room.type = settings.program == InteriorProgram::Office ? RoomType::Office
                              : area(room.rect) >= 7.0f && std::min(room.rect.max.x - room.rect.min.x, room.rect.max.y - room.rect.min.y) >= 2.4f
                                    ? RoomType::Bedroom : RoomType::Storage;
                if(room.type == RoomType::Bedroom) ++bedrooms;
            }
            std::sort(services.begin(), services.end(), [&](std::size_t a, std::size_t b){ return area(plan.rooms[a].rect) > area(plan.rooms[b].rect); });
            const std::size_t baths = std::max<std::size_t>(1, std::size_t(bedrooms / 2));
            for(std::size_t k = 0; k < services.size() && k < baths; ++k) plan.rooms[services[k]].type = RoomType::Bathroom;
        }
        if(plan.rooms.size() == firstRoom){ error = "floor " + std::to_string(floor) + " has no rooms"; return false; }
    }

    // Bays merged into a living room leave empty rectangles behind.
    plan.rooms.erase(std::remove_if(plan.rooms.begin(), plan.rooms.end(), [](const Room& r){ return area(r.rect) <= 0.0f; }),
                     plan.rooms.end());

    // 6. Access: open spaces that touch are one space; every other room gets a door into the
    //    open space it shares the longest wall with, or else into a neighbour that is reached.
    const float doorWidth = settings.doorWidth;
    for(uint32_t floor = 0; floor < footprint.floors; ++floor){
        std::vector<std::size_t> rooms;
        for(std::size_t i = 0; i < plan.rooms.size(); ++i) if(plan.rooms[i].floor == floor) rooms.push_back(i);
        struct Link{ std::size_t other; glm::vec2 from, to; };
        std::vector<std::vector<Link>> links(plan.rooms.size());
        for(std::size_t a = 0; a < rooms.size(); ++a)
            for(std::size_t b = a + 1; b < rooms.size(); ++b){
                glm::vec2 from, to;
                if(sharedSide(plan.rooms[rooms[a]].rect, plan.rooms[rooms[b]].rect, from, to)){
                    links[rooms[a]].push_back({rooms[b], from, to});
                    links[rooms[b]].push_back({rooms[a], to, from});
                }
            }
        // Start: the space behind the front door on the ground floor, the stairs above it.
        std::size_t start = plan.rooms.size();
        for(std::size_t i : rooms){
            const RoomType t = plan.rooms[i].type;
            if(floor > 0 && t == RoomType::Stairs){ start = i; break; }
            if(floor == 0 && (t == RoomType::Hall || t == RoomType::Corridor)){
                const LocalRect& r = plan.rooms[i].rect;
                const float low = edgeAlongX ? r.min.x : r.min.y, high = edgeAlongX ? r.max.x : r.max.y;
                const float side0 = edgeAlongX ? r.min.y : r.min.x, side1 = edgeAlongX ? r.max.y : r.max.x;
                if((std::abs(side0 - edgeLine) < 1e-3f || std::abs(side1 - edgeLine) < 1e-3f) &&
                   entranceLocal > low + eps && entranceLocal < high - eps){ start = i; break; }
            }
        }
        if(start == plan.rooms.size()){
            // Single floor without a hall slot: the door opens into the room behind it.
            for(std::size_t i : rooms){
                const LocalRect& r = plan.rooms[i].rect;
                const float low = edgeAlongX ? r.min.x : r.min.y, high = edgeAlongX ? r.max.x : r.max.y;
                const float side0 = edgeAlongX ? r.min.y : r.min.x, side1 = edgeAlongX ? r.max.y : r.max.x;
                if(floor == 0 && (std::abs(side0 - edgeLine) < 1e-3f || std::abs(side1 - edgeLine) < 1e-3f) &&
                   entranceLocal > low + eps && entranceLocal < high - eps){ start = i; break; }
            }
        }
        if(start == plan.rooms.size()){ error = "floor " + std::to_string(floor) + " has no way in"; return false; }
        if(floor == 0 && !isOpen(plan.rooms[start].type)) plan.rooms[start].type = RoomType::Hall;
        std::vector<bool> reached(plan.rooms.size(), false);
        std::queue<std::size_t> open;
        reached[start] = true; open.push(start);
        auto flood = [&](){
            while(!open.empty()){
                const std::size_t i = open.front(); open.pop();
                if(!isOpen(plan.rooms[i].type)) continue;
                for(const Link& l : links[i])
                    if(!reached[l.other] && isOpen(plan.rooms[l.other].type) && glm::length(l.to - l.from) >= 0.9f){
                        reached[l.other] = true; open.push(l.other);
                    }
            }
        };
        flood();
        auto addDoor = [&](std::size_t a, std::size_t b, glm::vec2 from, glm::vec2 to){
            const float length = glm::length(to - from);
            const glm::vec2 dir = (to - from) / length;
            // Near one end like a real door, 0.35 m from the corner when there is room.
            const float offset = length >= doorWidth + 0.7f ? 0.35f : (length - doorWidth) * 0.5f;
            const glm::vec2 d0 = from + dir * offset;
            plan.doors.push_back({floor, uint32_t(a), uint32_t(b), d0, d0 + dir * doorWidth});
        };
        // Rooms off the open space, then rooms reached through other rooms, until none is left.
        bool grew = true;
        while(grew){
            grew = false;
            for(const bool viaOpen : {true, false}){
                for(std::size_t i : rooms){
                    if(reached[i]) continue;
                    const Link* best = nullptr;
                    for(const Link& l : links[i]){
                        if(!reached[l.other] || glm::length(l.to - l.from) < doorWidth + 0.3f) continue;
                        if(viaOpen != isOpen(plan.rooms[l.other].type)) continue;
                        if(!best || glm::length(l.to - l.from) > glm::length(best->to - best->from)) best = &l;
                    }
                    if(!best) continue;
                    addDoor(best->other, i, best->from, best->to);
                    reached[i] = true; grew = true;
                    if(isOpen(plan.rooms[i].type)){ open.push(i); flood(); }
                }
                if(grew) break;
            }
        }
        for(std::size_t i : rooms)
            if(!reached[i]){
                error = roomTypeNames()[std::size_t(plan.rooms[i].type)] + " on floor " + std::to_string(floor) + " has no access";
                return false;
            }
    }

    // A home needs a living room, a kitchen and a bathroom; an office a toilet.
    auto has = [&](RoomType type){
        return std::any_of(plan.rooms.begin(), plan.rooms.end(), [&](const Room& r){ return r.type == type; });
    };
    const bool bigLiving = std::any_of(plan.rooms.begin(), plan.rooms.end(), [](const Room& r){
        return r.type == RoomType::Living && area(r.rect) >= 12.0f;
    });
    if(settings.program == InteriorProgram::Residential && (!bigLiving || !has(RoomType::Kitchen) || !has(RoomType::Bathroom))){
        error = "footprint is too small for a home: it needs room for a living room of 12 m2, a kitchen and a bathroom"; return false;
    }
    if(settings.program == InteriorProgram::Office && !has(RoomType::Bathroom)){
        error = "footprint is too small for an office: it needs room for a toilet"; return false;
    }

    // Front door position along its outline edge, from the edge's first corner.
    const float firstLocal = edgeAlongX ? la.x : la.y, lastLocal = edgeAlongX ? lb.x : lb.y;
    plan.entranceEdge = entranceEdge;
    plan.entranceCenter = std::abs(entranceLocal - firstLocal) * (edgeLength / std::abs(lastLocal - firstLocal));
    output = footprint;
    output.plan = std::move(plan);
    output.hasPlan = true;
    error.clear();
    return true;
}

namespace{

// Box over a local rectangle, turned into the world by the footprint frame.
void localBox(MeshData& mesh, const Footprint& fp, float x0, float x1, float z0, float z1, float y0, float y1,
              uint16_t semantic, uint16_t material, bool& overflow, std::size_t maxVertices){
    if(x1 - x0 < 1e-4f || z1 - z0 < 1e-4f || y1 - y0 < 1e-4f) return;
    if(mesh.vertices.size() + 36 > maxVertices){ overflow = true; return; }
    const glm::vec2 c[4] = {fp.toWorld({x0, z0}), fp.toWorld({x1, z0}), fp.toWorld({x1, z1}), fp.toWorld({x0, z1})};
    const glm::vec3 p[8] = {at(c[0], y0), at(c[1], y0), at(c[2], y0), at(c[3], y0), at(c[0], y1), at(c[1], y1), at(c[2], y1), at(c[3], y1)};
    const glm::vec3 center = (p[0] + p[6]) * 0.5f;
    auto quad = [&](int a, int b, int cc, int d){
        glm::vec3 normal = glm::normalize(glm::cross(p[b] - p[a], p[cc] - p[a]));
        const glm::vec3 faceCenter = (p[a] + p[cc]) * 0.5f;
        int order[4] = {a, b, cc, d};
        if(glm::dot(normal, faceCenter - center) < 0.0f){ std::swap(order[1], order[3]); normal = -normal; }
        const glm::vec3 axis = glm::abs(normal);
        auto uv = [&](const glm::vec3& q) -> glm::vec2{
            if(axis.y >= axis.x && axis.y >= axis.z) return {q.x, q.z};
            if(axis.x >= axis.z) return {q.z, q.y};
            return {q.x, q.y};
        };
        const uint32_t base = uint32_t(mesh.vertices.size());
        for(int k : order) mesh.vertices.push_back({p[k], normal, uv(p[k])});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        mesh.triangles.push_back({0, semantic, material});
        mesh.triangles.push_back({0, semantic, material});
    };
    quad(0, 1, 2, 3); quad(4, 5, 6, 7); quad(0, 1, 5, 4); quad(1, 2, 6, 5); quad(2, 3, 7, 6); quad(3, 0, 4, 7);
}

void localQuadUp(MeshData& mesh, const Footprint& fp, const LocalRect& r, float y, uint16_t semantic, uint16_t material){
    if(r.max.x - r.min.x < 1e-4f || r.max.y - r.min.y < 1e-4f) return;
    glm::vec3 p[4] = {at(fp.toWorld(r.min), y), at(fp.toWorld({r.max.x, r.min.y}), y), at(fp.toWorld(r.max), y),
                      at(fp.toWorld({r.min.x, r.max.y}), y)};
    if(glm::cross(p[1] - p[0], p[2] - p[0]).y < 0.0f) std::swap(p[1], p[3]);
    const uint32_t base = uint32_t(mesh.vertices.size());
    for(const glm::vec3& q : p) mesh.vertices.push_back({q, {0, 1, 0}, {q.x, q.z}});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    mesh.triangles.push_back({0, semantic, material});
    mesh.triangles.push_back({0, semantic, material});
}

}  // namespace

bool makeInterior(const Footprint& footprint, const InteriorNode& settings, MeshData& output, std::string& error,
                  std::size_t maxVertices){
    if(!footprint.hasPlan){ error = "Interior needs a RoomSplit before it"; return false; }
    const InteriorPlan& plan = footprint.plan;
    const float t = settings.partitionThickness, h = footprint.floorHeight;
    const uint16_t wallTag = semanticId("wall_interior"), doorTag = semanticId("door"), floorTag = semanticId("floor");
    const uint16_t stairsTag = semanticId("stairs"), railingTag = semanticId("railing");
    const uint16_t plaster = materialId("plaster"), wood = materialId("wood_planks"), tiles = materialId("paving");
    const uint16_t concrete = materialId("concrete"), metal = materialId("metal");
    MeshData mesh;
    bool overflow = false;
    const float doorHeight = 2.1f;

    // Flights run from the start strip at the core's low end to a landing at its high end.
    const LocalRect& core = plan.stairCore;
    const float cu0 = plan.stairsAlongX ? core.min.x : core.min.y, cu1 = plan.stairsAlongX ? core.max.x : core.max.y;
    const float cv0 = plan.stairsAlongX ? core.min.y : core.min.x, cv1 = plan.stairsAlongX ? core.max.y : core.max.x;
    auto coreBox = [&](float u0, float u1, float v0, float v1, float y0, float y1, uint16_t semantic, uint16_t material){
        if(plan.stairsAlongX) localBox(mesh, footprint, u0, u1, v0, v1, y0, y1, semantic, material, overflow, maxVertices);
        else localBox(mesh, footprint, v0, v1, u0, u1, y0, y1, semantic, material, overflow, maxVertices);
    };
    const float strip = 0.9f, landing = 1.1f;

    for(uint32_t floor = 0; floor < footprint.floors; ++floor){
        const float low = footprint.elevation + float(floor) * h, high = footprint.elevation + float(floor + 1) * h;
        // Partitions between rooms that are not both open space, split around their doors,
        // plus a post at every end so corners and joints close.
        std::vector<glm::vec2> posts;
        for(std::size_t a = 0; a < plan.rooms.size(); ++a){
            if(plan.rooms[a].floor != floor) continue;
            for(std::size_t b = a + 1; b < plan.rooms.size(); ++b){
                if(plan.rooms[b].floor != floor) continue;
                if(isOpen(plan.rooms[a].type) && isOpen(plan.rooms[b].type)) continue;
                glm::vec2 from, to;
                if(!sharedSide(plan.rooms[a].rect, plan.rooms[b].rect, from, to)) continue;
                const bool alongX = std::abs(from.y - to.y) < eps;
                const float line = alongX ? from.y : from.x;
                float s0 = alongX ? std::min(from.x, to.x) : std::min(from.y, to.y);
                float s1 = alongX ? std::max(from.x, to.x) : std::max(from.y, to.y);
                posts.push_back(alongX ? glm::vec2(s0, line) : glm::vec2(line, s0));
                posts.push_back(alongX ? glm::vec2(s1, line) : glm::vec2(line, s1));
                std::vector<std::pair<float, float>> gaps;
                for(const InteriorDoor& d : plan.doors){
                    if(d.floor != floor || !((d.roomA == a && d.roomB == b) || (d.roomA == b && d.roomB == a))) continue;
                    const float d0 = alongX ? d.from.x : d.from.y, d1 = alongX ? d.to.x : d.to.y;
                    gaps.push_back({std::min(d0, d1), std::max(d0, d1)});
                }
                std::sort(gaps.begin(), gaps.end());
                auto piece = [&](float p0, float p1, float y0, float y1){
                    if(alongX) localBox(mesh, footprint, p0, p1, line - t * 0.5f, line + t * 0.5f, y0, y1, wallTag, plaster, overflow, maxVertices);
                    else localBox(mesh, footprint, line - t * 0.5f, line + t * 0.5f, p0, p1, y0, y1, wallTag, plaster, overflow, maxVertices);
                };
                float cursor = s0 + t * 0.5f;   // posts fill the first and last half thickness
                for(const auto& [g0, g1] : gaps){
                    piece(cursor, g0, low, high);
                    piece(g0, g1, low + doorHeight, high);
                    cursor = g1;
                }
                piece(cursor, s1 - t * 0.5f, low, high);
            }
        }
        std::sort(posts.begin(), posts.end(), [](const glm::vec2& x, const glm::vec2& y){ return x.x < y.x || (x.x == y.x && x.y < y.y); });
        posts.erase(std::unique(posts.begin(), posts.end(), [](const glm::vec2& x, const glm::vec2& y){ return glm::length(x - y) < eps; }), posts.end());
        for(const glm::vec2& p : posts)
            localBox(mesh, footprint, p.x - t * 0.5f, p.x + t * 0.5f, p.y - t * 0.5f, p.y + t * 0.5f, low, high, wallTag, plaster, overflow, maxVertices);

        // Door leaves, opened 90 degrees into the room that is not open space.
        if(settings.doorLeaves)
            for(const InteriorDoor& d : plan.doors){
                if(d.floor != floor) continue;
                const std::size_t room = isOpen(plan.rooms[d.roomB].type) ? d.roomA : d.roomB;
                const LocalRect& r = plan.rooms[room].rect;
                const glm::vec2 center = (r.min + r.max) * 0.5f;
                const bool alongX = std::abs(d.from.y - d.to.y) < eps;
                const float line = alongX ? d.from.y : d.from.x;
                const float into = (alongX ? center.y : center.x) > line ? 1.0f : -1.0f;
                const float hinge = alongX ? d.from.x : d.from.y;
                const float width = glm::length(d.to - d.from);
                const float face = line + into * t * 0.5f;
                const float a0 = std::min(face, face + into * width), a1 = std::max(face, face + into * width);
                const float leaf = 0.04f;
                if(alongX) localBox(mesh, footprint, hinge, hinge + leaf, a0, a1, low + 0.01f, low + doorHeight - 0.02f, doorTag, wood, overflow, maxVertices);
                else localBox(mesh, footprint, a0, a1, hinge, hinge + leaf, low + 0.01f, low + doorHeight - 0.02f, doorTag, wood, overflow, maxVertices);
            }

        // Floor finish per room, 2 cm above the slab; over the stairwell only the arrival strip.
        if(settings.floorFinish)
            for(const Room& room : plan.rooms){
                if(room.floor != floor) continue;
                const RoomType type = room.type;
                const uint16_t material = type == RoomType::Bathroom || type == RoomType::Kitchen ? tiles
                                        : type == RoomType::Stairs || type == RoomType::Storage ? concrete : wood;
                LocalRect r = room.rect;
                if(type == RoomType::Stairs && floor > 0){
                    if(plan.stairsAlongX) r.max.x = r.min.x + strip; else r.max.y = r.min.y + strip;
                }
                localQuadUp(mesh, footprint, r, low + 0.02f, floorTag, material);
            }

        // Two flights with a landing up to the next floor, a wall between them, and a
        // balustrade round the stairwell on the top floor.
        if(settings.stairs && plan.hasStairs){
            const float width = cv1 - cv0, flight = (width - 0.1f) * 0.5f;
            if(floor + 1 < footprint.floors){
                const int steps = int(std::ceil(h / 0.175f)), first = steps / 2, second = steps - first;
                const float riser = h / float(steps), run = cu1 - landing - (cu0 + strip);
                const float tread1 = run / float(first), tread2 = run / float(second);
                // Each step is a block down to a 20 cm stair slab under it, so the underside follows
                // the slope and there is headroom below the flight above.
                auto under = [&](float top){ return std::max(low, top - riser - 0.2f); };
                for(int k = 0; k < first; ++k){
                    const float top = low + riser * float(k + 1);
                    coreBox(cu0 + strip + float(k) * tread1, cu0 + strip + float(k + 1) * tread1, cv0, cv0 + flight,
                            under(top), top, stairsTag, concrete);
                }
                const float landingTop = low + riser * float(first);
                coreBox(cu1 - landing, cu1, cv0, cv1, std::max(low, landingTop - 0.2f), landingTop, stairsTag, concrete);
                for(int k = 0; k < second; ++k){
                    const float top = low + riser * float(first + k + 1);
                    coreBox(cu1 - landing - float(k + 1) * tread2, cu1 - landing - float(k) * tread2, cv1 - flight, cv1,
                            under(top), top, stairsTag, concrete);
                }
                coreBox(cu0 + strip, cu1 - landing, cv0 + flight, cv1 - flight, low, high, wallTag, plaster);
            }else if(floor > 0){
                coreBox(cu0 + strip, cu0 + strip + 0.06f, cv0, cv0 + flight, low, low + 1.0f, railingTag, metal);
            }
        }
    }
    if(overflow){ error = "interior exceeds the configured vertex limit"; return false; }
    if(mesh.indices.empty()){ error = "interior produced no geometry"; return false; }
    output = std::move(mesh);
    error.clear();
    return true;
}

}  // namespace Engine::WeaverProcedura
