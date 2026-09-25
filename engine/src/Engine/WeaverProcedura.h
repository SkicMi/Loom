#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::WeaverProcedura{

// Version the graph data independently from the editor and generated geometry.
constexpr uint32_t graphSchemaVersion = 1;
using NodeId = uint64_t;

struct Node{
    NodeId id = 0;
    std::string typeId;
    float editorX = 0.0f;
    float editorY = 0.0f;
};

struct Link{
    NodeId from = 0;
    uint32_t fromPort = 0;
    NodeId to = 0;
    uint32_t toPort = 0;
};

// UI-independent recipe model. Serialization, evaluation, and geometry types
// will be added with the first working generator.
struct Graph{
    uint32_t schemaVersion = graphSchemaVersion;
    uint64_t seed = 1;
    std::vector<Node> nodes;
    std::vector<Link> links;
};

struct ValidationResult{
    bool valid = true;
    std::string error;

    explicit operator bool() const {return valid;}
};

// Validates stable node IDs, connection endpoints, and the DAG execution model.
ValidationResult validate(const Graph& graph);

}
