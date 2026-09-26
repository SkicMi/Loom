#include "WeaverProcedura.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Engine::WeaverProcedura{
namespace{

constexpr double pointEpsilon = 1e-6;
constexpr double vectorEpsilonSquared = 1e-16;
constexpr double pi = 3.1415926535897932384626433832795;

bool finite(float value){return std::isfinite(value);}
bool finite(const glm::vec2& value){return finite(value.x) && finite(value.y);}
bool finite(const glm::vec3& value){return finite(value.x) && finite(value.y) && finite(value.z);}

double distance(const glm::vec3& a, const glm::vec3& b){
    const double x = double(b.x) - double(a.x);
    const double y = double(b.y) - double(a.y);
    const double z = double(b.z) - double(a.z);
    return std::sqrt(x * x + y * y + z * z);
}

double distance(const glm::vec2& a, const glm::vec2& b){
    const double x = double(b.x) - double(a.x);
    const double y = double(b.y) - double(a.y);
    return std::sqrt(x * x + y * y);
}

bool normalized(const glm::vec3& value, glm::vec3& result){
    if(!finite(value)) return false;
    const double x = value.x, y = value.y, z = value.z;
    const double length = std::sqrt(x * x + y * y + z * z);
    if(!std::isfinite(length) || length <= pointEpsilon) return false;
    result = glm::vec3(float(x / length), float(y / length), float(z / length));
    return finite(result);
}

bool direction(const glm::vec3& from, const glm::vec3& to, glm::vec3& result){
    const double x = double(to.x) - double(from.x);
    const double y = double(to.y) - double(from.y);
    const double z = double(to.z) - double(from.z);
    const double length = std::sqrt(x * x + y * y + z * z);
    if(!std::isfinite(length) || length <= pointEpsilon) return false;
    result = glm::vec3(float(x / length), float(y / length), float(z / length));
    return finite(result);
}

bool transportSide(const glm::vec3& side, const glm::vec3& fromTangent,
                   const glm::vec3& toTangent, glm::vec3& result){
    const float cosine = std::clamp(glm::dot(fromTangent, toTangent), -1.0f, 1.0f);
    if(cosine < -0.9999f) return false;

    const glm::vec3 axis = glm::cross(fromTangent, toTangent);
    const float sineSquared = glm::dot(axis, axis);
    if(sineSquared > float(vectorEpsilonSquared)){
        const float sine = std::sqrt(sineSquared);
        const glm::vec3 unitAxis = axis / sine;
        result = side * cosine + glm::cross(unitAxis, side) * sine +
                 unitAxis * glm::dot(unitAxis, side) * (1.0f - cosine);
    }else{
        result = side;
    }

    result -= toTangent * glm::dot(result, toTangent);
    return normalized(result, result);
}

glm::vec3 rotateAround(const glm::vec3& value, const glm::vec3& axis, float angle){
    const float cosine = std::cos(angle), sine = std::sin(angle);
    return value * cosine + glm::cross(axis, value) * sine + axis * glm::dot(axis, value) * (1.0f - cosine);
}

bool prepareProfile(const Profile& source, bool capEnds, std::vector<glm::vec2>& points,
                    std::vector<double>& distanceAtPoint, double& perimeter,
                    double& minX, double& minY, double& maxX, double& maxY, std::string& error){
    points.reserve(source.points.size());
    for(const glm::vec2& point : source.points){
        if(!finite(point)){ error = "profile points must be finite"; return false; }
        if(points.empty() || distance(points.back(), point) > pointEpsilon) points.push_back(point);
    }
    if(points.size() > 1 && distance(points.front(), points.back()) <= pointEpsilon) points.pop_back();
    if(points.size() < 3){ error = "profile needs at least three distinct points"; return false; }

    double twiceArea = 0.0;
    perimeter = 0.0;
    minX = maxX = points.front().x;
    minY = maxY = points.front().y;
    for(std::size_t i = 0; i < points.size(); ++i){
        const glm::vec2& a = points[i];
        const glm::vec2& b = points[(i + 1) % points.size()];
        twiceArea += double(a.x) * b.y - double(b.x) * a.y;
        perimeter += distance(a, b);
        minX = std::min(minX, double(a.x)); maxX = std::max(maxX, double(a.x));
        minY = std::min(minY, double(a.y)); maxY = std::max(maxY, double(a.y));
    }
    if(std::fabs(twiceArea) <= pointEpsilon || !std::isfinite(perimeter) || perimeter <= pointEpsilon){
        error = "profile must enclose a non-zero area";
        return false;
    }
    if(twiceArea < 0.0) std::reverse(points.begin(), points.end());

    if(capEnds){
        int turnSign = 0;
        double totalTurn = 0.0;
        for(std::size_t i = 0; i < points.size(); ++i){
            const glm::vec2 a = points[i];
            const glm::vec2 b = points[(i + 1) % points.size()];
            const glm::vec2 c = points[(i + 2) % points.size()];
            const double inX = double(b.x) - a.x, inY = double(b.y) - a.y;
            const double outX = double(c.x) - b.x, outY = double(c.y) - b.y;
            const double turn = inX * outY - inY * outX;
            const double alignment = inX * outX + inY * outY;
            if(std::fabs(turn) <= pointEpsilon){
                if(alignment < 0.0){ error = "end caps require a convex profile"; return false; }
                continue;
            }
            const int sign = turn > 0.0 ? 1 : -1;
            if(turnSign != 0 && sign != turnSign){ error = "end caps require a convex profile"; return false; }
            turnSign = sign;
            totalTurn += std::atan2(turn, alignment);
        }
        const double turnTolerance = std::max(1e-5, double(points.size()) * 1e-7);
        if(turnSign <= 0 || std::fabs(totalTurn - 2.0 * pi) > turnTolerance){
            error = "end caps require a convex profile";
            return false;
        }
    }

    distanceAtPoint.assign(points.size() + 1, 0.0);
    for(std::size_t i = 0; i < points.size(); ++i)
        distanceAtPoint[i + 1] = distanceAtPoint[i] + distance(points[i], points[(i + 1) % points.size()]);
    perimeter = distanceAtPoint.back();
    if(!std::isfinite(perimeter) || perimeter <= pointEpsilon){ error = "profile perimeter is invalid"; return false; }
    return true;
}

bool sampleCurve(const Curve& curve, float spacing, std::size_t ringLimit,
                 std::vector<glm::vec3>& points, std::vector<double>& distanceAtPoint,
                 double& totalLength, std::string& error){
    if(!finite(spacing) || spacing <= 0.0f){ error = "sample spacing must be positive and finite"; return false; }

    std::vector<glm::vec3> control;
    control.reserve(curve.points.size());
    for(const glm::vec3& point : curve.points){
        if(!finite(point)){ error = "curve points must be finite"; return false; }
        if(control.empty() || distance(control.back(), point) > pointEpsilon) control.push_back(point);
    }
    if(curve.closed && control.size() > 1 && distance(control.front(), control.back()) <= pointEpsilon)
        control.pop_back();
    if(!curve.closed && control.size() > 2 && distance(control.front(), control.back()) <= pointEpsilon){
        error = "open curve endpoints coincide; mark the curve closed or move an endpoint";
        return false;
    }
    if((curve.closed && control.size() < 3) || (!curve.closed && control.size() < 2)){
        error = curve.closed ? "a closed curve needs at least three distinct points" : "an open curve needs at least two distinct points";
        return false;
    }

    const std::size_t segmentCount = curve.closed ? control.size() : control.size() - 1;
    std::vector<double> lengths(segmentCount);
    std::vector<std::size_t> intervals(segmentCount);
    totalLength = 0.0;
    std::size_t totalIntervals = 0;
    for(std::size_t i = 0; i < segmentCount; ++i){
        const double length = distance(control[i], control[(i + 1) % control.size()]);
        if(!std::isfinite(length) || length <= pointEpsilon){ error = "curve contains a degenerate segment"; return false; }
        const double requested = std::ceil(length / double(spacing));
        if(!std::isfinite(requested) || requested < 1.0 || requested > double(ringLimit)){
            error = "sweep exceeds the configured vertex limit";
            return false;
        }
        const std::size_t pieces = std::size_t(requested);
        if(totalIntervals > ringLimit - pieces){ error = "sweep exceeds the configured vertex limit"; return false; }
        lengths[i] = length;
        intervals[i] = pieces;
        totalIntervals += pieces;
        totalLength += length;
    }
    if(!std::isfinite(totalLength) || totalLength <= pointEpsilon || totalLength > std::numeric_limits<float>::max()){
        error = "curve length is invalid or too large for mesh UVs";
        return false;
    }

    const std::size_t ringCount = totalIntervals + (curve.closed ? 0u : 1u);
    const std::size_t meshRingCount = ringCount + (curve.closed ? 1u : 0u);
    if(meshRingCount > ringLimit || ringCount < (curve.closed ? 3u : 2u)){
        error = "sweep exceeds the configured vertex limit";
        return false;
    }
    points.reserve(ringCount);
    distanceAtPoint.reserve(ringCount);
    double traveled = 0.0;
    for(std::size_t segment = 0; segment < segmentCount; ++segment){
        const glm::vec3& a = control[segment];
        const glm::vec3& b = control[(segment + 1) % control.size()];
        for(std::size_t step = 0; step < intervals[segment]; ++step){
            const double fraction = double(step) / double(intervals[segment]);
            const glm::vec3 point = glm::vec3(double(a.x) + (double(b.x) - a.x) * fraction,
                                              double(a.y) + (double(b.y) - a.y) * fraction,
                                              double(a.z) + (double(b.z) - a.z) * fraction);
            points.push_back(point);
            distanceAtPoint.push_back(traveled + lengths[segment] * fraction);
        }
        traveled += lengths[segment];
    }
    if(!curve.closed){
        points.push_back(control.back());
        distanceAtPoint.push_back(totalLength);
    }
    return true;
}

bool appendBox(MeshData& mesh, const glm::vec3& center, const glm::vec3& size,
               std::size_t maxVertices, std::string& error){
    if(!finite(center) || !finite(size) || size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f){
        error = "box dimensions and center must be finite and positive";
        return false;
    }
    if(mesh.vertices.size() > maxVertices || maxVertices - mesh.vertices.size() < 24){
        error = "generated mesh exceeds the configured vertex limit";
        return false;
    }
    struct Face{ glm::vec3 normal, u, v; float halfNormal, halfU, halfV; };
    const glm::vec3 half = size * 0.5f;
    const Face faces[] = {
        {{ 1, 0, 0}, { 0, 0,-1}, { 0, 1, 0}, half.x, half.z, half.y},
        {{-1, 0, 0}, { 0, 0, 1}, { 0, 1, 0}, half.x, half.z, half.y},
        {{ 0, 1, 0}, { 1, 0, 0}, { 0, 0,-1}, half.y, half.x, half.z},
        {{ 0,-1, 0}, { 1, 0, 0}, { 0, 0, 1}, half.y, half.x, half.z},
        {{ 0, 0, 1}, { 1, 0, 0}, { 0, 1, 0}, half.z, half.x, half.y},
        {{ 0, 0,-1}, {-1, 0, 0}, { 0, 1, 0}, half.z, half.x, half.y},
    };
    for(const Face& face : faces){
        const uint32_t base = uint32_t(mesh.vertices.size());
        const glm::vec3 faceCenter = center + face.normal * face.halfNormal;
        const glm::vec3 corners[] = {
            faceCenter - face.u * face.halfU - face.v * face.halfV,
            faceCenter + face.u * face.halfU - face.v * face.halfV,
            faceCenter + face.u * face.halfU + face.v * face.halfV,
            faceCenter - face.u * face.halfU + face.v * face.halfV,
        };
        const float uvU = std::max(0.001f, face.halfU * 2.0f);
        const float uvV = std::max(0.001f, face.halfV * 2.0f);
        const glm::vec2 uvs[] = {{0,0}, {uvU,0}, {uvU,uvV}, {0,uvV}};
        for(int i = 0; i < 4; ++i){
            if(!finite(corners[i])){ error = "generated box vertex is not finite"; return false; }
            mesh.vertices.push_back({corners[i], face.normal, uvs[i]});
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2,
                                                  base, base + 2, base + 3});
    }
    return true;
}

bool validGridSettings(const GridNode& grid){
    return finite(grid.width) && finite(grid.depth) && grid.width > 0.0f && grid.depth > 0.0f &&
           grid.width <= 10000.0f && grid.depth <= 10000.0f && grid.cellsX >= 1 && grid.cellsZ >= 1 &&
           grid.cellsX <= 128 && grid.cellsZ <= 128 &&
           std::size_t(grid.cellsX + 1) * std::size_t(grid.cellsZ + 1) <= 16384;
}

bool validInteriorSettings(const InteriorBlockoutNode& room){
    const float values[] = {room.roomWidth, room.roomDepth, room.corridorWidth, room.wallHeight,
                            room.wallThickness, room.floorThickness, room.doorWidth};
    for(float value : values) if(!finite(value) || value <= 0.0f || value > 1000.0f) return false;
    return room.roomsPerSide >= 1 && room.roomsPerSide <= 8 &&
           room.doorWidth < room.roomWidth - 2.0f * room.wallThickness &&
           room.wallThickness * 2.0f < std::min(room.roomDepth, room.corridorWidth) &&
           room.floorThickness < room.wallHeight;
}

bool validMesh(const MeshData& mesh){
    if(mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0) return false;
    for(const MeshVertex& vertex : mesh.vertices)
        if(!finite(vertex.position) || !finite(vertex.normal) || !finite(vertex.uv)) return false;
    for(uint32_t index : mesh.indices) if(index >= mesh.vertices.size()) return false;
    if(!mesh.triangles.empty() && mesh.triangles.size() != mesh.indices.size() / 3) return false;
    return true;
}

TriangleAttributes attributesOf(const MeshData& mesh, std::size_t triangle){
    return triangle < mesh.triangles.size() ? mesh.triangles[triangle] : TriangleAttributes{};
}

// Triangles a node adds carry createdBy == 0 until the evaluator stamps them.
void stampProvenance(MeshData& mesh, NodeId node){
    mesh.triangles.resize(mesh.indices.size() / 3);
    for(TriangleAttributes& triangle : mesh.triangles)
        if(triangle.createdBy == 0) triangle.createdBy = node;
}

struct FacePatch{
    std::vector<std::size_t> triangles;
    std::vector<uint32_t> boundary;
    glm::vec3 normal{0.0f};
};

uint64_t edgeKey(uint32_t a, uint32_t b){
    const uint32_t lo = std::min(a, b), hi = std::max(a, b);
    return (uint64_t(lo) << 32u) | uint64_t(hi);
}

bool collectFacePatches(const MeshData& mesh, std::vector<FacePatch>& output, std::string& error){
    if(!validMesh(mesh)){ error = "mesh topology or vertex data is invalid"; return false; }
    const std::size_t triangleCount = mesh.indices.size() / 3;
    std::vector<glm::vec3> normals(triangleCount);
    std::unordered_map<uint64_t, std::vector<std::size_t>> edgeTriangles;
    edgeTriangles.reserve(triangleCount * 2);
    for(std::size_t triangle = 0; triangle < triangleCount; ++triangle){
        const uint32_t a = mesh.indices[triangle * 3], b = mesh.indices[triangle * 3 + 1], c = mesh.indices[triangle * 3 + 2];
        const glm::vec3 crossValue = glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                                                 mesh.vertices[c].position - mesh.vertices[a].position);
        if(!normalized(crossValue, normals[triangle])){ error = "mesh contains a degenerate triangle"; return false; }
        edgeTriangles[edgeKey(a,b)].push_back(triangle);
        edgeTriangles[edgeKey(b,c)].push_back(triangle);
        edgeTriangles[edgeKey(c,a)].push_back(triangle);
    }

    std::vector<uint8_t> assigned(triangleCount, 0);
    std::vector<FacePatch> patches;
    for(std::size_t seed = 0; seed < triangleCount; ++seed){
        if(assigned[seed]) continue;
        FacePatch patch;
        patch.normal = normals[seed];
        std::queue<std::size_t> pending;
        pending.push(seed);
        assigned[seed] = 1;
        while(!pending.empty()){
            const std::size_t triangle = pending.front(); pending.pop();
            patch.triangles.push_back(triangle);
            const uint32_t ids[] = {mesh.indices[triangle * 3], mesh.indices[triangle * 3 + 1], mesh.indices[triangle * 3 + 2]};
            for(int edge = 0; edge < 3; ++edge){
                const auto found = edgeTriangles.find(edgeKey(ids[edge], ids[(edge + 1) % 3]));
                if(found == edgeTriangles.end()) continue;
                for(std::size_t candidate : found->second){
                    if(assigned[candidate] || glm::dot(normals[seed], normals[candidate]) < 0.9999f) continue;
                    const glm::vec3 origin = mesh.vertices[ids[0]].position;
                    const uint32_t candidateId = mesh.indices[candidate * 3];
                    if(std::abs(glm::dot(mesh.vertices[candidateId].position - origin, normals[seed])) > 1e-4f) continue;
                    assigned[candidate] = 1;
                    pending.push(candidate);
                }
            }
        }

        std::unordered_map<uint64_t, std::pair<uint32_t,uint32_t>> boundaryEdges;
        std::unordered_map<uint64_t, uint32_t> edgeCounts;
        for(std::size_t triangle : patch.triangles){
            const uint32_t ids[] = {mesh.indices[triangle * 3], mesh.indices[triangle * 3 + 1], mesh.indices[triangle * 3 + 2]};
            for(int edge = 0; edge < 3; ++edge){
                const uint64_t key = edgeKey(ids[edge], ids[(edge + 1) % 3]);
                ++edgeCounts[key];
                boundaryEdges[key] = {ids[edge], ids[(edge + 1) % 3]};
            }
        }
        std::unordered_map<uint32_t, uint32_t> next;
        for(const auto& [key, count] : edgeCounts){
            if(count == 1){
                const auto directed = boundaryEdges.at(key);
                if(!next.emplace(directed.first, directed.second).second){ error = "mesh face boundary is not a simple loop"; return false; }
            }else if(count > 2){ error = "mesh has a non-manifold face edge"; return false; }
        }
        if(next.size() < 3){ error = "mesh face has no closed boundary"; return false; }
        uint32_t current = next.begin()->first;
        const uint32_t start = current;
        std::unordered_set<uint32_t> visited;
        do{
            if(!visited.insert(current).second){ error = "mesh face boundary is not a simple loop"; return false; }
            patch.boundary.push_back(current);
            const auto found = next.find(current);
            if(found == next.end()){ error = "mesh face boundary is open"; return false; }
            current = found->second;
        }while(current != start && patch.boundary.size() <= next.size());
        if(current != start || patch.boundary.size() != next.size()){
            error = "mesh face boundary contains multiple loops";
            return false;
        }
        patches.push_back(std::move(patch));
    }
    output = std::move(patches);
    error.clear();
    return true;
}

bool insetPolygon(const MeshData& mesh, const FacePatch& patch, float amount,
                  std::vector<glm::vec3>& output, std::string& error){
    const std::size_t count = patch.boundary.size();
    std::vector<glm::vec3> points(count);
    for(std::size_t i = 0; i < count; ++i) points[i] = mesh.vertices[patch.boundary[i]].position;
    double signedArea = 0.0;
    const glm::vec3 origin = points.front();
    for(std::size_t i = 0; i < count; ++i)
        signedArea += double(glm::dot(glm::cross(points[i]-origin, points[(i + 1) % count]-origin), patch.normal));
    if(std::abs(signedArea) <= pointEpsilon){ error = "mesh face area is zero"; return false; }
    const float winding = signedArea > 0.0 ? 1.0f : -1.0f;
    int turnDirection = 0;
    for(std::size_t i = 0; i < count; ++i){
        const glm::vec3 first = points[(i+1)%count]-points[i];
        const glm::vec3 second = points[(i+2)%count]-points[(i+1)%count];
        const float turn = glm::dot(glm::cross(first,second),patch.normal) * winding;
        if(std::abs(turn) <= 1e-6f) continue;
        if(turnDirection == 0) turnDirection = turn > 0.0f ? 1 : -1;
        else if((turn > 0.0f ? 1 : -1) != turnDirection){
            error = "bevel supports convex planar faces only";
            return false;
        }
    }
    if(turnDirection <= 0){ error = "bevel supports convex planar faces only"; return false; }
    std::vector<glm::vec3> inset(count);
    for(std::size_t i = 0; i < count; ++i){
        glm::vec3 incoming, outgoing;
        if(!direction(points[(i + count - 1) % count], points[i], incoming) ||
           !direction(points[i], points[(i + 1) % count], outgoing)){
            error = "mesh face has a degenerate boundary edge";
            return false;
        }
        const glm::vec3 inwardA = winding * glm::cross(patch.normal, incoming);
        const glm::vec3 inwardB = winding * glm::cross(patch.normal, outgoing);
        glm::vec3 bisector;
        if(!normalized(inwardA + inwardB, bisector)){ error = "mesh face has an invalid corner"; return false; }
        const float denominator = glm::dot(bisector, inwardB);
        if(denominator <= 1e-4f){ error = "bevel does not support this concave face boundary"; return false; }
        const float miter = amount / denominator;
        if(!finite(miter) || miter > amount * 8.0f){ error = "bevel amount is too large for this face"; return false; }
        inset[i] = points[i] + bisector * miter;
    }
    for(const glm::vec3& candidate : inset){
        for(std::size_t edge = 0; edge < count; ++edge){
            glm::vec3 tangent;
            if(!direction(points[edge],points[(edge+1)%count],tangent)){
                error = "mesh face has a degenerate boundary edge";
                return false;
            }
            const glm::vec3 inward = winding * glm::cross(patch.normal,tangent);
            if(glm::dot(candidate-points[edge],inward) < -1e-5f){
                error = "bevel amount is too large for this face";
                return false;
            }
        }
    }
    output = std::move(inset);
    return true;
}

enum class PortType{ Invalid, Curve, Profile, PointGrid, Mesh, Points };

PortType outputType(const Node& node, uint32_t port){
    if(port != 0) return PortType::Invalid;
    if(std::holds_alternative<CurveNode>(node.payload)) return PortType::Curve;
    if(std::holds_alternative<RectangleProfileNode>(node.payload) ||
       std::holds_alternative<CircleProfileNode>(node.payload)) return PortType::Profile;
    if(std::holds_alternative<GridNode>(node.payload) ||
       std::holds_alternative<SetGridPointHeightNode>(node.payload)) return PortType::PointGrid;
    if(std::holds_alternative<SweepNode>(node.payload) ||
       std::holds_alternative<GridToMeshNode>(node.payload) ||
       std::holds_alternative<InteriorBlockoutNode>(node.payload) ||
       std::holds_alternative<AddPrimitiveNode>(node.payload)) return PortType::Mesh;
    if(std::holds_alternative<MeshToPointNode>(node.payload) ||
       std::holds_alternative<PointFromMeshNode>(node.payload)) return PortType::Points;
    if(std::holds_alternative<MoveNode>(node.payload) || std::holds_alternative<RotateNode>(node.payload) ||
       std::holds_alternative<ScaleNode>(node.payload) || std::holds_alternative<ExtrudeNode>(node.payload) ||
       std::holds_alternative<BevelNode>(node.payload)) return PortType::Mesh;
    if(std::holds_alternative<MergeNode>(node.payload) || std::holds_alternative<SetSemanticNode>(node.payload) ||
       std::holds_alternative<SetMaterialNode>(node.payload) || std::holds_alternative<SmoothNormalsNode>(node.payload) ||
       std::holds_alternative<UVProjectNode>(node.payload) || std::holds_alternative<CopyToPointsNode>(node.payload))
        return PortType::Mesh;
    return PortType::Invalid;
}

PortType inputType(const Node& node, uint32_t port){
    if(std::holds_alternative<SweepNode>(node.payload)){
        if(port == 0) return PortType::Curve;
        if(port == 1) return PortType::Profile;
    }
    if(std::holds_alternative<SetGridPointHeightNode>(node.payload) ||
       std::holds_alternative<GridToMeshNode>(node.payload))
        if(port == 0) return PortType::PointGrid;
    if((std::holds_alternative<MoveNode>(node.payload) || std::holds_alternative<RotateNode>(node.payload) ||
        std::holds_alternative<ScaleNode>(node.payload) || std::holds_alternative<ExtrudeNode>(node.payload) ||
        std::holds_alternative<BevelNode>(node.payload) || std::holds_alternative<MeshToPointNode>(node.payload) ||
        std::holds_alternative<PointFromMeshNode>(node.payload) || std::holds_alternative<SetSemanticNode>(node.payload) ||
        std::holds_alternative<SetMaterialNode>(node.payload) || std::holds_alternative<SmoothNormalsNode>(node.payload) ||
        std::holds_alternative<UVProjectNode>(node.payload)) && port == 0) return PortType::Mesh;
    if(std::holds_alternative<MergeNode>(node.payload) && port < mergeInputCount) return PortType::Mesh;
    if(std::holds_alternative<CopyToPointsNode>(node.payload)){
        if(port == 0) return PortType::Mesh;
        if(port == 1) return PortType::Points;
    }
    return PortType::Invalid;
}

glm::vec3 rotateEulerDegrees(const glm::vec3& value, const glm::vec3& degrees){
    const glm::vec3 radians = degrees * float(pi / 180.0);
    glm::vec3 result = rotateAround(value, {1,0,0}, radians.x);
    result = rotateAround(result, {0,1,0}, radians.y);
    return rotateAround(result, {0,0,1}, radians.z);
}

bool transformMesh(const MeshData& input, const glm::vec3& scale, const glm::vec3& degrees,
                   const glm::vec3& offset, const glm::vec3& pivot,
                   MeshData& output, std::string& error){
    if(!validMesh(input)){ error = "mesh topology or vertex data is invalid"; return false; }
    if(!finite(scale) || !finite(degrees) || !finite(offset) || !finite(pivot) ||
       std::abs(scale.x) < 1e-6f || std::abs(scale.y) < 1e-6f || std::abs(scale.z) < 1e-6f){
        error = "transform values must be finite and scale cannot be zero";
        return false;
    }
    MeshData generated = input;
    for(MeshVertex& vertex : generated.vertices){
        glm::vec3 position = pivot + (vertex.position - pivot) * scale;
        position = pivot + rotateEulerDegrees(position - pivot, degrees) + offset;
        const glm::vec3 scaledNormal(vertex.normal.x / scale.x,
                                     vertex.normal.y / scale.y,
                                     vertex.normal.z / scale.z);
        glm::vec3 normal;
        if(!finite(position) || !normalized(rotateEulerDegrees(scaledNormal, degrees), normal)){
            error = "transform produced a non-finite position or invalid normal";
            return false;
        }
        vertex.position = position;
        vertex.normal = normal;
    }
    if(scale.x * scale.y * scale.z < 0.0f){
        for(std::size_t i = 0; i < generated.indices.size(); i += 3)
            std::swap(generated.indices[i + 1], generated.indices[i + 2]);
    }
    output = std::move(generated);
    error.clear();
    return true;
}

void appendOrientedTriangle(MeshData& mesh, uint32_t a, uint32_t b, uint32_t c,
                            const glm::vec3& desiredNormal){
    const glm::vec3 crossValue = glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                                             mesh.vertices[c].position - mesh.vertices[a].position);
    if(glm::dot(crossValue, desiredNormal) < 0.0f) std::swap(b,c);
    mesh.indices.insert(mesh.indices.end(), {a,b,c});
}

bool validPrimitiveType(PrimitiveType primitive){
    switch(primitive){
        case PrimitiveType::Cube: case PrimitiveType::Plane: case PrimitiveType::Sphere:
        case PrimitiveType::Pyramid: case PrimitiveType::Capsule: return true;
    }
    return false;
}

}

NodeId addNode(Graph& graph, NodePayload payload, float editorX, float editorY){
    if(graph.nextNodeId == 0 || std::holds_alternative<std::monostate>(payload) ||
       !finite(editorX) || !finite(editorY)) return 0;

    NodeId id = graph.nextNodeId;
    for(const Node& node : graph.nodes){
        if(id <= node.id){
            if(node.id == std::numeric_limits<NodeId>::max()) return 0;
            id = node.id + 1;
        }
    }
    graph.nextNodeId = id == std::numeric_limits<NodeId>::max() ? 0 : id + 1;
    graph.nodes.push_back({id, std::move(payload), editorX, editorY});
    return id;
}

bool makeGrid(const GridNode& settings, PointGrid& output, std::string& error){
    if(!validGridSettings(settings)){
        error = "grid dimensions must be positive and contain at most 16384 points";
        return false;
    }
    PointGrid generated;
    generated.cellsX = settings.cellsX;
    generated.cellsZ = settings.cellsZ;
    generated.points.reserve(std::size_t(settings.cellsX + 1) * std::size_t(settings.cellsZ + 1));
    for(uint32_t row = 0; row <= settings.cellsZ; ++row){
        const float z = -settings.depth * 0.5f + settings.depth * float(row) / float(settings.cellsZ);
        for(uint32_t column = 0; column <= settings.cellsX; ++column){
            const float x = -settings.width * 0.5f + settings.width * float(column) / float(settings.cellsX);
            generated.points.emplace_back(x, 0.0f, z);
        }
    }
    output = std::move(generated);
    error.clear();
    return true;
}

bool gridToMesh(const PointGrid& grid, MeshData& output, std::string& error, std::size_t maxVertices){
    if(grid.cellsX == 0 || grid.cellsZ == 0 || grid.cellsX > 128 || grid.cellsZ > 128 ||
       std::size_t(grid.cellsX + 1) * std::size_t(grid.cellsZ + 1) != grid.points.size()){
        error = "point grid dimensions do not match its point data";
        return false;
    }
    if(grid.points.size() > maxVertices || grid.points.size() > std::size_t(std::numeric_limits<uint32_t>::max())){
        error = "grid mesh exceeds the configured vertex limit";
        return false;
    }
    for(const glm::vec3& point : grid.points){
        if(!finite(point)){ error = "grid point coordinates must be finite"; return false; }
    }

    MeshData generated;
    generated.vertices.resize(grid.points.size());
    generated.indices.reserve(std::size_t(grid.cellsX) * grid.cellsZ * 6);
    for(uint32_t row = 0; row <= grid.cellsZ; ++row){
        for(uint32_t column = 0; column <= grid.cellsX; ++column){
            const std::size_t index = std::size_t(row) * (grid.cellsX + 1) + column;
            generated.vertices[index].position = grid.points[index];
            generated.vertices[index].uv = glm::vec2(float(column) / grid.cellsX, float(row) / grid.cellsZ);
        }
    }
    for(uint32_t row = 0; row < grid.cellsZ; ++row){
        for(uint32_t column = 0; column < grid.cellsX; ++column){
            const uint32_t a = row * (grid.cellsX + 1) + column;
            const uint32_t b = a + 1;
            const uint32_t c = a + grid.cellsX + 1;
            const uint32_t d = c + 1;
            // XZ rows wind toward +Y for the viewport's right-handed world.
            generated.indices.insert(generated.indices.end(), {a, c, b, b, c, d});
        }
    }
    for(std::size_t i = 0; i < generated.indices.size(); i += 3){
        const uint32_t a = generated.indices[i], b = generated.indices[i + 1], c = generated.indices[i + 2];
        const glm::vec3 normal = glm::cross(generated.vertices[b].position - generated.vertices[a].position,
                                            generated.vertices[c].position - generated.vertices[a].position);
        if(!finite(normal) || glm::dot(normal, normal) <= float(vectorEpsilonSquared)){
            error = "grid contains a degenerate cell";
            return false;
        }
        generated.vertices[a].normal += normal;
        generated.vertices[b].normal += normal;
        generated.vertices[c].normal += normal;
    }
    for(MeshVertex& vertex : generated.vertices){
        glm::vec3 normal;
        if(!normalized(vertex.normal, normal)){ error = "grid produced a degenerate normal"; return false; }
        vertex.normal = normal;
    }
    output = std::move(generated);
    error.clear();
    return true;
}

bool makeInteriorBlockout(const InteriorBlockoutNode& settings, MeshData& output,
                          std::string& error, std::size_t maxVertices){
    if(!validInteriorSettings(settings)){
        error = "interior dimensions are invalid or door opening does not fit a room wall";
        return false;
    }
    const float length = settings.roomsPerSide * settings.roomWidth;
    const float halfLength = length * 0.5f;
    const float corridorHalf = settings.corridorWidth * 0.5f;
    const float outerZ = corridorHalf + settings.roomDepth;
    const float wallY = settings.wallHeight * 0.5f;
    const float wallZ = settings.wallThickness * 0.5f;
    MeshData generated;
    const uint16_t floorTag = semanticId("floor");
    const uint16_t exteriorTag = semanticId("wall_exterior");
    const uint16_t interiorTag = semanticId("wall_interior");
    auto box = [&](float x, float y, float z, float sx, float sy, float sz, uint16_t semantic){
        if(!appendBox(generated, {x,y,z}, {sx,sy,sz}, maxVertices, error)) return false;
        generated.triangles.resize(generated.indices.size() / 3, TriangleAttributes{0, semantic, 0});
        return true;
    };
    auto wallAlongX = [&](float start, float end, float z, uint16_t semantic){
        if(end - start <= 1e-4f) return true;
        return box((start + end) * 0.5f, wallY, z, end - start, settings.wallHeight, settings.wallThickness, semantic);
    };
    auto wallAlongZ = [&](float x, float start, float end, uint16_t semantic){
        if(end - start <= 1e-4f) return true;
        return box(x, wallY, (start + end) * 0.5f, settings.wallThickness, settings.wallHeight, end - start, semantic);
    };

    // A continuous floor slab makes the first result easy to read as a blockout.
    if(!box(0.0f, -settings.floorThickness * 0.5f, 0.0f, length,
            settings.floorThickness, outerZ * 2.0f, floorTag)) return false;

    // Outer long walls and room-end walls; the two corridor ends stay open.
    if(!wallAlongX(-halfLength, halfLength, outerZ - wallZ, exteriorTag) ||
       !wallAlongX(-halfLength, halfLength, -outerZ + wallZ, exteriorTag)) return false;
    for(int side : {-1, 1}){
        const float roomStartZ = side > 0 ? corridorHalf + wallZ : -outerZ + wallZ;
        const float roomEndZ = side > 0 ? outerZ - wallZ : -corridorHalf - wallZ;
        if(!wallAlongZ(-halfLength + wallZ, roomStartZ, roomEndZ, exteriorTag) ||
           !wallAlongZ(halfLength - wallZ, roomStartZ, roomEndZ, exteriorTag)) return false;

        // Room-to-corridor walls have a centered door opening in each bay.
        for(uint32_t room = 0; room < settings.roomsPerSide; ++room){
            const float x0 = -halfLength + room * settings.roomWidth;
            const float x1 = x0 + settings.roomWidth;
            const float center = (x0 + x1) * 0.5f;
            const float gapHalf = settings.doorWidth * 0.5f;
            const float corridorWallZ = side > 0 ? corridorHalf + wallZ : -corridorHalf - wallZ;
            if(!wallAlongX(x0, center - gapHalf, corridorWallZ, interiorTag) ||
               !wallAlongX(center + gapHalf, x1, corridorWallZ, interiorTag)) return false;
        }

        // Partitions separate adjacent rooms without crossing the corridor.
        for(uint32_t divider = 1; divider < settings.roomsPerSide; ++divider){
            const float x = -halfLength + divider * settings.roomWidth;
            if(!wallAlongZ(x, roomStartZ, roomEndZ, interiorTag)) return false;
        }
    }

    if(generated.vertices.empty() || generated.indices.empty()){
        error = "interior blockout produced no geometry";
        return false;
    }
    output = std::move(generated);
    error.clear();
    return true;
}

bool makePrimitive(const AddPrimitiveNode& settings, MeshData& output, std::string& error,
                   std::size_t maxVertices){
    if(!validPrimitiveType(settings.primitive) || !finite(settings.size) ||
       settings.size.x <= 0.0f || settings.size.y <= 0.0f || settings.size.z <= 0.0f ||
       settings.size.x > 10000.0f || settings.size.y > 10000.0f || settings.size.z > 10000.0f){
        error = "primitive type and dimensions must be valid and positive";
        return false;
    }
    MeshData generated;
    auto quad = [&](const std::array<glm::vec3,4>& positions){
        glm::vec3 normal;
        if(!normalized(glm::cross(positions[1] - positions[0], positions[2] - positions[0]), normal)) return false;
        const uint32_t base = uint32_t(generated.vertices.size());
        if(generated.vertices.size() > maxVertices || maxVertices - generated.vertices.size() < 4) return false;
        for(int i = 0; i < 4; ++i)
            generated.vertices.push_back({positions[i], normal, {float(i == 1 || i == 2), float(i >= 2)}});
        generated.indices.insert(generated.indices.end(), {base,base+1,base+2,base,base+2,base+3});
        return true;
    };
    auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 outward){
        glm::vec3 normal;
        if(!normalized(glm::cross(b-a,c-a), normal)) return false;
        if(glm::dot(normal,outward) < 0.0f){ std::swap(b,c); normal = -normal; }
        if(generated.vertices.size() > maxVertices || maxVertices - generated.vertices.size() < 3) return false;
        const uint32_t base = uint32_t(generated.vertices.size());
        generated.vertices.push_back({a,normal,{0,0}});
        generated.vertices.push_back({b,normal,{1,0}});
        generated.vertices.push_back({c,normal,{0.5f,1}});
        generated.indices.insert(generated.indices.end(), {base,base+1,base+2});
        return true;
    };

    if(settings.primitive == PrimitiveType::Cube){
        if(!appendBox(generated,{0,0,0},{1,1,1},maxVertices,error)) return false;
    }else if(settings.primitive == PrimitiveType::Plane){
        if(!quad({glm::vec3(-0.5f,0,-0.5f),glm::vec3(-0.5f,0,0.5f),
                  glm::vec3(0.5f,0,0.5f),glm::vec3(0.5f,0,-0.5f)})){
            error = "plane primitive exceeds the configured vertex limit";
            return false;
        }
    }else if(settings.primitive == PrimitiveType::Pyramid){
        constexpr float h = 0.5f;
        const glm::vec3 base[] = {{-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h}};
        if(!quad({base[0],base[1],base[2],base[3]})){
            error = "pyramid primitive exceeds the configured vertex limit";
            return false;
        }
        const glm::vec3 tip{0,h,0};
        for(int i = 0; i < 4; ++i){
            const glm::vec3 a = base[i], b = base[(i+1)%4];
            if(!triangle(a,b,tip,(a+b+tip)/3.0f)){
                error = "pyramid primitive exceeds the configured vertex limit";
                return false;
            }
        }
    }else{
        constexpr uint32_t segments = 32;
        struct Ring{ float y, radius; };
        std::vector<Ring> rings;
        const bool capsule = settings.primitive == PrimitiveType::Capsule;
        if(capsule){
            for(uint32_t i = 1; i <= 8; ++i){
                const float angle = float(pi * 0.5) * float(i) / 8.0f;
                rings.push_back({0.5f + 0.5f * std::cos(angle), 0.5f * std::sin(angle)});
            }
            rings.push_back({-0.5f,0.5f});
            for(uint32_t i = 1; i < 8; ++i){
                const float angle = float(pi * 0.5) * float(i) / 8.0f;
                rings.push_back({-0.5f - 0.5f * std::sin(angle), 0.5f * std::cos(angle)});
            }
        }else{
            constexpr uint32_t latitudeRings = 16;
            for(uint32_t i = 1; i < latitudeRings; ++i){
                const float angle = float(pi) * float(i) / float(latitudeRings);
                rings.push_back({0.5f * std::cos(angle),0.5f * std::sin(angle)});
            }
        }
        const glm::vec3 top = capsule ? glm::vec3(0,1,0) : glm::vec3(0,0.5f,0);
        const glm::vec3 bottom = capsule ? glm::vec3(0,-1,0) : glm::vec3(0,-0.5f,0);
        const uint32_t topId = 0;
        generated.vertices.push_back({top,{0,1,0},{0.5f,0}});
        for(std::size_t ring = 0; ring < rings.size(); ++ring){
            for(uint32_t segment = 0; segment <= segments; ++segment){
                const float angle = float(2.0 * pi) * float(segment) / float(segments);
                const glm::vec3 radial{std::cos(angle),0,std::sin(angle)};
                const glm::vec3 position{rings[ring].radius * radial.x,rings[ring].y,rings[ring].radius * radial.z};
                glm::vec3 normal;
                if(!capsule) normal = glm::normalize(position);
                else if(rings[ring].y > 0.5f) normal = glm::normalize(position - glm::vec3(0,0.5f,0));
                else if(rings[ring].y < -0.5f) normal = glm::normalize(position - glm::vec3(0,-0.5f,0));
                else normal = glm::normalize(glm::vec3(radial.x,0,radial.z));
                generated.vertices.push_back({position,normal,{float(segment)/segments,1.0f-float(ring+1)/float(rings.size()+1)}});
            }
        }
        const uint32_t bottomId = uint32_t(generated.vertices.size());
        generated.vertices.push_back({bottom,{0,-1,0},{0.5f,1}});
        if(generated.vertices.size() > maxVertices){ error = "primitive exceeds the configured vertex limit"; return false; }
        const uint32_t stride = segments + 1;
        for(uint32_t segment = 0; segment < segments; ++segment){
            const uint32_t a = 1 + segment, b = a + 1;
            appendOrientedTriangle(generated,topId,a,b,{0,1,0});
        }
        for(uint32_t ring = 0; ring + 1 < rings.size(); ++ring){
            const uint32_t first = 1 + ring * stride, next = first + stride;
            for(uint32_t segment = 0; segment < segments; ++segment){
                const uint32_t a = first + segment, b = next + segment, c = b + 1, d = a + 1;
                const glm::vec3 outward = glm::normalize(generated.vertices[a].position + generated.vertices[b].position +
                                                         generated.vertices[c].position + generated.vertices[d].position -
                                                         4.0f * glm::vec3(0,capsule ? std::clamp((rings[ring].y+rings[ring+1].y)*0.5f,-0.5f,0.5f):0,0));
                appendOrientedTriangle(generated,a,b,c,outward);
                appendOrientedTriangle(generated,a,c,d,outward);
            }
        }
        const uint32_t lastRing = 1 + uint32_t(rings.size()-1) * stride;
        for(uint32_t segment = 0; segment < segments; ++segment){
            const uint32_t a = lastRing + segment, b = lastRing + segment + 1;
            appendOrientedTriangle(generated,a,b,bottomId,{0,-1,0});
        }
    }
    if(generated.empty()){ error = "primitive produced no geometry"; return false; }
    MeshData scaled;
    if(!transformMesh(generated,settings.size,{0,0,0},{0,0,0},{0,0,0},scaled,error)) return false;
    output = std::move(scaled);
    error.clear();
    return true;
}

bool moveMesh(const MeshData& input, const MoveNode& settings, MeshData& output, std::string& error){
    return transformMesh(input,{1,1,1},{0,0,0},settings.offset,{0,0,0},output,error);
}

bool rotateMesh(const MeshData& input, const RotateNode& settings, MeshData& output, std::string& error){
    return transformMesh(input,{1,1,1},settings.degrees,{0,0,0},settings.pivot,output,error);
}

bool scaleMesh(const MeshData& input, const ScaleNode& settings, MeshData& output, std::string& error){
    return transformMesh(input,settings.factor,{0,0,0},{0,0,0},settings.pivot,output,error);
}

bool extrudeFace(const MeshData& input, const ExtrudeNode& settings, MeshData& output,
                 std::string& error, std::size_t maxVertices){
    if(!finite(settings.distance) || std::abs(settings.distance) < 1e-6f || std::abs(settings.distance) > 10000.0f){
        error = "extrusion distance must be finite, non-zero, and within range";
        return false;
    }
    if(input.vertices.size() > maxVertices || input.vertices.size() > std::size_t(std::numeric_limits<uint32_t>::max())){
        error = "extruded mesh exceeds the configured vertex limit";
        return false;
    }
    std::vector<FacePatch> patches;
    if(!collectFacePatches(input,patches,error)) return false;
    const std::size_t triangleCount = input.indices.size()/3;
    if(settings.faceIndex >= triangleCount){ error = "extrusion face index is outside the mesh"; return false; }
    const auto selected = std::find_if(patches.begin(),patches.end(),[&](const FacePatch& patch){
        return std::find(patch.triangles.begin(),patch.triangles.end(),settings.faceIndex) != patch.triangles.end();
    });
    if(selected == patches.end()){ error = "extrusion face could not be selected"; return false; }
    std::unordered_set<std::size_t> selectedTriangles(selected->triangles.begin(),selected->triangles.end());
    MeshData generated = input;
    generated.indices.clear();
    generated.triangles.clear();
    generated.indices.reserve(input.indices.size() + selected->boundary.size()*6);
    for(std::size_t triangle = 0; triangle < triangleCount; ++triangle){
        if(selectedTriangles.count(triangle)) continue;
        generated.indices.insert(generated.indices.end(),input.indices.begin()+std::ptrdiff_t(triangle*3),
                                 input.indices.begin()+std::ptrdiff_t(triangle*3+3));
        generated.triangles.push_back(attributesOf(input,triangle));
    }
    TriangleAttributes sideAttributes = attributesOf(input,selected->triangles.front());
    sideAttributes.createdBy = 0;
    std::unordered_map<uint32_t,uint32_t> top;
    for(std::size_t triangle : selected->triangles){
        for(int corner = 0; corner < 3; ++corner){
            const uint32_t original = input.indices[triangle*3+std::size_t(corner)];
            if(top.find(original) != top.end()) continue;
            if(generated.vertices.size() >= maxVertices || generated.vertices.size() >= std::size_t(std::numeric_limits<uint32_t>::max())){
                error = "extruded mesh exceeds the configured vertex limit"; return false;
            }
            MeshVertex vertex = input.vertices[original];
            vertex.position += selected->normal * settings.distance;
            if(!finite(vertex.position)){ error = "extrusion produced a non-finite vertex"; return false; }
            top.emplace(original,uint32_t(generated.vertices.size()));
            generated.vertices.push_back(vertex);
        }
    }
    for(std::size_t triangle : selected->triangles){
        const uint32_t a = top.at(input.indices[triangle*3]);
        const uint32_t b = top.at(input.indices[triangle*3+1]);
        const uint32_t c = top.at(input.indices[triangle*3+2]);
        generated.indices.insert(generated.indices.end(),{a,b,c});
        generated.triangles.push_back(attributesOf(input,triangle));
    }
    for(std::size_t edge = 0; edge < selected->boundary.size(); ++edge){
        const uint32_t a = selected->boundary[edge];
        const uint32_t b = selected->boundary[(edge+1)%selected->boundary.size()];
        const glm::vec3 edgeDirection = input.vertices[b].position - input.vertices[a].position;
        glm::vec3 sideNormal;
        if(!normalized(glm::cross(edgeDirection,selected->normal),sideNormal)){ error = "extrusion has a degenerate boundary edge"; return false; }
        if(generated.vertices.size() > maxVertices || maxVertices-generated.vertices.size() < 4){
            error = "extruded mesh exceeds the configured vertex limit"; return false;
        }
        const uint32_t base = uint32_t(generated.vertices.size());
        const float length = float(distance(input.vertices[a].position,input.vertices[b].position));
        generated.vertices.push_back({input.vertices[a].position,sideNormal,{0,0}});
        generated.vertices.push_back({input.vertices[b].position,sideNormal,{length,0}});
        generated.vertices.push_back({generated.vertices[top.at(b)].position,sideNormal,{length,std::abs(settings.distance)}});
        generated.vertices.push_back({generated.vertices[top.at(a)].position,sideNormal,{0,std::abs(settings.distance)}});
        generated.indices.insert(generated.indices.end(),{base,base+1,base+2,base,base+2,base+3});
        generated.triangles.insert(generated.triangles.end(),{sideAttributes,sideAttributes});
    }
    if(generated.empty()){ error = "extrusion produced no geometry"; return false; }
    output = std::move(generated);
    error.clear();
    return true;
}

bool bevelMesh(const MeshData& input, const BevelNode& settings, MeshData& output,
               std::string& error, std::size_t maxVertices){
    if(!finite(settings.amount) || settings.amount <= 0.0f || settings.amount > 1000.0f ||
       settings.segments < 1 || settings.segments > 8){
        error = "bevel amount or segment count is invalid";
        return false;
    }
    if(input.vertices.size() > maxVertices){ error = "beveled mesh exceeds the configured vertex limit"; return false; }
    std::vector<FacePatch> patches;
    if(!collectFacePatches(input,patches,error)) return false;
    constexpr double keyRange = 8.0e13;
    for(const MeshVertex& vertex : input.vertices){
        if(std::abs(double(vertex.position.x)) > keyRange || std::abs(double(vertex.position.y)) > keyRange ||
           std::abs(double(vertex.position.z)) > keyRange){
            error = "mesh coordinates exceed the bevel position-key range";
            return false;
        }
    }

    using PositionKey = std::tuple<int64_t,int64_t,int64_t>;
    using GeometricEdge = std::pair<PositionKey,PositionKey>;
    auto positionKey = [](const glm::vec3& p){
        constexpr double tolerance = 1e-5;
        return PositionKey{int64_t(std::llround(double(p.x)/tolerance)),
                           int64_t(std::llround(double(p.y)/tolerance)),
                           int64_t(std::llround(double(p.z)/tolerance))};
    };
    struct EdgeOccurrence{
        std::size_t patch = 0;
        uint32_t start = 0, end = 0;
        uint32_t innerStart = 0, innerEnd = 0;
        PositionKey startKey, endKey;
    };
    struct BevelPatchData{ std::vector<uint32_t> insetIds; };
    struct CornerPath{ std::vector<uint32_t> ring; glm::vec3 normal{0.0f}; std::size_t patch = 0; };

    MeshData generated;
    auto patchAttributes = [&](std::size_t patch, bool keepSource){
        TriangleAttributes attributes = attributesOf(input,patches[patch].triangles.front());
        if(!keepSource) attributes.createdBy = 0;
        return attributes;
    };
    auto emit = [&](uint32_t a, uint32_t b, uint32_t c, const glm::vec3& normal, const TriangleAttributes& attributes){
        appendOrientedTriangle(generated,a,b,c,normal);
        generated.triangles.push_back(attributes);
    };
    std::vector<BevelPatchData> patchData(patches.size());
    std::map<GeometricEdge,std::vector<EdgeOccurrence>> edges;
    for(std::size_t patchIndex = 0; patchIndex < patches.size(); ++patchIndex){
        const FacePatch& patch = patches[patchIndex];
        std::vector<glm::vec3> inset;
        if(!insetPolygon(input,patch,settings.amount,inset,error)) return false;
        const std::size_t boundaryCount = patch.boundary.size();
        BevelPatchData& data = patchData[patchIndex];
        data.insetIds.resize(boundaryCount);
        for(std::size_t i = 0; i < boundaryCount; ++i){
            if(generated.vertices.size() >= maxVertices || generated.vertices.size() >= std::size_t(std::numeric_limits<uint32_t>::max())){
                error = "beveled mesh exceeds the configured vertex limit"; return false;
            }
            MeshVertex vertex = input.vertices[patch.boundary[i]];
            vertex.position = inset[i];
            vertex.normal = patch.normal;
            data.insetIds[i] = uint32_t(generated.vertices.size());
            generated.vertices.push_back(vertex);
        }

        // Replace each face with its inset cap; bevel strips bridge the cap edges.
        for(std::size_t i = 0; i < boundaryCount; ++i){
            const std::size_t next = (i+1)%boundaryCount;
            const uint32_t outerA = patch.boundary[i], outerB = patch.boundary[next];
            const uint32_t innerA = data.insetIds[i], innerB = data.insetIds[next];

            const PositionKey keyA = positionKey(input.vertices[outerA].position);
            const PositionKey keyB = positionKey(input.vertices[outerB].position);
            const GeometricEdge key = keyA < keyB ? GeometricEdge{keyA,keyB} : GeometricEdge{keyB,keyA};
            edges[key].push_back({patchIndex,outerA,outerB,innerA,innerB,keyA,keyB});
        }
        for(std::size_t i = 1; i+1 < boundaryCount; ++i)
            emit(data.insetIds[0],data.insetIds[i],data.insetIds[i+1],patch.normal,patchAttributes(patchIndex,true));
    }

    std::map<PositionKey,std::vector<CornerPath>> cornerPaths;
    for(const auto& [edgeKeyValue,occurrences] : edges){
        if(occurrences.size() != 2) continue;
        const EdgeOccurrence& first = occurrences[0];
        const EdgeOccurrence& second = occurrences[1];
        const glm::vec3 normalA = patches[first.patch].normal;
        const glm::vec3 normalB = patches[second.patch].normal;
        glm::vec3 edgeNormal;
        if(!normalized(normalA+normalB,edgeNormal)) continue;

        uint32_t aStart = first.innerStart, aEnd = first.innerEnd;
        uint32_t bStart = 0, bEnd = 0;
        if(first.startKey == second.startKey && first.endKey == second.endKey){
            bStart = second.innerStart; bEnd = second.innerEnd;
        }else if(first.startKey == second.endKey && first.endKey == second.startKey){
            bStart = second.innerEnd; bEnd = second.innerStart;
        }else{
            error = "bevel edge endpoints do not match";
            return false;
        }

        // Coplanar patches can share a boundary after extrusion. Their inset caps
        // leave a planar seam; fill it instead of leaving a visible open slot.
        if(glm::dot(normalA,normalB) > 0.9999f){
            emit(aStart,aEnd,bEnd,normalA,patchAttributes(first.patch,false));
            emit(aStart,bEnd,bStart,normalA,patchAttributes(first.patch,false));
            CornerPath startPath, endPath;
            startPath.normal = endPath.normal = normalA;
            startPath.patch = endPath.patch = first.patch;
            startPath.ring = {aStart,bStart};
            endPath.ring = {aEnd,bEnd};
            cornerPaths[first.startKey].push_back(std::move(startPath));
            cornerPaths[first.endKey].push_back(std::move(endPath));
            continue;
        }

        const glm::vec3 pStart = input.vertices[first.start].position;
        const glm::vec3 pEnd = input.vertices[first.end].position;
        std::vector<std::array<uint32_t,2>> rings(settings.segments+1);
        rings.front() = {aStart,aEnd};
        rings.back() = {bStart,bEnd};
        for(uint32_t segment = 1; segment < settings.segments; ++segment){
            const float t = float(segment)/float(settings.segments);
            const float theta = t*float(pi*0.5);
            const float bulge = settings.amount*(std::sin(theta)+std::cos(theta)-1.0f)*0.70710678f;
            const glm::vec3 adjustment = edgeNormal*bulge;
            if(generated.vertices.size() > maxVertices || maxVertices-generated.vertices.size() < 2){
                error = "beveled mesh exceeds the configured vertex limit"; return false;
            }
            MeshVertex startVertex = input.vertices[first.start];
            startVertex.position = glm::mix(generated.vertices[aStart].position,generated.vertices[bStart].position,t)+adjustment;
            startVertex.normal = glm::normalize(glm::mix(normalA,normalB,t));
            MeshVertex endVertex = input.vertices[first.end];
            endVertex.position = glm::mix(generated.vertices[aEnd].position,generated.vertices[bEnd].position,t)+adjustment;
            endVertex.normal = startVertex.normal;
            rings[segment] = {uint32_t(generated.vertices.size()),uint32_t(generated.vertices.size()+1)};
            generated.vertices.push_back(startVertex);
            generated.vertices.push_back(endVertex);
        }
        for(uint32_t segment = 0; segment < settings.segments; ++segment){
            const auto& a = rings[segment];
            const auto& b = rings[segment+1];
            emit(a[0],a[1],b[1],edgeNormal,patchAttributes(first.patch,false));
            emit(a[0],b[1],b[0],edgeNormal,patchAttributes(first.patch,false));
        }

        CornerPath startPath, endPath;
        startPath.normal = endPath.normal = edgeNormal;
        startPath.patch = endPath.patch = first.patch;
        for(const auto& ring : rings){ startPath.ring.push_back(ring[0]); endPath.ring.push_back(ring[1]); }
        cornerPaths[first.startKey].push_back(std::move(startPath));
        cornerPaths[first.endKey].push_back(std::move(endPath));
        (void)pStart;
        (void)pEnd;
        (void)edgeKeyValue;
    }

    // Close the small polygon where three or more bevelled faces meet at a corner.
    for(auto& [cornerKey,paths] : cornerPaths){
        if(paths.size() < 3) continue;
        std::unordered_map<uint32_t,std::vector<std::size_t>> incident;
        for(std::size_t path = 0; path < paths.size(); ++path){
            incident[paths[path].ring.front()].push_back(path);
            incident[paths[path].ring.back()].push_back(path);
        }
        bool cycle = true;
        for(const auto& [vertex,adjacent] : incident) if(adjacent.size() != 2){ cycle = false; break; }
        if(!cycle) continue;
        std::vector<uint32_t> boundary;
        std::vector<uint8_t> used(paths.size(),0);
        std::size_t pathIndex = 0;
        uint32_t current = paths[pathIndex].ring.front();
        const uint32_t start = current;
        for(std::size_t step = 0; step <= paths.size(); ++step){
            if(used[pathIndex]){ cycle = current == start; break; }
            used[pathIndex] = 1;
            const CornerPath& path = paths[pathIndex];
            const bool forward = path.ring.front() == current;
            if(forward){
                boundary.insert(boundary.end(),path.ring.begin(),path.ring.end()-1);
                current = path.ring.back();
            }else{
                boundary.insert(boundary.end(),path.ring.rbegin(),path.ring.rend()-1);
                current = path.ring.front();
            }
            const auto found = incident.find(current);
            if(found == incident.end()){ cycle = false; break; }
            pathIndex = found->second[0] == pathIndex ? found->second[1] : found->second[0];
            if(current == start){ cycle = true; break; }
        }
        if(!cycle || boundary.size() < 3) continue;
        if(generated.vertices.size() >= maxVertices || generated.vertices.size() >= std::size_t(std::numeric_limits<uint32_t>::max())){
            error = "beveled mesh exceeds the configured vertex limit"; return false;
        }
        glm::vec3 center{0.0f}, normal{0.0f};
        for(uint32_t id : boundary) center += generated.vertices[id].position;
        for(const CornerPath& path : paths) normal += path.normal;
        center /= float(boundary.size());
        if(!normalized(normal,normal)) normal = {0,1,0};
        const uint32_t centerId = uint32_t(generated.vertices.size());
        generated.vertices.push_back({center,normal,{0.5f,0.5f}});
        for(std::size_t i = 0; i < boundary.size(); ++i)
            emit(centerId,boundary[i],boundary[(i+1)%boundary.size()],normal,patchAttributes(paths.front().patch,false));
        (void)cornerKey;
    }

    if(generated.empty() || !validMesh(generated)){ error = "bevel produced invalid mesh geometry"; return false; }
    output = std::move(generated);
    error.clear();
    return true;
}

namespace{

// Point sets flow between nodes with one normal per point so copies can align to
// the surface they were taken from.
bool meshToPointSet(const MeshData& input, std::vector<glm::vec3>& output, std::vector<glm::vec3>* normals,
                    std::string& error, std::size_t maxPoints){
    if(!validMesh(input)){ error = "mesh topology or vertex data is invalid"; return false; }
    constexpr double keyRange = 8.0e13;
    using Key = std::tuple<int64_t,int64_t,int64_t>;
    struct Hash{
        std::size_t operator()(const Key& key) const noexcept{
            std::size_t hash = std::hash<int64_t>{}(std::get<0>(key));
            hash ^= std::hash<int64_t>{}(std::get<1>(key)) + 0x9e3779b9u + (hash<<6u) + (hash>>2u);
            hash ^= std::hash<int64_t>{}(std::get<2>(key)) + 0x9e3779b9u + (hash<<6u) + (hash>>2u);
            return hash;
        }
    };
    std::unordered_map<Key,std::size_t,Hash> seen;
    std::vector<glm::vec3> generated, summedNormals;
    seen.reserve(input.vertices.size());
    generated.reserve(std::min(input.vertices.size(),maxPoints));
    for(const MeshVertex& vertex : input.vertices){
        const glm::vec3& p = vertex.position;
        if(std::abs(double(p.x)) > keyRange || std::abs(double(p.y)) > keyRange || std::abs(double(p.z)) > keyRange){
            error = "mesh coordinates exceed the mesh-to-point key range";
            return false;
        }
        constexpr double positionTolerance = 1e-5;
        const Key key{int64_t(std::llround(double(p.x)/positionTolerance)),
                      int64_t(std::llround(double(p.y)/positionTolerance)),
                      int64_t(std::llround(double(p.z)/positionTolerance))};
        const auto [found, inserted] = seen.emplace(key, generated.size());
        if(inserted){
            if(generated.size() >= maxPoints){ error = "mesh-to-point output exceeds the configured point limit"; return false; }
            generated.push_back(p);
            summedNormals.push_back(vertex.normal);
        }else summedNormals[found->second] += vertex.normal;
    }
    if(generated.empty()){ error = "mesh-to-point produced no points"; return false; }
    if(normals){
        for(glm::vec3& normal : summedNormals) if(!normalized(normal,normal)) normal = {0.0f,1.0f,0.0f};
        *normals = std::move(summedNormals);
    }
    output = std::move(generated);
    error.clear();
    return true;
}

bool samplePointSet(const MeshData& input, uint32_t count, uint64_t seed,
                    std::vector<glm::vec3>& output, std::vector<glm::vec3>* normals,
                    std::string& error, std::size_t maxPoints){
    if(!validMesh(input)){ error = "mesh topology or vertex data is invalid"; return false; }
    if(count == 0 || count > maxPoints){ error = "point count must be within the configured point limit"; return false; }
    std::vector<double> cumulative;
    std::vector<glm::vec3> faceNormals;
    cumulative.reserve(input.indices.size()/3);
    faceNormals.reserve(input.indices.size()/3);
    double totalArea = 0.0;
    for(std::size_t i = 0; i < input.indices.size(); i += 3){
        const glm::vec3& a = input.vertices[input.indices[i]].position;
        const glm::vec3& b = input.vertices[input.indices[i+1]].position;
        const glm::vec3& c = input.vertices[input.indices[i+2]].position;
        const glm::vec3 crossValue = glm::cross(b-a,c-a);
        const double area = 0.5 * std::sqrt(double(glm::dot(crossValue,crossValue)));
        if(!std::isfinite(area) || area <= pointEpsilon){ error = "mesh has a degenerate triangle"; return false; }
        totalArea += area;
        cumulative.push_back(totalArea);
        faceNormals.push_back(glm::normalize(crossValue));
    }
    if(!std::isfinite(totalArea) || totalArea <= pointEpsilon){ error = "mesh surface area is zero"; return false; }
    auto nextRandom = [&seed](){
        seed += 0x9e3779b97f4a7c15ull;
        uint64_t value = seed;
        value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
        value ^= value >> 31u;
        return double(value >> 11u) * (1.0 / 9007199254740992.0);
    };
    std::vector<glm::vec3> generated, generatedNormals;
    generated.reserve(count);
    generatedNormals.reserve(count);
    for(uint32_t sample = 0; sample < count; ++sample){
        const double areaPick = nextRandom()*totalArea;
        const std::size_t triangle = std::min(cumulative.size()-1,
            std::size_t(std::lower_bound(cumulative.begin(),cumulative.end(),areaPick)-cumulative.begin()));
        const uint32_t ia = input.indices[triangle*3], ib = input.indices[triangle*3+1], ic = input.indices[triangle*3+2];
        const double root = std::sqrt(nextRandom()), v = nextRandom();
        const float wa = float(1.0-root), wb = float(root*(1.0-v)), wc = float(root*v);
        generated.push_back(input.vertices[ia].position*wa + input.vertices[ib].position*wb + input.vertices[ic].position*wc);
        generatedNormals.push_back(faceNormals[triangle]);
    }
    if(normals) *normals = std::move(generatedNormals);
    output = std::move(generated);
    error.clear();
    return true;
}

// Rebuilds a mesh with one vertex per (source vertex, key) pair; callers use it
// when a pass gives a shared vertex different values on different triangles.
struct CornerRemap{
    std::unordered_map<uint64_t,uint32_t> ids;
    MeshData mesh;
    uint32_t vertex(const MeshVertex& value, uint64_t key){
        const auto found = ids.find(key);
        if(found != ids.end()) return found->second;
        const uint32_t id = uint32_t(mesh.vertices.size());
        mesh.vertices.push_back(value);
        ids.emplace(key,id);
        return id;
    }
};

uint32_t quantizedNormalKey(const glm::vec3& normal){
    auto part = [](float value){ return uint32_t(std::clamp(std::lround((value + 1.0f) * 511.5f), 0l, 1023l)); };
    return (part(normal.x) << 20u) | (part(normal.y) << 10u) | part(normal.z);
}

}

bool meshToPoints(const MeshData& input, std::vector<glm::vec3>& output, std::string& error,
                  std::size_t maxPoints){
    return meshToPointSet(input,output,nullptr,error,maxPoints);
}

bool pointsFromMesh(const MeshData& input, uint32_t count, uint64_t seed,
                    std::vector<glm::vec3>& output, std::string& error, std::size_t maxPoints){
    return samplePointSet(input,count,seed,output,nullptr,error,maxPoints);
}

const std::vector<std::string>& semanticVocabulary(){
    static const std::vector<std::string> names = {
        "floor", "ceiling", "wall_exterior", "wall_interior", "roof", "window", "door", "frame",
        "stairs", "railing", "foundation", "trim", "glass", "road", "sidewalk", "curb",
        "rope", "chain_link", "terrain", "prop",
    };
    return names;
}

const std::vector<std::string>& materialLibrary(){
    static const std::vector<std::string> names = {
        "plaster", "brick", "stone", "concrete", "wood_planks", "wood_beam", "roof_tiles", "roof_metal",
        "glass", "metal", "steel_chain", "asphalt", "paving", "rope_fiber", "ground_dirt", "grass",
    };
    return names;
}

namespace{
uint16_t vocabularyId(const std::vector<std::string>& names, const std::string& name){
    if(name.empty()) return 0;
    const auto found = std::find(names.begin(), names.end(), name);
    return found == names.end() ? 0 : uint16_t(found - names.begin() + 1);
}
std::string vocabularyName(const std::vector<std::string>& names, uint16_t id){
    return id == 0 || id > names.size() ? std::string{} : names[id - 1];
}
}

uint16_t semanticId(const std::string& name){ return vocabularyId(semanticVocabulary(), name); }
uint16_t materialId(const std::string& name){ return vocabularyId(materialLibrary(), name); }
std::string semanticName(uint16_t id){ return vocabularyName(semanticVocabulary(), id); }
std::string materialName(uint16_t id){ return vocabularyName(materialLibrary(), id); }

bool mergeMeshes(const std::vector<const MeshData*>& inputs, MeshData& output, std::string& error,
                 std::size_t maxVertices){
    if(inputs.empty()){ error = "merge needs at least one mesh"; return false; }
    MeshData generated;
    for(const MeshData* input : inputs){
        if(!input || !validMesh(*input)){ error = "merge input mesh is invalid"; return false; }
        if(generated.vertices.size() + input->vertices.size() > maxVertices){
            error = "merged mesh exceeds the configured vertex limit"; return false;
        }
        const uint32_t base = uint32_t(generated.vertices.size());
        generated.vertices.insert(generated.vertices.end(), input->vertices.begin(), input->vertices.end());
        for(uint32_t index : input->indices) generated.indices.push_back(base + index);
        for(std::size_t triangle = 0; triangle < input->indices.size() / 3; ++triangle)
            generated.triangles.push_back(attributesOf(*input, triangle));
    }
    output = std::move(generated);
    error.clear();
    return true;
}

bool validTriangleFilter(const TriangleFilter& filter){
    if(!filter.semantic.empty() && semanticId(filter.semantic) == 0) return false;
    if(!filter.useDirection) return true;
    glm::vec3 unit;
    return normalized(filter.direction, unit) && finite(filter.maxAngleDegrees) &&
           filter.maxAngleDegrees >= 0.0f && filter.maxAngleDegrees <= 180.0f;
}

bool triangleMatches(const MeshData& mesh, std::size_t triangle, const TriangleFilter& filter){
    if(!filter.semantic.empty() && attributesOf(mesh, triangle).semantic != semanticId(filter.semantic)) return false;
    if(!filter.useDirection) return true;
    const glm::vec3& a = mesh.vertices[mesh.indices[triangle*3]].position;
    const glm::vec3& b = mesh.vertices[mesh.indices[triangle*3+1]].position;
    const glm::vec3& c = mesh.vertices[mesh.indices[triangle*3+2]].position;
    glm::vec3 normal, wanted;
    if(!normalized(glm::cross(b-a, c-a), normal) || !normalized(filter.direction, wanted)) return false;
    return glm::dot(normal, wanted) >= std::cos(double(filter.maxAngleDegrees) * pi / 180.0) - 1e-6;
}

namespace{
template<typename Apply>
bool retagTriangles(const MeshData& input, const TriangleFilter& filter, MeshData& output,
                    std::string& error, Apply apply){
    if(!validMesh(input)){ error = "mesh topology or vertex data is invalid"; return false; }
    if(!validTriangleFilter(filter)){ error = "triangle filter is invalid"; return false; }
    MeshData generated = input;
    generated.triangles.resize(generated.indices.size() / 3);
    for(std::size_t triangle = 0; triangle < generated.triangles.size(); ++triangle)
        if(triangleMatches(input, triangle, filter)) apply(generated.triangles[triangle]);
    output = std::move(generated);
    error.clear();
    return true;
}
}

bool setSemantic(const MeshData& input, const SetSemanticNode& settings, MeshData& output, std::string& error){
    const uint16_t id = semanticId(settings.semantic);
    if(id == 0){ error = "unknown semantic name: " + settings.semantic; return false; }
    return retagTriangles(input, settings.filter, output, error, [id](TriangleAttributes& t){ t.semantic = id; });
}

bool setMaterial(const MeshData& input, const SetMaterialNode& settings, MeshData& output, std::string& error){
    const uint16_t id = materialId(settings.material);
    if(id == 0){ error = "unknown material name: " + settings.material; return false; }
    return retagTriangles(input, settings.filter, output, error, [id](TriangleAttributes& t){ t.material = id; });
}

bool smoothNormals(const MeshData& input, float angleDegrees, MeshData& output, std::string& error){
    if(!validMesh(input)){ error = "mesh topology or vertex data is invalid"; return false; }
    if(!finite(angleDegrees) || angleDegrees < 0.0f || angleDegrees > 180.0f){
        error = "smoothing angle must be between 0 and 180 degrees"; return false;
    }
    const std::size_t triangleCount = input.indices.size() / 3;
    std::vector<glm::vec3> areaNormals(triangleCount), unitNormals(triangleCount);
    std::vector<uint8_t> usable(triangleCount, 0);
    using Key = std::tuple<int64_t,int64_t,int64_t>;
    std::map<Key,std::vector<std::size_t>> corners;
    auto keyOf = [](const glm::vec3& p){
        constexpr double tolerance = 1e-5;
        return Key{int64_t(std::llround(double(p.x)/tolerance)), int64_t(std::llround(double(p.y)/tolerance)),
                   int64_t(std::llround(double(p.z)/tolerance))};
    };
    for(std::size_t triangle = 0; triangle < triangleCount; ++triangle){
        const glm::vec3& a = input.vertices[input.indices[triangle*3]].position;
        const glm::vec3& b = input.vertices[input.indices[triangle*3+1]].position;
        const glm::vec3& c = input.vertices[input.indices[triangle*3+2]].position;
        areaNormals[triangle] = glm::cross(b-a, c-a);
        usable[triangle] = normalized(areaNormals[triangle], unitNormals[triangle]);
        for(int corner = 0; corner < 3; ++corner)
            corners[keyOf(input.vertices[input.indices[triangle*3+std::size_t(corner)]].position)].push_back(triangle);
    }
    const float cosine = float(std::cos(double(angleDegrees) * pi / 180.0)) - 1e-5f;
    CornerRemap remap;
    remap.mesh.triangles = input.triangles;
    remap.mesh.indices.reserve(input.indices.size());
    for(std::size_t triangle = 0; triangle < triangleCount; ++triangle){
        for(int corner = 0; corner < 3; ++corner){
            const uint32_t source = input.indices[triangle*3+std::size_t(corner)];
            MeshVertex vertex = input.vertices[source];
            if(usable[triangle]){
                glm::vec3 sum{0.0f};
                for(std::size_t other : corners[keyOf(vertex.position)])
                    if(usable[other] && glm::dot(unitNormals[triangle], unitNormals[other]) >= cosine)
                        sum += areaNormals[other];
                glm::vec3 normal;
                if(normalized(sum, normal)) vertex.normal = normal;
            }
            remap.mesh.indices.push_back(remap.vertex(vertex, (uint64_t(source) << 30u) | quantizedNormalKey(vertex.normal)));
        }
    }
    output = std::move(remap.mesh);
    error.clear();
    return true;
}

bool projectUVs(const MeshData& input, float tileSize, MeshData& output, std::string& error){
    if(!validMesh(input)){ error = "mesh topology or vertex data is invalid"; return false; }
    if(!finite(tileSize) || tileSize < 0.01f || tileSize > 1000.0f){
        error = "UV tile size must be between 0.01 and 1000 meters"; return false;
    }
    CornerRemap remap;
    remap.mesh.triangles = input.triangles;
    remap.mesh.indices.reserve(input.indices.size());
    for(std::size_t triangle = 0; triangle < input.indices.size() / 3; ++triangle){
        const glm::vec3& a = input.vertices[input.indices[triangle*3]].position;
        const glm::vec3& b = input.vertices[input.indices[triangle*3+1]].position;
        const glm::vec3& c = input.vertices[input.indices[triangle*3+2]].position;
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        normalized(glm::cross(b-a, c-a), normal);
        const glm::vec3 magnitude = glm::abs(normal);
        const int axis = magnitude.x >= magnitude.y && magnitude.x >= magnitude.z ? 0 : (magnitude.y >= magnitude.z ? 1 : 2);
        const int facing = (axis == 0 ? normal.x : axis == 1 ? normal.y : normal.z) >= 0.0f ? 0 : 1;
        for(int corner = 0; corner < 3; ++corner){
            const uint32_t source = input.indices[triangle*3+std::size_t(corner)];
            MeshVertex vertex = input.vertices[source];
            const glm::vec3& p = vertex.position;
            // U runs along the face as seen from outside, V points up (or along -Z on floors).
            if(axis == 0) vertex.uv = {facing == 0 ? -p.z : p.z, p.y};
            else if(axis == 1) vertex.uv = {p.x, facing == 0 ? -p.z : p.z};
            else vertex.uv = {facing == 0 ? p.x : -p.x, p.y};
            vertex.uv /= tileSize;
            remap.mesh.indices.push_back(remap.vertex(vertex, uint64_t(source) * 6u + uint64_t(axis * 2 + facing)));
        }
    }
    output = std::move(remap.mesh);
    error.clear();
    return true;
}

bool copyToPoints(const MeshData& instance, const std::vector<glm::vec3>& points,
                  const std::vector<glm::vec3>& normals, const CopyToPointsNode& settings,
                  MeshData& output, std::string& error, std::size_t maxVertices){
    if(!validMesh(instance)){ error = "copy instance mesh is invalid"; return false; }
    if(points.empty()){ error = "copy-to-points needs at least one point"; return false; }
    if(points.size() > settings.maxCopies){ error = "copy-to-points exceeds its copy limit"; return false; }
    if(instance.vertices.size() * points.size() > maxVertices){
        error = "copied mesh exceeds the configured vertex limit"; return false;
    }
    uint64_t state = settings.seed;
    auto nextRandom = [&state](){
        state += 0x9e3779b97f4a7c15ull;
        uint64_t value = state;
        value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
        value ^= value >> 31u;
        return double(value >> 11u) * (1.0 / 9007199254740992.0);
    };
    MeshData generated;
    generated.vertices.reserve(instance.vertices.size() * points.size());
    generated.indices.reserve(instance.indices.size() * points.size());
    for(std::size_t i = 0; i < points.size(); ++i){
        if(!finite(points[i])){ error = "copy point is not finite"; return false; }
        const float yaw = float((nextRandom() * 2.0 - 1.0) * double(settings.randomYawDegrees) * pi / 180.0);
        const float scale = settings.scale * float(1.0 + (nextRandom() * 2.0 - 1.0) * double(settings.randomScale));
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        if(settings.alignToNormal && i < normals.size()) normalized(normals[i], up);
        glm::vec3 side;
        if(!normalized(glm::cross(up, std::abs(up.z) < 0.99f ? glm::vec3(0,0,1) : glm::vec3(1,0,0)), side))
            side = {1.0f, 0.0f, 0.0f};
        const glm::vec3 forward = glm::cross(side, up);
        auto place = [&](const glm::vec3& local){
            const glm::vec3 turned = rotateAround(local, {0.0f, 1.0f, 0.0f}, yaw);
            return side * turned.x + up * turned.y + forward * turned.z;
        };
        const uint32_t base = uint32_t(generated.vertices.size());
        for(const MeshVertex& vertex : instance.vertices){
            MeshVertex copy = vertex;
            copy.position = points[i] + place(vertex.position * scale);
            copy.normal = place(vertex.normal);
            generated.vertices.push_back(copy);
        }
        for(uint32_t index : instance.indices) generated.indices.push_back(base + index);
        for(std::size_t triangle = 0; triangle < instance.indices.size() / 3; ++triangle)
            generated.triangles.push_back(attributesOf(instance, triangle));
    }
    output = std::move(generated);
    error.clear();
    return true;
}

bool makePointPreview(const std::vector<glm::vec3>& points, MeshData& output, std::string& error,
                      std::size_t maxDisplayedPoints, float markerSize, std::size_t maxVertices){
    if(points.empty() || maxDisplayedPoints == 0 || !finite(markerSize) || markerSize <= 0.0f){
        error = "point preview settings are invalid"; return false;
    }
    MeshData generated;
    const std::size_t stride = std::max<std::size_t>(1,(points.size()+maxDisplayedPoints-1)/maxDisplayedPoints);
    for(std::size_t i = 0; i < points.size(); i += stride){
        if(!appendBox(generated,points[i],glm::vec3(markerSize),maxVertices,error)) return false;
    }
    output = std::move(generated);
    error.clear();
    return true;
}

ValidationResult validate(const Graph& graph){
    if(graph.schemaVersion != graphSchemaVersion)
        return {false, "unsupported WeaverProcedura graph schema version"};
    if(graph.nodes.size() > 256 || graph.links.size() > 1024)
        return {false, "recipe exceeds the node or link limit"};

    std::unordered_map<NodeId, std::size_t> nodeIndex;
    nodeIndex.reserve(graph.nodes.size());
    NodeId highestNodeId = 0;
    for(std::size_t i = 0; i < graph.nodes.size(); ++i){
        const Node& node = graph.nodes[i];
        if(node.id == 0) return {false, "node ID must be non-zero"};
        if(std::holds_alternative<std::monostate>(node.payload)) return {false, "node payload type is missing"};
        if(!finite(node.editorX) || !finite(node.editorY)) return {false, "node position must be finite"};
        if(const auto* curve = std::get_if<CurveNode>(&node.payload)){
            if(curve->curve.points.size() > 4096) return {false, "curve exceeds the control point limit"};
            for(const glm::vec3& point : curve->curve.points)
                if(!finite(point)) return {false, "curve control points must be finite"};
        }else if(const auto* profile = std::get_if<RectangleProfileNode>(&node.payload)){
            if(!finite(profile->width) || !finite(profile->height) || profile->width <= 0.0f || profile->height <= 0.0f)
                return {false, "rectangle profile dimensions must be positive and finite"};
        }else if(const auto* sweep = std::get_if<SweepNode>(&node.payload)){
            if(!finite(sweep->settings.sampleSpacing) || sweep->settings.sampleSpacing <= 0.0f ||
               !finite(sweep->settings.referenceUp) || glm::dot(sweep->settings.referenceUp, sweep->settings.referenceUp) <= 1e-12f ||
               sweep->settings.maxVertices == 0 || sweep->settings.maxVertices > std::size_t(std::numeric_limits<uint32_t>::max()))
                return {false, "sweep settings are invalid"};
        }else if(const auto* grid = std::get_if<GridNode>(&node.payload)){
            if(!validGridSettings(*grid)) return {false, "grid settings are invalid"};
        }else if(const auto* height = std::get_if<SetGridPointHeightNode>(&node.payload)){
            if(!finite(height->height) || std::abs(height->height) > 10000.0f)
                return {false, "grid point height must be finite and within range"};
        }else if(const auto* interior = std::get_if<InteriorBlockoutNode>(&node.payload)){
            if(!validInteriorSettings(*interior)) return {false, "interior blockout settings are invalid"};
        }else if(const auto* primitive = std::get_if<AddPrimitiveNode>(&node.payload)){
            if(!validPrimitiveType(primitive->primitive) || !finite(primitive->size) ||
               primitive->size.x <= 0.0f || primitive->size.y <= 0.0f || primitive->size.z <= 0.0f ||
               primitive->size.x > 10000.0f || primitive->size.y > 10000.0f || primitive->size.z > 10000.0f)
                return {false, "primitive settings are invalid"};
        }else if(const auto* move = std::get_if<MoveNode>(&node.payload)){
            if(!finite(move->offset) || std::max({std::abs(move->offset.x),std::abs(move->offset.y),std::abs(move->offset.z)}) > 1000000.0f)
                return {false, "move offset must be finite and within range"};
        }else if(const auto* rotate = std::get_if<RotateNode>(&node.payload)){
            if(!finite(rotate->degrees) || !finite(rotate->pivot) ||
               std::max({std::abs(rotate->degrees.x),std::abs(rotate->degrees.y),std::abs(rotate->degrees.z)}) > 36000.0f ||
               std::max({std::abs(rotate->pivot.x),std::abs(rotate->pivot.y),std::abs(rotate->pivot.z)}) > 1000000.0f)
                return {false, "rotation values must be finite and within range"};
        }else if(const auto* scale = std::get_if<ScaleNode>(&node.payload)){
            if(!finite(scale->factor) || !finite(scale->pivot) ||
               std::abs(scale->factor.x) <= 1e-6f || std::abs(scale->factor.y) <= 1e-6f || std::abs(scale->factor.z) <= 1e-6f ||
               std::max({std::abs(scale->factor.x),std::abs(scale->factor.y),std::abs(scale->factor.z)}) > 10000.0f ||
               std::max({std::abs(scale->pivot.x),std::abs(scale->pivot.y),std::abs(scale->pivot.z)}) > 1000000.0f)
                return {false, "scale values must be finite, non-zero, and within range"};
        }else if(const auto* extrude = std::get_if<ExtrudeNode>(&node.payload)){
            if(!finite(extrude->distance) || std::abs(extrude->distance) <= 1e-6f || std::abs(extrude->distance) > 10000.0f)
                return {false, "extrusion distance must be finite, non-zero, and within range"};
        }else if(const auto* bevel = std::get_if<BevelNode>(&node.payload)){
            if(!finite(bevel->amount) || bevel->amount <= 0.0f || bevel->amount > 1000.0f ||
               bevel->segments < 1 || bevel->segments > 8)
                return {false, "bevel amount or segment count is invalid"};
        }else if(const auto* sample = std::get_if<PointFromMeshNode>(&node.payload)){
            if(sample->count == 0 || sample->count > 100000)
                return {false, "point sampling count must be between 1 and 100000"};
        }else if(const auto* circle = std::get_if<CircleProfileNode>(&node.payload)){
            if(!finite(circle->radius) || circle->radius <= 0.0f || circle->radius > 1000.0f ||
               circle->sides < 3 || circle->sides > 128)
                return {false, "circle profile needs a positive radius and 3 to 128 sides"};
        }else if(const auto* tag = std::get_if<SetSemanticNode>(&node.payload)){
            if(semanticId(tag->semantic) == 0) return {false, "unknown semantic name: " + tag->semantic};
            if(!validTriangleFilter(tag->filter)) return {false, "semantic filter is invalid"};
        }else if(const auto* paint = std::get_if<SetMaterialNode>(&node.payload)){
            if(materialId(paint->material) == 0) return {false, "unknown material name: " + paint->material};
            if(!validTriangleFilter(paint->filter)) return {false, "material filter is invalid"};
        }else if(const auto* smooth = std::get_if<SmoothNormalsNode>(&node.payload)){
            if(!finite(smooth->angleDegrees) || smooth->angleDegrees < 0.0f || smooth->angleDegrees > 180.0f)
                return {false, "smoothing angle must be between 0 and 180 degrees"};
        }else if(const auto* uv = std::get_if<UVProjectNode>(&node.payload)){
            if(!finite(uv->tileSize) || uv->tileSize < 0.01f || uv->tileSize > 1000.0f)
                return {false, "UV tile size must be between 0.01 and 1000 meters"};
        }else if(const auto* copy = std::get_if<CopyToPointsNode>(&node.payload)){
            if(!finite(copy->scale) || copy->scale <= 0.0f || copy->scale > 1000.0f ||
               !finite(copy->randomYawDegrees) || copy->randomYawDegrees < 0.0f || copy->randomYawDegrees > 180.0f ||
               !finite(copy->randomScale) || copy->randomScale < 0.0f || copy->randomScale > 0.9f ||
               copy->maxCopies == 0 || copy->maxCopies > 100000)
                return {false, "copy-to-points settings are invalid"};
        }
        if(!nodeIndex.emplace(node.id, i).second) return {false, "node IDs must be unique"};
        highestNodeId = std::max(highestNodeId, node.id);
    }
    if(graph.nextNodeId != 0 && graph.nextNodeId <= highestNodeId)
        return {false, "next node ID must be greater than all existing node IDs"};

    std::vector<std::size_t> incoming(graph.nodes.size(), 0);
    std::vector<std::vector<std::size_t>> outgoing(graph.nodes.size());
    std::set<std::tuple<NodeId, uint32_t, NodeId, uint32_t>> uniqueLinks;
    std::set<std::pair<NodeId, uint32_t>> occupiedInputs;
    for(const Link& link : graph.links){
        const auto from = nodeIndex.find(link.from);
        const auto to = nodeIndex.find(link.to);
        if(from == nodeIndex.end() || to == nodeIndex.end())
            return {false, "link references a missing node"};
        if(link.from == link.to) return {false, "a node cannot link to itself"};
        if(!uniqueLinks.emplace(link.from, link.fromPort, link.to, link.toPort).second)
            return {false, "duplicate link"};
        const Node& source = graph.nodes[from->second];
        const Node& destination = graph.nodes[to->second];
        const PortType sourceType = outputType(source, link.fromPort);
        const PortType destinationType = inputType(destination, link.toPort);
        if(sourceType == PortType::Invalid || destinationType == PortType::Invalid)
            return {false, "link references an invalid node port"};
        if(sourceType != destinationType)
            return {false, "link port types do not match"};
        if(!occupiedInputs.emplace(link.to, link.toPort).second)
            return {false, "an input port can have only one connection"};

        outgoing[from->second].push_back(to->second);
        ++incoming[to->second];
    }

    std::queue<std::size_t> ready;
    for(std::size_t i = 0; i < incoming.size(); ++i)
        if(incoming[i] == 0) ready.push(i);

    std::size_t visited = 0;
    while(!ready.empty()){
        const std::size_t node = ready.front();
        ready.pop();
        ++visited;
        for(std::size_t next : outgoing[node])
            if(--incoming[next] == 0) ready.push(next);
    }

    if(visited != graph.nodes.size()) return {false, "graph connections must be acyclic"};
    return {};
}

EvaluationResult evaluate(const Graph& graph){
    const ValidationResult validation = validate(graph);
    if(!validation) return {false, 0, {}, validation.error};

    std::unordered_set<NodeId> connectedOutputs;
    for(const Link& link : graph.links) connectedOutputs.insert(link.from);
    NodeId outputId = 0;
    bool pointCloudOutput = false;
    for(const Node& node : graph.nodes){
        if(connectedOutputs.count(node.id)) continue;
        const PortType type = outputType(node,0);
        if(type != PortType::Mesh && type != PortType::Points) continue;
        if(outputId != 0) return {false, 0, {}, "recipe must contain exactly one geometry output"};
        outputId = node.id;
        pointCloudOutput = type == PortType::Points;
    }
    if(outputId == 0) return {false, 0, {}, "recipe needs a mesh or point-cloud output node"};

    std::vector<std::size_t> incoming(graph.nodes.size(), 0);
    std::vector<std::vector<std::size_t>> outgoing(graph.nodes.size());
    std::unordered_map<NodeId, std::size_t> graphIndices;
    graphIndices.reserve(graph.nodes.size());
    for(std::size_t i = 0; i < graph.nodes.size(); ++i) graphIndices.emplace(graph.nodes[i].id, i);
    for(const Link& link : graph.links){
        const auto from = graphIndices.find(link.from);
        const auto to = graphIndices.find(link.to);
        if(from == graphIndices.end() || to == graphIndices.end()) return {false, outputId, {}, "link references a missing node"};
        const std::size_t fromIndex = from->second;
        const std::size_t toIndex = to->second;
        outgoing[fromIndex].push_back(toIndex);
        ++incoming[toIndex];
    }
    std::queue<std::size_t> ready;
    for(std::size_t i = 0; i < incoming.size(); ++i) if(incoming[i] == 0) ready.push(i);
    std::vector<std::size_t> order;
    order.reserve(graph.nodes.size());
    while(!ready.empty()){
        const std::size_t index = ready.front(); ready.pop();
        order.push_back(index);
        for(std::size_t next : outgoing[index]) if(--incoming[next] == 0) ready.push(next);
    }
    if(order.size() != graph.nodes.size()) return {false, outputId, {}, "graph connections must be acyclic"};

    struct PointSet{ std::vector<glm::vec3> positions, normals; };
    std::unordered_map<NodeId, PointGrid> grids;
    std::unordered_map<NodeId, MeshData> meshes;
    std::unordered_map<NodeId, PointSet> pointSets;
    std::unordered_map<NodeId, Curve> curves;
    std::unordered_map<NodeId, Profile> profiles;
    std::unordered_map<NodeId, std::vector<const Link*>> incomingLinks;
    for(const Link& link : graph.links) incomingLinks[link.to].push_back(&link);
    auto linkInto = [&](NodeId node, uint32_t port) -> const Link*{
        const auto found = incomingLinks.find(node);
        if(found == incomingLinks.end()) return nullptr;
        for(const Link* link : found->second) if(link->toPort == port) return link;
        return nullptr;
    };
    auto fail = [&](std::string message){ return EvaluationResult{false, outputId, {}, std::move(message)}; };
    std::string error;
    for(std::size_t index : order){
        const Node& node = graph.nodes[index];
        const std::string name = [&]{
            if(std::holds_alternative<SetGridPointHeightNode>(node.payload)) return std::string("Set Grid Point Height");
            if(std::holds_alternative<GridToMeshNode>(node.payload)) return std::string("Grid to Mesh");
            if(std::holds_alternative<MoveNode>(node.payload)) return std::string("Move");
            if(std::holds_alternative<RotateNode>(node.payload)) return std::string("Rotate");
            if(std::holds_alternative<ScaleNode>(node.payload)) return std::string("Scale");
            if(std::holds_alternative<ExtrudeNode>(node.payload)) return std::string("Extrude");
            if(std::holds_alternative<BevelNode>(node.payload)) return std::string("Bevel");
            if(std::holds_alternative<MeshToPointNode>(node.payload)) return std::string("Mesh to Point");
            if(std::holds_alternative<PointFromMeshNode>(node.payload)) return std::string("Point from Mesh");
            if(std::holds_alternative<SetSemanticNode>(node.payload)) return std::string("Set Semantic");
            if(std::holds_alternative<SetMaterialNode>(node.payload)) return std::string("Set Material");
            if(std::holds_alternative<SmoothNormalsNode>(node.payload)) return std::string("Smooth Normals");
            if(std::holds_alternative<UVProjectNode>(node.payload)) return std::string("UV Project");
            if(std::holds_alternative<CopyToPointsNode>(node.payload)) return std::string("Copy to Points");
            return std::string("Node");
        }();
        // Resolves input 0 as a mesh for the single-input mesh operations.
        const MeshData* meshInput = nullptr;
        std::string meshInputError;
        if(inputType(node, 0) == PortType::Mesh && !std::holds_alternative<MergeNode>(node.payload)){
            const Link* link = linkInto(node.id, 0);
            if(!link) meshInputError = name + " needs a Mesh input";
            else{
                const auto source = meshes.find(link->from);
                if(source == meshes.end()) meshInputError = name + " input did not produce a mesh";
                else meshInput = &source->second;
            }
        }
        MeshData produced;
        bool producesMesh = true;

        if(const auto* curve = std::get_if<CurveNode>(&node.payload)){
            curves.emplace(node.id, curve->curve);
            producesMesh = false;
        }else if(const auto* rectangle = std::get_if<RectangleProfileNode>(&node.payload)){
            profiles.emplace(node.id, makeRectangleProfile(rectangle->width, rectangle->height));
            producesMesh = false;
        }else if(const auto* circle = std::get_if<CircleProfileNode>(&node.payload)){
            profiles.emplace(node.id, makeCircularProfile(circle->radius, circle->sides));
            producesMesh = false;
        }else if(const auto* source = std::get_if<GridNode>(&node.payload)){
            PointGrid grid;
            if(!makeGrid(*source, grid, error)) return fail(error);
            grids.emplace(node.id, std::move(grid));
            producesMesh = false;
        }else if(const auto* height = std::get_if<SetGridPointHeightNode>(&node.payload)){
            const Link* input = linkInto(node.id, 0);
            if(!input) return fail("Set Grid Point Height needs a Point Grid input");
            const auto source = grids.find(input->from);
            if(source == grids.end()) return fail("height node input did not produce a point grid");
            if(height->column > source->second.cellsX || height->row > source->second.cellsZ)
                return fail("height node point index is outside the grid");
            PointGrid changed = source->second;
            changed.points[std::size_t(height->row) * (changed.cellsX + 1) + height->column].y = height->height;
            grids.emplace(node.id, std::move(changed));
            producesMesh = false;
        }else if(std::holds_alternative<GridToMeshNode>(node.payload)){
            const Link* input = linkInto(node.id, 0);
            if(!input) return fail("Grid to Mesh needs a Point Grid input");
            const auto source = grids.find(input->from);
            if(source == grids.end()) return fail("mesh input did not produce a point grid");
            if(!gridToMesh(source->second, produced, error)) return fail(error);
        }else if(const auto* sweepNode = std::get_if<SweepNode>(&node.payload)){
            const Link* curveLink = linkInto(node.id, 0);
            const Link* profileLink = linkInto(node.id, 1);
            if(!curveLink || !profileLink) return fail("Sweep needs both Curve and Profile inputs");
            const auto curve = curves.find(curveLink->from);
            const auto profile = profiles.find(profileLink->from);
            if(curve == curves.end() || profile == profiles.end())
                return fail("Sweep inputs did not produce a curve and a profile");
            if(!sweep(curve->second, profile->second, produced, error, sweepNode->settings)) return fail(error);
        }else if(const auto* interior = std::get_if<InteriorBlockoutNode>(&node.payload)){
            if(!makeInteriorBlockout(*interior, produced, error)) return fail(error);
        }else if(const auto* primitive = std::get_if<AddPrimitiveNode>(&node.payload)){
            if(!makePrimitive(*primitive, produced, error)) return fail(error);
        }else if(std::holds_alternative<MergeNode>(node.payload)){
            std::vector<const MeshData*> inputs;
            for(uint32_t port = 0; port < mergeInputCount; ++port){
                const Link* link = linkInto(node.id, port);
                if(!link) continue;
                const auto source = meshes.find(link->from);
                if(source == meshes.end()) return fail("Merge input did not produce a mesh");
                inputs.push_back(&source->second);
            }
            if(inputs.empty()) return fail("Merge needs at least one Mesh input");
            if(!mergeMeshes(inputs, produced, error)) return fail(error);
        }else if(const auto* copy = std::get_if<CopyToPointsNode>(&node.payload)){
            if(!meshInput) return fail(meshInputError);
            const Link* pointsLink = linkInto(node.id, 1);
            if(!pointsLink) return fail("Copy to Points needs a Points input");
            const auto points = pointSets.find(pointsLink->from);
            if(points == pointSets.end()) return fail("Copy to Points input did not produce points");
            if(!copyToPoints(*meshInput, points->second.positions, points->second.normals, *copy, produced, error))
                return fail(error);
        }else if(std::holds_alternative<MeshToPointNode>(node.payload) || std::holds_alternative<PointFromMeshNode>(node.payload)){
            if(!meshInput) return fail(meshInputError);
            PointSet points;
            const auto* sample = std::get_if<PointFromMeshNode>(&node.payload);
            const bool made = sample
                ? samplePointSet(*meshInput, sample->count, sample->seed, points.positions, &points.normals, error, 100'000)
                : meshToPointSet(*meshInput, points.positions, &points.normals, error, 1'000'000);
            if(!made) return fail(error);
            pointSets.emplace(node.id, std::move(points));
            producesMesh = false;
        }else{
            if(!meshInput) return fail(meshInputError);
            bool made = false;
            if(const auto* move = std::get_if<MoveNode>(&node.payload)) made = moveMesh(*meshInput, *move, produced, error);
            else if(const auto* rotate = std::get_if<RotateNode>(&node.payload)) made = rotateMesh(*meshInput, *rotate, produced, error);
            else if(const auto* scale = std::get_if<ScaleNode>(&node.payload)) made = scaleMesh(*meshInput, *scale, produced, error);
            else if(const auto* extrude = std::get_if<ExtrudeNode>(&node.payload)) made = extrudeFace(*meshInput, *extrude, produced, error);
            else if(const auto* bevel = std::get_if<BevelNode>(&node.payload)) made = bevelMesh(*meshInput, *bevel, produced, error);
            else if(const auto* tag = std::get_if<SetSemanticNode>(&node.payload)) made = setSemantic(*meshInput, *tag, produced, error);
            else if(const auto* paint = std::get_if<SetMaterialNode>(&node.payload)) made = setMaterial(*meshInput, *paint, produced, error);
            else if(const auto* smooth = std::get_if<SmoothNormalsNode>(&node.payload)) made = smoothNormals(*meshInput, smooth->angleDegrees, produced, error);
            else if(const auto* uv = std::get_if<UVProjectNode>(&node.payload)) made = projectUVs(*meshInput, uv->tileSize, produced, error);
            else return fail("unsupported node type");
            if(!made) return fail(error);
        }
        if(producesMesh){
            stampProvenance(produced, node.id);
            meshes.emplace(node.id, std::move(produced));
        }
    }

    if(pointCloudOutput){
        const auto points = pointSets.find(outputId);
        if(points == pointSets.end() || points->second.positions.empty()) return fail("recipe point-cloud output is empty");
        EvaluationResult result{true,outputId,{}, {}};
        result.points = std::move(points->second.positions);
        result.pointNormals = std::move(points->second.normals);
        result.pointCloudOutput = true;
        return result;
    }
    const auto mesh = meshes.find(outputId);
    if(mesh == meshes.end() || mesh->second.empty()) return fail("recipe output mesh is empty");
    return {true, outputId, std::move(mesh->second), {}};
}

Profile makeRectangleProfile(float width, float height){
    if(!finite(width) || !finite(height) || width <= 0.0f || height <= 0.0f) return {};
    const float halfWidth = width * 0.5f, halfHeight = height * 0.5f;
    return {{{-halfWidth, -halfHeight}, {halfWidth, -halfHeight},
             {halfWidth, halfHeight}, {-halfWidth, halfHeight}}};
}

Profile makeCircularProfile(float radius, uint32_t sides){
    if(!finite(radius) || radius <= 0.0f || sides < 3 || sides > 65536) return {};
    Profile profile;
    profile.points.reserve(sides);
    for(uint32_t i = 0; i < sides; ++i){
        const double angle = 2.0 * pi * double(i) / double(sides);
        profile.points.emplace_back(radius * float(std::cos(angle)), radius * float(std::sin(angle)));
    }
    return profile;
}

bool sweep(const Curve& curve, const Profile& sourceProfile, MeshData& output, std::string& error,
           SweepSettings settings){
    std::vector<glm::vec2> profile;
    std::vector<double> profileDistance;
    double profilePerimeter = 0.0, minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    if(!prepareProfile(sourceProfile, settings.capEnds && !curve.closed, profile, profileDistance,
                       profilePerimeter, minX, minY, maxX, maxY, error)) return false;
    if(settings.maxVertices == 0 || settings.maxVertices > std::size_t(std::numeric_limits<uint32_t>::max())){
        error = "maxVertices must fit the 32-bit mesh index range";
        return false;
    }

    const std::size_t profileCount = profile.size();
    const bool capEnds = settings.capEnds && !curve.closed;
    if(capEnds && profileCount > settings.maxVertices / 2){ error = "sweep exceeds the configured vertex limit"; return false; }
    const std::size_t capVertexCount = capEnds ? profileCount * 2 : 0;
    if(capVertexCount >= settings.maxVertices){ error = "sweep exceeds the configured vertex limit"; return false; }
    const std::size_t ringStride = profileCount + 1;
    const std::size_t ringLimit = (settings.maxVertices - capVertexCount) / ringStride;
    if(ringLimit == 0){ error = "sweep exceeds the configured vertex limit"; return false; }

    std::vector<glm::vec3> pathPoints;
    std::vector<double> pathDistance;
    double pathLength = 0.0;
    if(!sampleCurve(curve, settings.sampleSpacing, ringLimit, pathPoints, pathDistance, pathLength, error)) return false;

    glm::vec3 referenceUp;
    if(!normalized(settings.referenceUp, referenceUp)){ error = "referenceUp must be finite and non-zero"; return false; }
    std::vector<glm::vec3> tangents(pathPoints.size());
    for(std::size_t i = 0; i < pathPoints.size(); ++i){
        if(!curve.closed && i == 0){
            if(!direction(pathPoints[0], pathPoints[1], tangents[i])){ error = "curve has a degenerate tangent"; return false; }
        }else if(!curve.closed && i + 1 == pathPoints.size()){
            if(!direction(pathPoints[i - 1], pathPoints[i], tangents[i])){ error = "curve has a degenerate tangent"; return false; }
        }else{
            const std::size_t previous = i == 0 ? pathPoints.size() - 1 : i - 1;
            const std::size_t next = i + 1 == pathPoints.size() ? 0 : i + 1;
            glm::vec3 incoming, outgoing;
            if(!direction(pathPoints[previous], pathPoints[i], incoming) ||
               !direction(pathPoints[i], pathPoints[next], outgoing)){
                error = "curve has a degenerate tangent";
                return false;
            }
            if(!normalized(incoming + outgoing, tangents[i])){
                error = "curve contains a 180-degree cusp";
                return false;
            }
        }
    }

    glm::vec3 side;
    if(!normalized(glm::cross(referenceUp, tangents.front()), side)){
        const glm::vec3 candidates[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        const glm::vec3* fallback = &candidates[0];
        for(const glm::vec3& candidate : candidates)
            if(std::fabs(glm::dot(candidate, tangents.front())) < std::fabs(glm::dot(*fallback, tangents.front()))) fallback = &candidate;
        if(!normalized(glm::cross(*fallback, tangents.front()), side)){ error = "could not establish a sweep frame"; return false; }
    }

    std::vector<glm::vec3> sides(pathPoints.size());
    std::vector<glm::vec3> ups(pathPoints.size());
    sides[0] = side;
    ups[0] = glm::normalize(glm::cross(tangents[0], sides[0]));
    for(std::size_t i = 1; i < pathPoints.size(); ++i){
        if(!transportSide(sides[i - 1], tangents[i - 1], tangents[i], sides[i])){
            error = "curve contains a cusp that cannot be swept";
            return false;
        }
        ups[i] = glm::normalize(glm::cross(tangents[i], sides[i]));
    }
    if(curve.closed){
        glm::vec3 sideAtSeam;
        if(!transportSide(sides.back(), tangents.back(), tangents.front(), sideAtSeam)){
            error = "closed curve frame has a discontinuity";
            return false;
        }
        const float twist = std::atan2(glm::dot(tangents.front(), glm::cross(sides.front(), sideAtSeam)),
                                       glm::dot(sides.front(), sideAtSeam));
        for(std::size_t i = 0; i < sides.size(); ++i){
            const float correction = -twist * float(pathDistance[i] / pathLength);
            sides[i] = glm::normalize(rotateAround(sides[i], tangents[i], correction));
            ups[i] = glm::normalize(glm::cross(tangents[i], sides[i]));
        }
    }

    const std::size_t pathRings = pathPoints.size() + (curve.closed ? 1u : 0u);
    const std::size_t sideVertexCount = pathRings * ringStride;
    const std::size_t vertexCount = sideVertexCount + capVertexCount;
    if(vertexCount > settings.maxVertices || vertexCount > std::size_t(std::numeric_limits<uint32_t>::max())){
        error = "sweep exceeds the configured vertex limit";
        return false;
    }
    const std::size_t pathSegments = curve.closed ? pathPoints.size() : pathPoints.size() - 1;
    if(pathSegments > std::numeric_limits<std::size_t>::max() / profileCount / 6){
        error = "sweep index count overflows";
        return false;
    }
    const std::size_t sideIndexCount = pathSegments * profileCount * 6;
    const std::size_t capIndexCount = capEnds ? (profileCount - 2) * 6 : 0;
    if(sideIndexCount > std::vector<uint32_t>().max_size() - capIndexCount){
        error = "sweep index count overflows";
        return false;
    }

    MeshData generated;
    generated.vertices.reserve(vertexCount);
    generated.indices.reserve(sideIndexCount + capIndexCount);
    const std::size_t profileRing = profileCount + 1;
    for(std::size_t ring = 0; ring < pathRings; ++ring){
        const bool seamRing = curve.closed && ring == pathPoints.size();
        const std::size_t pathIndex = seamRing ? 0 : ring;
        const double u = seamRing ? pathLength : pathDistance[pathIndex];
        for(std::size_t j = 0; j <= profileCount; ++j){
            const std::size_t profileIndex = j == profileCount ? 0 : j;
            const glm::vec2& crossSection = profile[profileIndex];
            MeshVertex vertex;
            vertex.position = pathPoints[pathIndex] + sides[pathIndex] * crossSection.x + ups[pathIndex] * crossSection.y;
            vertex.uv = glm::vec2(float(u), float(profileDistance[j] / profilePerimeter));
            if(!finite(vertex.position) || !finite(vertex.uv)){ error = "generated vertex is not finite"; return false; }
            generated.vertices.push_back(vertex);
        }
    }

    for(std::size_t ring = 0; ring < pathSegments; ++ring){
        const std::size_t nextRing = ring + 1;
        for(std::size_t j = 0; j < profileCount; ++j){
            const uint32_t a = uint32_t(ring * profileRing + j);
            const uint32_t b = uint32_t(nextRing * profileRing + j);
            const uint32_t c = uint32_t(nextRing * profileRing + j + 1);
            const uint32_t d = uint32_t(ring * profileRing + j + 1);
            // Profile winding is CCW, so this order faces out from the swept surface.
            generated.indices.insert(generated.indices.end(), {a, d, b, b, d, c});
        }
    }

    if(capEnds){
        const uint32_t startCap = uint32_t(sideVertexCount);
        const uint32_t endCap = uint32_t(sideVertexCount + profileCount);
        const double profileWidth = std::max(pointEpsilon, maxX - minX);
        const double profileHeight = std::max(pointEpsilon, maxY - minY);
        for(std::size_t j = 0; j < profileCount; ++j){
            const glm::vec2& crossSection = profile[j];
            const glm::vec2 uv(float((double(crossSection.x) - minX) / profileWidth),
                               float((double(crossSection.y) - minY) / profileHeight));
            MeshVertex start;
            start.position = pathPoints.front() + sides.front() * crossSection.x + ups.front() * crossSection.y;
            start.normal = -tangents.front();
            start.uv = uv;
            if(!finite(start.position) || !finite(start.uv)){
                error = "generated cap vertex is not finite";
                return false;
            }
            generated.vertices.push_back(start);
        }
        for(std::size_t j = 0; j < profileCount; ++j){
            const glm::vec2& crossSection = profile[j];
            const glm::vec2 uv(float((double(crossSection.x) - minX) / profileWidth),
                               float((double(crossSection.y) - minY) / profileHeight));
            MeshVertex end;
            end.position = pathPoints.back() + sides.back() * crossSection.x + ups.back() * crossSection.y;
            end.normal = tangents.back();
            end.uv = uv;
            if(!finite(end.position) || !finite(end.uv)){
                error = "generated cap vertex is not finite";
                return false;
            }
            generated.vertices.push_back(end);
        }

        for(std::size_t i = 1; i + 1 < profileCount; ++i){
            generated.indices.insert(generated.indices.end(), {startCap, startCap + uint32_t(i + 1), startCap + uint32_t(i)});
            generated.indices.insert(generated.indices.end(), {endCap, endCap + uint32_t(i), endCap + uint32_t(i + 1)});
        }
    }

    std::vector<glm::vec3> accumulatedNormals(sideVertexCount, glm::vec3(0.0f));
    for(std::size_t i = 0; i < sideIndexCount; i += 3){
        const uint32_t ia = generated.indices[i], ib = generated.indices[i + 1], ic = generated.indices[i + 2];
        const glm::vec3 normal = glm::cross(generated.vertices[ib].position - generated.vertices[ia].position,
                                            generated.vertices[ic].position - generated.vertices[ia].position);
        if(finite(normal) && glm::dot(normal, normal) > float(vectorEpsilonSquared)){
            accumulatedNormals[ia] += normal;
            accumulatedNormals[ib] += normal;
            accumulatedNormals[ic] += normal;
        }
    }
    for(std::size_t ring = 0; ring < pathRings; ++ring){
        // The duplicated profile vertex closes the UV seam. Combine both
        // incident faces before assigning a shared normal to that seam.
        const std::size_t first = ring * profileRing;
        const std::size_t last = first + profileCount;
        accumulatedNormals[first] += accumulatedNormals[last];
        for(std::size_t j = 0; j < profileCount; ++j){
            glm::vec3 normal;
            const std::size_t index = ring * profileRing + j;
            if(!normalized(accumulatedNormals[index], normal)){ error = "sweep produced a degenerate surface"; return false; }
            generated.vertices[index].normal = normal;
        }
        glm::vec3 seamNormal;
        if(!normalized(accumulatedNormals[first], seamNormal)){ error = "sweep produced a degenerate profile seam"; return false; }
        generated.vertices[first].normal = seamNormal;
        generated.vertices[last].normal = seamNormal;
    }
    if(curve.closed){
        for(std::size_t j = 0; j <= profileCount; ++j){
            const std::size_t first = j;
            const std::size_t last = pathPoints.size() * profileRing + j;
            glm::vec3 normal;
            if(!normalized(generated.vertices[first].normal + generated.vertices[last].normal, normal)){
                error = "closed sweep produced a degenerate seam";
                return false;
            }
            generated.vertices[first].normal = normal;
            generated.vertices[last].normal = normal;
        }
    }

    output = std::move(generated);
    error.clear();
    return true;
}

}
