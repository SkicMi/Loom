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
    using ExtrudePositionKey = std::tuple<int,int,int>;
    using ExtrudeEdgeKey = std::pair<ExtrudePositionKey,ExtrudePositionKey>;
    std::map<ExtrudeEdgeKey,int> extrudeEdgeIncidence;
    auto extrudePositionKey = [](const glm::vec3& point){
        return ExtrudePositionKey{int(std::lround(point.x*10000.0f)),int(std::lround(point.y*10000.0f)),
                                  int(std::lround(point.z*10000.0f))};
    };
    if(extrudeReady){
        for(std::size_t i = 0; i+2 < extruded.indices.size(); i += 3){
            const uint32_t ids[] = {extruded.indices[i],extruded.indices[i+1],extruded.indices[i+2]};
            for(int edge = 0; edge < 3; ++edge){
                ExtrudePositionKey a = extrudePositionKey(extruded.vertices[ids[edge]].position);
                ExtrudePositionKey b = extrudePositionKey(extruded.vertices[ids[(edge+1)%3]].position);
                if(b < a) std::swap(a,b);
                ++extrudeEdgeIncidence[{a,b}];
            }
        }
    }
    bool extrudeWatertight = extrudeReady && !extrudeEdgeIncidence.empty();
    for(const auto& edge : extrudeEdgeIncidence) extrudeWatertight &= edge.second == 2;
    report.check("Extrude keeps every geometric edge watertight",extrudeWatertight,
                 fmt("%zu edges, %zu edges have incidence other than two",extrudeEdgeIncidence.size(),
                     std::size_t(std::count_if(extrudeEdgeIncidence.begin(),extrudeEdgeIncidence.end(),
                         [](const auto& edge){ return edge.second != 2; }))));
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
    bevelEdgeIncidence.clear();
    if(bevelAfterExtrudeReady){
        for(std::size_t i = 0; i+2 < bevelAfterExtrude.indices.size(); i += 3){
            const uint32_t ids[] = {bevelAfterExtrude.indices[i],bevelAfterExtrude.indices[i+1],
                                    bevelAfterExtrude.indices[i+2]};
            for(int edge = 0; edge < 3; ++edge){
                PositionKey a = positionKey(bevelAfterExtrude.vertices[ids[edge]].position);
                PositionKey b = positionKey(bevelAfterExtrude.vertices[ids[(edge+1)%3]].position);
                if(b < a) std::swap(a,b);
                ++bevelEdgeIncidence[{a,b}];
            }
        }
    }
    const bool extrudedBevelIsClosed = bevelAfterExtrudeReady && !bevelEdgeIncidence.empty() &&
        std::all_of(bevelEdgeIncidence.begin(),bevelEdgeIncidence.end(),[](const auto& edge){ return edge.second == 2; });
    std::string openExtrudedBevelEdges;
    for(const auto& edge : bevelEdgeIncidence){
        if(edge.second != 1) continue;
        const auto& a = edge.first.first;
        const auto& b = edge.first.second;
        openExtrudedBevelEdges += " [" + std::to_string(std::get<0>(a)) + "," +
            std::to_string(std::get<1>(a)) + "," + std::to_string(std::get<2>(a)) + "->" +
            std::to_string(std::get<0>(b)) + "," + std::to_string(std::get<1>(b)) + "," +
            std::to_string(std::get<2>(b)) + "]";
    }
    const Proc::EvaluationResult meshOperationResult = Proc::evaluate(meshOperationRecipe);
    report.check("Bevel accepts transformed extruded geometry",bevelAfterExtrudeReady,bevelAfterExtrudeError);
    report.check("Bevel keeps an extruded mesh watertight",extrudedBevelIsClosed,
                 fmt("%zu geometric edges (%zu open, %zu with incidence above two)",bevelEdgeIncidence.size(),
                     std::size_t(std::count_if(bevelEdgeIncidence.begin(),bevelEdgeIncidence.end(),
                         [](const auto& edge){ return edge.second == 1; })),
                     std::size_t(std::count_if(bevelEdgeIncidence.begin(),bevelEdgeIncidence.end(),
                         [](const auto& edge){ return edge.second > 2; }))) + openExtrudedBevelEdges);
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
    const std::string currentSchemaToken = "\"schema_version\":" + std::to_string(Proc::graphSchemaVersion);
    const std::size_t schemaPosition = legacyRecipeJson.find(currentSchemaToken);
    if(schemaPosition != std::string::npos) legacyRecipeJson.replace(schemaPosition,currentSchemaToken.size(),"\"schema_version\":3");
    bool readLegacyRecipe = false;
    try{ readLegacyRecipe = Loom::WeaverProceduraRecipe::parse(legacyRecipeJson).graph.schemaVersion == Proc::graphSchemaVersion; }
    catch(const std::exception&){ readLegacyRecipe = false; }
    report.check("current loader upgrades existing schema 3 recipes",readLegacyRecipe &&
                 legacyRecipeJson.find("\"schema_version\":3") != std::string::npos,"version 3 input maps to the current graph schema");

    //== AgentOfWeavers infrastruktura: provenance, semantika, materijali, Merge, Copy to Points ==
    auto attributesSized = [](const Proc::MeshData& mesh){ return mesh.triangles.size() == mesh.indices.size() / 3; };
    auto countSemantic = [](const Proc::MeshData& mesh, const char* name){
        return std::count_if(mesh.triangles.begin(), mesh.triangles.end(),
            [&](const Proc::TriangleAttributes& t){ return t.semantic == Proc::semanticId(name); });
    };

    report.check("closed vocabularies map names to stable non-zero IDs",
                 Proc::semanticId("roof") != 0 && Proc::semanticName(Proc::semanticId("roof")) == "roof" &&
                 Proc::materialId("brick") != 0 && Proc::materialName(Proc::materialId("brick")) == "brick" &&
                 Proc::semanticId("spaceship") == 0 && Proc::materialId("") == 0, "vocabulary lookup");

    Proc::Graph mergeRecipe;
    const auto firstCube = Proc::addNode(mergeRecipe, Proc::AddPrimitiveNode{});
    const auto secondCube = Proc::addNode(mergeRecipe, Proc::AddPrimitiveNode{});
    Proc::MoveNode apart; apart.offset = {3.0f, 0.0f, 0.0f};
    const auto shiftedCube = Proc::addNode(mergeRecipe, apart);
    const auto merged = Proc::addNode(mergeRecipe, Proc::MergeNode{});
    mergeRecipe.links = {{secondCube,0,shiftedCube,0},{firstCube,0,merged,0},{shiftedCube,0,merged,3}};
    const Proc::EvaluationResult mergeResult = Proc::evaluate(mergeRecipe);
    report.check("Merge joins meshes from any input ports and keeps provenance per source",
                 mergeResult.succeeded && mergeResult.mesh.vertices.size() == 48 && attributesSized(mergeResult.mesh) &&
                 mergeResult.mesh.triangles.front().createdBy == firstCube &&
                 mergeResult.mesh.triangles.back().createdBy == secondCube, mergeResult.error);

    Proc::Graph extrudeRecipe;
    const auto extrudeSource = Proc::addNode(extrudeRecipe, Proc::AddPrimitiveNode{});
    const auto extrudeNode = Proc::addNode(extrudeRecipe, Proc::ExtrudeNode{});
    const auto bevelNode = Proc::addNode(extrudeRecipe, Proc::BevelNode{0.05f, 2});
    extrudeRecipe.links = {{extrudeSource,0,extrudeNode,0},{extrudeNode,0,bevelNode,0}};
    Proc::Graph extrudeOnly = extrudeRecipe;
    extrudeOnly.nodes.pop_back(); extrudeOnly.links.pop_back();
    const Proc::EvaluationResult extrudedResult = Proc::evaluate(extrudeOnly);
    const auto sideTriangles = extrudedResult.succeeded ? std::count_if(extrudedResult.mesh.triangles.begin(), extrudedResult.mesh.triangles.end(),
        [&](const Proc::TriangleAttributes& t){ return t.createdBy == extrudeNode; }) : 0;
    report.check("Extrude stamps its new side walls and keeps the shiftedCube face's source",
                 extrudedResult.succeeded && attributesSized(extrudedResult.mesh) && sideTriangles == 8 &&
                 extrudedResult.mesh.triangles.size() == 20, extrudedResult.error);
    const Proc::EvaluationResult beveledResult = Proc::evaluate(extrudeRecipe);
    const bool bevelHasBothSources = beveledResult.succeeded &&
        std::any_of(beveledResult.mesh.triangles.begin(), beveledResult.mesh.triangles.end(),
                    [&](const Proc::TriangleAttributes& t){ return t.createdBy == extrudeSource; }) &&
        std::any_of(beveledResult.mesh.triangles.begin(), beveledResult.mesh.triangles.end(),
                    [&](const Proc::TriangleAttributes& t){ return t.createdBy == bevelNode; });
    report.check("Bevel keeps face caps from their source and stamps chamfer strips",
                 bevelHasBothSources && attributesSized(beveledResult.mesh), beveledResult.error);

    Proc::Graph interiorTags;
    Proc::addNode(interiorTags, Proc::InteriorBlockoutNode{});
    const Proc::EvaluationResult taggedInterior = Proc::evaluate(interiorTags);
    report.check("interior blockout labels floor, exterior walls, and interior walls",
                 taggedInterior.succeeded && attributesSized(taggedInterior.mesh) &&
                 countSemantic(taggedInterior.mesh,"floor") == 12 && countSemantic(taggedInterior.mesh,"wall_exterior") > 0 &&
                 countSemantic(taggedInterior.mesh,"wall_interior") > 0 &&
                 std::none_of(taggedInterior.mesh.triangles.begin(), taggedInterior.mesh.triangles.end(),
                              [](const Proc::TriangleAttributes& t){ return t.semantic == 0; }), taggedInterior.error);

    Proc::Graph tagRecipe;
    const auto tagSource = Proc::addNode(tagRecipe, Proc::AddPrimitiveNode{});
    Proc::SetSemanticNode roofTag; roofTag.semantic = "roof";
    roofTag.filter.useDirection = true; roofTag.filter.direction = {0,1,0}; roofTag.filter.maxAngleDegrees = 10.0f;
    const auto tagged = Proc::addNode(tagRecipe, roofTag);
    Proc::SetMaterialNode roofMaterial; roofMaterial.material = "roof_tiles"; roofMaterial.filter.semantic = "roof";
    const auto painted = Proc::addNode(tagRecipe, roofMaterial);
    tagRecipe.links = {{tagSource,0,tagged,0},{tagged,0,painted,0}};
    const Proc::EvaluationResult tagResult = Proc::evaluate(tagRecipe);
    const auto tiled = tagResult.succeeded ? std::count_if(tagResult.mesh.triangles.begin(), tagResult.mesh.triangles.end(),
        [](const Proc::TriangleAttributes& t){ return t.material == Proc::materialId("roof_tiles"); }) : 0;
    report.check("Set Semantic by direction and Set Material by semantic select only the top face",
                 tagResult.succeeded && countSemantic(tagResult.mesh,"roof") == 2 && tiled == 2, tagResult.error);

    Proc::Graph unknownTag = tagRecipe;
    std::get<Proc::SetMaterialNode>(unknownTag.nodes[2].payload).material = "unobtainium";
    report.check("validation rejects material names outside the library", !Proc::validate(unknownTag),
                 "closed material vocabulary");

    Proc::MeshData sphere;
    Proc::AddPrimitiveNode sphereSettings; sphereSettings.primitive = Proc::PrimitiveType::Sphere;
    Proc::makePrimitive(sphereSettings, sphere, error);
    Proc::MeshData flatSphere, smoothSphere;
    const bool flattened = Proc::smoothNormals(sphere, 0.0f, flatSphere, error);
    const bool smoothed = flattened && Proc::smoothNormals(flatSphere, 60.0f, smoothSphere, error);
    float worstSmooth = 1.0f;
    for(const Proc::MeshVertex& vertex : smoothSphere.vertices)
        worstSmooth = std::min(worstSmooth, glm::dot(vertex.normal, glm::normalize(vertex.position)));
    report.check("Smooth Normals flattens at 0 degrees and restores a round sphere at 60 degrees",
                 smoothed && flatSphere.vertices.size() > sphere.vertices.size() && worstSmooth > 0.98f,
                 "worst dot " + std::to_string(worstSmooth));

    Proc::MeshData cubeForUv, wideCube, projected;
    Proc::AddPrimitiveNode wideSettings; wideSettings.size = {4.0f, 2.0f, 1.0f};
    Proc::makePrimitive(wideSettings, wideCube, error);
    const bool uvDone = Proc::projectUVs(wideCube, 0.5f, projected, error);
    float uMin = 1e9f, uMax = -1e9f;
    for(const Proc::MeshVertex& vertex : projected.vertices){ uMin = std::min(uMin, vertex.uv.x); uMax = std::max(uMax, vertex.uv.x); }
    report.check("UV Project tiles in world meters", uvDone && std::abs((uMax - uMin) - 8.0f) < 1e-3f,
                 "U span " + std::to_string(uMax - uMin));

    Proc::Graph copyRecipe;
    Proc::GridNode copyGrid; copyGrid.cellsX = 4; copyGrid.cellsZ = 4;
    const auto copyGridId = Proc::addNode(copyRecipe, copyGrid);
    const auto copySurface = Proc::addNode(copyRecipe, Proc::GridToMeshNode{});
    const auto copyPoints = Proc::addNode(copyRecipe, Proc::MeshToPointNode{});
    Proc::AddPrimitiveNode post; post.size = glm::vec3(0.2f);
    const auto postId = Proc::addNode(copyRecipe, post);
    const auto copies = Proc::addNode(copyRecipe, Proc::CopyToPointsNode{});
    copyRecipe.links = {{copyGridId,0,copySurface,0},{copySurface,0,copyPoints,0},{postId,0,copies,0},{copyPoints,0,copies,1}};
    const Proc::EvaluationResult copyResult = Proc::evaluate(copyRecipe);
    report.check("Copy to Points places one instance on every point and points now feed the graph",
                 copyResult.succeeded && !copyResult.pointCloudOutput && copyResult.mesh.vertices.size() == 24u * 25u &&
                 attributesSized(copyResult.mesh) && copyResult.mesh.triangles.front().createdBy == postId, copyResult.error);

    Proc::Graph ropeRecipe;
    Proc::CurveNode ropePath; ropePath.curve.points = {{0,2,0},{2,1.5f,0},{4,2,0}};
    const auto ropeCurve = Proc::addNode(ropeRecipe, ropePath);
    const auto ropeProfile = Proc::addNode(ropeRecipe, Proc::CircleProfileNode{0.04f, 10});
    Proc::SweepNode ropeSweep; ropeSweep.settings.sampleSpacing = 0.25f;
    const auto ropeMesh = Proc::addNode(ropeRecipe, ropeSweep);
    ropeRecipe.links = {{ropeCurve,0,ropeMesh,0},{ropeProfile,0,ropeMesh,1}};
    const Proc::EvaluationResult ropeResult = Proc::evaluate(ropeRecipe);
    report.check("Sweep accepts a Circle Profile node", ropeResult.succeeded && attributesSized(ropeResult.mesh),
                 ropeResult.error);

    Proc::Graph infrastructureRecipe;
    Proc::addNode(infrastructureRecipe, Proc::CircleProfileNode{});
    Proc::addNode(infrastructureRecipe, Proc::MergeNode{});
    Proc::addNode(infrastructureRecipe, roofTag);
    Proc::addNode(infrastructureRecipe, roofMaterial);
    Proc::addNode(infrastructureRecipe, Proc::SmoothNormalsNode{45.0f});
    Proc::addNode(infrastructureRecipe, Proc::UVProjectNode{2.0f});
    Proc::CopyToPointsNode copySettings; copySettings.alignToNormal = true; copySettings.randomYawDegrees = 90.0f;
    Proc::addNode(infrastructureRecipe, copySettings);
    std::string infrastructureJson;
    bool infrastructureRoundTrip = false;
    try{
        infrastructureJson = Loom::WeaverProceduraRecipe::serialize({"Infrastructure", infrastructureRecipe});
        const auto back = Loom::WeaverProceduraRecipe::parse(infrastructureJson).graph;
        const auto& tagBack = std::get<Proc::SetSemanticNode>(back.nodes[2].payload);
        const auto& copyBack = std::get<Proc::CopyToPointsNode>(back.nodes[6].payload);
        infrastructureRoundTrip = back.nodes.size() == 7 && tagBack.semantic == "roof" && tagBack.filter.useDirection &&
            std::abs(tagBack.filter.maxAngleDegrees - 10.0f) < 1e-6f &&
            std::get<Proc::SetMaterialNode>(back.nodes[3].payload).filter.semantic == "roof" &&
            std::abs(std::get<Proc::UVProjectNode>(back.nodes[5].payload).tileSize - 2.0f) < 1e-6f &&
            copyBack.alignToNormal && std::abs(copyBack.randomYawDegrees - 90.0f) < 1e-6f;
    }catch(const std::exception& failure){ infrastructureJson = failure.what(); }
    report.check("current schema Recipe round-trips every infrastructure payload", infrastructureRoundTrip, infrastructureJson);

    //== Faza 1, drugi krug: valjak, torus, filtrirani Extrude, krivulje, Copy along Curve ==
    auto boundsOf = [](const Proc::MeshData& mesh){
        glm::vec3 low(1e9f), high(-1e9f);
        for(const Proc::MeshVertex& vertex : mesh.vertices){ low = glm::min(low, vertex.position); high = glm::max(high, vertex.position); }
        return std::make_pair(low, high);
    };
    Proc::MeshData cylinder, torus;
    Proc::AddPrimitiveNode cylinderSettings; cylinderSettings.primitive = Proc::PrimitiveType::Cylinder;
    Proc::AddPrimitiveNode torusSettings; torusSettings.primitive = Proc::PrimitiveType::Torus; torusSettings.tubeRatio = 0.2f;
    const bool cylinderMade = Proc::makePrimitive(cylinderSettings, cylinder, error);
    const bool torusMade = Proc::makePrimitive(torusSettings, torus, error);
    const auto [cylinderLow, cylinderHigh] = boundsOf(cylinder);
    const auto [torusLow, torusHigh] = boundsOf(torus);
    report.check("Cylinder and Torus primitives fit the unit box (torus height = tube diameter)",
                 cylinderMade && torusMade && std::abs(cylinderHigh.y - 0.5f) < 1e-5f && std::abs(cylinderHigh.x - 0.5f) < 1e-5f &&
                 std::abs(torusHigh.x - 0.5f) < 1e-3f && std::abs(torusHigh.y - 0.1f) < 1e-4f && std::abs(torusLow.y + 0.1f) < 1e-4f,
                 "torus height " + std::to_string(torusHigh.y - torusLow.y));

    Proc::Graph filteredExtrude;
    const auto slab = Proc::addNode(filteredExtrude, Proc::AddPrimitiveNode{});
    Proc::ExtrudeNode upward; upward.useFilter = true; upward.distance = 0.5f;
    upward.filter.useDirection = true; upward.filter.direction = {0,1,0}; upward.filter.maxAngleDegrees = 5.0f;
    const auto raised = Proc::addNode(filteredExtrude, upward);
    filteredExtrude.links = {{slab,0,raised,0}};
    const Proc::EvaluationResult raisedResult = Proc::evaluate(filteredExtrude);
    report.check("Extrude by filter lifts the upward face without a triangle index",
                 raisedResult.succeeded && std::abs(boundsOf(raisedResult.mesh).second.y - 1.0f) < 1e-5f &&
                 raisedResult.mesh.triangles.size() == 20, raisedResult.error);
    Proc::Graph sidesExtrude = filteredExtrude;
    auto& sideSettings = std::get<Proc::ExtrudeNode>(sidesExtrude.nodes[1].payload);
    sideSettings.filter.direction = {1,0,0}; sideSettings.filter.maxAngleDegrees = 95.0f;
    const Proc::EvaluationResult sidesResult = Proc::evaluate(sidesExtrude);
    report.check("Extrude by filter moves every matching face along its own normal",
                 sidesResult.succeeded && sidesResult.mesh.triangles.size() == 12 - 10 + 10 + 5 * 8, sidesResult.error);
    Proc::Graph missingFace = filteredExtrude;
    std::get<Proc::ExtrudeNode>(missingFace.nodes[1].payload).filter.semantic = "roof";
    report.check("Extrude by filter reports when nothing matches", !Proc::evaluate(missingFace).succeeded,
                 "empty selection is an error");

    Proc::Curve corner; corner.points = {{0,0,0},{2,0,0},{2,0,2}};
    Proc::Curve rounded;
    const bool smoothedCurve = Proc::smoothCurve(corner, 8, rounded, error);
    bool keepsControlPoints = smoothedCurve && rounded.points.size() == 17 && rounded.points[8] == corner.points[1] &&
                              rounded.points.back() == corner.points.back();
    report.check("Curve Smooth passes through every control point", keepsControlPoints, error);

    Proc::Curve hanging;
    Proc::CatenaryCurveNode level{{0,5,0},{10,5,0},2.0f,33};
    const bool hangingMade = Proc::makeCatenary(level, hanging, error);
    const float midDrop = hangingMade ? 5.0f - hanging.points[16].y : 0.0f;
    const double catenaryA = [&]{
        double low = 0.1, high = 1000.0;
        for(int i = 0; i < 200; ++i){ const double a = std::sqrt(low*high); if(a*(std::cosh(5.0/a)-1.0) > 2.0) low = a; else high = a; }
        return std::sqrt(low*high);
    }();
    const double quarterExpected = 5.0 - (2.0 - catenaryA*(std::cosh(2.5/catenaryA)-1.0));
    report.check("Catenary drops by sag at mid-span and follows a cosh shape",
                 hangingMade && std::abs(midDrop - 2.0f) < 1e-4f && std::abs(hanging.points.front().y - 5.0f) < 1e-5f &&
                 std::abs(double(hanging.points[8].y) - quarterExpected) < 1e-3,
                 "mid drop " + std::to_string(midDrop));

    Proc::Graph chainRecipe;
    const auto chainPath = Proc::addNode(chainRecipe, Proc::CatenaryCurveNode{{0,2,0},{2,2,0},0.0f,2});
    Proc::AddPrimitiveNode linkShape; linkShape.primitive = Proc::PrimitiveType::Torus; linkShape.size = {0.2f,0.2f,0.1f};
    const auto chainLink = Proc::addNode(chainRecipe, linkShape);
    Proc::CopyAlongCurveNode chainCopies; chainCopies.spacing = 0.5f; chainCopies.alternateRollDegrees = 90.0f;
    const auto chainNode = Proc::addNode(chainRecipe, chainCopies);
    chainRecipe.links = {{chainLink,0,chainNode,0},{chainPath,0,chainNode,1}};
    const Proc::EvaluationResult chainResult = Proc::evaluate(chainRecipe);
    bool secondLinkRolled = false;
    if(chainResult.succeeded && !torus.vertices.empty()){
        Proc::MeshData one;
        Proc::makePrimitive(linkShape, one, error);
        const std::size_t perLink = one.vertices.size();
        glm::vec3 low(1e9f), high(-1e9f);
        for(std::size_t i = perLink; i < 2 * perLink; ++i){
            low = glm::min(low, chainResult.mesh.vertices[i].position);
            high = glm::max(high, chainResult.mesh.vertices[i].position);
        }
        // Rolled 90 degrees about X: the link's thin Z depth now spans Y.
        secondLinkRolled = std::abs((high.z - low.z) - 0.2f * 0.25f) < 2e-3f && std::abs((high.y - low.y) - 0.1f) < 2e-3f &&
                           std::abs((low.x + high.x) * 0.5f - 0.5f) < 1e-4f;
    }
    report.check("Copy along Curve spaces copies by arc length and alternates the roll",
                 chainResult.succeeded && chainResult.mesh.triangles.size() % 5 == 0 && secondLinkRolled, chainResult.error);

    Proc::Graph curveRecipe;
    Proc::addNode(curveRecipe, Proc::CurveSmoothNode{4});
    Proc::addNode(curveRecipe, Proc::CatenaryCurveNode{});
    Proc::addNode(curveRecipe, chainCopies);
    Proc::addNode(curveRecipe, linkShape);
    Proc::addNode(curveRecipe, upward);
    bool curveRoundTrip = false;
    std::string curveJson;
    try{
        curveJson = Loom::WeaverProceduraRecipe::serialize({"Curves", curveRecipe});
        const auto back = Loom::WeaverProceduraRecipe::parse(curveJson).graph;
        curveRoundTrip = std::get<Proc::CurveSmoothNode>(back.nodes[0].payload).subdivisions == 4 &&
            std::abs(std::get<Proc::CopyAlongCurveNode>(back.nodes[2].payload).alternateRollDegrees - 90.0f) < 1e-6f &&
            std::get<Proc::AddPrimitiveNode>(back.nodes[3].payload).primitive == Proc::PrimitiveType::Torus &&
            std::get<Proc::ExtrudeNode>(back.nodes[4].payload).useFilter;
    }catch(const std::exception& failure){ curveJson = failure.what(); }
    report.check("schema 6 Recipe round-trips curve nodes, torus, and filtered Extrude", curveRoundTrip, curveJson);

    return report.result();
}
