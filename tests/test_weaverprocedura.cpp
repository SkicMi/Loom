#include "TestHarness.h"

#include "../src/LoomProceduraRecipe.h"

#include <Engine/WeaverProcedura.h>

#include <cmath>
#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Proc = Engine::WeaverProcedura;

int main(){
    TestReport report("WeaverProcedura recipes and geometry");

    Proc::GridNode gridSettings;
    gridSettings.width = 8.0f;
    gridSettings.depth = 6.0f;
    gridSettings.cellsX = 4;
    gridSettings.cellsZ = 3;
    Proc::PointGrid grid;
    std::string error;
    const bool gridReady = Proc::makeGrid(gridSettings, grid, error);
    report.check("grid creates a stable row-major set of XZ points", gridReady &&
                 grid.points.size() == 20 && grid.points.front() == glm::vec3(-4, 0, -3) &&
                 grid.points.back() == glm::vec3(4, 0, 3), error);

    Proc::MeshData flatMesh;
    const bool flatReady = Proc::gridToMesh(grid, flatMesh, error);
    const bool upwardNormals = flatReady && !flatMesh.vertices.empty() &&
        std::all_of(flatMesh.vertices.begin(), flatMesh.vertices.end(), [](const Proc::MeshVertex& vertex){
            return std::isfinite(vertex.normal.y) && vertex.normal.y > 0.99f;
        });
    report.check("grid to mesh triangulates cells with upward normals", flatReady &&
                 flatMesh.vertices.size() == 20 && flatMesh.indices.size() == 72 && upwardNormals, error);

    Proc::Graph gridRecipe;
    const Proc::NodeId gridId = Proc::addNode(gridRecipe, gridSettings, 0, 0);
    Proc::SetGridPointHeightNode lift;
    lift.column = 2;
    lift.row = 1;
    lift.height = 2.5f;
    const Proc::NodeId liftA = Proc::addNode(gridRecipe, lift, 180, 0);
    lift.column = 3;
    lift.row = 1;
    lift.height = 1.0f;
    const Proc::NodeId liftB = Proc::addNode(gridRecipe, lift, 360, 0);
    const Proc::NodeId meshId = Proc::addNode(gridRecipe, Proc::GridToMeshNode{}, 540, 0);
    gridRecipe.links = {{gridId, 0, liftA, 0}, {liftA, 0, liftB, 0}, {liftB, 0, meshId, 0}};
    const Proc::EvaluationResult gridResult = Proc::evaluate(gridRecipe);
    report.check("height nodes chain point edits before mesh generation", gridResult.succeeded &&
                 gridResult.outputNode == meshId && gridResult.mesh.vertices.size() == 20 &&
                 std::abs(gridResult.mesh.vertices[1 * 5 + 2].position.y - 2.5f) < 1e-5f &&
                 std::abs(gridResult.mesh.vertices[1 * 5 + 3].position.y - 1.0f) < 1e-5f,
                 gridResult.error);

    Proc::Graph invalidHeight = gridRecipe;
    std::get<Proc::SetGridPointHeightNode>(invalidHeight.nodes[1].payload).row = 4;
    const Proc::EvaluationResult invalidHeightResult = Proc::evaluate(invalidHeight);
    report.check("height node rejects point indices outside the source grid",
                 !invalidHeightResult.succeeded && invalidHeightResult.error.find("outside the grid") != std::string::npos,
                 invalidHeightResult.error);

    Proc::InteriorBlockoutNode interiorSettings;
    Proc::MeshData interiorMesh;
    const bool interiorReady = Proc::makeInteriorBlockout(interiorSettings, interiorMesh, error);
    float minY = 0.0f, maxY = 0.0f;
    if(interiorReady){
        minY = maxY = interiorMesh.vertices.front().position.y;
        for(const Proc::MeshVertex& vertex : interiorMesh.vertices){
            minY = std::min(minY, vertex.position.y);
            maxY = std::max(maxY, vertex.position.y);
        }
    }
    bool doorGapsOpen = interiorReady;
    if(interiorReady){
        const float corridorWallZ = interiorSettings.corridorWidth * 0.5f + interiorSettings.wallThickness * 0.5f;
        const float gapHalf = interiorSettings.doorWidth * 0.5f;
        for(int side : {-1, 1}){
            for(uint32_t room = 0; room < interiorSettings.roomsPerSide; ++room){
                const float centerX = -interiorSettings.roomsPerSide * interiorSettings.roomWidth * 0.5f +
                                      (float(room) + 0.5f) * interiorSettings.roomWidth;
                for(const Proc::MeshVertex& vertex : interiorMesh.vertices){
                    const bool atWallPlane = std::abs(vertex.position.z - float(side) * corridorWallZ) < 1e-5f;
                    const bool withinDoor = vertex.position.x > centerX - gapHalf + 1e-4f &&
                                            vertex.position.x < centerX + gapHalf - 1e-4f;
                    if(atWallPlane && withinDoor && vertex.position.y > 0.05f) doorGapsOpen = false;
                }
            }
        }
    }
    report.check("interior blockout generates floor slab and raised room walls", interiorReady &&
                 interiorMesh.vertices.size() > 200 && interiorMesh.indices.size() > 300 &&
                 std::abs(minY + interiorSettings.floorThickness) < 1e-5f &&
                 std::abs(maxY - interiorSettings.wallHeight) < 1e-5f,
                 fmt("ready %d, vertices %zu, indices %zu, y %.4f..%.4f, %s", interiorReady,
                     interiorMesh.vertices.size(), interiorMesh.indices.size(), double(minY), double(maxY), error.c_str()));
    report.check("interior room walls preserve centered door openings", doorGapsOpen,
                 "door-width spans are empty above the floor on both corridor sides");

    Proc::Graph interiorRecipe;
    const Proc::NodeId interiorNodeId = Proc::addNode(interiorRecipe, interiorSettings, 24, 72);
    const Proc::EvaluationResult interiorResult = Proc::evaluate(interiorRecipe);
    report.check("interior blockout is a valid one-node mesh recipe", interiorResult.succeeded &&
                 interiorResult.outputNode == interiorNodeId && !interiorResult.mesh.empty(), interiorResult.error);

    const std::string encoded = Loom::WeaverProceduraRecipe::serialize({"Hodnik č Rooms", gridRecipe});
    const Loom::WeaverProceduraRecipe::Document decoded = Loom::WeaverProceduraRecipe::parse(encoded);
    const Proc::EvaluationResult roundTrip = Proc::evaluate(decoded.graph);
    report.check("versioned JSON recipe round trips node parameters and typed links",
                 decoded.name == "Hodnik č Rooms" && decoded.graph.nodes.size() == gridRecipe.nodes.size() &&
                 decoded.graph.links.size() == gridRecipe.links.size() && roundTrip.succeeded &&
                 roundTrip.mesh.vertices.size() == gridResult.mesh.vertices.size() &&
                 std::abs(roundTrip.mesh.vertices[7].position.y - 2.5f) < 1e-5f, roundTrip.error);

    bool rejectedOldSchema = false;
    try{
        (void)Loom::WeaverProceduraRecipe::parse(
            "{\"format\":\"loom.weaverprocedura.recipe\",\"schema_version\":1}");
    }catch(const std::exception&){ rejectedOldSchema = true; }
    report.check("recipe loader rejects unsupported schema versions", rejectedOldSchema, "schema version guard");

    std::string primitiveError;
    Proc::MeshData cube;
    Proc::AddPrimitiveNode primitiveSettings;
    const bool cubeReady = Proc::makePrimitive(primitiveSettings,cube,primitiveError);
    report.check("Add Primitive creates the existing cube shape with flat face normals",cubeReady &&
                 cube.vertices.size() == 24 && cube.indices.size() == 36 &&
                 std::all_of(cube.vertices.begin(),cube.vertices.end(),[](const Proc::MeshVertex& vertex){
                     return std::isfinite(vertex.normal.x) && std::abs(glm::length(vertex.normal)-1.0f) < 1e-5f;
                 }),primitiveError);
    auto bounds = [](const Proc::MeshData& mesh){
        glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
        for(const Proc::MeshVertex& vertex : mesh.vertices){
            low = glm::min(low,vertex.position);
            high = glm::max(high,vertex.position);
        }
        return std::pair<glm::vec3,glm::vec3>{low,high};
    };
    const auto cubeBounds = bounds(cube);
    report.check("box primitive places each face at the correct half extent",
                 cubeReady && cubeBounds.first == glm::vec3(-0.5f) && cubeBounds.second == glm::vec3(0.5f),
                 "unit cube bounds are -0.5 to +0.5 on all axes");
    bool allPrimitivesReady = true;
    for(Proc::PrimitiveType type : {Proc::PrimitiveType::Cube,Proc::PrimitiveType::Plane,
                                    Proc::PrimitiveType::Sphere,Proc::PrimitiveType::Pyramid,
                                    Proc::PrimitiveType::Capsule}){
        Proc::AddPrimitiveNode settings;
        settings.primitive = type;
        Proc::MeshData mesh;
        std::string primitiveFailure;
        const bool ready = Proc::makePrimitive(settings,mesh,primitiveFailure);
        allPrimitivesReady &= ready && !mesh.empty();
        if(!ready) primitiveError = primitiveFailure;
    }
    report.check("Add Primitive covers cube, plane, sphere, pyramid, and capsule",allPrimitivesReady,
                 primitiveError.empty() ? "all five engine shapes create indexed meshes" : primitiveError);
    bool existingPrimitiveExtents = true;
    const std::vector<std::pair<Proc::PrimitiveType,glm::vec3>> expectedHighs = {
        {Proc::PrimitiveType::Plane,{0.5f,0.0f,0.5f}},
        {Proc::PrimitiveType::Sphere,{0.5f,0.5f,0.5f}},
        {Proc::PrimitiveType::Pyramid,{0.5f,0.5f,0.5f}},
        {Proc::PrimitiveType::Capsule,{0.5f,1.0f,0.5f}},
    };
    for(const auto& [type,expectedHigh] : expectedHighs){
        Proc::AddPrimitiveNode settings; settings.primitive = type;
        Proc::MeshData mesh; std::string primitiveFailure;
        if(!Proc::makePrimitive(settings,mesh,primitiveFailure)){ existingPrimitiveExtents = false; continue; }
        const auto measured = bounds(mesh);
        existingPrimitiveExtents &= glm::length(measured.second-expectedHigh) < 1e-5f;
        if(type == Proc::PrimitiveType::Plane) existingPrimitiveExtents &= std::abs(measured.first.y) < 1e-5f;
        else existingPrimitiveExtents &= glm::length(measured.first+expectedHigh) < 1e-5f;
    }
    report.check("primitive dimensions match Loom's centered plane, sphere, pyramid, and capsule",
                 existingPrimitiveExtents,"capsule is 2 m tall; the other listed unit shapes are 1 m tall");

    Proc::MoveNode moveSettings;
    moveSettings.offset = {2.0f,-1.0f,3.0f};
    Proc::MeshData moved;
    const bool moveReady = Proc::moveMesh(cube,moveSettings,moved,error);
    report.check("Move translates every mesh position and preserves topology",moveReady &&
                 moved.indices == cube.indices && moved.vertices.size() == cube.vertices.size() &&
                 moved.vertices.front().position == cube.vertices.front().position + moveSettings.offset,error);

    Proc::RotateNode rotateSettings;
    rotateSettings.degrees = {0.0f,0.0f,90.0f};
    Proc::MeshData rotated;
    const bool rotateReady = Proc::rotateMesh(cube,rotateSettings,rotated,error);
    const glm::vec3 expectedRotated{-cube.vertices.front().position.y,cube.vertices.front().position.x,
                                    cube.vertices.front().position.z};
    report.check("Rotate applies Euler degrees around the selected pivot",rotateReady &&
                 glm::length(rotated.vertices.front().position-expectedRotated) < 1e-5f,error);

    Proc::ScaleNode scaleSettings;
    scaleSettings.factor = {2.0f,3.0f,0.5f};
    Proc::MeshData scaled;
    const bool scaleReady = Proc::scaleMesh(cube,scaleSettings,scaled,error);
    report.check("Scale applies per-axis factors and inverse-transpose normals",scaleReady &&
                 glm::length(scaled.vertices.front().position-cube.vertices.front().position*scaleSettings.factor) < 1e-5f &&
                 std::abs(glm::length(scaled.vertices.front().normal)-1.0f) < 1e-5f,error);

    Proc::ExtrudeNode extrudeSettings;
    extrudeSettings.faceIndex = 0;
    extrudeSettings.distance = 0.75f;
    Proc::MeshData extruded;
    const bool extrudeReady = Proc::extrudeFace(cube,extrudeSettings,extruded,error);
    float extrudedMaxX = -1000.0f;
    for(const Proc::MeshVertex& vertex : extruded.vertices) extrudedMaxX = std::max(extrudedMaxX,vertex.position.x);
    report.check("Extrude expands the selected connected coplanar face and creates side walls",extrudeReady &&
                 extruded.indices.size() > cube.indices.size() && std::abs(extrudedMaxX-1.25f) < 1e-5f,
                 fmt("ready %d, triangles %zu->%zu, maxX %.4f, %s",extrudeReady,cube.indices.size()/3,
                     extruded.indices.size()/3,double(extrudedMaxX),error.c_str()));
    extrudeSettings.faceIndex = uint32_t(cube.indices.size()/3);
    Proc::MeshData rejectedExtrusion;
    report.check("Extrude rejects a seed triangle outside the mesh",
                 !Proc::extrudeFace(cube,extrudeSettings,rejectedExtrusion,error) &&
                 error.find("outside") != std::string::npos,error);

    Proc::BevelNode bevelSettings;
    bevelSettings.amount = 0.08f;
    bevelSettings.segments = 3;
    Proc::MeshData beveled;
    const bool bevelReady = Proc::bevelMesh(cube,bevelSettings,beveled,error);
    using PositionKey = std::tuple<int,int,int>;
    using GeometricEdgeKey = std::pair<PositionKey,PositionKey>;
    std::map<GeometricEdgeKey,int> bevelEdgeIncidence;
    auto positionKey = [](const glm::vec3& point){
        return PositionKey{int(std::lround(point.x*10000.0f)),int(std::lround(point.y*10000.0f)),
                           int(std::lround(point.z*10000.0f))};
    };
    for(std::size_t i = 0; i+2 < beveled.indices.size(); i += 3){
        const uint32_t ids[] = {beveled.indices[i],beveled.indices[i+1],beveled.indices[i+2]};
        for(int edge = 0; edge < 3; ++edge){
            PositionKey a = positionKey(beveled.vertices[ids[edge]].position);
            PositionKey b = positionKey(beveled.vertices[ids[(edge+1)%3]].position);
            if(b < a) std::swap(a,b);
            ++bevelEdgeIncidence[{a,b}];
        }
    }
    const bool bevelIsClosed = bevelReady && !bevelEdgeIncidence.empty() &&
        std::all_of(bevelEdgeIncidence.begin(),bevelEdgeIncidence.end(),[](const auto& edge){ return edge.second == 2; });
    std::size_t openBevelEdges = 0, nonManifoldBevelEdges = 0;
    std::map<int,std::size_t> bevelIncidenceHistogram;
    for(const auto& edge : bevelEdgeIncidence){
        ++bevelIncidenceHistogram[edge.second];
        if(edge.second == 1) ++openBevelEdges;
        else if(edge.second != 2) ++nonManifoldBevelEdges;
    }
    report.check("Bevel creates the requested segmented planar-face chamfer",bevelReady &&
                 beveled.vertices.size() > cube.vertices.size() && beveled.indices.size() > cube.indices.size() &&
                 std::all_of(beveled.vertices.begin(),beveled.vertices.end(),[](const Proc::MeshVertex& vertex){
                     return std::isfinite(vertex.position.x) && std::isfinite(vertex.normal.z);
                 }),error);
    report.check("Bevel closes every geometric edge around a cube",bevelIsClosed,
                 fmt("%zu geometric edges; incidence 1:%zu 2:%zu 3:%zu 4:%zu; %zu open, %zu other",
                     bevelEdgeIncidence.size(),bevelIncidenceHistogram[1],bevelIncidenceHistogram[2],
                     bevelIncidenceHistogram[3],bevelIncidenceHistogram[4],openBevelEdges,nonManifoldBevelEdges));
    bevelSettings.segments = 0;
    Proc::MeshData rejectedBevel;
    report.check("Bevel rejects an invalid segment count",!Proc::bevelMesh(cube,bevelSettings,rejectedBevel,error) &&
                 error.find("segment") != std::string::npos,error);

    std::vector<glm::vec3> vertexPoints;
    const bool meshToPointReady = Proc::meshToPoints(cube,vertexPoints,error);
    report.check("Mesh to Point deduplicates face-split primitive vertices",meshToPointReady && vertexPoints.size() == 8,
                 fmt("ready %d, got %zu points, %s",meshToPointReady,vertexPoints.size(),error.c_str()));
    std::vector<glm::vec3> surfacePointsA, surfacePointsB;
    const bool sampledA = Proc::pointsFromMesh(cube,256,31415,surfacePointsA,error);
    const bool sampledB = Proc::pointsFromMesh(cube,256,31415,surfacePointsB,error);
    const bool pointsInBounds = sampledA && std::all_of(surfacePointsA.begin(),surfacePointsA.end(),[](const glm::vec3& point){
        return point.x >= -0.50001f && point.x <= 0.50001f && point.y >= -0.50001f && point.y <= 0.50001f &&
               point.z >= -0.50001f && point.z <= 0.50001f;
    });
    report.check("Point from Mesh samples the surface uniformly and repeats for the same seed",sampledA && sampledB &&
                 surfacePointsA.size() == 256 && surfacePointsA == surfacePointsB && pointsInBounds,error);

    Proc::Graph meshOperationRecipe;
    const Proc::NodeId primitiveId = Proc::addNode(meshOperationRecipe,primitiveSettings,0,0);
    Proc::NodeId moveId = Proc::addNode(meshOperationRecipe,moveSettings,200,0);
    Proc::NodeId rotateId = Proc::addNode(meshOperationRecipe,rotateSettings,400,0);
    Proc::NodeId scaleId = Proc::addNode(meshOperationRecipe,scaleSettings,600,0);
    const Proc::NodeId extrudeId = Proc::addNode(meshOperationRecipe,Proc::ExtrudeNode{},800,0);
    const Proc::NodeId bevelId = Proc::addNode(meshOperationRecipe,Proc::BevelNode{},1000,0);
    meshOperationRecipe.links = {{primitiveId,0,moveId,0},{moveId,0,rotateId,0},{rotateId,0,scaleId,0},
                                 {scaleId,0,extrudeId,0},{extrudeId,0,bevelId,0}};
    Proc::Graph preBevelRecipe = meshOperationRecipe;
    preBevelRecipe.nodes.pop_back();
    preBevelRecipe.links.pop_back();
    const Proc::EvaluationResult preBevelResult = Proc::evaluate(preBevelRecipe);
    Proc::MeshData bevelAfterExtrude;
    std::string bevelAfterExtrudeError;
    const bool bevelAfterExtrudeReady = preBevelResult.succeeded &&
        Proc::bevelMesh(preBevelResult.mesh,Proc::BevelNode{},bevelAfterExtrude,bevelAfterExtrudeError);
    const Proc::EvaluationResult meshOperationResult = Proc::evaluate(meshOperationRecipe);
    report.check("Bevel accepts transformed extruded geometry",bevelAfterExtrudeReady,bevelAfterExtrudeError);
    report.check("Recipe evaluator chains Add Primitive, Move, Rotate, Scale, Extrude, and Bevel",
                 meshOperationResult.succeeded && meshOperationResult.outputNode == bevelId && !meshOperationResult.mesh.empty(),
                 meshOperationResult.error);

    Proc::Graph vertexPointRecipe;
    const Proc::NodeId pointSource = Proc::addNode(vertexPointRecipe,Proc::AddPrimitiveNode{},0,0);
    const Proc::NodeId meshToPointId = Proc::addNode(vertexPointRecipe,Proc::MeshToPointNode{},200,0);
    vertexPointRecipe.links = {{pointSource,0,meshToPointId,0}};
    const Proc::EvaluationResult vertexPointResult = Proc::evaluate(vertexPointRecipe);
    Proc::MeshData pointPreview;
    const bool pointPreviewReady = vertexPointResult.succeeded && vertexPointResult.pointCloudOutput &&
                                   Proc::makePointPreview(vertexPointResult.points,pointPreview,error);
    report.check("Mesh to Point is a point-cloud graph output with a visible viewport marker mesh",
                 pointPreviewReady && vertexPointResult.points.size() == 8 && !pointPreview.empty(),
                 fmt("ready %d, result %zu points, mesh vertices %zu, %s",pointPreviewReady,
                     vertexPointResult.points.size(),pointPreview.vertices.size(),error.c_str()));

    Proc::Graph surfacePointRecipe;
    const Proc::NodeId surfaceSource = Proc::addNode(surfacePointRecipe,Proc::AddPrimitiveNode{},0,0);
    Proc::PointFromMeshNode surfaceSettings;
    surfaceSettings.count = 64;
    surfaceSettings.seed = 123;
    const Proc::NodeId pointFromMeshId = Proc::addNode(surfacePointRecipe,surfaceSettings,200,0);
    surfacePointRecipe.links = {{surfaceSource,0,pointFromMeshId,0}};
    const Proc::EvaluationResult surfacePointResult = Proc::evaluate(surfacePointRecipe);
    report.check("Point from Mesh becomes the typed point-cloud recipe output",surfacePointResult.succeeded &&
                 surfacePointResult.pointCloudOutput && surfacePointResult.points.size() == 64,
                 surfacePointResult.error);

    Proc::Graph newVocabularyRecipe;
    Proc::addNode(newVocabularyRecipe,Proc::AddPrimitiveNode{});
    Proc::addNode(newVocabularyRecipe,Proc::MoveNode{});
    Proc::addNode(newVocabularyRecipe,Proc::RotateNode{});
    Proc::addNode(newVocabularyRecipe,Proc::ScaleNode{});
    Proc::addNode(newVocabularyRecipe,Proc::ExtrudeNode{});
    Proc::addNode(newVocabularyRecipe,Proc::BevelNode{});
    Proc::addNode(newVocabularyRecipe,Proc::MeshToPointNode{});
    Proc::addNode(newVocabularyRecipe,Proc::PointFromMeshNode{});
    const std::string newVocabularyJson = Loom::WeaverProceduraRecipe::serialize({"Mesh operations",newVocabularyRecipe});
    const Loom::WeaverProceduraRecipe::Document newVocabularyRoundTrip = Loom::WeaverProceduraRecipe::parse(newVocabularyJson);
    report.check("schema 4 Recipe round-trips every new operation payload",
                 newVocabularyRoundTrip.graph.schemaVersion == Proc::graphSchemaVersion &&
                 newVocabularyRoundTrip.graph.nodes.size() == 8 &&
                 std::holds_alternative<Proc::AddPrimitiveNode>(newVocabularyRoundTrip.graph.nodes[0].payload) &&
                 std::holds_alternative<Proc::MoveNode>(newVocabularyRoundTrip.graph.nodes[1].payload) &&
                 std::holds_alternative<Proc::RotateNode>(newVocabularyRoundTrip.graph.nodes[2].payload) &&
                 std::holds_alternative<Proc::ScaleNode>(newVocabularyRoundTrip.graph.nodes[3].payload) &&
                 std::holds_alternative<Proc::ExtrudeNode>(newVocabularyRoundTrip.graph.nodes[4].payload) &&
                 std::holds_alternative<Proc::BevelNode>(newVocabularyRoundTrip.graph.nodes[5].payload) &&
                 std::holds_alternative<Proc::MeshToPointNode>(newVocabularyRoundTrip.graph.nodes[6].payload) &&
                 std::holds_alternative<Proc::PointFromMeshNode>(newVocabularyRoundTrip.graph.nodes[7].payload),newVocabularyJson);
    std::string legacyRecipeJson = encoded;
    const std::string currentSchemaToken = "\"schema_version\":4";
    const std::size_t schemaPosition = legacyRecipeJson.find(currentSchemaToken);
    if(schemaPosition != std::string::npos) legacyRecipeJson.replace(schemaPosition,currentSchemaToken.size(),"\"schema_version\":3");
    bool readLegacyRecipe = false;
    try{ readLegacyRecipe = Loom::WeaverProceduraRecipe::parse(legacyRecipeJson).graph.schemaVersion == Proc::graphSchemaVersion; }
    catch(const std::exception&){ readLegacyRecipe = false; }
    report.check("schema 4 loader upgrades existing schema 3 recipes",readLegacyRecipe,"version 3 input maps to the current graph schema");

    return report.result();
}
