// 300 houses from the building and interior rules: random footprints (the last 100 drawn by
// hand as curves and snapped to right angles), floors, programs and roofs, sampled inside the
// footprint rules the way a data generator will. Every house either passes or fails with a
// rule's reason; a passing house must have closed walls, a window in every living room,
// bedroom, kitchen and office, rooms of sensible size, and a kitchen and a bathroom in every home.
// Furniture: no two pieces overlap, none leaves its room or stands in a door, no tall piece covers
// a window, and every bedroom has a bed, kitchen a counter, bathroom a toilet and basin, living
// room a sofa.
#include "TestHarness.h"

#include "../src/LoomProceduraAssets.h"

#include <Engine/WeaverProcedura.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <tuple>

namespace Proc = Engine::WeaverProcedura;

namespace{

bool closedWalls(const Proc::MeshData& mesh){
    std::map<std::tuple<float, float, float, float, float, float>, int> edges;
    for(std::size_t t = 0; t < mesh.triangles.size(); ++t){
        const std::string semantic = Proc::semanticName(mesh.triangles[t].semantic);
        if(semantic == "window" || semantic == "door" || semantic == "stairs") continue;   // panes, leaves, entrance steps
        for(int k = 0; k < 3; ++k){
            const glm::vec3 a = mesh.vertices[mesh.indices[t * 3 + k]].position, b = mesh.vertices[mesh.indices[t * 3 + (k + 1) % 3]].position;
            auto ka = std::make_tuple(a.x, a.y, a.z), kb = std::make_tuple(b.x, b.y, b.z);
            if(kb < ka) std::swap(ka, kb);
            ++edges[std::tuple_cat(ka, kb)];
        }
    }
    return std::all_of(edges.begin(), edges.end(), [](const auto& entry){ return entry.second == 2; });
}

// A hand-drawn right-angled plan: T, H, Z, cross, steps or a notched block, every corner
// nudged up to 0.3 m as a sketch would be, turned and moved at random.
std::vector<glm::vec3> sketch(std::mt19937& rng){
    auto uniform = [&](float a, float b){ return std::uniform_real_distribution<float>(a, b)(rng); };
    const int kind = std::uniform_int_distribution<int>(0, 5)(rng);
    const float w = uniform(12.0f, 22.0f), d = uniform(10.0f, 16.0f), a = uniform(5.0f, 7.0f);
    const float hw = w * 0.5f, hd = d * 0.5f, ha = a * 0.5f;
    std::vector<glm::vec2> p;
    switch(kind){
        case 0: p = {{-hw, -hd}, {hw, -hd}, {hw, -hd + a}, {ha, -hd + a}, {ha, hd}, {-ha, hd}, {-ha, -hd + a}, {-hw, -hd + a}}; break;       // T
        case 1: p = {{-hw, -hd}, {-hw + a, -hd}, {-hw + a, -ha}, {hw - a, -ha}, {hw - a, -hd}, {hw, -hd}, {hw, hd}, {hw - a, hd},
                     {hw - a, ha}, {-hw + a, ha}, {-hw + a, hd}, {-hw, hd}}; break;                                                          // H
        case 2: p = {{-hw, -hd}, {ha, -hd}, {ha, -hd + a}, {hw, -hd + a}, {hw, hd}, {-ha, hd}, {-ha, hd - a}, {-hw, hd - a}}; break;          // Z
        case 3: p = {{-ha, -hd}, {ha, -hd}, {ha, -ha}, {hw, -ha}, {hw, ha}, {ha, ha}, {ha, hd}, {-ha, hd}, {-ha, ha}, {-hw, ha},
                     {-hw, -ha}, {-ha, -ha}}; break;                                                                                         // cross
        case 4: p = {{-hw, -hd}, {hw, -hd}, {hw, 0.0f}, {hw * 0.3f, 0.0f}, {hw * 0.3f, hd}, {-hw, hd}}; break;                               // steps
        default: p = {{-hw, -hd}, {hw, -hd}, {hw, hd}, {hw * 0.2f, hd}, {hw * 0.2f, hd - 3.0f}, {-hw * 0.3f, hd - 3.0f},
                      {-hw * 0.3f, hd}, {-hw, hd}}; break;                                                                                   // notch
    }
    const float turn = uniform(0.0f, 6.2831853f), c = std::cos(turn), s = std::sin(turn);
    const glm::vec2 offset{uniform(-20.0f, 20.0f), uniform(-20.0f, 20.0f)};
    std::vector<glm::vec3> points;
    for(const glm::vec2& q : p){
        const glm::vec2 j = q + glm::vec2(uniform(-0.3f, 0.3f), uniform(-0.3f, 0.3f));
        points.push_back({j.x * c + j.y * s + offset.x, 0.0f, -j.x * s + j.y * c + offset.y});
    }
    return points;
}

}  // namespace

int main(){
    TestReport report("Procedura: 300 houses from rules");
    Loom::WeaverProceduraRecipe::RecipeAssetLibrary library;
    library.load(std::filesystem::path(__FILE__).parent_path().parent_path() / "procedura" / "assets");
    report.check("asset library loads", library.loadError.empty() && !library.ids().empty(), library.loadError);
    int collisions = 0, outside = 0, blockedDoors = 0, coveredWindows = 0, missingPieces = 0;
    std::size_t pieces = 0;
    std::map<std::string, std::string> firstOf;   // first example of each furniture problem
    auto note = [&](const char* kind, const std::string& what){ firstOf.emplace(kind, what); };
    const auto started = std::chrono::steady_clock::now();
    std::mt19937 rng(11);
    auto uniform = [&](float a, float b){ return std::uniform_real_distribution<float>(a, b)(rng); };
    auto pick = [&](int a, int b){ return std::uniform_int_distribution<int>(a, b)(rng); };

    int sketchPassed = 0;
    int passed = 0, holes = 0, darkRooms = 0, smallRooms = 0, incomplete = 0, unexplained = 0;
    std::map<std::string, int> reasons;
    std::string firstProblem;
    const int houses = 300, sketched = 100;   // the last 100 come from hand-drawn curves
    for(int n = 0; n < houses; ++n){
        const bool fromSketch = n >= houses - sketched;
        Proc::FootprintNode footprint;
        footprint.shape = Proc::FootprintShape(pick(0, 2));
        footprint.width = uniform(7.0f, 22.0f);
        footprint.depth = uniform(6.0f, 16.0f);
        const float armLimit = std::min({8.0f, footprint.shape == Proc::FootprintShape::UShape ? (footprint.width - 2.0f) * 0.5f : footprint.width - 2.5f,
                                        footprint.depth - 2.5f});
        if(footprint.shape != Proc::FootprintShape::Rectangle && armLimit < 4.0f) footprint.shape = Proc::FootprintShape::Rectangle;
        footprint.wingWidth = footprint.shape == Proc::FootprintShape::Rectangle ? 4.0f : uniform(4.0f, armLimit);
        const Proc::FloorStackNode stack{uint32_t(pick(1, 3)), uniform(2.7f, 3.2f), uniform(0.0f, 0.8f)};
        Proc::RoomSplitNode split;
        split.seed = uint64_t(n + 1);
        split.program = pick(0, 5) == 0 ? Proc::InteriorProgram::Office : Proc::InteriorProgram::Residential;
        Proc::WallsNode walls;
        walls.windowWidth = uniform(1.0f, 1.5f);
        walls.windowHeight = uniform(1.2f, 1.5f);
        walls.sillHeight = uniform(0.8f, 0.95f);
        walls.windowSpacing = walls.windowWidth + uniform(1.0f, 2.2f);
        Proc::RoofNode roof;
        roof.type = Proc::RoofType(pick(0, 2));
        roof.pitchDegrees = uniform(28.0f, 42.0f);
        roof.overhang = uniform(0.3f, 0.6f);
        Proc::SlabNode slab;
        slab.topCeiling = true;

        Proc::Graph graph;
        Proc::NodeId a;
        Proc::CurveNode drawn;
        if(fromSketch){
            drawn.curve.points = sketch(rng);
            drawn.curve.closed = true;
            const auto curveNode = Proc::addNode(graph, drawn);
            a = Proc::addNode(graph, Proc::FootprintFromCurveNode{true});
            graph.links.push_back({curveNode, 0, a, 0});
        }else a = Proc::addNode(graph, footprint);
        const auto b = Proc::addNode(graph, stack), c = Proc::addNode(graph, split);
        const auto w = Proc::addNode(graph, walls), s = Proc::addNode(graph, slab), r = Proc::addNode(graph, roof);
        const auto i = Proc::addNode(graph, Proc::InteriorNode{}), m = Proc::addNode(graph, Proc::MergeNode{});
        Proc::FurnishNode furnishing;
        furnishing.seed = uint64_t(n + 1);
        furnishing.wallThickness = walls.thickness;
        const auto f = Proc::addNode(graph, furnishing), pa = Proc::addNode(graph, Proc::PlaceAssetsNode{});
        graph.links.insert(graph.links.end(), {{a,0,b,0},{b,0,c,0},{c,0,w,0},{c,0,s,0},{c,0,r,0},{c,0,i,0},{w,0,m,0},{s,0,m,1},{r,0,m,2},{i,0,m,3},
                                               {c,0,f,0},{f,0,pa,0},{pa,0,m,4}});
        const Proc::EvaluationResult result = Proc::evaluate(graph, &library);
        if(!result.succeeded){
            ++reasons[(fromSketch ? "sketch: " : "") + result.error];
            if(result.error.empty()) ++unexplained;
            continue;
        }
        ++passed;
        if(fromSketch) ++sketchPassed;

        std::string error;
        Proc::Footprint base, planned;
        if(fromSketch) Proc::footprintFromCurve(drawn.curve, base, error, true);
        else Proc::makeFootprint(footprint, base, error);
        base.floors = stack.floors; base.floorHeight = stack.floorHeight; base.elevation = stack.elevation;
        Proc::planInterior(base, split, planned, error);
        Proc::MeshData wallMesh;
        Proc::makeWalls(planned, walls, wallMesh, error);
        if(!closedWalls(wallMesh)){ ++holes; if(firstProblem.empty()) firstProblem = "holes in house " + std::to_string(n); }

        // Window panes back to rooms: pane centre -> local frame -> the room it lies in.
        const Proc::InteriorPlan& plan = planned.plan;
        std::vector<int> windows(plan.rooms.size(), 0);
        const glm::vec2 axis = planned.frameAxis, across{-axis.y, axis.x};
        for(std::size_t t = 0; t < wallMesh.triangles.size(); ++t){
            if(Proc::semanticName(wallMesh.triangles[t].semantic) != "window") continue;
            glm::vec3 centre(0.0f);
            for(int k = 0; k < 3; ++k) centre += wallMesh.vertices[wallMesh.indices[t * 3 + k]].position / 3.0f;
            const glm::vec2 d = glm::vec2(centre.x, centre.z) - planned.frameCenter;
            const glm::vec2 local{glm::dot(d, axis), glm::dot(d, across)};
            const uint32_t floor = uint32_t((centre.y - planned.elevation) / planned.floorHeight);
            for(std::size_t k = 0; k < plan.rooms.size(); ++k){
                const Proc::Room& room = plan.rooms[k];
                if(room.floor == floor && local.x > room.rect.min.x - 0.3f && local.x < room.rect.max.x + 0.3f &&
                   local.y > room.rect.min.y - 0.3f && local.y < room.rect.max.y + 0.3f){ ++windows[k]; break; }
            }
        }
        bool kitchen = false, bathroom = false;
        for(std::size_t k = 0; k < plan.rooms.size(); ++k){
            const Proc::Room& room = plan.rooms[k];
            const float width = room.rect.max.x - room.rect.min.x, depth = room.rect.max.y - room.rect.min.y;
            const float area = width * depth, narrow = std::min(width, depth);
            const bool habitable = room.type == Proc::RoomType::Living || room.type == Proc::RoomType::Bedroom ||
                                   room.type == Proc::RoomType::Kitchen || room.type == Proc::RoomType::Office ||
                                   room.type == Proc::RoomType::Meeting;
            if(habitable && windows[k] == 0){ ++darkRooms; if(firstProblem.empty()) firstProblem = "dark room in house " + std::to_string(n); }
            const bool small = (room.type == Proc::RoomType::Bedroom && (area < 7.0f || narrow < 2.4f)) ||
                               (room.type == Proc::RoomType::Living && area < 12.0f) ||
                               (room.type == Proc::RoomType::Bathroom && (area < 2.5f || narrow < 1.4f)) ||
                               (room.type == Proc::RoomType::Kitchen && area < 5.0f);
            if(small){ ++smallRooms; if(firstProblem.empty()) firstProblem = "small room in house " + std::to_string(n); }
            kitchen |= room.type == Proc::RoomType::Kitchen;
            bathroom |= room.type == Proc::RoomType::Bathroom;
        }
        if(!bathroom || (split.program == Proc::InteriorProgram::Residential && !kitchen)) ++incomplete;

        // Furniture.
        const std::vector<Proc::Placement>& layout = result.placements;
        pieces += layout.size();
        const std::string house = " in house " + std::to_string(n);
        auto hit = [](const Proc::LocalRect& x, const Proc::LocalRect& y, float by){
            return x.min.x < y.max.x - by && x.max.x > y.min.x + by && x.min.y < y.max.y - by && x.max.y > y.min.y + by;
        };
        std::map<std::size_t, std::map<std::string, int>> byRoom;
        for(std::size_t p = 0; p < layout.size(); ++p){
            const Proc::Placement& piece = layout[p];
            const Proc::Room& room = plan.rooms.at(piece.room);
            byRoom[piece.room][library.info(piece.asset)->category]++;
            if(piece.area.min.x < room.rect.min.x - 0.001f || piece.area.max.x > room.rect.max.x + 0.001f ||
               piece.area.min.y < room.rect.min.y - 0.001f || piece.area.max.y > room.rect.max.y + 0.001f){ ++outside; note("outside", piece.asset + " leaves its room" + house); }
            for(std::size_t q = p + 1; q < layout.size(); ++q)
                if(layout[q].floor == piece.floor && hit(piece.area, layout[q].area, 0.005f)){
                    ++collisions; note("overlap", piece.asset + " overlaps " + layout[q].asset + house);
                }
            // Door openings: the span, 0.6 m to both sides of the wall line.
            for(const Proc::InteriorDoor& door : plan.doors){
                if(door.floor != piece.floor) continue;
                const Proc::LocalRect zone{glm::min(door.from, door.to) - glm::vec2(0.6f), glm::max(door.from, door.to) + glm::vec2(0.6f)};
                const bool alongX = std::abs(door.from.y - door.to.y) < 1e-3f;
                Proc::LocalRect span = zone;
                if(alongX){ span.min.x += 0.55f; span.max.x -= 0.55f; } else { span.min.y += 0.55f; span.max.y -= 0.55f; }
                if(hit(piece.area, span, 0.005f)){ ++blockedDoors; note("door", piece.asset + " stands in a door" + house); }
            }
            // A tall piece near a window pane of its floor.
            if(piece.height > 1.2f)
                for(std::size_t t = 0; t < wallMesh.triangles.size(); ++t){
                    if(Proc::semanticName(wallMesh.triangles[t].semantic) != "window") continue;
                    glm::vec3 centre(0.0f);
                    for(int k = 0; k < 3; ++k) centre += wallMesh.vertices[wallMesh.indices[t * 3 + k]].position / 3.0f;
                    const uint32_t floor = uint32_t((centre.y - planned.elevation) / planned.floorHeight);
                    if(floor != piece.floor) continue;
                    const glm::vec2 d = glm::vec2(centre.x, centre.z) - planned.frameCenter;
                    const glm::vec2 local{glm::dot(d, axis), glm::dot(d, across)};
                    const glm::vec2 nearest = glm::clamp(local, piece.area.min, piece.area.max);
                    if(glm::length(nearest - local) < 0.45f){ ++coveredWindows; note("window", piece.asset + " covers a window" + house); break; }
                }
        }
        for(std::size_t k = 0; k < plan.rooms.size(); ++k){
            const Proc::RoomType type = plan.rooms[k].type;
            auto& has = byRoom[k];
            const bool ok = type == Proc::RoomType::Bedroom ? has["bed"] > 0
                          : type == Proc::RoomType::Kitchen ? has["kitchen_counter"] > 0
                          : type == Proc::RoomType::Bathroom ? has["toilet"] > 0 && has["sink"] > 0
                          : type == Proc::RoomType::Living ? has["sofa"] > 0
                          : type == Proc::RoomType::Office ? has["desk"] > 0 : true;
            if(!ok){ ++missingPieces; note("missing", Proc::roomTypeNames()[std::size_t(type)] + " without its main piece" + house); }
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    std::string reasonText;
    for(const auto& [reason, count] : reasons) reasonText += "\n        x" + std::to_string(count) + " " + reason;
    report.check("at least 90% of the houses satisfy every rule", passed >= houses * 9 / 10,
                 fmt("%d/%d passed", passed, houses) + reasonText);
    report.check("every failed house says which rule failed", unexplained == 0, fmt("%d without a reason", unexplained));
    report.check("walls of every passing house are closed", holes == 0, fmt("%d with holes ", holes) + firstProblem);
    report.check("every living room, bedroom, kitchen and office has a window", darkRooms == 0, fmt("%d dark rooms ", darkRooms) + firstProblem);
    report.check("rooms have sensible sizes (bedroom 7 m2 / 2.4 m, living 12 m2, bath 2.5 m2, kitchen 5 m2)", smallRooms == 0,
                 fmt("%d too small ", smallRooms) + firstProblem);
    report.check("every home has a kitchen and a bathroom, every office a toilet", incomplete == 0, fmt("%d incomplete", incomplete));
    report.check("furniture: no two pieces overlap", collisions == 0, fmt("%d overlaps in %zu pieces ", collisions, pieces) + firstOf["overlap"]);
    report.check("furniture: every piece stays in its room", outside == 0, fmt("%d outside ", outside) + firstOf["outside"]);
    report.check("furniture: no piece stands in a door opening", blockedDoors == 0, fmt("%d in doors ", blockedDoors) + firstOf["door"]);
    report.check("furniture: no piece taller than 1.2 m stands at a window", coveredWindows == 0, fmt("%d at windows ", coveredWindows) + firstOf["window"]);
    report.check("furniture: bedroom bed, kitchen counter, bathroom toilet and basin, living room sofa, office desk", missingPieces == 0,
                 fmt("%d rooms without ", missingPieces) + firstOf["missing"]);
    report.check("300 furnished houses in under 5 s", seconds < 5.0, fmt("%.2f s, %zu pieces", seconds, pieces));
    report.check("at least 85% of the hand-drawn outlines become a planned house", sketchPassed >= sketched * 85 / 100,
                 fmt("%d/%d sketches passed", sketchPassed, sketched));
    return report.result();
}
