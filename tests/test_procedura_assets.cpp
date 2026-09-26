// Asset library: every furniture asset under procedura/assets loads, builds at its smallest,
// default and largest parameters, and fills exactly the box its bounds promise (base on the
// floor, centred, front toward +Z), so Furnish can place it by the box alone. Plus the new
// recipe nodes (asset, furnish, place_assets) through JSON and evaluate.
#include "TestHarness.h"

#include "../src/LoomHandPose.h"
#include "../src/LoomProceduraAssets.h"
#include "../src/LoomProceduraGlb.h"

#include <Spool/Gltf.h>

#include <Engine/WeaverProcedura.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
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
            if(off > 0.005f)
                boxProblem += (boxProblem.empty() ? "" : "; ") + id + fmt(" variant %d: mesh box (%.3f %.3f %.3f)-(%.3f %.3f %.3f) but bounds (%.3f %.3f %.3f)", variant,
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
    // Styles: every style has the main pieces; a furnished house keeps to one style.
    std::string missingStyled;
    for(const std::string& style : Proc::styleNames())
        for(const char* category : {"bed", "wardrobe", "sofa", "table", "chair", "kitchen_counter"}){
            bool found = false;
            for(const std::string& id : Proc::assetsInCategory(library, category)) found |= library.info(id)->style == style;
            if(!found) missingStyled += " " + style + "/" + category;
        }
    report.check("every style has a bed, wardrobe, sofa, table, chair and kitchen counter", missingStyled.empty(), "missing:" + missingStyled);
    std::string mixed;
    for(const std::string& style : Proc::styleNames()){
        Proc::Graph styled = houseBack.graph;
        std::get<Proc::FurnishNode>(styled.nodes.at(3).payload).style = style;
        const Proc::EvaluationResult result = Proc::evaluate(styled, &library);
        int own = 0;
        for(const Proc::Placement& piece : result.placements){
            const std::string pieceStyle = library.info(piece.asset)->style;
            own += pieceStyle == style;
            if(pieceStyle != style && pieceStyle != "basic") mixed += " " + style + " house has " + piece.asset;
        }
        if(!result.succeeded || own < 5) mixed += " " + style + fmt(" house has %d own pieces ", own) + result.error;
    }
    report.check("furnish with a style uses that style's pieces, basic only where the style has none", mixed.empty(), mixed);
    Proc::Graph unknownStyle = houseBack.graph;
    std::get<Proc::FurnishNode>(unknownStyle.nodes.at(3).payload).style = "baroque";
    const Proc::ValidationResult badStyle = Proc::validate(unknownStyle);
    report.check("an unknown style is refused with its name", !badStyle.valid && badStyle.error.find("baroque") != std::string::npos, badStyle.error);
    Recipe::Document styledDocument = houseBack;
    std::get<Proc::FurnishNode>(styledDocument.graph.nodes.at(3).payload).style = "rustic";
    const Recipe::Document styledBack = Recipe::parse(Recipe::serialize(styledDocument));
    report.check("the style survives JSON", std::get<Proc::FurnishNode>(styledBack.graph.nodes.at(3).payload).style == "rustic", "");
    bool styleRejected = false;
    try{ Recipe::RecipeAssetLibrary broken; broken.add(R"({"format":"loom.weaverprocedura.asset","id":"x","category":"bed","style":"baroque",
        "parameters":[],"bounds":[1,1,1],"placement":"wall","clearance_front":0,"recipe":{}})"); }
    catch(const std::exception& problem){ styleRejected = std::string(problem.what()).find("baroque") != std::string::npos; }
    report.check("an asset with an unknown style does not load", styleRejected, "");
    // Tools, weapons and props: every category has an asset, held ones have grips on their handle.
    std::string emptyCategories;
    for(const std::string& category : Proc::assetCategories())
        if(Proc::assetsInCategory(library, category).empty()) emptyCategories += " " + category;
    report.check("the library has an asset for every category (furniture, tools, weapons, props)", emptyCategories.empty(),
                 "missing:" + emptyCategories);
    std::string gripProblem;
    int gripsChecked = 0;
    for(const std::string& id : library.ids()){
        const Proc::AssetInfo* info = library.info(id);
        const std::string kind = Proc::assetKind(info->category);
        for(int variant = 0; variant < 3; ++variant){
            Proc::AssetParameters parameters;
            for(const Proc::AssetParameter& p : info->parameters)
                parameters.push_back({p.name, variant == 0 ? p.minValue : variant == 1 ? p.defaultValue : p.maxValue});
            std::vector<Proc::AssetGrip> grips;
            Proc::MeshData mesh;
            glm::vec3 size;
            if(!library.grips(id, parameters, grips, error) || !library.build(id, parameters, mesh, error) ||
               !library.bounds(id, parameters, size, error)){ gripProblem += " " + id + ": " + error; continue; }
            if((kind == "tool" || kind == "weapon") && grips.empty()) gripProblem += " " + id + " has no grip";
            for(const Proc::AssetGrip& grip : grips){
                ++gripsChecked;
                const bool inside = std::abs(grip.point.x) <= size.x * 0.5f + 1e-3f && grip.point.y >= -1e-3f &&
                                    grip.point.y <= size.y + 1e-3f && std::abs(grip.point.z) <= size.z * 0.5f + 1e-3f;
                // The handle is really there: cut the mesh with the plane through the grip point across
                // the axis; the nearest cut must lie about one handle radius from the point.
                float nearest = 1e9f;
                for(std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3){
                    glm::vec3 q[3];
                    float h[3];
                    for(int k = 0; k < 3; ++k){ q[k] = mesh.vertices[mesh.indices[t + k]].position; h[k] = glm::dot(q[k] - grip.point, grip.axis); }
                    std::vector<glm::vec3> cut;
                    for(int k = 0; k < 3; ++k){
                        const int m = (k + 1) % 3;
                        if((h[k] < 0.0f) != (h[m] < 0.0f)) cut.push_back(q[k] + (q[m] - q[k]) * (h[k] / (h[k] - h[m])));
                    }
                    if(cut.size() != 2) continue;
                    const glm::vec3 d = cut[1] - cut[0];
                    const float f = glm::dot(d, d) > 1e-12f ? std::clamp(glm::dot(grip.point - cut[0], d) / glm::dot(d, d), 0.0f, 1.0f) : 0.0f;
                    nearest = std::min(nearest, glm::length(cut[0] + d * f - grip.point));
                }
                if(!inside || nearest > grip.thickness + 0.01f || std::abs(glm::dot(grip.axis, grip.palm)) > 1e-3f)
                    gripProblem += " " + id + "/" + grip.name + fmt(" (inside %d, handle surface %.3f m away)", int(inside), nearest);
            }
        }
    }
    report.check("every tool and weapon has a grip, and each grip sits on its handle inside the box", gripProblem.empty() && gripsChecked > 0,
                 fmt("%d grips ", gripsChecked) + gripProblem);
    std::string presetMismatch;
    std::vector<std::string> handPresets;
    for(const Loom::GripPreset& preset : Loom::gripPresets()) handPresets.push_back(preset.name);
    if(handPresets != Recipe::assetGripPresetNames()) presetMismatch = "asset grip presets differ from LoomHandPose gripPresets()";
    report.check("asset grip presets are the hand poses Loom knows", presetMismatch.empty(), presetMismatch);
    bool badCategory = false, toolWithoutGrip = false;
    try{ Recipe::RecipeAssetLibrary broken; broken.add(R"({"format":"loom.weaverprocedura.asset","id":"x","category":"spaceship",
        "parameters":[],"bounds":[1,1,1],"placement":"center","clearance_front":0,"recipe":{}})"); }
    catch(const std::exception& problem){ badCategory = std::string(problem.what()).find("spaceship") != std::string::npos; }
    try{
        Recipe::RecipeAssetLibrary broken;
        std::string source = R"({"format":"loom.weaverprocedura.asset","id":"x","category":"hammer","parameters":[],"bounds":[1,1,1],
            "placement":"center","clearance_front":0,"recipe":)";
        source += Recipe::serialize(document) + "}";
        broken.add(source);
    }catch(const std::exception& problem){ toolWithoutGrip = std::string(problem.what()).find("grip") != std::string::npos; }
    report.check("an unknown category and a tool without a grip do not load", badCategory && toolWithoutGrip, "");

    // A sword to .glb with its grip, and back through Loom's glTF reader.
    {
        Proc::MeshData sword;
        std::vector<Proc::AssetGrip> grips;
        library.build("sword_basic", {}, sword, error);
        library.grips("sword_basic", {}, grips, error);
        error.clear();
        const std::string path = (std::filesystem::temp_directory_path() / "loom_test_sword.glb").string();
        const bool written = Recipe::writeGlb(sword, path, [](uint16_t){ return glm::vec3(0.5f); }, "weapon", grips, error);
        Spool::GltfScene scene;
        std::string loadError;
        const bool loaded = written && Spool::loadGltf(path, scene, loadError);
        std::size_t triangles = 0;
        std::set<uint16_t> materials;
        for(const Proc::TriangleAttributes& t : sword.triangles) materials.insert(t.material);
        if(loaded) for(const Spool::GltfPrimitive& primitive : scene.meshes.at(0).primitives) triangles += primitive.indices.size() / 3;
        std::ifstream in(path, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        report.check("a sword exports to .glb with one primitive per material, all triangles and its grip in extras",
                     loaded && scene.meshes.size() == 1 && scene.meshes[0].primitives.size() == materials.size() &&
                     triangles == sword.indices.size() / 3 && bytes.find("\"loom_tool\"") != std::string::npos &&
                     bytes.find("\"weapon\"") != std::string::npos,
                     error + loadError + fmt(" %zu/%zu triangles", triangles, sword.indices.size() / 3));
        std::filesystem::remove(path);
    }

    // Panel export: a recipe ending in one weapon Asset carries that weapon's grips; furniture carries none.
    {
        Proc::Graph weapon;
        Proc::AssetNode swordNode; swordNode.asset = "sword_basic";
        Proc::addNode(weapon, swordNode);
        const Proc::EvaluationResult built = Proc::evaluate(weapon, &library);
        const std::string recipePath = (std::filesystem::temp_directory_path() / "loom_export" / "Sword.loomrecipe.json").string();
        const std::string path = Recipe::glbPathFor(recipePath);
        std::string exportError;
        const bool exported = built.succeeded &&
            Recipe::exportRecipeGlb(weapon, built.mesh, &library, path, [](uint16_t){ return glm::vec3(0.5f); }, exportError);
        std::ifstream in(path, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const std::string chairId = Proc::assetsInCategory(library, "chair").front();
        Proc::Graph chair;
        Proc::AssetNode chairNode; chairNode.asset = chairId;
        Proc::addNode(chair, chairNode);
        const Proc::EvaluationResult chairBuilt = Proc::evaluate(chair, &library);
        const std::string chairPath = Recipe::glbPathFor(recipePath + ".chair.json");
        const bool chairExported = chairBuilt.succeeded &&
            Recipe::exportRecipeGlb(chair, chairBuilt.mesh, &library, chairPath, [](uint16_t){ return glm::vec3(0.5f); }, exportError);
        std::ifstream chairIn(chairPath, std::ios::binary);
        const std::string chairBytes{std::istreambuf_iterator<char>(chairIn), std::istreambuf_iterator<char>()};
        std::vector<Proc::AssetGrip> expected, readBack, none;
        library.grips("sword_basic", {}, expected, exportError);
        std::string readKind, chairKind;
        const bool swordRead = Recipe::readGlbTool(path, readKind, readBack);
        const bool chairRead = Recipe::readGlbTool(chairPath, chairKind, none);
        bool sameGrips = swordRead && readBack.size() == expected.size() && !expected.empty();
        for(std::size_t g = 0; sameGrips && g < expected.size(); ++g)
            sameGrips = glm::length(readBack[g].point - expected[g].point) < 1e-5f &&
                        glm::length(readBack[g].axis - expected[g].axis) < 1e-5f &&
                        glm::length(readBack[g].palm - expected[g].palm) < 1e-5f &&
                        readBack[g].preset == expected[g].preset && std::abs(readBack[g].thickness - expected[g].thickness) < 1e-6f;
        report.check("import reads the recipe's grips back from the .glb; other models have none",
                     sameGrips && readKind == "weapon" && !chairRead && none.empty(), readKind);
        report.check("Export GLB names the file after the recipe and adds grips only for a held asset",
                     exported && chairExported && path.size() > 9 && path.compare(path.size() - 9, 9, "Sword.glb") == 0 &&
                     bytes.find("\"loom_tool\"") != std::string::npos && chairBytes.find("\"loom_tool\"") == std::string::npos,
                     exportError + " " + path);
        std::filesystem::remove_all(std::filesystem::temp_directory_path() / "loom_export");
    }
    report.check("furnish without a room plan says so", !noPlan.succeeded && noPlan.error.find("RoomSplit") != std::string::npos, noPlan.error);
    return report.result();
}
