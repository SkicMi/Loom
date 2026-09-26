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
constexpr uint32_t graphSchemaVersion = 6;
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

// Closed vocabularies. A triangle stores the index + 1 into these lists (0 = none), so
// merged meshes need no remapping and a model can only choose names that exist.
// Append only: existing positions are stable IDs in saved data and training sets.
const std::vector<std::string>& semanticVocabulary();
const std::vector<std::string>& materialLibrary();
uint16_t semanticId(const std::string& name);  // 0 when the name is unknown or empty
uint16_t materialId(const std::string& name);
std::string semanticName(uint16_t id);
std::string materialName(uint16_t id);

// Provenance and look of one triangle. createdBy is the Recipe node that made the
// triangle; the evaluator fills it for every triangle a node adds.
struct TriangleAttributes{
    NodeId createdBy = 0;
    uint16_t semantic = 0;
    uint16_t material = 0;
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
    std::vector<TriangleAttributes> triangles;  // empty, or one entry per triangle

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

struct CircleProfileNode{
    float radius = 0.05f;
    uint32_t sides = 12;
};

struct SweepNode{
    SweepSettings settings;
};

// Curve -> Curve: uniform Catmull-Rom through every control point, so corners
// round off while the path still passes through the points the user placed.
struct CurveSmoothNode{
    uint32_t subdivisions = 8;   // samples per control segment, 1..32
};

// A hanging rope or chain between two points. sag is the drop below the straight
// chord at mid-span in meters; the shape is an exact catenary for level ends.
struct CatenaryCurveNode{
    glm::vec3 start{0.0f, 2.0f, 0.0f};
    glm::vec3 end{4.0f, 2.0f, 0.0f};
    float sag = 0.5f;
    uint32_t samples = 32;
};

// Mesh + Curve -> Mesh: copies spaced along the curve by arc length. The instance's
// +X follows the tangent and +Y stays toward referenceUp. Odd copies add
// alternateRollDegrees around the tangent (90 makes a chain).
struct CopyAlongCurveNode{
    float spacing = 0.5f;
    float startOffset = 0.0f;
    float rollDegrees = 0.0f;
    float alternateRollDegrees = 0.0f;
    glm::vec3 referenceUp{0.0f, 1.0f, 0.0f};
    uint32_t maxCopies = 10000;
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

enum class PrimitiveType : uint8_t{ Cube, Plane, Sphere, Pyramid, Capsule, Cylinder, Torus };

// Every primitive fits a unit box before size scales it. Cylinder: axis Y, radius 0.5.
// Torus: ring in XZ with outer radius 0.5; tubeRatio is the tube radius as a fraction of it.
struct AddPrimitiveNode{
    PrimitiveType primitive = PrimitiveType::Cube;
    glm::vec3 size{1.0f};
    float tubeRatio = 0.25f;
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

// Chooses triangles by semantic name and/or facing direction. An empty semantic
// and useDirection == false select every triangle.
struct TriangleFilter{
    std::string semantic;
    bool useDirection = false;
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float maxAngleDegrees = 30.0f;
};

// faceIndex selects a triangle as the seed for its connected coplanar face. With
// useFilter, every flat face whose triangles match the filter moves along its own normal.
struct ExtrudeNode{
    uint32_t faceIndex = 0;
    float distance = 1.0f;
    bool useFilter = false;
    TriangleFilter filter;
};

// Inset each planar face and create a segmented chamfer around its boundary.
struct BevelNode{
    float amount = 0.1f;
    uint32_t segments = 1;
};

// Joins up to mergeInputCount meshes; any subset of the inputs may be connected.
constexpr uint32_t mergeInputCount = 8;
struct MergeNode{};

struct SetSemanticNode{
    std::string semantic = "wall_exterior";
    TriangleFilter filter;
};

struct SetMaterialNode{
    std::string material = "plaster";
    TriangleFilter filter;
};

// Averages normals across edges whose faces meet at less than angleDegrees.
struct SmoothNormalsNode{
    float angleDegrees = 30.0f;
};

// Box projection in world meters: each triangle uses the plane of its dominant
// normal axis, so textures tile at tileSize meters on every wall and floor.
struct UVProjectNode{
    float tileSize = 1.0f;
};

// Places a copy of the mesh on input 0 at every point from input 1.
struct CopyToPointsNode{
    bool alignToNormal = false;
    float scale = 1.0f;
    float randomYawDegrees = 0.0f;   // 0..180, per copy around the point up axis
    float randomScale = 0.0f;        // 0..0.9, uniform scale jitter as a fraction
    uint64_t seed = 1;
    uint32_t maxCopies = 10000;
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
                                 PointFromMeshNode, CircleProfileNode, MergeNode, SetSemanticNode,
                                 SetMaterialNode, SmoothNormalsNode, UVProjectNode, CopyToPointsNode,
                                 CurveSmoothNode, CatenaryCurveNode, CopyAlongCurveNode>;

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
    std::vector<glm::vec3> pointNormals;  // same length as points
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
// Number of input ports a node has (Merge 8, Sweep and the Copy nodes 2, sources 0).
uint32_t inputPortCount(const Node& node);
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
bool mergeMeshes(const std::vector<const MeshData*>& inputs, MeshData& output, std::string& error,
                 std::size_t maxVertices = 1'000'000);
bool validTriangleFilter(const TriangleFilter& filter);
bool triangleMatches(const MeshData& mesh, std::size_t triangle, const TriangleFilter& filter);
bool setSemantic(const MeshData& input, const SetSemanticNode& settings, MeshData& output, std::string& error);
bool setMaterial(const MeshData& input, const SetMaterialNode& settings, MeshData& output, std::string& error);
bool smoothNormals(const MeshData& input, float angleDegrees, MeshData& output, std::string& error);
bool projectUVs(const MeshData& input, float tileSize, MeshData& output, std::string& error);
bool copyToPoints(const MeshData& instance, const std::vector<glm::vec3>& points,
                  const std::vector<glm::vec3>& normals, const CopyToPointsNode& settings,
                  MeshData& output, std::string& error, std::size_t maxVertices = 2'000'000);
bool smoothCurve(const Curve& input, uint32_t subdivisions, Curve& output, std::string& error);
bool makeCatenary(const CatenaryCurveNode& settings, Curve& output, std::string& error);
bool copyAlongCurve(const MeshData& instance, const Curve& curve, const CopyAlongCurveNode& settings,
                    MeshData& output, std::string& error, std::size_t maxVertices = 2'000'000);
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
