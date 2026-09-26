// ProceduraGen: AgentOfWeavers training data from the Procedura rules, without a window.
//
//   procedura-gen --schema schema.json
//   procedura-gen --textures DIR [--size 512]      every library material's PBR maps as PNG
//   procedura-gen --houses 10000 --seed 1 --out DIR [--retries 2] [--val 0.02] [--save-recipes K]
//
// For every house: sample the template on the schema grid, turn the recipe into actions and back
// (a sequence that does not survive the round trip is a generator bug and stops the run),
// evaluate it with the asset library, measure it, check the house rules, describe it in English.
// A failed house is written with its reason (Fail -> Why); a repair rule then changes one thing
// and the retry is written with retry_of and the repair (Retry), so repairs are data as well.
//
// DIR/houses.jsonl   one sample per line
// DIR/schema.json    the action schema the samples use
// DIR/summary.json   counts, failure reasons, timing
// DIR/recipes/       with --save-recipes K: the first K passing houses as .loomrecipe.json (to look at)

#include "LoomProceduraHouses.h"
#include <Engine/WeaverProceduraTextures.h>
#include <Spool/ImageFile.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string>

namespace Recipe = Loom::WeaverProceduraRecipe;
namespace Proc = Engine::WeaverProcedura;

namespace{

bool writeText(const std::filesystem::path& path, const std::string& text){
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return bool(out);
}

std::string jsonList(const std::vector<std::string>& items){
    std::string text = "[";
    for(std::size_t k = 0; k < items.size(); ++k) text += (k ? "," : "") + Loom::agentJsonEscape(items[k]);
    return text + "]";
}

// Stable split: the same id is always in the same split, whatever N is.
bool validationId(uint64_t seed, uint64_t id, double share){
    uint64_t value = seed * 0x9e3779b97f4a7c15ull + id;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    value ^= value >> 31u;
    return double(value >> 11u) * (1.0 / 9007199254740992.0) < share;
}

}

int main(int argc, char** argv){
    std::string schemaPath, outDir;
    long long houses = 0;
    uint64_t seed = 1;
    int retries = 2;
    double validationShare = 0.02;
    long long saveRecipes = 0;
    std::string texturesDir;
    uint32_t textureSize = 512;
    for(int i = 1; i < argc; ++i){
        const std::string argument = argv[i];
        if(argument == "--schema" && i + 1 < argc) schemaPath = argv[++i];
        else if(argument == "--houses" && i + 1 < argc) houses = std::atoll(argv[++i]);
        else if(argument == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if(argument == "--out" && i + 1 < argc) outDir = argv[++i];
        else if(argument == "--retries" && i + 1 < argc) retries = std::max(0, std::atoi(argv[++i]));
        else if(argument == "--val" && i + 1 < argc) validationShare = std::atof(argv[++i]);
        else if(argument == "--save-recipes" && i + 1 < argc) saveRecipes = std::atoll(argv[++i]);
        else if(argument == "--textures" && i + 1 < argc) texturesDir = argv[++i];
        else if(argument == "--size" && i + 1 < argc) textureSize = uint32_t(std::atoi(argv[++i]));
        else{
            std::fprintf(stderr, "usage: procedura-gen --schema FILE | --houses N --seed S --out DIR [--retries R] [--val SHARE]\n");
            return 2;
        }
    }
    if(!texturesDir.empty()){
        for(const std::string& name : Proc::materialLibrary()){
            Proc::MaterialTextures maps;
            std::string error;
            if(!Proc::makeMaterialTextures(name, textureSize, maps, error)){ std::fprintf(stderr, "%s: %s\n", name.c_str(), error.c_str()); return 1; }
            auto save = [&](const char* suffix, const std::vector<uint8_t>& pixels){
                Spool::Image image;
                image.pixels = pixels;
                image.width = image.height = maps.size;
                image.sourceChannels = 4;
                Spool::savePng((std::filesystem::path(texturesDir) / (name + suffix)).string(), image);
            };
            save("_color.png", maps.color);
            save("_mr.png", maps.metallicRoughness);
            save("_normal.png", maps.normal);
        }
        std::printf("textures: %zu materials in %s\n", Proc::materialLibrary().size(), texturesDir.c_str());
        if(houses == 0 && schemaPath.empty()) return 0;
    }
    if(!schemaPath.empty()){
        if(!writeText(schemaPath, Recipe::schemaJson())){ std::fprintf(stderr, "cannot write %s\n", schemaPath.c_str()); return 1; }
        std::printf("schema: %s\n", schemaPath.c_str());
        if(houses == 0) return 0;
    }
    if(houses <= 0 || outDir.empty()){ std::fprintf(stderr, "need --houses N and --out DIR\n"); return 2; }

    const Recipe::RecipeAssetLibrary& library = Recipe::defaultAssetLibrary();
    if(!library.loadError.empty()){ std::fprintf(stderr, "asset library: %s\n", library.loadError.c_str()); return 1; }
    std::filesystem::create_directories(outDir);
    writeText(std::filesystem::path(outDir) / "schema.json", Recipe::schemaJson());
    std::ofstream out(std::filesystem::path(outDir) / "houses.jsonl", std::ios::binary | std::ios::trunc);
    if(!out){ std::fprintf(stderr, "cannot write into %s\n", outDir.c_str()); return 1; }

    std::mt19937_64 rng(seed);
    std::map<std::string, int> reasons, repairs;
    long long passed = 0, failed = 0, repaired = 0, records = 0, validation = 0;
    std::size_t actionCount = 0;
    const auto started = std::chrono::steady_clock::now();
    for(long long n = 0; n < houses; ++n){
        Recipe::HouseSample house = Recipe::sampleHouse(rng);
        long long retryOf = -1;
        std::string repair;
        for(int attempt = 0; attempt <= retries; ++attempt){
            const long long id = records++;
            const Recipe::Document document = Recipe::houseDocument(house, "house " + std::to_string(id));
            std::vector<std::string> actions;
            std::string error;
            if(!Recipe::recipeToActions(document, actions, error)){ std::fprintf(stderr, "house %lld: actions: %s\n", id, error.c_str()); return 1; }
            Recipe::Document back;
            std::size_t failedStep = 0;
            std::vector<std::string> again;
            if(!Recipe::actionsToDocument(actions, document.name, back, error, failedStep) ||
               !Recipe::recipeToActions(back, again, error) || again != actions){
                std::fprintf(stderr, "house %lld: round trip failed at step %zu: %s\n", id, failedStep, error.c_str());
                return 1;
            }
            const Proc::EvaluationResult result = Proc::evaluate(back.graph, &library);
            Recipe::HouseFacts facts;
            std::string reason;
            if(!result.succeeded) reason = result.error.empty() ? "evaluation failed without a reason" : result.error;
            else if(!Recipe::measureHouse(house, result, facts, error)) reason = "measure: " + error;
            else reason = Recipe::houseRuleProblem(house, facts);

            const bool val = validationId(seed, uint64_t(n), validationShare);
            std::ostringstream line;
            line.precision(6);
            line << "{\"id\":" << id << ",\"house\":" << n << ",\"generator\":\"" << Recipe::houseGeneratorVersion << "\",\"seed\":" << seed
                 << ",\"split\":\"" << (val ? "val" : "train") << "\",\"status\":\"" << (reason.empty() ? "pass" : "fail") << '"';
            if(!reason.empty()) line << ",\"reason\":" << Loom::agentJsonEscape(reason);
            if(retryOf >= 0) line << ",\"retry_of\":" << retryOf << ",\"repair\":" << Loom::agentJsonEscape(repair);
            if(reason.empty()){
                std::mt19937_64 words(seed * 1000003ull + uint64_t(id));
                line << ",\"descriptions\":" << jsonList(Recipe::describeHouse(house, facts, words));
                line << ",\"facts\":{\"rooms\":{";
                bool first = true;
                for(const auto& [type, count] : facts.rooms){ line << (first ? "" : ",") << '"' << type << "\":" << count; first = false; }
                line << "},\"floors\":" << house.stack.floors << ",\"footprint_area\":" << facts.footprintArea << ",\"height\":" << facts.height
                     << ",\"style\":\"" << house.furnish.style << "\",\"furniture\":" << facts.pieces << ",\"triangles\":" << facts.triangles << '}';
            }
            line << ",\"actions\":" << jsonList(actions) << "}\n";
            out << line.str();
            actionCount += actions.size();
            if(val) ++validation;
            if(reason.empty()){
                if(passed < saveRecipes){
                    std::filesystem::create_directories(std::filesystem::path(outDir) / "recipes");
                    writeText(std::filesystem::path(outDir) / "recipes" / ("house_" + std::to_string(id) + ".loomrecipe.json"),
                              Recipe::serialize(back));
                }
                ++passed;
                if(retryOf >= 0) ++repaired;
                break;
            }
            ++failed;
            ++reasons[reason.substr(0, reason.find(':') == std::string::npos ? reason.size() : std::min<std::size_t>(reason.size(), 80))];
            if(attempt == retries || !Recipe::repairHouse(house, reason, repair)) break;
            ++repairs[repair];
            retryOf = id;
        }
        if((n + 1) % 1000 == 0) std::printf("%lld / %lld houses\n", n + 1, houses), std::fflush(stdout);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::ostringstream summary;
    summary << "{\"generator\":\"" << Recipe::houseGeneratorVersion << "\",\"seed\":" << seed << ",\"houses\":" << houses
            << ",\"records\":" << records << ",\"passed\":" << passed << ",\"failed_attempts\":" << failed
            << ",\"repaired\":" << repaired << ",\"validation_records\":" << validation << ",\"mean_actions\":"
            << double(actionCount) / double(std::max(1LL, records)) << ",\"seconds\":" << seconds << ",\"reasons\":{";
    bool first = true;
    for(const auto& [reason, count] : reasons){ summary << (first ? "" : ",") << Loom::agentJsonEscape(reason) << ':' << count; first = false; }
    summary << "},\"repairs\":{";
    first = true;
    for(const auto& [what, count] : repairs){ summary << (first ? "" : ",") << Loom::agentJsonEscape(what) << ':' << count; first = false; }
    summary << "}}\n";
    writeText(std::filesystem::path(outDir) / "summary.json", summary.str());
    std::printf("%s", summary.str().c_str());
    return 0;
}
