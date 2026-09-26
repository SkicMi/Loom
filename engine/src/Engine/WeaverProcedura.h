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
constexpr uint32_t graphSchemaVersion = 8;
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

// ---- Buildings and roads (schema 7) -------------------------------------------------
// Middle-level nodes: the model chooses a footprint, floors, walls with openings and a roof
// type instead of editing triangles. Openings are cut as wall panels, never by booleans.

// One rectangle of a footprint; roofs are built per part, ridge along the long side.
// joined marks sides that run into another part (bit 0: -axis end, 1: +axis end,
// 2: -across side, 3: +across side); the roof has no overhang or hip there, so its ridge
// runs on into the neighbour's roof.
struct FootprintPart{
    glm::vec2 center{0.0f};        // XZ
    glm::vec2 halfSize{1.0f};      // along axis, across axis
    glm::vec2 axis{1.0f, 0.0f};    // unit XZ direction of halfSize.x; across is (-axis.y, axis.x)
    uint8_t joined = 0;
};

// outline is the exterior wall line in XZ with positive area (x0*z1 - x1*z0 summed), so an
// edge (dx, dz) faces outward along (dz, -dx). Footprints traced from a curve have no parts.
// Interior plan (RoomSplit). Rooms are axis-aligned rectangles in the footprint's local frame
// (see Footprint::toWorld), one list for all floors.
enum class RoomType : uint8_t{ Hall, Corridor, Stairs, Living, Kitchen, Bedroom, Bathroom, Office, Meeting, Storage };
const std::vector<std::string>& roomTypeNames();   // "hall", "corridor", ... in enum order

struct LocalRect{
    glm::vec2 min{0.0f}, max{0.0f};   // local (x, z)
};

struct Room{
    uint32_t floor = 0;
    RoomType type = RoomType::Living;
    LocalRect rect;
};

// A door in the partition between two rooms of one floor; from/to are the door span's ends
// on their shared wall line (local).
struct InteriorDoor{
    uint32_t floor = 0;
    uint32_t roomA = 0, roomB = 0;   // indices into InteriorPlan::rooms
    glm::vec2 from{0.0f}, to{0.0f};
};

struct InteriorPlan;
// The room a door's leaf swings into: the room that is not open space, but never a bathroom.
std::size_t doorLeafRoom(const InteriorPlan& plan, const InteriorDoor& door);

struct InteriorPlan{
    std::vector<Room> rooms;
    std::vector<InteriorDoor> doors;
    bool hasStairs = false;
    LocalRect stairCore;               // same rectangle on every floor
    bool stairsAlongX = true;          // flights run along local x (else z)
    uint32_t entranceEdge = 0;         // outline edge of the front door
    float entranceCenter = 0.0f;       // meters along that edge from its first corner
};

struct Footprint{
    std::vector<glm::vec2> outline;
    std::vector<FootprintPart> parts;
    float elevation = 0.0f;        // Y of the ground floor
    uint32_t floors = 1;
    float floorHeight = 3.0f;
    // Local frame of rectangle footprints: world = frameCenter + frameAxis * x + across * z,
    // across = (-frameAxis.y, frameAxis.x). zones tile the outline in that frame.
    glm::vec2 frameCenter{0.0f};
    glm::vec2 frameAxis{1.0f, 0.0f};
    std::vector<LocalRect> zones;
    bool hasPlan = false;
    InteriorPlan plan;

    glm::vec2 toWorld(const glm::vec2& local) const{
        return frameCenter + frameAxis * local.x + glm::vec2(-frameAxis.y, frameAxis.x) * local.y;
    }
};

enum class FootprintShape : uint8_t{ Rectangle, LShape, UShape };

// width along X and depth along Z before rotation. L and U: a back bar along X that is
// wingWidth deep, with arms wingWidth wide running to +Z (L: left arm, U: both arms).
struct FootprintNode{
    FootprintShape shape = FootprintShape::Rectangle;
    float width = 10.0f;
    float depth = 8.0f;
    float wingWidth = 4.0f;
    glm::vec2 center{0.0f};
    float rotationDegrees = 0.0f;
};

// Closed Curve -> Footprint from the XZ of its control points. A right-angled outline (or any
// outline with rectify, which snaps a sketch to right angles along its longest edge) is split
// into rectangles, so it takes pitched roofs and RoomSplit; any other outline a flat roof only.
struct FootprintFromCurveNode{
    bool rectify = false;
};

// Footprint -> Footprint: floor count, storey height and the ground floor's height.
struct FloorStackNode{
    uint32_t floors = 2;
    float floorHeight = 3.0f;
    float elevation = 0.0f;
};

// Footprint -> Mesh: exterior walls of every floor. Windows are spaced evenly on each edge;
// the door sits in the middle of outline edge doorEdge (mod edge count) on the ground floor.
struct WallsNode{
    float thickness = 0.25f;
    bool windows = true;
    float windowWidth = 1.2f;
    float windowHeight = 1.4f;
    float sillHeight = 0.9f;
    float windowSpacing = 3.0f;    // target distance between window centers
    bool door = true;
    uint32_t doorEdge = 0;
    float doorWidth = 1.0f;
    float doorHeight = 2.2f;
};

// Footprint -> Mesh: a floor slab at every storey, inset so its edges hide in the walls;
// optionally the top floor's ceiling (under a pitched roof) and a plinth from Y 0 up to
// the elevation.
struct SlabNode{
    float thickness = 0.2f;
    float inset = 0.1f;
    bool topCeiling = false;
    bool foundation = true;
};

enum class RoofType : uint8_t{ Flat, Gable, Hip, Shed };

// Footprint -> Mesh on top of the last floor. Gable, hip and shed are built per footprint
// part; flat covers the outline and fits every footprint.
struct RoofNode{
    RoofType type = RoofType::Gable;
    float pitchDegrees = 35.0f;
    float overhang = 0.4f;
    float thickness = 0.2f;        // flat roof slab
    float parapetHeight = 0.0f;    // flat roof only; 0 = none
};

enum class InteriorProgram : uint8_t{ Residential, Office };

// Footprint -> Footprint: plans rooms, corridors, the entrance hall, a staircase for more
// than one floor, and a door into every room. Rules, not a drawing: the same settings give
// a fitting plan for any rectangle, L or U footprint. Needs a footprint built from rectangles.
struct RoomSplitNode{
    InteriorProgram program = InteriorProgram::Residential;
    uint64_t seed = 1;                 // which side corridors and stairs take, room order and size jitter
    float corridorWidth = 1.3f;
    float doorWidth = 0.9f;
    uint32_t entranceEdge = 0;         // outline edge of the front door (mod edge count)
};

// Footprint with a plan -> Mesh: partition walls with door openings, door leaves, a floor
// finish per room and the staircase with its railing.
struct InteriorNode{
    float partitionThickness = 0.12f;
    bool doorLeaves = true;
    bool floorFinish = true;
    bool stairs = true;
};

// Straight solid stairs from the origin along +Z, rising in +Y.
struct StairsNode{
    float width = 1.2f;
    float totalRise = 3.0f;
    uint32_t steps = 16;
    float treadDepth = 0.28f;
    bool railing = true;
};

// Curve -> Mesh: road surface on the curve, curbs and sidewalks on both sides.
struct RoadFromCurveNode{
    float roadWidth = 6.0f;
    bool sidewalks = true;
    float sidewalkWidth = 1.8f;
    float curbHeight = 0.15f;
    float sampleSpacing = 1.0f;
};

// ---- Furniture and props (schema 8) ------------------------------------------------------
// An asset is its own procedural recipe with named parameters (a bed of any width), kept
// apart from the house recipes: the library builds it, Furnish only decides where it goes.
// Asset space: base centred on the origin, width along X, height up Y, front toward +Z, so
// the back (z = -depth / 2) stands against a wall.

struct AssetParameter{
    std::string name;
    float defaultValue = 0.0f, minValue = 0.0f, maxValue = 0.0f;
};

enum class AssetPlacement : uint8_t{ Wall, Center, Corner };

// Furniture styles, a closed list like the materials (append only). Furnish gives a whole house
// one style; a category without an asset in that style falls back to "basic".
const std::vector<std::string>& styleNames();   // "basic", "modern", "rustic"

struct AssetInfo{
    std::string id;
    std::string category;              // bed, wardrobe, sofa, ... (see furnitureCategories)
    std::string style = "basic";       // one of styleNames()
    std::vector<AssetParameter> parameters;
    AssetPlacement placement = AssetPlacement::Wall;
    float clearanceFront = 0.0f;       // free floor the asset needs in front of it
};

// Parameter values by name; parameters that are not listed keep their default.
using AssetParameters = std::vector<std::pair<std::string, float>>;

// Where a hand holds an asset, in asset space; the same fields as Warp's Grip, so a generated
// tool goes straight into Loom's hold. axis runs along the handle from the little finger toward the
// thumb (toward the head or blade), palm from the handle toward the palm.
struct AssetGrip{
    std::string name = "Main";
    glm::vec3 point{0.0f};
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    glm::vec3 palm{1.0f, 0.0f, 0.0f};
    float thickness = 0.0f;            // handle radius
    std::string preset = "grip";       // finger pose: grip, pistol, cup, fist, point, relaxed, open
    int hand = 0;                      // 0 either, 1 right, 2 left
};

// Every asset category, a closed list (append only), and its kind: furniture, tool, weapon or prop.
// Tools and weapons are held, so their assets must carry a grip. Tool space: the handle's end on
// the floor (y = 0), the head or blade up +Y, the striking edge or face toward +Z.
const std::vector<std::string>& assetCategories();
std::string assetKind(const std::string& category);    // empty for an unknown category

class AssetLibrary{
public:
    virtual ~AssetLibrary() = default;
    virtual std::vector<std::string> ids() const = 0;                      // sorted
    virtual const AssetInfo* info(const std::string& id) const = 0;        // nullptr when unknown
    // Box of the asset for these parameters: x width, y height, z depth.
    virtual bool bounds(const std::string& id, const AssetParameters& parameters, glm::vec3& size,
                        std::string& error) const = 0;
    virtual bool build(const std::string& id, const AssetParameters& parameters, MeshData& output,
                       std::string& error) const = 0;
    // Grips for these parameters; none for most furniture.
    virtual bool grips(const std::string&, const AssetParameters&, std::vector<AssetGrip>& output, std::string&) const{
        output.clear();
        return true;
    }
};

// Categories Furnish asks for; a library may hold more (tools, props).
const std::vector<std::string>& furnitureCategories();
std::vector<std::string> assetsInCategory(const AssetLibrary& library, const std::string& category);

// One piece of furniture: which asset with which parameters, where and turned how. A layout is
// data without geometry, so a data set can store it and a model can learn to edit it.
struct Placement{
    std::string asset;
    AssetParameters parameters;
    glm::vec3 position{0.0f};          // world position of the asset's base centre
    float yawDegrees = 0.0f;           // about +Y; 0 keeps the front toward +Z
    uint32_t floor = 0;
    uint32_t room = 0;                 // index into the plan's rooms
    LocalRect area;                    // floor the asset covers, in the footprint's local frame
    float base = 0.0f;                 // bottom above the floor: a lamp on a night stand, a wall cabinet
    float height = 0.0f;
    int32_t under = -1;                // a chair pushed under this placement (index), else -1
};

// Footprint with a plan -> Placements: furniture for every room by its type (bedroom: bed
// against the wall farthest from the door with night stands, a wardrobe; living room: sofa
// facing a TV, a coffee table; kitchen: a counter run and a fridge; bathroom: toilet, basin,
// bath or shower; ...). Nothing blocks a door, tall pieces keep off window walls. The wall
// thicknesses should match the Walls and Interior nodes, so pieces stand at the wall faces.
struct FurnishNode{
    uint64_t seed = 1;
    float wallThickness = 0.25f;
    float partitionThickness = 0.12f;
    float fill = 1.0f;                 // 0..1: share of the optional pieces (armchair, desk, shelves)
    std::string style;                 // one of styleNames(); empty: chosen by seed
};

// Placements -> Mesh: builds every (asset, parameters) once and places its copies.
struct PlaceAssetsNode{};

// A single asset as a mesh at the origin (a prop or tool on its own).
struct AssetNode{
    std::string asset;
    AssetParameters parameters;
};

using NodePayload = std::variant<std::monostate, CurveNode, RectangleProfileNode, SweepNode,
                                 GridNode, SetGridPointHeightNode, GridToMeshNode,
                                 InteriorBlockoutNode, AddPrimitiveNode, MoveNode, RotateNode,
                                 ScaleNode, ExtrudeNode, BevelNode, MeshToPointNode,
                                 PointFromMeshNode, CircleProfileNode, MergeNode, SetSemanticNode,
                                 SetMaterialNode, SmoothNormalsNode, UVProjectNode, CopyToPointsNode,
                                 CurveSmoothNode, CatenaryCurveNode, CopyAlongCurveNode,
                                 FootprintNode, FootprintFromCurveNode, FloorStackNode, WallsNode,
                                 SlabNode, RoofNode, StairsNode, RoadFromCurveNode, RoomSplitNode,
                                 InteriorNode, FurnishNode, PlaceAssetsNode, AssetNode>;

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
    std::vector<Placement> placements;    // every Furnish node's layout, in graph order

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
// assets serves Furnish, Place Assets and Asset nodes; without it those nodes fail with a reason.
EvaluationResult evaluate(const Graph& graph, const AssetLibrary* assets = nullptr);

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

bool makeFootprint(const FootprintNode& settings, Footprint& output, std::string& error);
bool footprintFromCurve(const Curve& curve, Footprint& output, std::string& error, bool rectify = false);
// Ear clipping of a simple polygon with positive area (see Footprint); indices into polygon.
bool triangulatePolygon(const std::vector<glm::vec2>& polygon, std::vector<uint32_t>& output, std::string& error);
// Moves every edge of a positive-area polygon inward by distance (negative: outward).
bool offsetPolygon(const std::vector<glm::vec2>& polygon, float distance, std::vector<glm::vec2>& output,
                   std::string& error);
bool makeWalls(const Footprint& footprint, const WallsNode& settings, MeshData& output, std::string& error,
               std::size_t maxVertices = 2'000'000);
bool makeSlabs(const Footprint& footprint, const SlabNode& settings, MeshData& output, std::string& error,
               std::size_t maxVertices = 2'000'000);
bool makeRoof(const Footprint& footprint, const RoofNode& settings, MeshData& output, std::string& error,
              std::size_t maxVertices = 2'000'000);
bool makeStairs(const StairsNode& settings, MeshData& output, std::string& error);
// Adds the interior plan to a copy of the footprint; error says which rule could not be met.
bool planInterior(const Footprint& footprint, const RoomSplitNode& settings, Footprint& output, std::string& error);
bool makeInterior(const Footprint& footprint, const InteriorNode& settings, MeshData& output, std::string& error,
                  std::size_t maxVertices = 4'000'000);
bool roadFromCurve(const Curve& curve, const RoadFromCurveNode& settings, MeshData& output, std::string& error);
// Furniture layout for a planned footprint; error names the rule that failed.
bool furnish(const Footprint& footprint, const FurnishNode& settings, const AssetLibrary& library,
             std::vector<Placement>& output, std::string& error);
bool placeAssets(const std::vector<Placement>& placements, const AssetLibrary& library, MeshData& output,
                 std::string& error, std::size_t maxVertices = 4'000'000);

Profile makeRectangleProfile(float width, float height);
Profile makeCircularProfile(float radius, uint32_t sides = 12);

// Sweeps a closed 2D profile along a 3D polyline using parallel-transport frames.
// On failure, output is left unchanged and error receives a short explanation.
bool sweep(const Curve& curve, const Profile& profile, MeshData& output, std::string& error,
           SweepSettings settings = {});

}
