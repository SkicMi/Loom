// 300 houses from the building and interior rules: random footprints, floors, programs and
// roofs, sampled inside the footprint rules the way a data generator will. Every house either
// passes or fails with a rule's reason; a passing house must have closed walls, a window in
// every living room, bedroom, kitchen and office, rooms of sensible size, and a kitchen and a
// bathroom in every home.
#include "TestHarness.h"

#include <Engine/WeaverProcedura.h>

#include <algorithm>
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

}  // namespace

int main(){
    TestReport report("Procedura: 300 houses from rules");
    std::mt19937 rng(11);
    auto uniform = [&](float a, float b){ return std::uniform_real_distribution<float>(a, b)(rng); };
    auto pick = [&](int a, int b){ return std::uniform_int_distribution<int>(a, b)(rng); };

    int passed = 0, holes = 0, darkRooms = 0, smallRooms = 0, incomplete = 0, unexplained = 0;
    std::map<std::string, int> reasons;
    std::string firstProblem;
    const int houses = 300;
    for(int n = 0; n < houses; ++n){
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
        const auto a = Proc::addNode(graph, footprint), b = Proc::addNode(graph, stack), c = Proc::addNode(graph, split);
        const auto w = Proc::addNode(graph, walls), s = Proc::addNode(graph, slab), r = Proc::addNode(graph, roof);
        const auto i = Proc::addNode(graph, Proc::InteriorNode{}), m = Proc::addNode(graph, Proc::MergeNode{});
        graph.links = {{a,0,b,0},{b,0,c,0},{c,0,w,0},{c,0,s,0},{c,0,r,0},{c,0,i,0},{w,0,m,0},{s,0,m,1},{r,0,m,2},{i,0,m,3}};
        const Proc::EvaluationResult result = Proc::evaluate(graph);
        if(!result.succeeded){
            ++reasons[result.error];
            if(result.error.empty()) ++unexplained;
            continue;
        }
        ++passed;

        std::string error;
        Proc::Footprint base, planned;
        Proc::makeFootprint(footprint, base, error);
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
    }

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
    return report.result();
}
