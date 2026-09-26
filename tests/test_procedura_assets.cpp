// Asset library: every furniture asset under procedura/assets loads, builds at its smallest,
// default and largest parameters, and fills exactly the box its bounds promise (base on the
// floor, centred, front toward +Z), so Furnish can place it by the box alone. Plus the new
// recipe nodes (asset, furnish, place_assets) through JSON and evaluate.
#include "TestHarness.h"

#include "../src/LoomProceduraAssets.h"

#include <Engine/WeaverProcedura.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace Proc = Engine::WeaverProcedura;
namespace Recipe = Loom::WeaverProceduraRecipe;

int main(){
    TestReport report("Procedura: asset library");
    Recipe::RecipeAssetLibrary library;
    const std::filesystem::path folder = std::filesystem::path(__FILE__).parent_path().parent_path() / "procedura" / "assets";
    const bool loaded = library.load(folder);
    report.check("every asset file loads", loaded && !library.ids().empty(),
                 fmt("%zu assets ", library.ids().size()) + library.loadError);

    std::string missing;
    for(const std::string& category : Proc::furnitureCategories())
        if(Proc::assetsInCategory(library, category).empty()) missing += " " + category;
    report.check("the library has an asset for every furniture category", missing.empty(), "missing:" + missing);

    std::string boxProblem, tagProblem;
    int built = 0;
    for(const std::string& id : library.ids()){
        const Proc::AssetInfo* info = library.info(id);
        for(int variant = 0; variant < 3; ++variant){
            Proc::AssetParameters parameters;
            for(const Proc::AssetParameter& p : info->parameters)
                parameters.push_back({p.name, variant == 0 ? p.minValue : variant == 1 ? p.defaultValue : p.maxValue});
            Proc::MeshData mesh;
            glm::vec3 size;
            std::string error;
            if(!library.build(id, parameters, mesh, error) || !library.bounds(id, parameters, size, error)){
                if(boxProblem.empty()) boxProblem = id + ": " + error;
                continue;
            }
            ++built;
            glm::vec3 low(1e9f), high(-1e9f);
            for(const Proc::MeshVertex& v : mesh.vertices){ low = glm::min(low, v.position); high = glm::max(high, v.position); }
            const glm::vec3 wantLow{-size.x * 0.5f, 0.0f, -size.z * 0.5f}, wantHigh{size.x * 0.5f, size.y, size.z * 0.5f};
            const float off = std::max(glm::length(low - wantLow), glm::length(high - wantHigh));
            if(off > 0.005f && boxProblem.empty())
                boxProblem = id + fmt(" variant %d: mesh box (%.3f %.3f %.3f)-(%.3f %.3f %.3f) but bounds (%.3f %.3f %.3f)", variant,
                                     low.x, low.y, low.z, high.x, high.y, high.z, size.x, size.y, size.z);
            for(const Proc::TriangleAttributes& t : mesh.triangles)
                if((t.semantic == 0 || t.material == 0) && tagProblem.empty()) tagProblem = id + " has a triangle without semantic or material";
        }
    }
    report.check("every asset builds at min, default and max parameters and fills its bounds box", boxProblem.empty() && built > 0,
                 fmt("%d built ", built) + boxProblem);
    report.check("every asset triangle has a semantic and a material", tagProblem.empty(), tagProblem);

    // Clamping and unknown names.
    glm::vec3 wide, narrow;
    std::string error;
    library.bounds("bed_basic", {{"width", 9.0f}}, wide, error);
    library.bounds("bed_basic", {{"width", 0.1f}}, narrow, error);
    report.check("parameters are clamped to their range", std::abs(wide.x - 2.0f) < 1e-4f && std::abs(narrow.x - 0.8f) < 1e-4f,
                 fmt("%.2f %.2f", wide.x, narrow.x));
    report.check("an unknown parameter name is an error", !library.bounds("bed_basic", {{"wdith", 1.0f}}, wide, error) &&
                 error.find("wdith") != std::string::npos, error);
    bool rejected = false;
    try{ Recipe::RecipeAssetLibrary broken; broken.add(R"({"format":"loom.weaverprocedura.asset","id":"x","category":"bed",
        "parameters":[],"bounds":[{"param":"width"},1,1],"placement":"wall","clearance_front":0,"recipe":{}})"); }
    catch(const std::exception& problem){ rejected = std::string(problem.what()).find("width") != std::string::npos; }
    report.check("an asset whose bounds name a missing parameter does not load", rejected, "");

    // Asset node in a recipe: JSON round trip, evaluate with and without a library.
    Recipe::Document document;
    document.name = "Chair";
    Proc::AssetNode chairNode{"table_basic", {{"width", 2.0f}}};
    Proc::addNode(document.graph, chairNode);
    const std::string encoded = Recipe::serialize(document);
    const Recipe::Document decoded = Recipe::parse(encoded);
    const auto* back = std::get_if<Proc::AssetNode>(&decoded.graph.nodes.at(0).payload);
    report.check("asset node survives JSON", back && back->asset == "table_basic" && back->parameters.size() == 1 &&
                 back->parameters[0].second == 2.0f, encoded);
    const Proc::EvaluationResult noLibrary = Proc::evaluate(decoded.graph);
    report.check("asset node without a library fails with a reason", !noLibrary.succeeded &&
                 noLibrary.error.find("library") != std::string::npos, noLibrary.error);
    const Proc::EvaluationResult table = Proc::evaluate(decoded.graph, &library);
    glm::vec3 low(1e9f), high(-1e9f);
    for(const Proc::MeshVertex& v : table.mesh.vertices){ low = glm::min(low, v.position); high = glm::max(high, v.position); }
    report.check("asset node builds the asset with its parameters", table.succeeded && std::abs(high.x - low.x - 2.0f) < 0.005f,
                 table.error + fmt(" width %.3f", high.x - low.x));
    bool stamped = table.succeeded;
    for(const Proc::TriangleAttributes& t : table.mesh.triangles) stamped &= t.createdBy == decoded.graph.nodes[0].id;
    report.check("asset triangles are stamped with the node that placed them", stamped, "");

    // Furnish and Place Assets in a house recipe, through JSON.
    Recipe::Document house;
    house.name = "Furnished";
    Proc::FootprintNode footprint;
    footprint.width = 14.0f; footprint.depth = 10.0f;
    const auto f = Proc::addNode(house.graph, footprint);
    const auto s = Proc::addNode(house.graph, Proc::FloorStackNode{2, 3.0f, 0.3f});
    const auto r = Proc::addNode(house.graph, Proc::RoomSplitNode{});
    const auto u = Proc::addNode(house.graph, Proc::FurnishNode{7, 0.25f, 0.12f, 0.8f});
    const auto p = Proc::addNode(house.graph, Proc::PlaceAssetsNode{});
    house.graph.links = {{f, 0, s, 0}, {s, 0, r, 0}, {r, 0, u, 0}, {u, 0, p, 0}};
    const Recipe::Document houseBack = Recipe::parse(Recipe::serialize(house));
    const auto* furnishBack = std::get_if<Proc::FurnishNode>(&houseBack.graph.nodes.at(3).payload);
    report.check("furnish node survives JSON", furnishBack && furnishBack->seed == 7 && std::abs(furnishBack->fill - 0.8f) < 1e-6f &&
                 std::holds_alternative<Proc::PlaceAssetsNode>(houseBack.graph.nodes.at(4).payload), "");
    const Proc::EvaluationResult furnished = Proc::evaluate(houseBack.graph, &library);
    report.check("a planned house is furnished and the layout is in the result", furnished.succeeded && furnished.placements.size() >= 10 &&
                 !furnished.mesh.empty(), furnished.error + fmt(" %zu pieces", furnished.placements.size()));
    const Proc::EvaluationResult again = Proc::evaluate(houseBack.graph, &library);
    bool same = again.placements.size() == furnished.placements.size();
    for(std::size_t k = 0; same && k < again.placements.size(); ++k)
        same = again.placements[k].asset == furnished.placements[k].asset && again.placements[k].position == furnished.placements[k].position;
    report.check("the same seed gives the same layout", same, "");
    Proc::Graph unplanned = houseBack.graph;
    unplanned.links = {{f, 0, s, 0}, {s, 0, u, 0}, {u, 0, p, 0}};
    unplanned.nodes.erase(unplanned.nodes.begin() + 2);
    const Proc::EvaluationResult noPlan = Proc::evaluate(unplanned, &library);
    report.check("furnish without a room plan says so", !noPlan.succeeded && noPlan.error.find("RoomSplit") != std::string::npos, noPlan.error);
    return report.result();
}
