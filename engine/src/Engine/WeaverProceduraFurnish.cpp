// Furniture rules for WeaverProcedura (Furnish, Place Assets). A layout is rules over the room
// plan, not a drawing: every room type asks for its pieces in order of importance, each piece
// takes the best free spot along a wall (or in the middle for tables) by that type's taste, and
// nothing may stand in a door's swing or in the free floor another piece needs in front of it.
// The pieces themselves come from an asset library, so the same rules furnish with any style.
#include "WeaverProcedura.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

namespace Engine::WeaverProcedura{
namespace{

constexpr float eps = 1e-3f;
constexpr float pi = 3.14159265358979f;
constexpr float tallHeight = 1.2f;     // pieces above this keep off window walls

struct Random{
    uint64_t state;
    explicit Random(uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull){}
    uint64_t next(){ state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
    float uniform(float low, float high){ return low + (high - low) * float(next() % 1000000) / 999999.0f; }
};

bool overlaps(const LocalRect& a, const LocalRect& b){
    return a.min.x < b.max.x - eps && a.max.x > b.min.x + eps && a.min.y < b.max.y - eps && a.max.y > b.min.y + eps;
}
bool contains(const LocalRect& outer, const LocalRect& inner){
    return inner.min.x >= outer.min.x - eps && inner.max.x <= outer.max.x + eps &&
           inner.min.y >= outer.min.y - eps && inner.max.y <= outer.max.y + eps;
}
glm::vec2 centre(const LocalRect& r){ return (r.min + r.max) * 0.5f; }
LocalRect rectOf(const glm::vec2& a, const glm::vec2& b){ return {glm::min(a, b), glm::max(a, b)}; }
LocalRect grown(const LocalRect& r, float by){ return {r.min - glm::vec2(by), r.max + glm::vec2(by)}; }
bool isOpen(RoomType type){ return type == RoomType::Hall || type == RoomType::Corridor || type == RoomType::Stairs; }

// Sides of a room's floor: 0 low z, 1 high x, 2 high z, 3 low x. Walking round them this way,
// `along` is the asset's +X when its front faces into the room.
const glm::vec2 inward[4] = {{0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}, {1.0f, 0.0f}};
const glm::vec2 alongDir[4] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}};
glm::vec2 sideStart(const LocalRect& r, int s){
    switch(s){
        case 0: return r.min;
        case 1: return {r.max.x, r.min.y};
        case 2: return r.max;
        default: return {r.min.x, r.max.y};
    }
}
float sideLength(const LocalRect& r, int s){ return s % 2 == 0 ? r.max.x - r.min.x : r.max.y - r.min.y; }
// The side a wall line of the rectangle lies on, or -1.
int sideOfLine(const LocalRect& r, bool lineAlongX, float line){
    if(lineAlongX){
        if(std::abs(line - r.min.y) < 1e-3f) return 0;
        if(std::abs(line - r.max.y) < 1e-3f) return 2;
    }else{
        if(std::abs(line - r.max.x) < 1e-3f) return 1;
        if(std::abs(line - r.min.x) < 1e-3f) return 3;
    }
    return -1;
}

glm::vec2 toLocal(const Footprint& footprint, const glm::vec2& world){
    const glm::vec2 d = world - footprint.frameCenter;
    const glm::vec2 across{-footprint.frameAxis.y, footprint.frameAxis.x};
    return {glm::dot(d, footprint.frameAxis), glm::dot(d, across)};
}

// Usable floor of one room and what is already on it.
struct Space{
    std::size_t index = 0;
    RoomType type = RoomType::Living;
    uint32_t floor = 0;
    LocalRect inner;                    // between the wall faces
    bool window[4] = {};                // an outside wall that gets windows
    bool open[4] = {};                  // no wall: the side runs into another open space
    std::vector<LocalRect> keepClear;   // door swings and the way through open sides
    std::vector<LocalRect> windowZones; // floor in front of window stretches: nothing tall
    std::vector<glm::vec2> doors;       // centres of the doors into the room
    std::vector<LocalRect> bodies;
    std::vector<LocalRect> clearances;
};

LocalRect wallRect(const Space& space, int s, float t, float width, float from, float to){
    const glm::vec2 o = sideStart(space.inner, s);
    return rectOf(o + alongDir[s] * t + inward[s] * from, o + alongDir[s] * (t + width) + inward[s] * to);
}

// A body may not touch doors, other bodies or the free floor in front of them; the free floor a
// piece asks for must stay inside the room and off every body.
bool fits(const Space& space, const LocalRect& body, const std::vector<LocalRect>& clear){
    if(!contains(space.inner, body)) return false;
    for(const LocalRect& k : space.keepClear) if(overlaps(body, k)) return false;
    for(const LocalRect& b : space.bodies) if(overlaps(body, b)) return false;
    for(const LocalRect& c : space.clearances) if(overlaps(body, c)) return false;
    for(const LocalRect& c : clear){
        if(!contains(space.inner, c)) return false;
        for(const LocalRect& b : space.bodies) if(overlaps(c, b)) return false;
    }
    return true;
}

// Free floor: inside the room and off every body (it may overlap a door swing).
bool freeFloor(const Space& space, const LocalRect& floor){
    if(!contains(space.inner, floor)) return false;
    for(const LocalRect& b : space.bodies) if(overlaps(floor, b)) return false;
    return true;
}

// A rug: inside the room and out of every door swing; furniture may stand on it.
bool floorLayerFits(const Space& space, const LocalRect& rug){
    if(!contains(space.inner, rug)) return false;
    for(const LocalRect& k : space.keepClear) if(overlaps(rug, k)) return false;
    return true;
}

float distanceToDoor(const Space& space, const glm::vec2& p){
    float best = 100.0f;
    for(const glm::vec2& d : space.doors) best = std::min(best, glm::length(d - p));
    return best;
}

struct Item{
    std::string asset;
    AssetParameters parameters;
    glm::vec3 size{0.0f};               // width, height, depth
    float clearance = 0.0f;
    bool tall() const{ return size.y > tallHeight; }
};

// Wall spot request: free floor in front, and at the ends (from `sideFrom` off the wall, so a
// night stand can stand in a bed's side clearance). sides: 0 none, 1 either end, 2 both ends.
struct WallRequest{
    float width = 1.0f, depth = 0.5f, front = 0.0f;
    float sideClear = 0.0f, sideFrom = 0.0f;
    int sides = 0;
    bool tall = false;
    float frontBody = 0.0f;             // floor in front that a companion piece (a chair) takes
};

struct Spot{
    int side = -1;
    float t = 0.0f;
    LocalRect body;
    std::vector<LocalRect> clear;
    float score = -std::numeric_limits<float>::infinity();
    bool found() const{ return side >= 0; }
};

// Offsets along a side worth trying: both ends, the middle, flush against everything already
// in the room, and a 0.3 m grid.
std::vector<float> offsetsAlong(const Space& space, int s, float width){
    const float length = sideLength(space.inner, s), last = length - width;
    std::vector<float> result = {0.0f, last, last * 0.5f};
    const glm::vec2 o = sideStart(space.inner, s);
    auto flush = [&](const LocalRect& r){
        float a = glm::dot(r.min - o, alongDir[s]), b = glm::dot(r.max - o, alongDir[s]);
        if(a > b) std::swap(a, b);
        result.push_back(b + 0.02f);
        result.push_back(a - width - 0.02f);
    };
    for(const LocalRect& r : space.keepClear) flush(r);
    for(const LocalRect& r : space.bodies) flush(r);
    for(const LocalRect& r : space.clearances) flush(r);
    for(float g = 0.3f; g < last; g += 0.3f) result.push_back(g);
    std::vector<float> valid;
    for(float t : result) if(t >= -eps && t <= last + eps) valid.push_back(std::clamp(t, 0.0f, std::max(0.0f, last)));
    return valid;
}

// Every free spot for the request, best first.
template<class Score>
std::vector<Spot> wallSpots(const Space& space, const WallRequest& request, Score score){
    std::vector<Spot> spots;
    for(int s = 0; s < 4; ++s){
        if(space.open[s] || (request.tall && space.window[s])) continue;
        if(request.width > sideLength(space.inner, s) + eps) continue;
        for(float t : offsetsAlong(space, s, request.width)){
            const LocalRect body = wallRect(space, s, t, request.width, 0.0f, request.depth);
            std::vector<LocalRect> clear;
            if(request.front > 0.0f) clear.push_back(wallRect(space, s, t, request.width, request.depth, request.depth + request.front));
            bool ok = true;
            if(request.sides > 0 && request.sideClear > 0.0f){
                const LocalRect left = wallRect(space, s, t - request.sideClear, request.sideClear, request.sideFrom, request.depth);
                const LocalRect right = wallRect(space, s, t + request.width, request.sideClear, request.sideFrom, request.depth);
                const bool leftFree = freeFloor(space, left), rightFree = freeFloor(space, right);
                if(request.sides == 2){ ok = leftFree && rightFree; clear.push_back(left); clear.push_back(right); }
                else if(leftFree) clear.push_back(left);
                else if(rightFree) clear.push_back(right);
                else ok = false;
            }
            if(!ok || !fits(space, body, clear)) continue;
            if(request.frontBody > 0.0f &&
               !fits(space, wallRect(space, s, t, request.width, request.depth, request.depth + request.frontBody), {})) continue;
            if(request.tall && std::any_of(space.windowZones.begin(), space.windowZones.end(),
                                           [&](const LocalRect& zone){ return overlaps(body, zone); })) continue;
            spots.push_back({s, t, body, clear, score(s, t, body)});
        }
    }
    std::stable_sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b){ return a.score > b.score; });
    return spots;
}

template<class Score>
Spot bestWallSpot(const Space& space, const WallRequest& request, Score score){
    std::vector<Spot> spots = wallSpots(space, request, score);
    return spots.empty() ? Spot{} : spots.front();
}

struct Context{
    const Footprint& footprint;
    const FurnishNode& settings;
    const AssetLibrary& library;
    Random random;
    std::map<std::string, std::string> style;       // category -> asset, one choice per house
    std::vector<Placement>& output;
    std::string error;

    bool item(const std::string& category, const AssetParameters& wanted, Item& result){
        auto chosen = style.find(category);
        if(chosen == style.end()){
            const std::vector<std::string> ids = assetsInCategory(library, category);
            if(ids.empty()){ error = "asset library has no " + category; return false; }
            chosen = style.emplace(category, ids[std::size_t(random.next() % ids.size())]).first;
        }
        const AssetInfo* info = library.info(chosen->second);
        if(!info){ error = "asset library lost " + chosen->second; return false; }
        result = {};
        result.asset = info->id;
        for(const auto& [name, value] : wanted)
            for(const AssetParameter& parameter : info->parameters)
                if(parameter.name == name) result.parameters.push_back({name, std::clamp(value, parameter.minValue, parameter.maxValue)});
        std::string why;
        if(!library.bounds(info->id, result.parameters, result.size, why)){ error = info->id + ": " + why; return false; }
        result.clearance = info->clearanceFront;
        return true;
    }
    bool optional(float share){ return settings.fill >= 1.0f || random.uniform(0.0f, 1.0f) < share * settings.fill; }

    // Adds a placement and returns its index. base lifts it off the floor (a lamp on a stand);
    // a piece that is not solid (rug, lamp on top, wall cabinet) leaves the floor free for others.
    // turn twists it a few degrees from facing; its area is then the box round the turned piece.
    std::size_t put(Space& space, const Item& piece, const LocalRect& body, const glm::vec2& facing, float base = 0.0f,
                    bool solid = true, float turn = 0.0f, int32_t under = -1){
        Placement p;
        p.asset = piece.asset;
        p.parameters = piece.parameters;
        const float floorY = footprint.elevation + float(space.floor) * footprint.floorHeight + 0.02f;
        const glm::vec2 c = footprint.toWorld(centre(body));
        p.position = {c.x, floorY + base, c.y};
        const glm::vec2 across{-footprint.frameAxis.y, footprint.frameAxis.x};
        const glm::vec2 world = footprint.frameAxis * facing.x + across * facing.y;
        p.yawDegrees = std::atan2(world.x, world.y) * 180.0f / pi + turn;
        p.floor = space.floor;
        p.room = uint32_t(space.index);
        LocalRect area = body;
        if(turn != 0.0f){
            const float cs = std::abs(std::cos(turn * pi / 180.0f)), sn = std::abs(std::sin(turn * pi / 180.0f));
            const glm::vec2 size = body.max - body.min, half = glm::vec2(size.x * cs + size.y * sn, size.x * sn + size.y * cs) * 0.5f;
            area = {centre(body) - half, centre(body) + half};
        }
        p.area = area;
        p.base = base;
        p.height = piece.size.y;
        p.under = under;
        output.push_back(std::move(p));
        if(solid) space.bodies.push_back(area);
        return output.size() - 1;
    }
    void putAt(Space& space, const Item& piece, const Spot& spot){
        put(space, piece, spot.body, inward[spot.side]);
        space.clearances.insert(space.clearances.end(), spot.clear.begin(), spot.clear.end());
    }
    std::string where(const Space& space) const{
        char size[48];
        std::snprintf(size, sizeof(size), " (%.2f x %.2f m, %zu doors)", double(space.inner.max.x - space.inner.min.x),
                      double(space.inner.max.y - space.inner.min.y), space.doors.size());
        return roomTypeNames()[std::size_t(space.type)] + " on floor " + std::to_string(space.floor) + size;
    }
};

bool atCorner(const Space& space, int s, float t, float width){
    return t < 0.05f || t > sideLength(space.inner, s) - width - 0.05f;
}

// A wall piece: the first of the widths that finds a spot. Returns false only on a library error.
template<class Score>
bool wallPiece(Context& ctx, Space& space, const std::string& category, const std::vector<float>& widths,
               const AssetParameters& extra, float front, Score score, bool& placed, Spot* where = nullptr){
    placed = false;
    for(float width : widths){
        AssetParameters wanted = extra;
        wanted.push_back({"width", width});
        Item piece;
        if(!ctx.item(category, wanted, piece)) return false;
        WallRequest request{piece.size.x, piece.size.z, std::max(front, piece.clearance)};
        request.tall = piece.tall();
        const Spot spot = bestWallSpot(space, request, score);
        if(!spot.found()) continue;
        ctx.putAt(space, piece, spot);
        if(where) *where = spot;
        placed = true;
        return true;
    }
    return true;
}

float jitter(Context& ctx){ return ctx.random.uniform(0.0f, 0.05f); }

// A table with chairs round it, in the middle of the free floor. Chairs face the table; the
// walkway round the chairs stays free.
bool tableGroup(Context& ctx, Space& space, const std::vector<glm::vec2>& sizes, bool& placed){
    placed = false;
    const LocalRect& r = space.inner;
    const bool longX = r.max.x - r.min.x >= r.max.y - r.min.y;
    for(const glm::vec2& wanted : sizes){
        Item table, chair;
        if(!ctx.item("table", {{"width", wanted.x}, {"depth", wanted.y}}, table)) return false;
        if(!ctx.item("chair", {}, chair)) return false;
        const float length = table.size.x, depth = table.size.z, cw = std::max(chair.size.x, chair.size.z);
        const int perSide = std::clamp(int(length / 0.62f), 1, 4);
        const bool ends = length >= 1.6f;
        const float reach = cw + 0.04f, walk = 0.3f;
        for(const bool alongX : {longX, !longX}){
            const glm::vec2 half = alongX ? glm::vec2(length, depth) * 0.5f : glm::vec2(depth, length) * 0.5f;
            const glm::vec2 groupHalf = half + (alongX ? glm::vec2(ends ? reach : 0.0f, reach) : glm::vec2(reach, ends ? reach : 0.0f));
            const glm::vec2 middle = centre(r);
            std::vector<glm::vec2> candidates;
            for(float x = r.min.x + groupHalf.x; x <= r.max.x - groupHalf.x + eps; x += 0.2f)
                for(float z = r.min.y + groupHalf.y; z <= r.max.y - groupHalf.y + eps; z += 0.2f) candidates.push_back({x, z});
            std::sort(candidates.begin(), candidates.end(), [&](const glm::vec2& a, const glm::vec2& b){
                return glm::length(a - middle) < glm::length(b - middle);
            });
            for(const glm::vec2& c : candidates){
                const LocalRect group{c - groupHalf, c + groupHalf};
                if(!fits(space, group, {})) continue;
                bool walkway = true;
                for(const LocalRect& b : space.bodies) walkway &= !overlaps(grown(group, walk), b);
                if(!walkway) continue;
                const int32_t tableIndex = int32_t(ctx.put(space, table, {c - half, c + half},
                                                           alongX ? glm::vec2(0.0f, 1.0f) : glm::vec2(-1.0f, 0.0f), 0.0f, false));
                space.bodies.push_back(group);          // the chairs' floor is taken too
                const glm::vec2 u = alongX ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f, 1.0f), v{-u.y, u.x};
                const float hl = length * 0.5f, hd = depth * 0.5f, hc = cw * 0.5f;
                // Chairs pushed 12 cm under the table and turned a little, as people leave them.
                auto seat = [&](const glm::vec2& at, const glm::vec2& facing){
                    ctx.put(space, chair, {at - glm::vec2(hc), at + glm::vec2(hc)}, facing, 0.0f, false,
                            ctx.random.uniform(-8.0f, 8.0f), tableIndex);
                };
                const float tuck = 0.12f;
                for(int k = 0; k < perSide; ++k){
                    const float along = -hl + length * (float(k) + 0.5f) / float(perSide);
                    seat(c + u * along - v * (hd - tuck + hc), v);
                    seat(c + u * along + v * (hd - tuck + hc), -v);
                }
                if(ends){
                    seat(c - u * (hl - tuck + hc), u);
                    seat(c + u * (hl - tuck + hc), -u);
                }
                space.clearances.push_back(grown(group, walk));
                placed = true;
                return true;
            }
        }
    }
    return true;
}

// A desk against a wall with its chair pulled up in front.
bool deskWithChair(Context& ctx, Space& space, float width, bool preferWindow, bool& placed){
    placed = false;
    Item desk, chair;
    if(!ctx.item("desk", {{"width", width}}, desk)) return false;
    if(!ctx.item("chair", {}, chair)) return false;
    const float cw = std::max(chair.size.x, chair.size.z);
    WallRequest request{desk.size.x, desk.size.z, cw + 0.45f};
    request.frontBody = cw + 0.04f;
    const Spot spot = bestWallSpot(space, request, [&](int s, float t, const LocalRect& body){
        float score = (space.window[s] == preferWindow ? 2.0f : 0.0f) + std::min(distanceToDoor(space, centre(body)), 3.0f) * 0.3f;
        for(const LocalRect& b : space.bodies) score -= 0.02f * glm::length(centre(b) - centre(body));   // desks side by side
        return score + (atCorner(space, s, t, desk.size.x) ? 0.3f : 0.0f) + jitter(ctx);
    });
    if(!spot.found()) return true;
    const int32_t deskIndex = int32_t(ctx.output.size());
    ctx.putAt(space, desk, spot);
    const glm::vec2 seat = sideStart(space.inner, spot.side) + alongDir[spot.side] * (spot.t + desk.size.x * 0.5f) +
                           inward[spot.side] * (desk.size.z - 0.12f + cw * 0.5f);
    ctx.put(space, chair, {seat - glm::vec2(cw * 0.5f), seat + glm::vec2(cw * 0.5f)}, -inward[spot.side], 0.0f, true,
            ctx.random.uniform(-10.0f, 10.0f), deskIndex);
    placed = true;
    return true;
}

bool furnishBedroom(Context& ctx, Space& space){
    Item stand;
    if(!ctx.item("nightstand", {}, stand)) return false;
    Spot bed;
    Item bedItem;
    for(const float width : {1.6f, 1.4f, 0.9f}){
        if(!ctx.item("bed", {{"width", width}, {"length", 2.0f}}, bedItem)) return false;
        const bool twoSides = bedItem.size.x >= 1.3f;
        WallRequest request{bedItem.size.x, bedItem.size.z, std::max(0.6f, bedItem.clearance), 0.55f, stand.size.z + 0.05f,
                            twoSides ? 2 : 1, bedItem.tall()};
        bed = bestWallSpot(space, request, [&](int s, float t, const LocalRect& body){
            const float middle = (sideLength(space.inner, s) - bedItem.size.x) * 0.5f;
            return std::min(distanceToDoor(space, centre(body)), 4.0f) + (space.window[s] ? 0.0f : 1.5f) -
                   std::abs(t - middle) * 0.3f + jitter(ctx);
        });
        if(bed.found()) break;
    }
    if(!bed.found()){ ctx.error = ctx.where(space) + " has no wall for a bed clear of its door"; return false; }
    ctx.putAt(space, bedItem, bed);
    Item lamp;
    if(!ctx.item("table_lamp", {}, lamp)) return false;
    for(const float t : {bed.t - stand.size.x - 0.03f, bed.t + bedItem.size.x + 0.03f}){
        const LocalRect body = wallRect(space, bed.side, t, stand.size.x, 0.0f, stand.size.z);
        if(!fits(space, body, {})) continue;
        ctx.put(space, stand, body, inward[bed.side]);
        if(ctx.optional(0.8f)){
            const glm::vec2 c = centre(body), half = glm::vec2(std::max(lamp.size.x, lamp.size.z)) * 0.5f;
            ctx.put(space, lamp, {c - half, c + half}, inward[bed.side], stand.size.y, false);
        }
    }
    // A rug under the lower part of the bed, reaching out at the sides.
    if(ctx.optional(0.7f))
        for(const float side : {0.5f, 0.3f, 0.0f}){
            const float from = bedItem.size.z * 0.45f, to = bedItem.size.z + 0.4f;
            const LocalRect rug = wallRect(space, bed.side, bed.t - side, bedItem.size.x + 2.0f * side, from, to);
            if(!floorLayerFits(space, rug)) continue;
            Item piece;
            if(!ctx.item("rug", {{"width", bedItem.size.x + 2.0f * side}, {"depth", to - from}}, piece)) return false;
            ctx.put(space, piece, rug, inward[bed.side], 0.0f, false);
            break;
        }
    bool placed = false;
    if(!wallPiece(ctx, space, "wardrobe", {2.0f, 1.6f, 1.2f, 1.0f, 0.8f}, {}, 0.7f, [&](int s, float t, const LocalRect&){
        return (atCorner(space, s, t, 1.0f) ? 1.0f : 0.0f) + (s != bed.side ? 0.5f : 0.0f) + jitter(ctx);
    }, placed)) return false;
    const float area = (space.inner.max.x - space.inner.min.x) * (space.inner.max.y - space.inner.min.y);
    if(area >= 11.0f && ctx.optional(0.5f) && !deskWithChair(ctx, space, 1.2f, true, placed)) return false;
    return true;
}

bool furnishLiving(Context& ctx, Space& space){
    Item sofa, table, tv;
    if(!ctx.item("coffee_table", {}, table)) return false;
    Spot seat;
    for(const float width : {2.2f, 1.8f, 1.4f}){
        if(!ctx.item("sofa", {{"width", width}}, sofa)) return false;
        const float front = std::max(sofa.clearance, 0.45f + table.size.z + 0.45f);
        WallRequest request{sofa.size.x, sofa.size.z, front};
        request.tall = sofa.tall();
        seat = bestWallSpot(space, request, [&](int s, float t, const LocalRect& body){
            const float facing = sideLength(space.inner, (s + 1) % 4) - sofa.size.z;   // to the wall opposite
            const float middle = (sideLength(space.inner, s) - sofa.size.x) * 0.5f;
            return (facing >= 2.2f && facing <= 5.0f ? 2.0f : 0.0f) + (space.window[s] ? 0.0f : 0.5f) +
                   std::min(distanceToDoor(space, centre(body)), 3.0f) * 0.3f - std::abs(t - middle) * 0.2f + jitter(ctx);
        });
        if(seat.found()) break;
    }
    if(!seat.found()){ ctx.error = ctx.where(space) + " has no wall for a sofa clear of its doors"; return false; }
    ctx.putAt(space, sofa, seat);
    const int s = seat.side;
    const LocalRect coffee = wallRect(space, s, seat.t + (sofa.size.x - table.size.x) * 0.5f, table.size.x,
                                      sofa.size.z + 0.45f, sofa.size.z + 0.45f + table.size.z);
    ctx.put(space, table, coffee, inward[s]);
    // A rug under the coffee table, reaching under the front of the sofa.
    if(ctx.optional(0.8f))
        for(const glm::vec2 size : {glm::vec2(2.0f, 1.4f), glm::vec2(1.6f, 1.1f), glm::vec2(1.2f, 0.8f)}){
            const glm::vec2 c = centre(coffee), half = (s % 2 == 0 ? size : glm::vec2(size.y, size.x)) * 0.5f;
            const LocalRect rug{c - half, c + half};
            if(!floorLayerFits(space, rug)) continue;
            Item piece;
            if(!ctx.item("rug", {{"width", size.x}, {"depth", size.y}}, piece)) return false;
            ctx.put(space, piece, rug, inward[s], 0.0f, false);
            break;
        }
    // TV opposite the sofa, lined up with it when the doors allow.
    const int opposite = (s + 2) % 4;
    const glm::vec2 sofaCentre = centre(seat.body);
    bool haveTv = false;
    for(const float width : {1.6f, 1.2f}){
        if(haveTv) break;
        if(!ctx.item("tv_stand", {{"width", width}}, tv)) return false;
        const float along = glm::dot(sofaCentre - sideStart(space.inner, opposite), alongDir[opposite]) - tv.size.x * 0.5f;
        for(const float shift : {0.0f, 0.3f, -0.3f, 0.6f, -0.6f, 0.9f, -0.9f}){
            const float t = along + shift;
            if(t < 0.0f || t > sideLength(space.inner, opposite) - tv.size.x || space.open[opposite]) continue;
            const LocalRect body = wallRect(space, opposite, t, tv.size.x, 0.0f, tv.size.z);
            const LocalRect front = wallRect(space, opposite, t, tv.size.x, tv.size.z, tv.size.z + 0.3f);
            if(!fits(space, body, {front})) continue;
            ctx.put(space, tv, body, inward[opposite]);
            space.clearances.push_back(front);
            haveTv = true;
            break;
        }
    }
    bool placed = false;
    const glm::vec2 coffeeCentre = centre(coffee);
    if(ctx.optional(0.7f) && !wallPiece(ctx, space, "armchair", {0.85f}, {}, 0.4f, [&](int, float, const LocalRect& body){
        return -glm::length(centre(body) - coffeeCentre) + jitter(ctx);
    }, placed)) return false;
    if(ctx.optional(0.6f) && !wallPiece(ctx, space, "shelf", {1.2f, 0.9f}, {}, 0.6f, [&](int s2, float t, const LocalRect&){
        return (atCorner(space, s2, t, 1.0f) ? 1.0f : 0.0f) + jitter(ctx);
    }, placed)) return false;
    // A floor lamp beside the sofa (a tall piece, so never at a window).
    if(ctx.optional(0.6f) && !wallPiece(ctx, space, "floor_lamp", {0.35f}, {}, 0.0f, [&](int, float, const LocalRect& body){
        return -glm::length(centre(body) - sofaCentre) + jitter(ctx);
    }, placed)) return false;
    const float area = (space.inner.max.x - space.inner.min.x) * (space.inner.max.y - space.inner.min.y);
    if(area >= 24.0f && !tableGroup(ctx, space, {{1.6f, 0.9f}, {1.2f, 0.8f}}, placed)) return false;
    return true;
}

bool furnishKitchen(Context& ctx, Space& space){
    const float longest = std::max(space.inner.max.x - space.inner.min.x, space.inner.max.y - space.inner.min.y);
    std::vector<float> widths;
    for(float w = std::min(longest, 4.2f); w >= 1.2f - eps; w -= 0.2f) widths.push_back(w);
    bool counter = false, fridge = false;
    Spot run;
    // Longest run first (widths go down); among runs of one width a corner, and a wall without a
    // window, so wall cabinets can hang over it.
    auto counterScore = [&](int s, float t, const LocalRect&){
        return (atCorner(space, s, t, 1.2f) ? 1.0f : 0.0f) + (space.window[s] ? 0.0f : 0.6f) + jitter(ctx);
    };
    auto fridgeScore = [&](int, float, const LocalRect& body){
        return -glm::length(centre(body) - centre(run.body)) + jitter(ctx);
    };
    // The run takes the longest free wall; the fridge goes next to it, on the same wall when the
    // run is not under a window. Without room for both, the run gives up a fridge width.
    const std::size_t before = ctx.output.size();
    const Space empty = space;
    if(!wallPiece(ctx, space, "kitchen_counter", widths, {}, 1.0f, counterScore, counter, &run)) return false;
    if(counter && !wallPiece(ctx, space, "fridge", {0.6f}, {}, 0.8f, fridgeScore, fridge)) return false;
    if(counter && !fridge){
        const float runWidth = std::max(run.body.max.x - run.body.min.x, run.body.max.y - run.body.min.y);
        std::vector<float> shorter;
        for(float w : widths) if(w <= runWidth - 0.65f) shorter.push_back(w);
        const Space full = space;
        const std::vector<Placement> fullOutput(ctx.output.begin() + std::ptrdiff_t(before), ctx.output.end());
        const Spot fullRun = run;
        if(!shorter.empty()){
            space = empty;
            ctx.output.resize(before);
            bool again = false;
            if(!wallPiece(ctx, space, "kitchen_counter", shorter, {}, 1.0f, counterScore, again, &run)) return false;
            if(again && !wallPiece(ctx, space, "fridge", {0.6f}, {}, 0.8f, fridgeScore, fridge)) return false;
            if(!again || !fridge){
                space = full;
                run = fullRun;
                ctx.output.resize(before);
                ctx.output.insert(ctx.output.end(), fullOutput.begin(), fullOutput.end());
            }
        }
    }
    if(!counter){ ctx.error = ctx.where(space) + " has no wall for a 1.2 m counter clear of its doors"; return false; }
    std::vector<std::pair<Spot, float>> runs = {{run, std::max(run.body.max.x - run.body.min.x, run.body.max.y - run.body.min.y)}};
    // Corner run: a run that ends in a corner turns onto the next wall (an L kitchen), without a
    // second sink and cooktop. Its own free floor may overlap the first run's.
    const float area = (space.inner.max.x - space.inner.min.x) * (space.inner.max.y - space.inner.min.y);
    if(area >= 6.0f && atCorner(space, run.side, run.t, runs[0].second) && ctx.optional(0.7f)){
        Space probe = space;
        probe.clearances.erase(std::remove_if(probe.clearances.begin(), probe.clearances.end(), [&](const LocalRect& c){
            return std::any_of(run.clear.begin(), run.clear.end(), [&](const LocalRect& r){ return glm::all(glm::equal(r.min, c.min)) && glm::all(glm::equal(r.max, c.max)); });
        }), probe.clearances.end());
        for(const float width : {2.4f, 2.0f, 1.6f, 1.2f}){
            Item leg;
            if(!ctx.item("kitchen_counter", {{"width", width}, {"fixtures", 0.0f}}, leg)) return false;
            WallRequest request{leg.size.x, leg.size.z, 1.0f};
            const Spot spot = bestWallSpot(probe, request, [&](int s, float, const LocalRect& body){
                if(s == run.side || s == (run.side + 2) % 4) return -1000.0f;
                const glm::vec2 gap = glm::max(glm::max(run.body.min - body.max, body.min - run.body.max), glm::vec2(0.0f));
                return glm::length(gap) < 0.05f ? jitter(ctx) : -1000.0f;
            });
            if(!spot.found() || spot.score < -100.0f) continue;
            ctx.putAt(space, leg, spot);
            runs.push_back({spot, leg.size.x});
            break;
        }
    }
    // Wall cabinets over every run that is not under a window, cut short where the run reaches
    // the window stretch of the next wall.
    for(const auto& [spot, width] : runs){
        if(space.window[spot.side]) continue;
        float t0 = spot.t, t1 = spot.t + width;
        const glm::vec2 o = sideStart(space.inner, spot.side);
        for(const LocalRect& zone : space.windowZones){
            if(!overlaps(wallRect(space, spot.side, t0, t1 - t0, 0.0f, 0.35f), zone)) continue;
            float a = glm::dot(zone.min - o, alongDir[spot.side]), b = glm::dot(zone.max - o, alongDir[spot.side]);
            if(a > b) std::swap(a, b);
            if(a <= t0 + eps) t0 = b + 0.02f; else t1 = a - 0.02f;
        }
        if(t1 - t0 < 0.4f) continue;
        Item cabinet;
        if(!ctx.item("wall_cabinet", {{"width", t1 - t0}}, cabinet)) return false;
        const LocalRect body = wallRect(space, spot.side, t0, cabinet.size.x, 0.0f, cabinet.size.z);
        if(std::any_of(space.windowZones.begin(), space.windowZones.end(), [&](const LocalRect& z){ return overlaps(body, z); })) continue;
        ctx.put(space, cabinet, body, inward[spot.side], 1.45f, false);
    }
    bool placed = false;
    if(!tableGroup(ctx, space, {{1.2f, 0.8f}, {0.8f, 0.8f}}, placed)) return false;
    return true;
}

bool furnishBathroom(Context& ctx, Space& space){
    const std::size_t before = ctx.output.size();
    const Space empty = space;
    Item toilet;
    if(!ctx.item("toilet", {}, toilet)) return false;
    const WallRequest toiletRequest{toilet.size.x, toilet.size.z, std::max(0.5f, toilet.clearance), 0.2f, 0.0f, 2};
    auto toiletScore = [&](int, float, const LocalRect& body){ return std::min(distanceToDoor(space, centre(body)), 3.0f) + jitter(ctx); };
    auto placeToilet = [&](bool& placed){
        const Spot seat = bestWallSpot(space, toiletRequest, toiletScore);
        placed = seat.found();
        if(placed) ctx.putAt(space, toilet, seat);
    };
    auto placeSink = [&](bool& placed){
        return wallPiece(ctx, space, "sink", {0.6f, 0.5f}, {}, 0.6f, [&](int, float, const LocalRect& body){
            return -distanceToDoor(space, centre(body)) * 0.3f + jitter(ctx);
        }, placed);
    };
    // Bath, else shower, else neither; toilet then basin, else basin then toilet. The toilet and
    // the basin must fit either way.
    for(int plan = 0; plan < 6; ++plan){
        space = empty;
        ctx.output.resize(before);
        bool placed = true;
        auto corner = [&](int s, float t, const LocalRect& body){
            return (atCorner(space, s, t, body.max.x - body.min.x) ? 1.0f : 0.0f) + distanceToDoor(space, centre(body)) * 0.2f + jitter(ctx);
        };
        if(plan / 2 == 0 && !wallPiece(ctx, space, "bathtub", {1.7f, 1.6f, 1.5f}, {}, 0.6f, corner, placed)) return false;
        if(plan / 2 == 1 && !wallPiece(ctx, space, "shower", {0.9f, 0.8f}, {}, 0.6f, corner, placed)) return false;
        if(!placed) continue;
        bool first = false, second = false;
        if(plan % 2 == 0){
            // Every free spot for the toilet in turn, until the basin fits beside it.
            const Space withoutToilet = space;
            const std::size_t count = ctx.output.size();
            for(const Spot& seat : wallSpots(space, toiletRequest, toiletScore)){
                space = withoutToilet;
                ctx.output.resize(count);
                ctx.putAt(space, toilet, seat);
                first = true;
                if(!placeSink(second)) return false;
                if(second) break;
            }
        }else{
            if(!placeSink(first)) return false;
            if(first) placeToilet(second);
        }
        if(first && second) return true;
    }
    ctx.error = ctx.where(space) + " has no room for a toilet and a basin clear of its door";
    return false;
}

bool furnishOffice(Context& ctx, Space& space){
    int desks = 0;
    for(int k = 0; k < 16; ++k){
        bool placed = false;
        if(!deskWithChair(ctx, space, 1.4f, true, placed)) return false;
        if(!placed && !deskWithChair(ctx, space, 1.2f, true, placed)) return false;
        if(!placed) break;
        ++desks;
    }
    if(desks == 0){ ctx.error = ctx.where(space) + " has no wall for a desk clear of its door"; return false; }
    bool placed = false;
    if(ctx.optional(0.6f) && !wallPiece(ctx, space, "shelf", {1.2f, 0.9f}, {}, 0.6f, [&](int s, float t, const LocalRect&){
        return (atCorner(space, s, t, 1.0f) ? 1.0f : 0.0f) + jitter(ctx);
    }, placed)) return false;
    return true;
}

bool furnishMeeting(Context& ctx, Space& space){
    const float longest = std::max(space.inner.max.x - space.inner.min.x, space.inner.max.y - space.inner.min.y);
    std::vector<glm::vec2> sizes;
    for(float length = std::min(longest - 2.0f, 4.0f); length >= 1.2f - eps; length -= 0.4f) sizes.push_back({length, 1.0f});
    sizes.push_back({1.2f, 0.8f});
    bool placed = false;
    if(!tableGroup(ctx, space, sizes, placed)) return false;
    if(!placed){ ctx.error = ctx.where(space) + " has no room for a table with chairs"; return false; }
    return true;
}

bool furnishHall(Context& ctx, Space& space){
    bool placed = false;
    return wallPiece(ctx, space, "shoe_cabinet", {0.8f}, {}, 0.5f, [&](int s, float t, const LocalRect& body){
        return (atCorner(space, s, t, body.max.x - body.min.x) ? 0.5f : 0.0f) + jitter(ctx);
    }, placed);
}

bool furnishStorage(Context& ctx, Space& space){
    for(int k = 0; k < 3; ++k){
        bool placed = false;
        if(!wallPiece(ctx, space, "shelf", {1.2f, 0.9f}, {}, 0.6f, [&](int s, float t, const LocalRect&){
            return (atCorner(space, s, t, 0.9f) ? 1.0f : 0.0f) + jitter(ctx);
        }, placed)) return false;
        if(!placed) break;
    }
    return true;
}

}  // namespace

const std::vector<std::string>& furnitureCategories(){
    static const std::vector<std::string> names = {
        "bed", "nightstand", "wardrobe", "desk", "chair", "sofa", "armchair", "coffee_table", "tv_stand", "shelf",
        "table", "kitchen_counter", "fridge", "toilet", "sink", "bathtub", "shower", "shoe_cabinet",
        "rug", "floor_lamp", "table_lamp", "wall_cabinet",
    };
    return names;
}

std::vector<std::string> assetsInCategory(const AssetLibrary& library, const std::string& category){
    std::vector<std::string> result;
    for(const std::string& id : library.ids()){
        const AssetInfo* info = library.info(id);
        if(info && info->category == category) result.push_back(id);
    }
    return result;
}

bool furnish(const Footprint& footprint, const FurnishNode& settings, const AssetLibrary& library,
             std::vector<Placement>& output, std::string& error){
    if(!footprint.hasPlan){ error = "Furnish needs a RoomSplit before it"; return false; }
    const InteriorPlan& plan = footprint.plan;
    std::vector<Placement> layout;
    Context ctx{footprint, settings, library, Random(settings.seed), {}, layout, {}};

    std::vector<glm::vec2> outline;
    for(const glm::vec2& p : footprint.outline) outline.push_back(toLocal(footprint, p));
    // Front door, in the local frame.
    const std::size_t edges = outline.size();
    const glm::vec2 ea = outline[plan.entranceEdge % edges], eb = outline[(plan.entranceEdge + 1) % edges];
    const glm::vec2 entrance = ea + glm::normalize(eb - ea) * plan.entranceCenter;

    for(std::size_t index = 0; index < plan.rooms.size(); ++index){
        const Room& room = plan.rooms[index];
        if(room.type == RoomType::Corridor || room.type == RoomType::Stairs) continue;
        Space space;
        space.index = index;
        space.type = room.type;
        space.floor = room.floor;
        const LocalRect& r = room.rect;
        float inset[4];
        for(int s = 0; s < 4; ++s){
            // An outside wall: the side lies on an outline edge.
            const glm::vec2 a = sideStart(r, s), b = sideStart(r, (s + 1) % 4);
            bool exterior = false;
            for(std::size_t i = 0; i < edges && !exterior; ++i){
                const glm::vec2 p = outline[i], q = outline[(i + 1) % edges];
                if(s % 2 == 0 && std::abs(p.y - q.y) < 1e-3f && std::abs(p.y - a.y) < 1e-3f)
                    exterior = std::min(std::max(p.x, q.x), std::max(a.x, b.x)) - std::max(std::min(p.x, q.x), std::min(a.x, b.x)) > 0.05f;
                if(s % 2 == 1 && std::abs(p.x - q.x) < 1e-3f && std::abs(p.x - a.x) < 1e-3f)
                    exterior = std::min(std::max(p.y, q.y), std::max(a.y, b.y)) - std::max(std::min(p.y, q.y), std::min(a.y, b.y)) > 0.05f;
            }
            inset[s] = exterior ? settings.wallThickness : settings.partitionThickness * 0.5f;
            space.window[s] = exterior && room.type != RoomType::Storage;
        }
        space.inner = {{r.min.x + inset[3], r.min.y + inset[0]}, {r.max.x - inset[1], r.max.y - inset[2]}};
        // Windows keep 0.35 m from the room's corners (Walls); a tall piece may stand beside that
        // stretch, not in front of it, also when it stands on the next wall.
        for(int s = 0; s < 4; ++s){
            if(!space.window[s]) continue;
            const glm::vec2 o = sideStart(r, s);
            space.windowZones.push_back(rectOf(o + alongDir[s] * 0.35f - inward[s] * 0.1f,
                                               o + alongDir[s] * (sideLength(r, s) - 0.35f) + inward[s] * (settings.wallThickness + 0.5f)));
        }
        if(space.inner.max.x - space.inner.min.x < 0.5f || space.inner.max.y - space.inner.min.y < 0.5f) continue;

        // Doors: keep the leaf's whole swing free in the room it opens into, and the step through
        // the door on the other side.
        for(const InteriorDoor& door : plan.doors){
            if(door.floor != room.floor || (door.roomA != index && door.roomB != index)) continue;
            const bool lineAlongX = std::abs(door.from.y - door.to.y) < 1e-3f;
            const int s = sideOfLine(r, lineAlongX, lineAlongX ? door.from.y : door.from.x);
            if(s < 0) continue;
            const float width = glm::length(door.to - door.from);
            const float depth = doorLeafRoom(plan, door) == index ? std::max(1.0f, width + 0.1f) : 0.6f;
            const glm::vec2 o = sideStart(space.inner, s);
            float a = glm::dot(door.from - o, alongDir[s]), b = glm::dot(door.to - o, alongDir[s]);
            if(a > b) std::swap(a, b);
            space.keepClear.push_back(wallRect(space, s, a - 0.1f, b - a + 0.2f, -0.5f, depth));
            space.doors.push_back(centre(space.keepClear.back()));
        }
        if(room.floor == 0){
            for(int s = 0; s < 4; ++s){
                const glm::vec2 a = sideStart(r, s), b = sideStart(r, (s + 1) % 4);
                const bool onSide = s % 2 == 0 ? std::abs(entrance.y - a.y) < 1e-2f && entrance.x > std::min(a.x, b.x) && entrance.x < std::max(a.x, b.x)
                                               : std::abs(entrance.x - a.x) < 1e-2f && entrance.y > std::min(a.y, b.y) && entrance.y < std::max(a.y, b.y);
                if(!onSide) continue;
                const float t = glm::dot(entrance - sideStart(space.inner, s), alongDir[s]);
                space.keepClear.push_back(wallRect(space, s, t - 0.75f, 1.5f, -0.5f, 1.3f));
                space.doors.push_back(centre(space.keepClear.back()));
            }
        }
        // Open space runs into open space without a wall; keep the way through free.
        if(isOpen(room.type))
            for(std::size_t other = 0; other < plan.rooms.size(); ++other){
                const Room& o = plan.rooms[other];
                if(other == index || o.floor != room.floor || !isOpen(o.type)) continue;
                for(int s = 0; s < 4; ++s){
                    const glm::vec2 a = sideStart(r, s), b = sideStart(r, (s + 1) % 4);
                    const LocalRect& q = o.rect;
                    const bool touches = s % 2 == 0
                        ? (std::abs(a.y - q.min.y) < 1e-3f || std::abs(a.y - q.max.y) < 1e-3f) &&
                          std::min(std::max(a.x, b.x), q.max.x) - std::max(std::min(a.x, b.x), q.min.x) > 0.3f
                        : (std::abs(a.x - q.min.x) < 1e-3f || std::abs(a.x - q.max.x) < 1e-3f) &&
                          std::min(std::max(a.y, b.y), q.max.y) - std::max(std::min(a.y, b.y), q.min.y) > 0.3f;
                    if(!touches) continue;
                    space.open[s] = true;
                    space.keepClear.push_back(wallRect(space, s, -0.5f, sideLength(space.inner, s) + 1.0f, -0.5f, 1.0f));
                }
            }

        bool ok = true;
        switch(room.type){
            case RoomType::Bedroom: ok = furnishBedroom(ctx, space); break;
            case RoomType::Living: ok = furnishLiving(ctx, space); break;
            case RoomType::Kitchen: ok = furnishKitchen(ctx, space); break;
            case RoomType::Bathroom: ok = furnishBathroom(ctx, space); break;
            case RoomType::Office: ok = furnishOffice(ctx, space); break;
            case RoomType::Meeting: ok = furnishMeeting(ctx, space); break;
            case RoomType::Hall: ok = furnishHall(ctx, space); break;
            case RoomType::Storage: ok = furnishStorage(ctx, space); break;
            default: break;
        }
        if(!ok){ error = ctx.error; return false; }
    }
    output = std::move(layout);
    error.clear();
    return true;
}

bool placeAssets(const std::vector<Placement>& placements, const AssetLibrary& library, MeshData& output,
                 std::string& error, std::size_t maxVertices){
    std::map<std::string, MeshData> built;
    MeshData mesh;
    const uint16_t furniture = semanticId("furniture");
    for(const Placement& placement : placements){
        std::string key = placement.asset;
        for(const auto& [name, value] : placement.parameters){
            char number[32];
            std::snprintf(number, sizeof(number), "%.5g", double(value));
            key += '|' + name + '=' + number;
        }
        auto found = built.find(key);
        if(found == built.end()){
            MeshData made;
            std::string why;
            if(!library.build(placement.asset, placement.parameters, made, why)){ error = "asset " + placement.asset + ": " + why; return false; }
            found = built.emplace(key, std::move(made)).first;
        }
        const MeshData& source = found->second;
        if(mesh.vertices.size() + source.vertices.size() > maxVertices){ error = "placed assets exceed the configured vertex limit"; return false; }
        const float yaw = placement.yawDegrees * pi / 180.0f, c = std::cos(yaw), s = std::sin(yaw);
        const glm::vec3 x{c, 0.0f, -s}, z{s, 0.0f, c};
        const uint32_t base = uint32_t(mesh.vertices.size());
        for(const MeshVertex& vertex : source.vertices){
            MeshVertex copy = vertex;
            copy.position = placement.position + x * vertex.position.x + glm::vec3(0.0f, vertex.position.y, 0.0f) + z * vertex.position.z;
            copy.normal = x * vertex.normal.x + glm::vec3(0.0f, vertex.normal.y, 0.0f) + z * vertex.normal.z;
            mesh.vertices.push_back(copy);
        }
        for(uint32_t index : source.indices) mesh.indices.push_back(base + index);
        const std::size_t triangles = source.indices.size() / 3;
        for(std::size_t t = 0; t < triangles; ++t){
            TriangleAttributes attributes = t < source.triangles.size() ? source.triangles[t] : TriangleAttributes{};
            attributes.createdBy = 0;          // the Place Assets node that uses them stamps them
            if(attributes.semantic == 0) attributes.semantic = furniture;
            mesh.triangles.push_back(attributes);
        }
    }
    output = std::move(mesh);
    error.clear();
    return true;
}

}  // namespace Engine::WeaverProcedura
