#pragma once

#include "LoomAgentActions.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Loom{

struct AgentJsonValue{
    enum class Kind{ Null, Boolean, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<AgentJsonValue> array;
    std::map<std::string, AgentJsonValue> object;

    const AgentJsonValue* get(const std::string& key) const{
        const auto found = object.find(key);
        return found == object.end() ? nullptr : &found->second;
    }
    const AgentJsonValue* at(size_t index) const{
        return index < array.size() ? &array[index] : nullptr;
    }
};

class AgentJsonParser{
public:
    explicit AgentJsonParser(const std::string& source) : source(source){}
    AgentJsonValue parse(){
        AgentJsonValue value = parseValue();
        whitespace();
        if(position != source.size()) fail("Trailing JSON data");
        return value;
    }
private:
    const std::string& source;
    size_t position = 0;

    [[noreturn]] void fail(const char* message) const{
        throw std::runtime_error(std::string("Agent JSON: ") + message + " at byte " + std::to_string(position));
    }
    void whitespace(){ while(position < source.size() && (source[position] == ' ' || source[position] == '\n' ||
                            source[position] == '\r' || source[position] == '\t')) ++position; }
    bool take(char expected){ whitespace(); if(position < source.size() && source[position] == expected){ ++position; return true; } return false; }
    void expect(char expected){ if(!take(expected)) fail("Unexpected token"); }
    static void appendUtf8(std::string& out, unsigned value){
        if(value <= 0x7f) out.push_back(char(value));
        else if(value <= 0x7ff){ out.push_back(char(0xc0 | (value >> 6))); out.push_back(char(0x80 | (value & 0x3f))); }
        else if(value <= 0xffff){ out.push_back(char(0xe0 | (value >> 12))); out.push_back(char(0x80 | ((value >> 6) & 0x3f))); out.push_back(char(0x80 | (value & 0x3f))); }
        else{ out.push_back(char(0xf0 | (value >> 18))); out.push_back(char(0x80 | ((value >> 12) & 0x3f))); out.push_back(char(0x80 | ((value >> 6) & 0x3f))); out.push_back(char(0x80 | (value & 0x3f))); }
    }
    unsigned hex4(){
        if(position + 4 > source.size()) fail("Short Unicode escape");
        unsigned value = 0;
        for(int i = 0; i < 4; ++i){
            const char c = source[position++];
            value <<= 4;
            if(c >= '0' && c <= '9') value |= unsigned(c - '0');
            else if(c >= 'a' && c <= 'f') value |= unsigned(c - 'a' + 10);
            else if(c >= 'A' && c <= 'F') value |= unsigned(c - 'A' + 10);
            else fail("Invalid Unicode escape");
        }
        return value;
    }
    std::string parseString(){
        expect('"');
        std::string result;
        while(position < source.size()){
            const unsigned char c = static_cast<unsigned char>(source[position++]);
            if(c == '"') return result;
            if(c < 0x20) fail("Control byte in string");
            if(c != '\\'){ result.push_back(char(c)); continue; }
            if(position >= source.size()) fail("Short escape");
            switch(source[position++]){
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    unsigned codepoint = hex4();
                    if(codepoint >= 0xd800 && codepoint <= 0xdbff){
                        if(position + 2 > source.size() || source[position++] != '\\' || source[position++] != 'u') fail("Invalid surrogate pair");
                        const unsigned low = hex4();
                        if(low < 0xdc00 || low > 0xdfff) fail("Invalid low surrogate");
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                    }
                    appendUtf8(result, codepoint); break;
                }
                default: fail("Unknown string escape");
            }
        }
        fail("Unterminated string");
    }
    AgentJsonValue parseValue(){
        whitespace();
        if(position >= source.size()) fail("Missing value");
        if(source[position] == '"'){ AgentJsonValue v; v.kind = AgentJsonValue::Kind::String; v.string = parseString(); return v; }
        if(source[position] == '{') return parseObject();
        if(source[position] == '[') return parseArray();
        if(source.compare(position, 4, "true") == 0){ position += 4; AgentJsonValue v; v.kind = AgentJsonValue::Kind::Boolean; v.boolean = true; return v; }
        if(source.compare(position, 5, "false") == 0){ position += 5; AgentJsonValue v; v.kind = AgentJsonValue::Kind::Boolean; return v; }
        if(source.compare(position, 4, "null") == 0){ position += 4; return {}; }
        const size_t start = position;
        while(position < source.size() && (source[position] == '-' || source[position] == '+' || source[position] == '.' ||
              source[position] == 'e' || source[position] == 'E' || (source[position] >= '0' && source[position] <= '9'))) ++position;
        if(start == position) fail("Invalid value");
        AgentJsonValue v; v.kind = AgentJsonValue::Kind::Number;
        try{ v.number = std::stod(source.substr(start, position - start)); }
        catch(...){ fail("Invalid number"); }
        if(!std::isfinite(v.number)) fail("Non-finite number");
        return v;
    }
    AgentJsonValue parseObject(){
        expect('{'); AgentJsonValue value; value.kind = AgentJsonValue::Kind::Object;
        if(take('}')) return value;
        while(true){
            whitespace(); if(position >= source.size() || source[position] != '"') fail("Object key is not a string");
            std::string key = parseString(); expect(':');
            if(!value.object.emplace(std::move(key), parseValue()).second) fail("Duplicate object key");
            if(take('}')) return value;
            expect(',');
        }
    }
    AgentJsonValue parseArray(){
        expect('['); AgentJsonValue value; value.kind = AgentJsonValue::Kind::Array;
        if(take(']')) return value;
        while(true){ value.array.push_back(parseValue()); if(take(']')) return value; expect(','); }
    }
};

inline std::string agentJsonEscape(const std::string& value){
    std::string out; out.reserve(value.size() + 2); out.push_back('"');
    static const char* hex = "0123456789abcdef";
    for(unsigned char c : value){
        if(c == '"' || c == '\\'){ out.push_back('\\'); out.push_back(char(c)); }
        else if(c == '\b') out += "\\b";
        else if(c == '\f') out += "\\f";
        else if(c == '\n') out += "\\n";
        else if(c == '\r') out += "\\r";
        else if(c == '\t') out += "\\t";
        else if(c < 0x20){ out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
        else out.push_back(char(c));
    }
    out.push_back('"'); return out;
}

inline std::string agentSceneContextJson(const Warp::Stage& stage, Warp::Id selected, double frame){
    constexpr size_t maxEntities = 24;
    const double safeFrame = std::isfinite(frame) ? frame : 1.0;
    std::ostringstream out;
    out << std::setprecision(6) << "{\"schema\":\"loom.scene-context\",\"version\":1,\"frame\":"
        << safeFrame << ",\"selected_path\":";
    if(stage.contains(selected)) out << agentJsonEscape(stage.path(selected));
    else out << "null";
    out << ",\"entities\":[";
    size_t written = 0;
    auto appendEntity = [&](const Warp::Entity& entity){
        if(written >= maxEntities) return;
        if(written++) out << ',';
        const Warp::Entity* parent = stage.get(entity.parent);
        const glm::mat4 world = stage.worldMatrix(entity.id, safeFrame);
        const glm::vec3 position(world[3]);
        const Warp::Transform local = stage.localAt(entity.id, safeFrame);
        out << "{\"path\":" << agentJsonEscape(stage.path(entity.id))
            << ",\"name\":" << agentJsonEscape(entity.name)
            << ",\"type\":" << agentJsonEscape(agentEntityType(entity))
            << ",\"parent_path\":" << agentJsonEscape(parent ? stage.path(parent->id) : std::string())
            << ",\"visible\":" << (entity.visible ? "true" : "false");
        if(finiteVector(position))
            out << ",\"world_position\":[" << position.x << ',' << position.y << ',' << position.z << ']';
        if(finiteVector(local.translation))
            out << ",\"local_position\":[" << local.translation.x << ',' << local.translation.y << ',' << local.translation.z << ']';
        if(finiteVector(local.scale))
            out << ",\"local_scale\":[" << local.scale.x << ',' << local.scale.y << ',' << local.scale.z << ']';
        const glm::quat rotation = local.rotation;
        if(std::isfinite(rotation.x) && std::isfinite(rotation.y) &&
           std::isfinite(rotation.z) && std::isfinite(rotation.w))
            out << ",\"local_rotation_xyzw\":[" << rotation.x << ',' << rotation.y << ','
                << rotation.z << ',' << rotation.w << ']';
        out << '}';
    };
    if(const Warp::Entity* entity = stage.get(selected)) appendEntity(*entity);
    stage.walk([&](const Warp::Entity& entity, int){
        if(entity.id != selected) appendEntity(entity);
    });
    out << "],\"truncated\":" << (stage.size() > maxEntities ? "true" : "false") << '}';
    return out.str();
}

struct AgentPendingAction{ AgentAction action; bool confirmationRequired = false; };
struct AgentApiResponse{ std::string reply, error; std::vector<AgentPendingAction> actions; };

inline std::string agentJsonString(const AgentJsonValue* value, const char* field){
    if(!value || value->kind != AgentJsonValue::Kind::String) throw std::runtime_error(std::string("Missing string: ") + field);
    return value->string;
}
inline std::optional<glm::vec3> agentJsonVector(const AgentJsonValue* value, const char* field){
    if(!value) return std::nullopt;
    if(value->kind != AgentJsonValue::Kind::Array || value->array.size() != 3) throw std::runtime_error(std::string("Invalid vector: ") + field);
    for(const AgentJsonValue& component : value->array)
        if(component.kind != AgentJsonValue::Kind::Number || !std::isfinite(component.number)) throw std::runtime_error(std::string("Invalid vector: ") + field);
    return glm::vec3(float(value->array[0].number), float(value->array[1].number), float(value->array[2].number));
}
inline std::optional<float> agentJsonOptionalFloat(const AgentJsonValue* value, const char* field){
    if(!value) return std::nullopt;
    if(value->kind != AgentJsonValue::Kind::Number || !std::isfinite(value->number) ||
       std::abs(value->number) > double(std::numeric_limits<float>::max()))
        throw std::runtime_error(std::string("Invalid finite number: ") + field);
    return float(value->number);
}
inline std::optional<uint32_t> agentJsonOptionalU32(const AgentJsonValue* value, const char* field){
    if(!value) return std::nullopt;
    if(value->kind != AgentJsonValue::Kind::Number || !std::isfinite(value->number) ||
       value->number < 0.0 || value->number > double(std::numeric_limits<uint32_t>::max()) ||
       std::floor(value->number) != value->number)
        throw std::runtime_error(std::string("Invalid non-negative integer: ") + field);
    return uint32_t(value->number);
}
inline float agentJsonNumberOr(const AgentJsonValue* value, float fallback, const char* field){
    const std::optional<float> parsed = agentJsonOptionalFloat(value, field);
    return parsed ? *parsed : fallback;
}
inline uint32_t agentJsonU32Or(const AgentJsonValue* value, uint32_t fallback, const char* field){
    const std::optional<uint32_t> parsed = agentJsonOptionalU32(value, field);
    return parsed ? *parsed : fallback;
}
inline AgentAction agentActionFromJson(const AgentJsonValue& value, bool& confirmation){
    if(value.kind != AgentJsonValue::Kind::Object) throw std::runtime_error("Agent action is not an object");
    AgentAction action;
    action.tool = agentJsonString(value.get("tool"), "tool");
    const AgentJsonValue* args = value.get("arguments");
    if(!args || args->kind != AgentJsonValue::Kind::Object) throw std::runtime_error("Agent action arguments are not an object");
    if(const auto* v = args->get("target")) action.target = agentJsonString(v, "target");
    if(const auto* v = args->get("name")) action.name = agentJsonString(v, "name");
    if(const auto* v = args->get("primitive")) action.primitive = agentJsonString(v, "primitive");
    action.color = agentJsonVector(args->get("color"), "color");
    if(const auto* v = args->get("parent")){
        if(v->kind == AgentJsonValue::Kind::String) action.parent = v->string;
        else if(v->kind != AgentJsonValue::Kind::Null) throw std::runtime_error("Agent parent must be a path or null");
    }
    action.translation = agentJsonVector(args->get("translation"), "translation");
    action.rotationDegrees = agentJsonVector(args->get("rotation_degrees"), "rotation_degrees");
    action.scale = agentJsonVector(args->get("scale"), "scale");
    if(const auto* v = args->get("visible")){
        if(v->kind != AgentJsonValue::Kind::Boolean) throw std::runtime_error("Agent visibility must be boolean");
        action.visible = v->boolean;
    }
    if(const auto* v = args->get("frame")){
        if(v->kind != AgentJsonValue::Kind::Number) throw std::runtime_error("Agent frame must be numeric");
        action.frame = v->number;
    }
    if(action.tool == "procedura.create_recipe"){
        AgentProceduraRecipeRequest request;
        request.generator = agentJsonString(args->get("generator"), "generator");
        std::set<std::string> allowedFields{"generator", "name"};
        if(request.generator == "grid_surface"){
            allowedFields.insert({"width", "depth", "cells_x", "cells_z", "point_heights"});
        }else if(request.generator == "interior_blockout"){
            allowedFields.insert({"rooms_per_side", "room_width", "room_depth", "corridor_width",
                                  "wall_height", "wall_thickness", "floor_thickness", "door_width"});
        }else{
            throw std::runtime_error("Unsupported Procedura generator: " + request.generator);
        }
        for(const auto& entry : args->object)
            if(!allowedFields.count(entry.first)) throw std::runtime_error("Unknown Procedura argument: " + entry.first);
        if(request.generator == "grid_surface"){
            request.grid.width = agentJsonNumberOr(args->get("width"), request.grid.width, "width");
            request.grid.depth = agentJsonNumberOr(args->get("depth"), request.grid.depth, "depth");
            request.grid.cellsX = agentJsonU32Or(args->get("cells_x"), request.grid.cellsX, "cells_x");
            request.grid.cellsZ = agentJsonU32Or(args->get("cells_z"), request.grid.cellsZ, "cells_z");
            if(const AgentJsonValue* edits = args->get("point_heights")){
                if(edits->kind != AgentJsonValue::Kind::Array || edits->array.size() > 64)
                    throw std::runtime_error("point_heights must contain at most 64 grid points");
                for(const AgentJsonValue& item : edits->array){
                    if(item.kind != AgentJsonValue::Kind::Object)
                        throw std::runtime_error("point_heights entries must be objects");
                    if(item.object.size() != 3 || !item.get("column") || !item.get("row") || !item.get("height"))
                        throw std::runtime_error("point_heights entries need only column, row, and height");
                    AgentGridHeightEdit edit;
                    const auto column = agentJsonOptionalU32(item.get("column"), "point_heights.column");
                    const auto row = agentJsonOptionalU32(item.get("row"), "point_heights.row");
                    const auto height = agentJsonOptionalFloat(item.get("height"), "point_heights.height");
                    if(!column || !row || !height) throw std::runtime_error("point_heights entries need column, row, and height");
                    edit.column = *column;
                    edit.row = *row;
                    edit.height = *height;
                    for(const AgentGridHeightEdit& prior : request.pointHeights)
                        if(prior.column == edit.column && prior.row == edit.row)
                            throw std::runtime_error("point_heights cannot edit the same point twice");
                    request.pointHeights.push_back(edit);
                }
            }
        }else if(request.generator == "interior_blockout"){
            request.interior.roomsPerSide = agentJsonU32Or(args->get("rooms_per_side"), request.interior.roomsPerSide, "rooms_per_side");
            request.interior.roomWidth = agentJsonNumberOr(args->get("room_width"), request.interior.roomWidth, "room_width");
            request.interior.roomDepth = agentJsonNumberOr(args->get("room_depth"), request.interior.roomDepth, "room_depth");
            request.interior.corridorWidth = agentJsonNumberOr(args->get("corridor_width"), request.interior.corridorWidth, "corridor_width");
            request.interior.wallHeight = agentJsonNumberOr(args->get("wall_height"), request.interior.wallHeight, "wall_height");
            request.interior.wallThickness = agentJsonNumberOr(args->get("wall_thickness"), request.interior.wallThickness, "wall_thickness");
            request.interior.floorThickness = agentJsonNumberOr(args->get("floor_thickness"), request.interior.floorThickness, "floor_thickness");
            request.interior.doorWidth = agentJsonNumberOr(args->get("door_width"), request.interior.doorWidth, "door_width");
        }else{
            throw std::runtime_error("Unsupported Procedura generator: " + request.generator);
        }
        action.proceduraRecipe = std::move(request);
    }
    if(const auto* v = value.get("confirmation_required")){
        if(v->kind != AgentJsonValue::Kind::Boolean) throw std::runtime_error("Agent confirmation flag must be boolean");
        confirmation = v->boolean;
    }
    return action;
}
inline AgentApiResponse agentResponseFromJson(const std::string& source){
    const AgentJsonValue root = AgentJsonParser(source).parse();
    if(root.kind != AgentJsonValue::Kind::Object) throw std::runtime_error("Agent response is not a JSON object");
    if(const auto* error = root.get("error")){ AgentApiResponse response; response.error = agentJsonString(error, "error"); return response; }
    const AgentJsonValue* result = root.get("result");
    if(!result || result->kind != AgentJsonValue::Kind::Object) throw std::runtime_error("Agent response has no result object");
    AgentApiResponse response;
    response.reply = agentJsonString(result->get("reply"), "reply");
    const AgentJsonValue* actions = result->get("actions");
    if(!actions || actions->kind != AgentJsonValue::Kind::Array || actions->array.size() > 16) throw std::runtime_error("Invalid agent action list");
    for(const AgentJsonValue& item : actions->array){
        bool confirmation = false;
        response.actions.push_back({agentActionFromJson(item, confirmation), confirmation});
    }
    return response;
}

} // namespace Loom
