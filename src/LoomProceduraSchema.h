#pragma once

// AgentOfWeavers: the Recipe language as data. Every node type a model may write, with each
// parameter's kind, range, step and options, and the node's typed ports. The generator uses it to
// turn recipes into action sequences; training uses the JSON export to mask illegal actions and to
// turn numbers into bins. Parameter names are the Recipe JSON names; a dot is a nested object
// ("filter.semantic" is parameters.filter.semantic).
//
// Ranges are design ranges (what a sensible building uses), narrower than what validate() accepts.
// Only nodes listed here can become actions. Append only: a changed range or order is a new
// schemaVersion, because stored data sets depend on it.

#include "LoomProceduraRecipe.h"
#include <Engine/WeaverProcedura.h>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace Loom::WeaverProceduraRecipe{

constexpr uint32_t actionSchemaVersion = 1;

struct ParamSpec{
    enum class Kind{ Float, Int, Bool, Enum, Vec2, Vec3 };
    std::string name;
    Kind kind = Kind::Float;
    double min = 0.0, max = 0.0, step = 1.0;   // Float/Int/Vec: every component in [min, max] on the step grid
    std::vector<std::string> options;           // Enum
};

struct NodeSpec{
    std::string type;
    std::vector<ParamSpec> params;
    std::vector<std::string> inputs;            // port types of input 0, 1, ...
    std::string output;                         // port type of output 0
};

inline std::vector<std::string> withEmpty(const std::vector<std::string>& names){
    std::vector<std::string> result{""};
    result.insert(result.end(), names.begin(), names.end());
    return result;
}

inline const std::vector<NodeSpec>& nodeSchemas(){
    namespace Proc = Engine::WeaverProcedura;
    using K = ParamSpec::Kind;
    auto real = [](const char* n, double lo, double hi, double step){ return ParamSpec{n, K::Float, lo, hi, step, {}}; };
    auto integer = [](const char* n, double lo, double hi){ return ParamSpec{n, K::Int, lo, hi, 1.0, {}}; };
    auto flag = [](const char* n){ return ParamSpec{n, K::Bool, 0, 1, 1, {}}; };
    auto choice = [](const char* n, std::vector<std::string> o){ return ParamSpec{n, K::Enum, 0, 0, 1, std::move(o)}; };
    static const std::vector<NodeSpec> schemas = {
        {"footprint", {choice("shape", footprintShapeNames()), real("width", 5, 30, 0.1), real("depth", 5, 24, 0.1),
                       real("wing_width", 3, 12, 0.1), ParamSpec{"center", K::Vec2, -50, 50, 0.1, {}},
                       real("rotation_degrees", 0, 355, 5)}, {}, "Footprint"},
        {"floor_stack", {integer("floors", 1, 4), real("floor_height", 2.5, 4.0, 0.05), real("elevation", 0, 1.5, 0.05)},
                        {"Footprint"}, "Footprint"},
        {"room_split", {choice("program", interiorProgramNames()), integer("seed", 1, 999999), real("corridor_width", 1.0, 2.0, 0.05),
                        real("door_width", 0.8, 1.2, 0.05), integer("entrance_edge", 0, 7)}, {"Footprint"}, "Footprint"},
        {"walls", {real("thickness", 0.15, 0.5, 0.01), flag("windows"), real("window_width", 0.6, 2.4, 0.05),
                   real("window_height", 0.6, 2.2, 0.05), real("sill_height", 0.3, 1.2, 0.05), real("window_spacing", 1.5, 6.0, 0.1),
                   flag("door"), integer("door_edge", 0, 7), real("door_width", 0.8, 2.0, 0.05), real("door_height", 2.0, 2.8, 0.05)},
                  {"Footprint"}, "Mesh"},
        {"slab", {real("thickness", 0.1, 0.4, 0.01), real("inset", 0, 0.3, 0.01), flag("top_ceiling"), flag("foundation")},
                 {"Footprint"}, "Mesh"},
        {"roof", {choice("roof_type", roofTypeNames()), real("pitch_degrees", 5, 60, 1), real("overhang", 0, 1.2, 0.05),
                  real("thickness", 0.1, 0.5, 0.01), real("parapet_height", 0, 1.2, 0.05)}, {"Footprint"}, "Mesh"},
        {"interior", {real("partition_thickness", 0.08, 0.2, 0.01), flag("door_leaves"), flag("floor_finish"), flag("stairs")},
                     {"Footprint"}, "Mesh"},
        {"furnish", {integer("seed", 1, 999999), real("wall_thickness", 0.15, 0.5, 0.01), real("partition_thickness", 0.08, 0.2, 0.01),
                     real("fill", 0, 1, 0.05), choice("style", withEmpty(Proc::styleNames()))}, {"Footprint"}, "Placements"},
        {"place_assets", {}, {"Placements"}, "Mesh"},
        {"merge", {}, std::vector<std::string>(Proc::mergeInputCount, "Mesh"), "Mesh"},
        {"set_material", {choice("material", Proc::materialLibrary()), choice("filter.semantic", withEmpty(Proc::semanticVocabulary())),
                          flag("filter.use_direction"), ParamSpec{"filter.direction", K::Vec3, -1, 1, 1, {}},
                          real("filter.max_angle_degrees", 0, 90, 5)}, {"Mesh"}, "Mesh"},
        {"uv_project", {real("tile_size", 0.1, 5.0, 0.05)}, {"Mesh"}, "Mesh"},
    };
    return schemas;
}

inline const NodeSpec* findSchema(const std::string& type){
    for(const NodeSpec& spec : nodeSchemas()) if(spec.type == type) return &spec;
    return nullptr;
}

// Index of value on the parameter's grid, or -1 when it is outside the range or off the grid.
inline long long gridIndex(const ParamSpec& spec, double value){
    if(!std::isfinite(value) || value < spec.min - 1e-6 || value > spec.max + 1e-6) return -1;
    const double steps = (value - spec.min) / spec.step;
    const long long index = std::llround(steps);
    if(std::abs(steps - double(index)) > 1e-3) return -1;
    return index;
}

inline std::string schemaJson(){
    using K = ParamSpec::Kind;
    std::ostringstream out;
    out.precision(9);
    out << "{\"format\":\"loom.agentofweavers.schema\",\"action_schema_version\":" << actionSchemaVersion
        << ",\"recipe_schema_version\":" << Engine::WeaverProcedura::graphSchemaVersion << ",\"actions\":[\"ADD\",\"SET\",\"CONNECT\",\"END\"],\"nodes\":[";
    for(std::size_t n = 0; n < nodeSchemas().size(); ++n){
        const NodeSpec& spec = nodeSchemas()[n];
        out << (n ? "," : "") << "{\"type\":" << agentJsonEscape(spec.type) << ",\"output\":" << agentJsonEscape(spec.output) << ",\"inputs\":[";
        for(std::size_t i = 0; i < spec.inputs.size(); ++i) out << (i ? "," : "") << agentJsonEscape(spec.inputs[i]);
        out << "],\"params\":[";
        for(std::size_t p = 0; p < spec.params.size(); ++p){
            const ParamSpec& param = spec.params[p];
            static const char* kinds[] = {"float", "int", "bool", "enum", "vec2", "vec3"};
            out << (p ? "," : "") << "{\"name\":" << agentJsonEscape(param.name) << ",\"kind\":\"" << kinds[int(param.kind)] << '"';
            if(param.kind == K::Enum){
                out << ",\"options\":[";
                for(std::size_t o = 0; o < param.options.size(); ++o) out << (o ? "," : "") << agentJsonEscape(param.options[o]);
                out << ']';
            }else if(param.kind != K::Bool) out << ",\"min\":" << param.min << ",\"max\":" << param.max << ",\"step\":" << param.step;
            out << '}';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

} // namespace Loom::WeaverProceduraRecipe
