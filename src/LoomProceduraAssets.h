#pragma once

// Asset library from files: every procedura/assets/**/*.loomasset.json is one asset, a normal
// Procedura recipe whose numbers may name its parameters. Assets are data like the house
// recipes, so a model can learn to write furniture, props and tools the same way it writes houses.
//
//   {"format":"loom.weaverprocedura.asset", "id":"bed_basic", "category":"bed",
//    "parameters":[{"name":"width","default":1.6,"min":0.9,"max":2.0}, ...],
//    "bounds":[{"param":"width"}, 1.0, {"param":"length","offset":0.05}],
//    "placement":"wall", "clearance_front":0.6, "style":"basic",
//    "grips":[{"name":"Main", "point":[0,{"param":"length","scale":0.3},0], "axis":[0,1,0], "palm":[1,0,0],
//              "thickness":0.015, "preset":"grip", "hand":0}],        (tools and weapons must have one)
//    "recipe":{ ... a loom.weaverprocedura.recipe ... }}
//
// A number written as {"param":name, "scale":s, "offset":o} becomes value * s + o (scale 1 and
// offset 0 when left out). Asset space: base centred on the origin, front toward +Z.

#include "LoomProceduraRecipe.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

namespace Loom::WeaverProceduraRecipe{

// Finger poses a grip may ask for; the same names as gripPresets() in LoomHandPose.h (a test checks).
inline const std::vector<std::string>& assetGripPresetNames(){
    static const std::vector<std::string> names = {"grip", "pistol", "cup", "fist", "point", "relaxed", "open"};
    return names;
}

inline const std::vector<std::string>& assetPlacementNames(){
    static const std::vector<std::string> names = {"wall", "center", "corner"};
    return names;
}

class RecipeAssetLibrary : public Proc::AssetLibrary{
public:
    // Reads every asset under the folder; the first problem is kept in loadError (the other
    // assets still load). Two files with one id are an error.
    bool load(const std::filesystem::path& folder){
        assets.clear();
        loadError.clear();
        std::error_code code;
        if(!std::filesystem::is_directory(folder, code)){ loadError = "asset folder not found: " + folder.string(); return false; }
        std::vector<std::filesystem::path> files;
        for(const auto& entry : std::filesystem::recursive_directory_iterator(folder, code)){
            const std::string name = entry.path().filename().string();
            if(entry.is_regular_file() && name.size() > 15 && name.compare(name.size() - 15, 15, ".loomasset.json") == 0) files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        for(const auto& file : files){
            try{
                std::ifstream in(file, std::ios::binary);
                const std::string source{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                add(source);
            }catch(const std::exception& problem){
                if(loadError.empty()) loadError = file.filename().string() + ": " + problem.what();
            }
        }
        return loadError.empty();
    }

    // One asset from its JSON text; throws with the reason when it is not a valid asset.
    void add(const std::string& source){
        if(source.empty() || source.size() > 1'000'000) throw std::runtime_error("asset file must contain 1 to 1000000 bytes");
        Entry entry;
        const AgentJsonValue root = AgentJsonParser(source).parse();
        if(root.kind != AgentJsonValue::Kind::Object || readString(required(root, "format"), "format") != "loom.weaverprocedura.asset")
            throw std::runtime_error("not a Loom WeaverProcedura asset");
        Proc::AssetInfo& info = entry.info;
        info.id = readString(required(root, "id"), "id");
        if(info.id.empty() || info.id.size() > 120) throw std::runtime_error("asset id must contain 1 to 120 characters");
        if(assets.count(info.id)) throw std::runtime_error("two assets are called " + info.id);
        info.category = readString(required(root, "category"), "category");
        const std::string kind = Proc::assetKind(info.category);
        if(kind.empty()) throw std::runtime_error("unknown asset category " + info.category);
        if(const AgentJsonValue* style = root.get("style")) info.style = readString(*style, "style");
        if(std::find(Proc::styleNames().begin(), Proc::styleNames().end(), info.style) == Proc::styleNames().end())
            throw std::runtime_error("unknown style " + info.style + " (known: basic, modern, rustic)");
        const AgentJsonValue& parameters = required(root, "parameters");
        if(parameters.kind != AgentJsonValue::Kind::Array || parameters.array.size() > 32)
            throw std::runtime_error("asset parameters must be an array of at most 32 entries");
        for(const AgentJsonValue& p : parameters.array){
            Proc::AssetParameter parameter;
            parameter.name = readString(required(p, "name"), "parameter.name");
            parameter.defaultValue = readFloat(required(p, "default"), "parameter.default");
            parameter.minValue = readFloat(required(p, "min"), "parameter.min");
            parameter.maxValue = readFloat(required(p, "max"), "parameter.max");
            if(parameter.name.empty() || parameter.minValue > parameter.defaultValue || parameter.defaultValue > parameter.maxValue)
                throw std::runtime_error("parameter " + parameter.name + " needs a name and min <= default <= max");
            info.parameters.push_back(parameter);
        }
        info.placement = Proc::AssetPlacement(readName(required(root, "placement"), assetPlacementNames(), "placement"));
        info.clearanceFront = readFloat(required(root, "clearance_front"), "clearance_front");
        entry.bounds = required(root, "bounds");
        if(entry.bounds.kind != AgentJsonValue::Kind::Array || entry.bounds.array.size() != 3)
            throw std::runtime_error("bounds must hold width, height and depth");
        entry.recipe = required(root, "recipe");
        if(const AgentJsonValue* grips = root.get("grips")){
            if(grips->kind != AgentJsonValue::Kind::Array || grips->array.size() > 8)
                throw std::runtime_error("grips must be an array of at most 8 entries");
            entry.grips = *grips;
        }
        // The defaults must give a valid recipe and box, so a broken asset fails when it loads.
        const std::string id = info.id;
        assets.emplace(id, std::move(entry));
        try{
            glm::vec3 size;
            std::string error;
            if(!bounds(id, {}, size, error)) throw std::runtime_error(error);
            std::vector<Proc::AssetGrip> held;
            if(!grips(id, {}, held, error)) throw std::runtime_error(error);
            if((kind == "tool" || kind == "weapon") && held.empty()) throw std::runtime_error("a " + kind + " needs a grip");
            parse(resolve(assets.at(id).recipe, values(assets.at(id).info, {})));
        }catch(...){
            assets.erase(id);
            throw;
        }
    }

    std::string loadError;

    std::vector<std::string> ids() const override{
        std::vector<std::string> result;
        for(const auto& [id, entry] : assets) result.push_back(id);
        return result;
    }
    const Proc::AssetInfo* info(const std::string& id) const override{
        const auto found = assets.find(id);
        return found == assets.end() ? nullptr : &found->second.info;
    }
    bool bounds(const std::string& id, const Proc::AssetParameters& parameters, glm::vec3& size, std::string& error) const override{
        const auto found = assets.find(id);
        if(found == assets.end()){ error = "unknown asset: " + id; return false; }
        try{
            const AgentJsonValue box = resolve(found->second.bounds, values(found->second.info, parameters));
            size = {readFloat(box.array[0], "bounds.width"), readFloat(box.array[1], "bounds.height"), readFloat(box.array[2], "bounds.depth")};
        }catch(const std::exception& problem){ error = problem.what(); return false; }
        if(!(size.x > 0.0f && size.y > 0.0f && size.z > 0.0f)){ error = "asset " + id + " has an empty box"; return false; }
        return true;
    }
    bool grips(const std::string& id, const Proc::AssetParameters& parameters, std::vector<Proc::AssetGrip>& output,
               std::string& error) const override{
        output.clear();
        const auto found = assets.find(id);
        if(found == assets.end()){ error = "unknown asset: " + id; return false; }
        try{
            const AgentJsonValue resolved = resolve(found->second.grips, values(found->second.info, parameters));
            for(const AgentJsonValue& g : resolved.array){
                Proc::AssetGrip grip;
                if(const AgentJsonValue* name = g.get("name")) grip.name = readString(*name, "grip.name");
                grip.point = readVec3(required(g, "point"), "grip.point");
                grip.axis = readVec3(required(g, "axis"), "grip.axis");
                grip.palm = readVec3(required(g, "palm"), "grip.palm");
                if(const AgentJsonValue* thick = g.get("thickness")) grip.thickness = readFloat(*thick, "grip.thickness");
                if(const AgentJsonValue* preset = g.get("preset")) grip.preset = readString(*preset, "grip.preset");
                if(const AgentJsonValue* hand = g.get("hand")) grip.hand = int(readU32(*hand, "grip.hand"));
                if(glm::length(grip.axis) < 1e-4f || glm::length(grip.palm) < 1e-4f)
                    throw std::runtime_error("grip " + grip.name + " needs an axis and a palm direction");
                grip.axis = glm::normalize(grip.axis);
                grip.palm = glm::normalize(grip.palm - grip.axis * glm::dot(grip.palm, grip.axis));
                if(std::find(assetGripPresetNames().begin(), assetGripPresetNames().end(), grip.preset) == assetGripPresetNames().end())
                    throw std::runtime_error("unknown grip preset " + grip.preset);
                if(grip.hand < 0 || grip.hand > 2 || grip.thickness < 0.0f) throw std::runtime_error("grip hand is 0..2 and thickness >= 0");
                output.push_back(grip);
            }
        }catch(const std::exception& problem){ error = problem.what(); output.clear(); return false; }
        return true;
    }

    bool build(const std::string& id, const Proc::AssetParameters& parameters, Proc::MeshData& output, std::string& error) const override{
        const auto found = assets.find(id);
        if(found == assets.end()){ error = "unknown asset: " + id; return false; }
        std::map<std::string, float> chosen;
        try{ chosen = values(found->second.info, parameters); }
        catch(const std::exception& problem){ error = problem.what(); return false; }
        std::string key = id;
        for(const auto& [name, value] : chosen) key += '|' + name + '=' + std::to_string(value);
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            const auto cached = cache.find(key);
            if(cached != cache.end()){ output = cached->second; return true; }
        }
        Proc::EvaluationResult result;
        try{
            result = Proc::evaluate(parse(resolve(found->second.recipe, chosen)).graph);
        }catch(const std::exception& problem){ error = problem.what(); return false; }
        if(!result.succeeded || result.pointCloudOutput){ error = result.succeeded ? "asset recipe gives points, not a mesh" : result.error; return false; }
        std::lock_guard<std::mutex> lock(cacheMutex);
        if(cache.size() > 4096) cache.clear();
        cache.emplace(key, result.mesh);
        output = std::move(result.mesh);
        return true;
    }

private:
    struct Entry{
        Proc::AssetInfo info;
        AgentJsonValue bounds, recipe, grips;       // grips: an empty array when the asset has none
    };
    std::map<std::string, Entry> assets;
    mutable std::mutex cacheMutex;
    mutable std::map<std::string, Proc::MeshData> cache;

    // Defaults, overridden by the given values clamped to each parameter's range; unknown names
    // are an error so a typo does not silently build the default.
    static std::map<std::string, float> values(const Proc::AssetInfo& info, const Proc::AssetParameters& given){
        std::map<std::string, float> result;
        for(const Proc::AssetParameter& p : info.parameters) result[p.name] = p.defaultValue;
        for(const auto& [name, value] : given){
            const auto p = std::find_if(info.parameters.begin(), info.parameters.end(), [&](const Proc::AssetParameter& q){ return q.name == name; });
            if(p == info.parameters.end()) throw std::runtime_error("asset " + info.id + " has no parameter " + name);
            result[name] = std::clamp(value, p->minValue, p->maxValue);
        }
        return result;
    }

    static AgentJsonValue resolve(const AgentJsonValue& value, const std::map<std::string, float>& parameters){
        if(value.kind == AgentJsonValue::Kind::Object && value.get("param") && !value.get("type")){
            const std::string name = readString(*value.get("param"), "param");
            const auto found = parameters.find(name);
            if(found == parameters.end()) throw std::runtime_error("recipe uses unknown parameter " + name);
            const double scale = value.get("scale") ? readNumber(*value.get("scale"), "scale") : 1.0;
            const double offset = value.get("offset") ? readNumber(*value.get("offset"), "offset") : 0.0;
            AgentJsonValue number;
            number.kind = AgentJsonValue::Kind::Number;
            number.number = double(found->second) * scale + offset;
            return number;
        }
        AgentJsonValue copy = value;
        for(AgentJsonValue& item : copy.array) item = resolve(item, parameters);
        for(auto& [key, item] : copy.object) item = resolve(item, parameters);
        return copy;
    }
};

// Library under the source tree's procedura/assets (LOOM_ROOT_DIR), for the editor and tools.
inline const RecipeAssetLibrary& defaultAssetLibrary(){
    static RecipeAssetLibrary library;
    static const bool loaded = []{
#ifdef LOOM_ROOT_DIR
        library.load(std::filesystem::path(LOOM_ROOT_DIR) / "procedura" / "assets");
#endif
        return true;
    }();
    (void)loaded;
    return library;
}

}  // namespace Loom::WeaverProceduraRecipe
