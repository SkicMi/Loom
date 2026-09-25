#include "WeaverProcedura.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>
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

enum class PortType{ Invalid, Curve, Profile, Mesh };

PortType outputType(const Node& node, uint32_t port){
    if(port != 0) return PortType::Invalid;
    if(std::holds_alternative<CurveNode>(node.payload)) return PortType::Curve;
    if(std::holds_alternative<RectangleProfileNode>(node.payload)) return PortType::Profile;
    if(std::holds_alternative<SweepNode>(node.payload)) return PortType::Mesh;
    return PortType::Invalid;
}

PortType inputType(const Node& node, uint32_t port){
    if(!std::holds_alternative<SweepNode>(node.payload)) return PortType::Invalid;
    if(port == 0) return PortType::Curve;
    if(port == 1) return PortType::Profile;
    return PortType::Invalid;
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

ValidationResult validate(const Graph& graph){
    if(graph.schemaVersion != graphSchemaVersion)
        return {false, "unsupported WeaverProcedura graph schema version"};

    std::unordered_map<NodeId, std::size_t> nodeIndex;
    nodeIndex.reserve(graph.nodes.size());
    NodeId highestNodeId = 0;
    for(std::size_t i = 0; i < graph.nodes.size(); ++i){
        const Node& node = graph.nodes[i];
        if(node.id == 0) return {false, "node ID must be non-zero"};
        if(std::holds_alternative<std::monostate>(node.payload)) return {false, "node payload type is missing"};
        if(!finite(node.editorX) || !finite(node.editorY)) return {false, "node position must be finite"};
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

    const Node* output = nullptr;
    for(const Node& node : graph.nodes){
        if(!std::holds_alternative<SweepNode>(node.payload)) continue;
        if(output) return {false, 0, {}, "first evaluator supports one Sweep output"};
        output = &node;
    }
    if(!output) return {false, 0, {}, "graph needs a Sweep output node"};

    const Link* curveLink = nullptr;
    const Link* profileLink = nullptr;
    for(const Link& link : graph.links){
        if(link.to != output->id) continue;
        if(link.toPort == 0) curveLink = &link;
        else if(link.toPort == 1) profileLink = &link;
    }
    if(!curveLink || !profileLink) return {false, output->id, {}, "Sweep needs both Curve and Profile inputs"};

    const Node* curveSource = nullptr;
    const Node* profileSource = nullptr;
    for(const Node& node : graph.nodes){
        if(node.id == curveLink->from) curveSource = &node;
        if(node.id == profileLink->from) profileSource = &node;
    }
    const auto* curveNode = curveSource ? std::get_if<CurveNode>(&curveSource->payload) : nullptr;
    const auto* profileNode = profileSource ? std::get_if<RectangleProfileNode>(&profileSource->payload) : nullptr;
    if(!curveNode || !profileNode) return {false, output->id, {}, "Sweep inputs must come from Curve and Rectangle Profile nodes"};

    MeshData generated;
    std::string error;
    const SweepNode& sweepNode = std::get<SweepNode>(output->payload);
    const Profile profile = makeRectangleProfile(profileNode->width, profileNode->height);
    if(!sweep(curveNode->curve, profile, generated, error, sweepNode.settings))
        return {false, output->id, {}, error};

    EvaluationResult result;
    result.succeeded = true;
    result.outputNode = output->id;
    result.mesh = std::move(generated);
    return result;
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
