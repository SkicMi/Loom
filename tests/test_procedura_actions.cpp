// AgentOfWeavers: the action language. A sampled house becomes actions and back without change,
// every kind of illegal step is caught at the step where it happens, the schema exports as JSON,
// and the house generator produces passing, described samples.

#include "TestHarness.h"

#include "../src/LoomProceduraHouses.h"

#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace Proc = Engine::WeaverProcedura;
namespace Recipe = Loom::WeaverProceduraRecipe;

int main(){
    TestReport report("Procedura: AgentOfWeavers actions");
    Recipe::RecipeAssetLibrary library;
    library.load(std::filesystem::path(__FILE__).parent_path().parent_path() / "procedura" / "assets");
    report.check("asset library loads", library.loadError.empty() && !library.ids().empty(), library.loadError);
    std::string error;

    std::mt19937_64 rng(5);
    const Recipe::HouseSample house = Recipe::sampleHouse(rng);
    const Recipe::Document document = Recipe::houseDocument(house, "house");
    std::vector<std::string> actions, again;
    const bool encoded = Recipe::recipeToActions(document, actions, error);
    Recipe::Document back;
    std::size_t failedStep = 0;
    const bool decoded = encoded && Recipe::actionsToDocument(actions, "house", back, error, failedStep);
    const bool reencoded = decoded && Recipe::recipeToActions(back, again, error);
    report.check("a house becomes actions and back to the same actions", reencoded && again == actions && actions.back() == "END",
                 error + " " + std::to_string(actions.size()) + " actions");
    const Proc::EvaluationResult original = Proc::evaluate(document.graph, &library);
    const Proc::EvaluationResult rebuilt = decoded ? Proc::evaluate(back.graph, &library) : Proc::EvaluationResult{};
    report.check("the rebuilt recipe makes the same house", original.succeeded == rebuilt.succeeded &&
                 original.mesh.indices.size() == rebuilt.mesh.indices.size() && original.placements.size() == rebuilt.placements.size(),
                 original.error + rebuilt.error);

    auto rejectedAt = [&](std::vector<std::string> changed, std::size_t expected, const char* what){
        Recipe::Document ignored;
        std::string why;
        std::size_t step = 0;
        const bool ok = Recipe::actionsToDocument(changed, "x", ignored, why, step);
        report.check(what, !ok && step == expected, why + " at " + std::to_string(step));
    };
    auto indexOf = [&](const std::string& prefix){
        for(std::size_t k = 0; k < actions.size(); ++k) if(actions[k].rfind(prefix, 0) == 0) return k;
        return actions.size();
    };
    {
        auto changed = actions; const std::size_t k = indexOf("SET width");
        changed[k] = "SET width 99"; rejectedAt(changed, k, "a value outside the schema range is illegal where it is written");
        changed[k] = "SET width 12.43"; rejectedAt(changed, k, "a value off the schema grid is illegal");
    }
    {
        auto changed = actions; const std::size_t k = indexOf("SET depth");
        std::swap(changed[k], changed[k - 1]); rejectedAt(changed, k - 1, "parameters out of schema order are illegal");
    }
    {
        auto changed = actions; const std::size_t k = indexOf("SET roof_type");
        changed[k] = "SET roof_type dome"; rejectedAt(changed, k, "an enum value that is not an option is illegal");
    }
    {
        auto changed = actions; const std::size_t k = indexOf("SET material");
        changed[k] = "SET material unobtainium"; rejectedAt(changed, k, "a material outside the library is illegal");
    }
    {
        // place_assets takes Placements; wiring the footprint into it is a type error.
        auto changed = actions; const std::size_t add = indexOf("ADD place_assets");
        changed[add + 1] = "CONNECT 0 0"; rejectedAt(changed, add + 1, "connecting a Footprint into a Placements input is illegal");
    }
    {
        auto changed = actions; const std::size_t add = indexOf("ADD floor_stack");
        std::size_t connect = add + 1;
        while(changed[connect].rfind("CONNECT", 0) != 0) ++connect;
        changed.erase(changed.begin() + std::ptrdiff_t(connect));
        rejectedAt(changed, connect, "a node without its input is illegal at the next ADD");
    }
    {
        auto changed = actions; changed.insert(changed.begin() + 1, "ADD spaceship"); rejectedAt(changed, 1, "an unknown node type is illegal");
    }
    {
        auto changed = actions; changed.pop_back(); rejectedAt(changed, changed.size(), "a sequence without END is incomplete");
    }

    const std::string schema = Recipe::schemaJson();
    bool schemaParses = false;
    try{
        const Loom::AgentJsonValue root = Loom::AgentJsonParser(schema).parse();
        schemaParses = root.get("nodes") && root.get("nodes")->array.size() == Recipe::nodeSchemas().size();
    }catch(const std::exception& problem){ error = problem.what(); }
    report.check("the schema exports as JSON with every node", schemaParses && schema.find("\"uv_project\"") != std::string::npos &&
                 schema.find("\"roof_tiles\"") != std::string::npos, error);

    // 60 sampled houses: sampling stays on the grid (round trip), most pass, every pass is described.
    int passed = 0, described = 0, repairedOrExplained = 0, failures = 0;
    std::mt19937_64 houses(17), words(3);
    for(int n = 0; n < 60; ++n){
        Recipe::HouseSample sample = Recipe::sampleHouse(houses);
        for(int attempt = 0; attempt < 3; ++attempt){
            std::vector<std::string> sequence;
            if(!Recipe::recipeToActions(Recipe::houseDocument(sample, "h"), sequence, error)){ report.check("sampled house encodes", false, error); return report.result(); }
            const Proc::EvaluationResult result = Proc::evaluate(Recipe::houseDocument(sample, "h").graph, &library);
            Recipe::HouseFacts facts;
            std::string reason = result.succeeded ? (Recipe::measureHouse(sample, result, facts, error) ? Recipe::houseRuleProblem(sample, facts) : error)
                                                  : result.error;
            if(reason.empty()){
                ++passed;
                const auto texts = Recipe::describeHouse(sample, facts, words);
                bool good = texts.size() == 3;
                for(const std::string& text : texts) good = good && text.size() > 10 && text.find(" a a") == std::string::npos;
                described += good ? 1 : 0;
                break;
            }
            ++failures;
            std::string repair;
            if(!Recipe::repairHouse(sample, reason, repair)) break;
            ++repairedOrExplained;
        }
    }
    report.check("at least 90% of sampled houses pass after at most two repairs", passed >= 54,
                 std::to_string(passed) + "/60, " + std::to_string(failures) + " failed attempts, " + std::to_string(repairedOrExplained) + " repairs");
    report.check("every passing house gets three English descriptions", described == passed, std::to_string(described));
    return report.result();
}
