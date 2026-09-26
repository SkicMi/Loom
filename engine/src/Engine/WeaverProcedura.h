#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Engine::WeaverProcedura{

// Recipe files carry this schema version. Increment it when serialized node
// payloads or port meanings change.
constexpr uint32_t graphSchemaVersion = 4;
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

// A regular XZ lattice. rows/columns describe cells, so it contains
// (cellsZ + 1) * (cellsX + 1) editable points.
struct GridNode{
    float width = 8.0f;
    float depth = 8.0f;
    uint32_t cellsX = 4;
    uint32_t cellsZ = 4;
};

// Sets the absolute Y coordinate of one point in an incoming grid.
// Several nodes can be chained to raise multiple points before meshing.
struct SetGridPointHeightNode{
    uint32_t column = 0;
    uint32_t row = 0;
    float height = 1.0f;
};

struct GridToMeshNode{};

// A small blockout generator: a central corridor with equal room bays on both
// sides, floor slab, perimeter walls, room dividers, and door openings.
struct InteriorBlockoutNode{
    uint32_t roomsPerSide = 2;
    float roomWidth = 4.0f;
    float roomDepth = 4.0f;
    float corridorWidth = 2.0f;
    float wallHeight = 3.0f;
    float wallThickness = 0.15f;
    float floorThickness = 0.12f;
    float doorWidth = 0.9f;
};

enum class PrimitiveType : uint8_t{ Cube, Plane, Sphere, Pyramid, Capsule };

struct AddPrimitiveNode{
    PrimitiveType primitive = PrimitiveType::Cube;
    glm::vec3 size{1.0f};
};

struct MoveNode{
    glm::vec3 offset{0.0f};
};

struct RotateNode{
    glm::vec3 degrees{0.0f};
    glm::vec3 pivot{0.0f};
};

struct ScaleNode{
    glm::vec3 factor{1.0f};
    glm::vec3 pivot{0.0f};
};

// faceIndex selects a triangle as the seed for its connected coplanar face.
struct ExtrudeNode{
    uint32_t faceIndex = 0;
    float distance = 1.0f;
};

// Inset each planar face and create a segmented chamfer around its boundary.
struct BevelNode{
    float amount = 0.1f;
    uint32_t segments = 1;
};

struct MeshToPointNode{};

struct PointFromMeshNode{
    uint32_t count = 100;
    uint64_t seed = 1;
};

using NodePayload = std::variant<std::monostate, CurveNode, RectangleProfileNode, SweepNode,
                                 GridNode, SetGridPointHeightNode, GridToMeshNode,
                                 InteriorBlockoutNode, AddPrimitiveNode, MoveNode, RotateNode,
                                 ScaleNode, ExtrudeNode, BevelNode, MeshToPointNode,
                                 PointFromMeshNode>;

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
    std::vector<glm::vec3> points;
    bool pointCloudOutput = false;

    EvaluationResult() = default;
    EvaluationResult(bool didSucceed, NodeId output, MeshData outputMesh, std::string message)
        : succeeded(didSucceed), outputNode(output), mesh(std::move(outputMesh)), error(std::move(message)){}
};

struct PointGrid{
    uint32_t cellsX = 0;
    uint32_t cellsZ = 0;
    std::vector<glm::vec3> points;
};

NodeId addNode(Graph& graph, NodePayload payload, float editorX = 0.0f, float editorY = 0.0f);
ValidationResult validate(const Graph& graph);
EvaluationResult evaluate(const Graph& graph);

bool makeGrid(const GridNode& settings, PointGrid& output, std::string& error);
bool gridToMesh(const PointGrid& grid, MeshData& output, std::string& error,
                std::size_t maxVertices = 1'000'000);
bool makeInteriorBlockout(const InteriorBlockoutNode& settings, MeshData& output,
                          std::string& error, std::size_t maxVertices = 1'000'000);
bool makePrimitive(const AddPrimitiveNode& settings, MeshData& output, std::string& error,
                   std::size_t maxVertices = 1'000'000);
bool moveMesh(const MeshData& input, const MoveNode& settings, MeshData& output, std::string& error);
bool rotateMesh(const MeshData& input, const RotateNode& settings, MeshData& output, std::string& error);
bool scaleMesh(const MeshData& input, const ScaleNode& settings, MeshData& output, std::string& error);
bool extrudeFace(const MeshData& input, const ExtrudeNode& settings, MeshData& output,
                 std::string& error, std::size_t maxVertices = 1'000'000);
bool bevelMesh(const MeshData& input, const BevelNode& settings, MeshData& output,
               std::string& error, std::size_t maxVertices = 1'000'000);
bool meshToPoints(const MeshData& input, std::vector<glm::vec3>& output, std::string& error,
                  std::size_t maxPoints = 1'000'000);
bool pointsFromMesh(const MeshData& input, uint32_t count, uint64_t seed,
                    std::vector<glm::vec3>& output, std::string& error,
                    std::size_t maxPoints = 100'000);
bool makePointPreview(const std::vector<glm::vec3>& points, MeshData& output, std::string& error,
                      std::size_t maxDisplayedPoints = 20'000, float markerSize = 0.06f,
                      std::size_t maxVertices = 1'000'000);

Profile makeRectangleProfile(float width, float height);
Profile makeCircularProfile(float radius, uint32_t sides = 12);

// Sweeps a closed 2D profile along a 3D polyline using parallel-transport frames.
// On failure, output is left unchanged and error receives a short explanation.
bool sweep(const Curve& curve, const Profile& profile, MeshData& output, std::string& error,
           SweepSettings settings = {});

}
