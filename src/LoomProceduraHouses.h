#pragma once

// AgentOfWeavers V0 data: one house template (the graph every sample shares), a sampler that
// only draws values on the action schema's grid, facts measured from the evaluated house, English
// descriptions made from those facts, and repair rules for the reasons a house can fail.
//
// Template (node order = action order):
//   0 footprint  1 floor_stack  2 room_split  3 walls  4 slab  5 roof  6 interior  7 furnish
//   8 place_assets  9 merge(3,4,5,6,8)  10-15 set_material (exterior walls, roof, frames, doors,
//   floors, interior walls)  16 uv_project box (everything)  17-20 uv_project surface (exterior
//   walls, interior walls, floors, roof), each with its own tile size; floors may be laid at 45°
// The model therefore learns parameters, materials and UV scale over a fixed structure first;
// choosing the structure itself is a later data set.

#include "LoomProceduraActions.h"
#include "LoomProceduraAssets.h"
#include <Engine/WeaverProcedura.h>

#include <algorithm>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace Loom::WeaverProceduraRecipe{

constexpr const char* houseGeneratorVersion = "houses-v0.3";

struct HouseSample{
    Engine::WeaverProcedura::FootprintNode footprint;
    Engine::WeaverProcedura::FloorStackNode stack;
    Engine::WeaverProcedura::RoomSplitNode split;
    Engine::WeaverProcedura::WallsNode walls;
    Engine::WeaverProcedura::SlabNode slab;
    Engine::WeaverProcedura::RoofNode roof;
    Engine::WeaverProcedura::InteriorNode interior;
    Engine::WeaverProcedura::FurnishNode furnish;
    std::string wallMaterial = "plaster", roofMaterial = "roof_tiles", frameMaterial = "plaster", doorMaterial = "wood_planks";
    std::string floorMaterial = "wood_planks", innerWallMaterial = "plaster";
    float wallTile = 1.0f, innerWallTile = 1.0f, floorTile = 1.0f, roofTile = 1.0f, floorRotation = 0.0f;
};

// Value snapped to the schema grid of type.param (clamped into its range).
inline float snapToSchema(const std::string& type, const std::string& param, float value){
    const NodeSpec* spec = findSchema(type);
    for(const ParamSpec& p : spec->params)
        if(p.name == param){
            const double clamped = std::clamp(double(value), p.min, p.max);
            return float(p.min + std::round((clamped - p.min) / p.step) * p.step);
        }
    return value;
}

inline HouseSample sampleHouse(std::mt19937_64& rng){
    namespace Proc = Engine::WeaverProcedura;
    auto uniform = [&](float a, float b){ return std::uniform_real_distribution<float>(a, b)(rng); };
    auto pick = [&](int a, int b){ return std::uniform_int_distribution<int>(a, b)(rng); };
    auto oneOf = [&](const std::vector<std::string>& options){ return options[std::size_t(pick(0, int(options.size()) - 1))]; };
    auto snap = snapToSchema;
    HouseSample h;
    h.footprint.shape = Proc::FootprintShape(pick(0, 2));
    h.footprint.width = snap("footprint", "width", uniform(7.0f, 22.0f));
    h.footprint.depth = snap("footprint", "depth", uniform(6.0f, 16.0f));
    const float armLimit = std::min({8.0f, h.footprint.shape == Proc::FootprintShape::UShape ? (h.footprint.width - 2.0f) * 0.5f
                                                                                            : h.footprint.width - 2.5f,
                                     h.footprint.depth - 2.5f});
    if(h.footprint.shape != Proc::FootprintShape::Rectangle && armLimit < 4.0f) h.footprint.shape = Proc::FootprintShape::Rectangle;
    h.footprint.wingWidth = h.footprint.shape == Proc::FootprintShape::Rectangle ? 4.0f
                          : std::min(snap("footprint", "wing_width", uniform(4.0f, armLimit)), std::floor(armLimit * 10.0f) / 10.0f);
    h.stack.floors = uint32_t(pick(1, 3));
    h.stack.floorHeight = snap("floor_stack", "floor_height", uniform(2.7f, 3.5f));
    h.stack.elevation = snap("floor_stack", "elevation", pick(0, 3) == 0 ? uniform(0.5f, 1.0f) : uniform(0.0f, 0.3f));
    h.split.program = pick(0, 5) == 0 ? Proc::InteriorProgram::Office : Proc::InteriorProgram::Residential;
    const bool office = h.split.program == Proc::InteriorProgram::Office;
    h.split.seed = uint64_t(pick(1, 999999));
    h.walls.thickness = snap("walls", "thickness", uniform(0.2f, 0.35f));
    h.walls.windowWidth = snap("walls", "window_width", pick(0, 4) == 0 ? uniform(1.8f, 2.4f) : uniform(0.8f, 1.5f));
    h.walls.windowHeight = snap("walls", "window_height", uniform(1.1f, 1.6f));
    h.walls.sillHeight = snap("walls", "sill_height", uniform(0.8f, 0.95f));
    h.walls.windowSpacing = snap("walls", "window_spacing", h.walls.windowWidth + uniform(1.0f, 2.2f));
    h.walls.doorWidth = snap("walls", "door_width", uniform(0.9f, 1.3f));
    h.slab.topCeiling = true;
    h.roof.type = Proc::RoofType(pick(0, 3));
    // A shed roof needs a rectangle; most L and U houses get another roof, a few keep it so the
    // data holds that failure and its repair.
    if(h.roof.type == Proc::RoofType::Shed && h.footprint.shape != Proc::FootprintShape::Rectangle && pick(0, 9) != 0)
        h.roof.type = pick(0, 1) ? Proc::RoofType::Gable : Proc::RoofType::Hip;
    h.roof.pitchDegrees = snap("roof", "pitch_degrees", h.roof.type == Proc::RoofType::Shed ? uniform(10.0f, 25.0f) : uniform(22.0f, 50.0f));
    h.roof.overhang = snap("roof", "overhang", uniform(0.2f, 0.7f));
    h.roof.parapetHeight = h.roof.type == Proc::RoofType::Flat && pick(0, 1) ? snap("roof", "parapet_height", uniform(0.4f, 1.0f)) : 0.0f;
    h.furnish.seed = uint64_t(pick(1, 999999));
    h.furnish.wallThickness = h.walls.thickness;
    h.interior.wallThickness = h.walls.thickness;
    h.furnish.fill = snap("furnish", "fill", uniform(0.3f, 1.0f));
    h.furnish.style = Proc::styleNames()[std::size_t(pick(0, int(Proc::styleNames().size()) - 1))];
    const bool rustic = h.furnish.style == "rustic", modern = h.furnish.style == "modern";
    h.wallMaterial = rustic ? oneOf({"stone", "wood_planks", "brick", "plaster"}) : modern ? oneOf({"plaster", "concrete", "brick"})
                            : oneOf({"plaster", "brick", "stone", "wood_planks", "concrete"});
    h.roofMaterial = h.roof.type == Proc::RoofType::Flat ? oneOf({"concrete", "roof_metal"})
                   : modern ? oneOf({"roof_metal", "roof_tiles"}) : oneOf({"roof_tiles", "roof_tiles", "roof_metal"});
    h.frameMaterial = rustic ? oneOf({"wood_beam", "stone"}) : modern ? oneOf({"metal", "plaster"}) : oneOf({"plaster", "wood_beam", "metal"});
    h.doorMaterial = modern ? oneOf({"lacquer", "metal", "wood_planks"}) : oneOf({"wood_planks", "wood_planks", "lacquer"});
    h.floorMaterial = office ? oneOf({"ceramic", "concrete", "wood_planks"}) : rustic ? oneOf({"wood_planks", "stone", "ceramic"})
                    : modern ? oneOf({"concrete", "ceramic", "wood_planks"}) : oneOf({"wood_planks", "ceramic", "stone", "concrete"});
    h.innerWallMaterial = rustic ? oneOf({"plaster", "brick", "wood_planks", "stone"}) : modern ? oneOf({"plaster", "plaster", "concrete", "brick"})
                        : oneOf({"plaster", "plaster", "brick", "wood_planks"});
    // Pattern scale: 1 is the material's natural size (bricks 25 cm, boards 14 cm, ...).
    auto scale = [&](){ const int kind = pick(0, 5); return kind == 0 ? uniform(0.6f, 0.8f) : kind == 1 ? uniform(1.35f, 1.8f) : uniform(0.9f, 1.15f); };
    h.wallTile = snap("uv_project", "tile_size", scale());
    h.innerWallTile = snap("uv_project", "tile_size", scale());
    h.floorTile = snap("uv_project", "tile_size", scale());
    h.roofTile = snap("uv_project", "tile_size", scale());
    h.floorRotation = pick(0, 3) == 0 ? 45.0f : 0.0f;
    return h;
}

inline Document houseDocument(const HouseSample& h, const std::string& name){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Graph graph;
    const auto a = Proc::addNode(graph, h.footprint), b = Proc::addNode(graph, h.stack), c = Proc::addNode(graph, h.split);
    const auto w = Proc::addNode(graph, h.walls), s = Proc::addNode(graph, h.slab), r = Proc::addNode(graph, h.roof);
    const auto i = Proc::addNode(graph, h.interior), f = Proc::addNode(graph, h.furnish), pa = Proc::addNode(graph, Proc::PlaceAssetsNode{});
    const auto m = Proc::addNode(graph, Proc::MergeNode{});
    auto paint = [&](const std::string& material, const char* semantic){
        Proc::SetMaterialNode node;
        node.material = material;
        node.filter.semantic = semantic;
        return Proc::addNode(graph, node);
    };
    const auto mw = paint(h.wallMaterial, "wall_exterior"), mr = paint(h.roofMaterial, "roof");
    const auto mf = paint(h.frameMaterial, "frame"), md = paint(h.doorMaterial, "door");
    const auto mfl = paint(h.floorMaterial, "floor"), miw = paint(h.innerWallMaterial, "wall_interior");
    auto project = [&](Proc::UVMode mode, float tile, float rotation, const char* semantic){
        Proc::UVProjectNode node;
        node.mode = mode;
        node.tileSize = tile;
        node.rotationDegrees = rotation;
        node.filter.semantic = semantic;
        return Proc::addNode(graph, node);
    };
    const auto uvAll = project(Proc::UVMode::Box, 1.0f, 0.0f, "");
    const auto uvWall = project(Proc::UVMode::Surface, h.wallTile, 0.0f, "wall_exterior");
    const auto uvInner = project(Proc::UVMode::Surface, h.innerWallTile, 0.0f, "wall_interior");
    const auto uvFloor = project(Proc::UVMode::Surface, h.floorTile, h.floorRotation, "floor");
    const auto uvRoof = project(Proc::UVMode::Surface, h.roofTile, 0.0f, "roof");
    graph.links = {{a,0,b,0},{b,0,c,0},{c,0,w,0},{c,0,s,0},{c,0,r,0},{c,0,i,0},{c,0,f,0},{f,0,pa,0},
                   {w,0,m,0},{s,0,m,1},{r,0,m,2},{i,0,m,3},{pa,0,m,4},{m,0,mw,0},{mw,0,mr,0},{mr,0,mf,0},{mf,0,md,0},
                   {md,0,mfl,0},{mfl,0,miw,0},{miw,0,uvAll,0},{uvAll,0,uvWall,0},{uvWall,0,uvInner,0},{uvInner,0,uvFloor,0},
                   {uvFloor,0,uvRoof,0}};
    return {name, graph};
}

// What the evaluated house is, measured rather than read off the parameters where it differs.
struct HouseFacts{
    std::map<std::string, int> rooms;        // by room type name, all floors
    float footprintArea = 0.0f, height = 0.0f;
    std::size_t pieces = 0, triangles = 0;
};

inline bool measureHouse(const HouseSample& h, const Engine::WeaverProcedura::EvaluationResult& result, HouseFacts& facts, std::string& error){
    namespace Proc = Engine::WeaverProcedura;
    Proc::Footprint base, planned;
    if(!Proc::makeFootprint(h.footprint, base, error)) return false;
    base.floors = h.stack.floors; base.floorHeight = h.stack.floorHeight; base.elevation = h.stack.elevation;
    if(!Proc::planInterior(base, h.split, planned, error)) return false;
    for(const Proc::Room& room : planned.plan.rooms) ++facts.rooms[Proc::roomTypeNames()[std::size_t(room.type)]];
    for(std::size_t k = 0; k < base.outline.size(); ++k){
        const glm::vec2 p = base.outline[k], q = base.outline[(k + 1) % base.outline.size()];
        facts.footprintArea += 0.5f * (p.x * q.y - q.x * p.y);
    }
    facts.footprintArea = std::abs(facts.footprintArea);
    float top = -1e9f, bottom = 1e9f;
    for(const Proc::MeshVertex& v : result.mesh.vertices){ top = std::max(top, v.position.y); bottom = std::min(bottom, v.position.y); }
    facts.height = top - std::min(bottom, 0.0f);
    facts.pieces = result.placements.size();
    facts.triangles = result.mesh.indices.size() / 3;
    return true;
}

// Rules on top of the engine's own: what a person asking for a house would count as wrong.
inline std::string houseRuleProblem(const HouseSample& h, const HouseFacts& facts){
    if(h.split.program == Engine::WeaverProcedura::InteriorProgram::Residential){
        if(facts.rooms.count("kitchen") == 0) return "rule: a home needs a kitchen";
        if(facts.rooms.count("bathroom") == 0) return "rule: a home needs a bathroom";
        if(facts.rooms.count("bedroom") == 0 && facts.rooms.count("living") == 0) return "rule: a home needs a bedroom or living room";
    }
    if(facts.pieces == 0) return "rule: the house has no furniture";
    return {};
}

// One change that addresses a failure reason, or false when resampling is the only answer.
inline bool repairHouse(HouseSample& h, const std::string& reason, std::string& repair){
    namespace Proc = Engine::WeaverProcedura;
    auto grow = [&](float by){
        h.footprint.width = snapToSchema("footprint", "width", h.footprint.width * by);
        h.footprint.depth = snapToSchema("footprint", "depth", h.footprint.depth * by);
    };
    if(reason.find("shed roof") != std::string::npos){
        h.roof.type = Proc::RoofType::Gable;
        h.roof.pitchDegrees = snapToSchema("roof", "pitch_degrees", std::max(h.roof.pitchDegrees, 25.0f));
        repair = "use a gable roof"; return true;
    }
    if(reason.find("for a bed") != std::string::npos){
        grow(1.15f); h.split.seed = h.split.seed % 999999 + 1; repair = "enlarge the footprint by 15% and replan"; return true;
    }
    if(reason.find("too small") != std::string::npos || reason.find("zone") != std::string::npos){
        grow(1.25f); repair = "enlarge the footprint by 25%"; return true;
    }
    if(reason.find("staircase") != std::string::npos){
        if(h.footprint.depth < 12.0f){ h.footprint.depth = snapToSchema("footprint", "depth", h.footprint.depth + 3.0f); repair = "make the footprint 3 m deeper"; }
        else{ h.stack.floors = 1; repair = "use a single floor"; }
        return true;
    }
    if(reason.find("door") != std::string::npos || reason.find("entrance") != std::string::npos){
        h.walls.doorEdge = 0; h.split.entranceEdge = 0; repair = "put the entrance on edge 0"; return true;
    }
    if(reason.find("kitchen") != std::string::npos || reason.find("bathroom") != std::string::npos ||
       reason.find("bedroom") != std::string::npos){
        grow(1.2f); h.split.seed = h.split.seed % 999999 + 1; repair = "enlarge the footprint by 20% and replan"; return true;
    }
    if(reason.find("wing") != std::string::npos || reason.find("arm") != std::string::npos){
        h.footprint.shape = Proc::FootprintShape::Rectangle; repair = "use a rectangular footprint"; return true;
    }
    return false;
}

// English prompts a person might write for this house: brief, medium and detailed, each naming
// only true facts; attributes a prompt leaves out are the model's own choice.
inline std::vector<std::string> describeHouse(const HouseSample& h, const HouseFacts& facts, std::mt19937_64& rng){
    namespace Proc = Engine::WeaverProcedura;
    auto pick = [&](int a, int b){ return std::uniform_int_distribution<int>(a, b)(rng); };
    auto oneOf = [&](const std::vector<std::string>& options){ return options[std::size_t(pick(0, int(options.size()) - 1))]; };
    const bool office = h.split.program == Proc::InteriorProgram::Office;
    const float floorArea = facts.footprintArea * float(h.stack.floors);
    const std::string size = floorArea < 110.0f ? oneOf({"small", "compact", "little"}) : floorArea < 260.0f ? oneOf({"medium-sized", "mid-sized"})
                                                                                         : oneOf({"large", "big", "spacious"});
    const std::string storeys[] = {"", "single-storey", "two-storey", "three-storey", "four-storey"};
    const std::string floorsWord = h.stack.floors == 1 ? (office ? oneOf({"single-storey", "one-storey", "one-floor"})
                                                                : oneOf({"single-storey", "one-storey", "one-floor", "bungalow-style"}))
                                                       : oneOf({storeys[h.stack.floors], std::to_string(h.stack.floors) + "-storey"});
    const std::string shape = h.footprint.shape == Proc::FootprintShape::LShape ? "L-shaped"
                            : h.footprint.shape == Proc::FootprintShape::UShape ? "U-shaped" : oneOf({"rectangular", "box-shaped"});
    const std::string style = h.furnish.style == "modern" ? oneOf({"modern", "contemporary"}) : h.furnish.style == "rustic"
                            ? oneOf({"rustic", "country-style", "farmhouse-style"}) : oneOf({"simple", "plain", "basic"});
    const std::string noun = office ? oneOf({"office building", "office", "office block"})
                           : oneOf({"house", "home", "family house", "residence"});
    std::vector<std::string> features;   // "with ..." phrases, each true
    // "a [steep|low-pitched] [tiled|metal|concrete] <type> roof [with a parapet]"
    const std::string roofTypes[] = {"flat", "gable", "hip", "shed"};
    const std::map<std::string, std::string> roofMaterial = {{"roof_tiles", "tiled"}, {"roof_metal", "metal"}, {"concrete", "concrete"}};
    std::string roof = "a";
    if(h.roof.type != Proc::RoofType::Flat && h.roof.pitchDegrees >= 42.0f) roof += " steep";
    else if(h.roof.type != Proc::RoofType::Flat && h.roof.pitchDegrees <= 25.0f) roof += " low-pitched";
    if(pick(0, 1) || h.roofTile >= 1.35f) roof += " " + std::string(h.roofTile >= 1.35f && h.roofMaterial == "roof_tiles" ? "large-tiled"
                                                                : roofMaterial.at(h.roofMaterial));
    roof += " " + roofTypes[int(h.roof.type)] + " roof";
    if(h.roof.type == Proc::RoofType::Flat && h.roof.parapetHeight > 0.0f) roof += " with a parapet";
    features.push_back(roof);
    const std::map<std::string, std::string> walls = {{"plaster", "plastered walls"}, {"brick", "brick walls"}, {"stone", "stone walls"},
                                                       {"wood_planks", "timber cladding"}, {"concrete", "concrete walls"}};
    std::string wallPhrase = walls.at(h.wallMaterial);
    if(h.wallTile >= 1.35f && h.wallMaterial != "plaster" && h.wallMaterial != "concrete"){
        const std::map<std::string, std::string> large = {{"brick", "walls of large bricks"}, {"stone", "walls of large stone blocks"},
                                                          {"wood_planks", "wide timber boards"}};
        wallPhrase = large.at(h.wallMaterial);
    }else if(h.wallTile <= 0.8f && h.wallMaterial != "plaster" && h.wallMaterial != "concrete"){
        const std::map<std::string, std::string> small = {{"brick", "walls of small bricks"}, {"stone", "walls of small stones"},
                                                          {"wood_planks", "narrow timber boards"}};
        wallPhrase = small.at(h.wallMaterial);
    }
    features.push_back(wallPhrase);
    const std::map<std::string, std::string> floors = {{"wood_planks", "wooden floors"}, {"ceramic", "tiled floors"},
                                                        {"stone", "stone floors"}, {"concrete", "polished concrete floors"}};
    std::string floorPhrase = floors.at(h.floorMaterial);
    const std::map<std::string, std::string> diagonal = {{"wood_planks", "diagonal floorboards"}, {"ceramic", "diagonally laid floor tiles"},
                                                          {"stone", "stone floors laid on the diagonal"}};
    if(h.floorRotation == 45.0f && diagonal.count(h.floorMaterial)) floorPhrase = diagonal.at(h.floorMaterial);
    if(h.floorTile >= 1.35f && h.floorRotation != 45.0f)
        floorPhrase = h.floorMaterial == "wood_planks" ? "wide floorboards" : h.floorMaterial == "ceramic" ? "large floor tiles" : floorPhrase;
    features.push_back(floorPhrase);
    const std::map<std::string, std::string> inner = {{"plaster", "plastered interior walls"}, {"brick", "exposed brick inside"},
                                                       {"wood_planks", "wood-panelled rooms"}, {"stone", "stone interior walls"},
                                                       {"concrete", "bare concrete interior walls"}};
    features.push_back(inner.at(h.innerWallMaterial));
    const int bedrooms = facts.rooms.count("bedroom") ? facts.rooms.at("bedroom") : 0;
    const int bathrooms = facts.rooms.count("bathroom") ? facts.rooms.at("bathroom") : 0;
    if(!office && bedrooms > 0) features.push_back(bedrooms == 1 ? "one bedroom" : std::to_string(bedrooms) + " bedrooms");
    if(bathrooms > 1) features.push_back(std::to_string(bathrooms) + " bathrooms");
    if(office && facts.rooms.count("office")) features.push_back(std::to_string(facts.rooms.at("office")) + " offices");
    if(h.walls.windowWidth >= 1.8f) features.push_back(oneOf({"large windows", "wide windows", "big picture windows"}));
    else if(h.walls.windowWidth <= 0.9f) features.push_back("small windows");
    if(h.stack.elevation >= 0.6f) features.push_back("a raised ground floor");
    if(h.stack.floorHeight >= 3.3f) features.push_back("high ceilings");
    const std::map<std::string, std::string> frames = {{"wood_beam", "wooden window frames"}, {"stone", "stone window surrounds"},
                                                        {"metal", "metal window frames"}, {"plaster", "plain window surrounds"}};
    features.push_back(frames.at(h.frameMaterial));
    const std::map<std::string, std::string> doors = {{"wood_planks", "a wooden front door"}, {"lacquer", "a painted front door"},
                                                       {"metal", "a metal front door"}};
    features.push_back(doors.at(h.doorMaterial));
    const std::string furnished = h.furnish.fill >= 0.8f ? "fully furnished" : h.furnish.fill <= 0.45f ? "sparsely furnished" : "furnished";
    std::ostringstream dims;
    dims.precision(3);
    dims << "about " << h.footprint.width << " by " << h.footprint.depth << " meters";

    auto sentence = [&](std::size_t count, bool numbers){
        std::vector<std::string> chosen = features;
        std::shuffle(chosen.begin(), chosen.end(), rng);
        chosen.resize(std::min(count, chosen.size()));
        if(numbers) chosen.push_back(dims.str());
        std::vector<std::string> adjectives;
        if(pick(0, 1) || count >= 4) adjectives.push_back(size);
        if(pick(0, 2) || count >= 3) adjectives.push_back(style);
        if(pick(0, 1) || count >= 2) adjectives.push_back(floorsWord);
        if(h.footprint.shape != Proc::FootprintShape::Rectangle || count >= 5) adjectives.push_back(shape);
        std::string text = oneOf({"A", "Build a", "Make a", "I want a", "Create a", "Generate a"});
        for(const std::string& adjective : adjectives) text += " " + adjective;
        text += " " + noun;
        for(std::size_t k = 0; k < chosen.size(); ++k){
            text += k == 0 ? " with " : (k + 1 == chosen.size() ? " and " : ", ");
            text += chosen[k];
        }
        if(count >= 3 || pick(0, 3) == 0) text += ", " + furnished;
        text += ".";
        for(const char* vowel : {" a e", " a i", " a o", " a u", " a a"}){
            std::size_t at;
            while((at = text.find(vowel)) != std::string::npos) text.replace(at, 3, " an ");
        }
        if(text.rfind("A a", 0) == 0 || text.rfind("A e", 0) == 0 || text.rfind("A i", 0) == 0 || text.rfind("A o", 0) == 0 || text.rfind("A u", 0) == 0)
            text.replace(0, 1, "An");
        return text;
    };
    return {sentence(std::size_t(pick(0, 1)), false), sentence(std::size_t(pick(2, 4)), false), sentence(features.size(), pick(0, 1) == 1)};
}

} // namespace Loom::WeaverProceduraRecipe
