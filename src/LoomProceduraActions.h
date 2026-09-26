#pragma once

// AgentOfWeavers: a Recipe as the action sequence a model writes, and back.
//
//   ADD <type>                 new node; nodes are numbered 0, 1, ... in the order they are added
//   SET <param> <value>        every parameter of the node, in schema order, on the schema's grid
//   CONNECT <node> <port>      output of an earlier node into input <port> of this node, ports ascending
//   END                        the last node added is the recipe's single output
//
// Values: numbers as text, bools true/false, enum names ("-" for the empty option), vectors as
// comma-separated numbers. Canonical form: nodes in topological order (smallest recipe id first),
// all parameters written, so one recipe has exactly one action sequence.
//
// actionsToDocument checks every step against the schema (the same rules a model's decoder masks)
// and reports the first illegal step, so a generated sequence either becomes a valid Recipe or
// says exactly where and why it is not one.

#include "LoomProceduraRecipe.h"
#include "LoomProceduraSchema.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace Loom::WeaverProceduraRecipe{

inline std::string formatGridValue(const ParamSpec& spec, long long index){
    const double value = spec.min + double(index) * spec.step;
    std::ostringstream out;
    out.precision(10);
    // Round to the step's decimals so 0.1 grids print 12.4, not 12.400000001.
    int decimals = 0;
    for(double s = spec.step; decimals < 6 && std::abs(s - std::round(s)) > 1e-9; s *= 10.0) ++decimals;
    out << std::fixed;
    out.precision(decimals);
    out << value;
    return out.str();
}

inline const AgentJsonValue* jsonAtPath(const AgentJsonValue& object, const std::string& path){
    const AgentJsonValue* at = &object;
    std::size_t start = 0;
    while(at){
        const std::size_t dot = path.find('.', start);
        at = at->get(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start).c_str());
        if(dot == std::string::npos) return at;
        start = dot + 1;
    }
    return nullptr;
}

// One parameter value as action text; false when it is not representable on the schema.
inline bool encodeValue(const ParamSpec& spec, const AgentJsonValue& value, std::string& text, std::string& error){
    using K = ParamSpec::Kind;
    auto number = [&](const AgentJsonValue& v, std::string& out){
        if(v.kind != AgentJsonValue::Kind::Number){ error = spec.name + " is not a number"; return false; }
        const long long index = gridIndex(spec, v.number);
        if(index < 0){ error = spec.name + " = " + std::to_string(v.number) + " is outside the schema range or grid"; return false; }
        out = spec.kind == K::Int ? std::to_string((long long)std::llround(spec.min) + index) : formatGridValue(spec, index);
        return true;
    };
    switch(spec.kind){
        case K::Float: case K::Int: return number(value, text);
        case K::Bool:
            if(value.kind != AgentJsonValue::Kind::Boolean){ error = spec.name + " is not a boolean"; return false; }
            text = value.boolean ? "true" : "false";
            return true;
        case K::Enum:{
            if(value.kind != AgentJsonValue::Kind::String){ error = spec.name + " is not a name"; return false; }
            if(std::find(spec.options.begin(), spec.options.end(), value.string) == spec.options.end()){
                error = spec.name + " = " + value.string + " is not a schema option"; return false;
            }
            text = value.string.empty() ? "-" : value.string;
            return true;
        }
        case K::Vec2: case K::Vec3:{
            const std::size_t count = spec.kind == K::Vec2 ? 2 : 3;
            if(value.kind != AgentJsonValue::Kind::Array || value.array.size() != count){ error = spec.name + " has the wrong length"; return false; }
            text.clear();
            for(std::size_t c = 0; c < count; ++c){
                std::string part;
                if(!number(value.array[c], part)) return false;
                text += (c ? "," : "") + part;
            }
            return true;
        }
    }
    return false;
}

inline bool recipeToActions(const Document& document, std::vector<std::string>& actions, std::string& error){
    actions.clear();
    AgentJsonValue root;
    try{ root = AgentJsonParser(serialize(document)).parse(); }
    catch(const std::exception& problem){ error = problem.what(); return false; }
    const AgentJsonValue& nodes = *root.get("nodes");
    const AgentJsonValue& links = *root.get("links");
    struct Entry{ uint64_t id; const AgentJsonValue* parameters; };
    std::vector<Entry> entries;
    for(const AgentJsonValue& node : nodes.array) entries.push_back({uint64_t(node.get("id")->number), node.get("parameters")});
    std::map<uint64_t, std::size_t> incoming;
    std::map<uint64_t, std::vector<uint64_t>> outgoing;
    for(const Entry& entry : entries) incoming[entry.id] = 0;
    for(const AgentJsonValue& link : links.array){
        if(link.get("from_port")->number != 0){ error = "only output port 0 exists"; return false; }
        ++incoming[uint64_t(link.get("to")->number)];
        outgoing[uint64_t(link.get("from")->number)].push_back(uint64_t(link.get("to")->number));
    }
    // Topological order, smallest recipe id first among the ready nodes.
    std::set<uint64_t> ready;
    for(const auto& [id, count] : incoming) if(count == 0) ready.insert(id);
    std::map<uint64_t, std::size_t> order;
    while(!ready.empty()){
        const uint64_t id = *ready.begin();
        ready.erase(ready.begin());
        order.emplace(id, order.size());
        for(uint64_t next : outgoing[id]) if(--incoming[next] == 0) ready.insert(next);
    }
    if(order.size() != entries.size()){ error = "recipe has a cycle"; return false; }
    std::vector<const Entry*> sorted(entries.size());
    for(const Entry& entry : entries) sorted[order.at(entry.id)] = &entry;
    std::size_t terminals = 0;
    for(const Entry& entry : entries) terminals += outgoing[entry.id].empty() ? 1 : 0;
    if(terminals != 1 || !outgoing[sorted.back()->id].empty()){ error = "recipe needs exactly one output node, added last"; return false; }

    for(const Entry* entry : sorted){
        const std::string type = entry->parameters->get("type")->string;
        const NodeSpec* spec = findSchema(type);
        if(!spec){ error = "node type " + type + " is not in the action schema"; return false; }
        actions.push_back("ADD " + type);
        for(const ParamSpec& param : spec->params){
            const AgentJsonValue* value = jsonAtPath(*entry->parameters, param.name);
            if(!value){ error = type + "." + param.name + " is missing"; return false; }
            std::string text;
            if(!encodeValue(param, *value, text, error)){ error = type + "." + error; return false; }
            actions.push_back("SET " + param.name + " " + text);
        }
        std::vector<std::pair<uint32_t, std::size_t>> inputs;
        for(const AgentJsonValue& link : links.array)
            if(uint64_t(link.get("to")->number) == entry->id)
                inputs.push_back({uint32_t(link.get("to_port")->number), order.at(uint64_t(link.get("from")->number))});
        std::sort(inputs.begin(), inputs.end());
        for(const auto& [port, from] : inputs) actions.push_back("CONNECT " + std::to_string(from) + " " + std::to_string(port));
    }
    actions.push_back("END");
    return true;
}

// Builds the Recipe an action sequence describes. On failure failedStep is the index of the
// first illegal action (actions.size() when the sequence ends too early) and error says why.
inline bool actionsToDocument(const std::vector<std::string>& actions, const std::string& name, Document& document,
                              std::string& error, std::size_t& failedStep){
    using K = ParamSpec::Kind;
    struct Built{ const NodeSpec* spec; std::size_t nextParam = 0; std::vector<std::pair<std::string, std::string>> values;
                  std::vector<std::pair<std::size_t, uint32_t>> inputs; bool feeds = false; };
    std::vector<Built> nodes;
    auto fail = [&](std::size_t step, std::string why){ failedStep = step; error = std::move(why); return false; };
    auto complete = [&](std::size_t step) -> bool{
        if(nodes.empty()) return true;
        const Built& node = nodes.back();
        if(node.nextParam != node.spec->params.size())
            return fail(step, node.spec->type + " still needs " + node.spec->params[node.nextParam].name);
        if(node.spec->type == "merge"){
            if(node.inputs.empty()) return fail(step, "merge needs at least one input");
        }else if(node.inputs.size() != node.spec->inputs.size())
            return fail(step, node.spec->type + " needs input " + std::to_string(node.inputs.size()));
        return true;
    };
    bool ended = false;
    for(std::size_t step = 0; step < actions.size(); ++step){
        if(ended) return fail(step, "action after END");
        std::istringstream words(actions[step]);
        std::string verb;
        words >> verb;
        if(verb == "ADD"){
            std::string type;
            words >> type;
            if(!complete(step)) return false;
            const NodeSpec* spec = findSchema(type);
            if(!spec) return fail(step, "unknown node type " + type);
            if(nodes.size() >= 256) return fail(step, "too many nodes");
            nodes.push_back({spec});
        }else if(verb == "SET"){
            std::string param, value;
            words >> param >> value;
            if(nodes.empty()) return fail(step, "SET before ADD");
            Built& node = nodes.back();
            if(node.nextParam >= node.spec->params.size()) return fail(step, node.spec->type + " has no more parameters");
            const ParamSpec& spec = node.spec->params[node.nextParam];
            if(param != spec.name) return fail(step, "expected " + spec.name + ", got " + param);
            std::string json;
            if(spec.kind == K::Bool){
                if(value != "true" && value != "false") return fail(step, spec.name + " must be true or false");
                json = value;
            }else if(spec.kind == K::Enum){
                const std::string option = value == "-" ? std::string{} : value;
                if(std::find(spec.options.begin(), spec.options.end(), option) == spec.options.end())
                    return fail(step, value + " is not an option of " + spec.name);
                json = agentJsonEscape(option);
            }else{
                const std::size_t count = spec.kind == K::Vec2 ? 2 : spec.kind == K::Vec3 ? 3 : 1;
                std::vector<std::string> parts;
                std::stringstream split(value);
                for(std::string part; std::getline(split, part, ',');) parts.push_back(part);
                if(parts.size() != count) return fail(step, spec.name + " needs " + std::to_string(count) + " numbers");
                for(const std::string& part : parts){
                    char* end = nullptr;
                    const double number = std::strtod(part.c_str(), &end);
                    if(part.empty() || *end != '\0' || gridIndex(spec, number) < 0)
                        return fail(step, spec.name + " = " + part + " is outside the range or grid");
                }
                json = count == 1 ? parts[0] : "[" + value + "]";
            }
            node.values.push_back({spec.name, json});
            ++node.nextParam;
        }else if(verb == "CONNECT"){
            long long from = -1, port = -1;
            words >> from >> port;
            if(nodes.empty()) return fail(step, "CONNECT before ADD");
            Built& node = nodes.back();
            if(node.nextParam != node.spec->params.size()) return fail(step, "CONNECT before every parameter is set");
            if(from < 0 || std::size_t(from) + 1 >= nodes.size()) return fail(step, "CONNECT from a node that is not earlier");
            if(port < 0 || std::size_t(port) >= node.spec->inputs.size()) return fail(step, node.spec->type + " has no input " + std::to_string(port));
            if(!node.inputs.empty() && node.inputs.back().second >= uint32_t(port)) return fail(step, "inputs must be connected in ascending order");
            if(nodes[std::size_t(from)].spec->output != node.spec->inputs[std::size_t(port)])
                return fail(step, nodes[std::size_t(from)].spec->output + " cannot go into a " + node.spec->inputs[std::size_t(port)] + " input");
            node.inputs.push_back({std::size_t(from), uint32_t(port)});
            nodes[std::size_t(from)].feeds = true;
        }else if(verb == "END"){
            if(nodes.empty()) return fail(step, "END without nodes");
            if(!complete(step)) return false;
            for(std::size_t n = 0; n + 1 < nodes.size(); ++n)
                if(!nodes[n].feeds) return fail(step, "node " + std::to_string(n) + " (" + nodes[n].spec->type + ") is not used");
            ended = true;
        }else return fail(step, "unknown action " + verb);
    }
    if(!ended) return fail(actions.size(), "the sequence ends without END");

    std::ostringstream json;
    json << "{\"format\":\"loom.weaverprocedura.recipe\",\"schema_version\":" << Engine::WeaverProcedura::graphSchemaVersion
         << ",\"name\":" << agentJsonEscape(name) << ",\"seed\":1,\"nodes\":[";
    for(std::size_t n = 0; n < nodes.size(); ++n){
        json << (n ? "," : "") << "{\"id\":" << n + 1 << ",\"position\":[" << float(n) * 240.0f << ",100],\"parameters\":{\"type\":"
             << agentJsonEscape(nodes[n].spec->type);
        // Dotted names become one nested object per prefix, in schema order.
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> nested;
        std::vector<std::string> nestedOrder;
        for(const auto& [key, value] : nodes[n].values){
            const std::size_t dot = key.find('.');
            if(dot == std::string::npos){ json << ',' << agentJsonEscape(key) << ':' << value; continue; }
            const std::string head = key.substr(0, dot);
            if(!nested.count(head)) nestedOrder.push_back(head);
            nested[head].push_back({key.substr(dot + 1), value});
        }
        for(const std::string& head : nestedOrder){
            json << ',' << agentJsonEscape(head) << ":{";
            for(std::size_t k = 0; k < nested[head].size(); ++k)
                json << (k ? "," : "") << agentJsonEscape(nested[head][k].first) << ':' << nested[head][k].second;
            json << '}';
        }
        json << "}}";
    }
    json << "],\"links\":[";
    bool first = true;
    for(std::size_t n = 0; n < nodes.size(); ++n)
        for(const auto& [from, port] : nodes[n].inputs){
            json << (first ? "" : ",") << "{\"from\":" << from + 1 << ",\"from_port\":0,\"to\":" << n + 1 << ",\"to_port\":" << port << '}';
            first = false;
        }
    json << "]}";
    try{ document = parse(json.str()); }
    catch(const std::exception& problem){ return fail(actions.size() - 1, std::string("recipe is not valid: ") + problem.what()); }
    return true;
}

} // namespace Loom::WeaverProceduraRecipe
