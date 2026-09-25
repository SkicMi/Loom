#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Engine::WeaverProcedura{

// Graph payloads changed from the initial UI scaffold; there is no serialized v1 format yet.
constexpr uint32_t graphSchemaVersion = 2;
using NodeId = uint64_t;

struct Link{
    NodeId from = 0;
    uint32_t fromPort = 0;
    NodeId to = 0;
    uint32_t toPort = 0;
};

// Polyline path; every supplied control point is retained during resampling.
struct Curve{
    std::vector<glm::vec3> points;
    bool closed = false;
};

// A simple, closed polygon in (sideways, up) coordinates around the curve.
// Points follow counter-clockwise winding when viewed along the curve tangent.
struct Profile{
    std::vector<glm::vec2> points;
};

struct MeshVertex{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 uv{0.0f};
};

// CPU-side engine output. Renderer adapters split 32-bit indexed geometry into
// the GPU's supported chunks without making the generator depend on Vulkan.
struct MeshData{
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    bool empty() const {return vertices.empty() || indices.empty();}
};

struct SweepSettings{
    float sampleSpacing = 1.0f;            //maximum spacing; control points remain in the curve
    glm::vec3 referenceUp{0.0f, 1.0f, 0.0f};
    bool capEnds = true;                    //end caps require a convex profile
    std::size_t maxVertices = 1'000'000;
};

struct CurveNode{
    Curve curve;
};

struct RectangleProfileNode{
    float width = 1.2f;
    float height = 0.14f;
};

struct SweepNode{
    SweepSettings settings;
};

//The first evaluator slice is intentionally a closed, typed node set.
using NodePayload = std::variant<std::monostate, CurveNode, RectangleProfileNode, SweepNode>;

struct Node{
    NodeId id = 0;
    NodePayload payload;
    float editorX = 0.0f;
    float editorY = 0.0f;
};

struct Graph{
    uint32_t schemaVersion = graphSchemaVersion;
    uint64_t seed = 1;
    NodeId nextNodeId = 1;
    std::vector<Node> nodes;
    std::vector<Link> links;
};

struct ValidationResult{
    bool valid = true;
    std::string error;

    explicit operator bool() const {return valid;}
};

struct EvaluationResult{
    bool succeeded = false;
    NodeId outputNode = 0;
    MeshData mesh;
    std::string error;
};

NodeId addNode(Graph& graph, NodePayload payload, float editorX = 0.0f, float editorY = 0.0f);
ValidationResult validate(const Graph& graph);
EvaluationResult evaluate(const Graph& graph);

Profile makeRectangleProfile(float width, float height);
Profile makeCircularProfile(float radius, uint32_t sides = 12);

// Sweeps a closed 2D profile along a 3D polyline using parallel-transport frames.
// On failure, output is left unchanged and error receives a short explanation.
bool sweep(const Curve& curve, const Profile& profile, MeshData& output, std::string& error,
           SweepSettings settings = {});

}
