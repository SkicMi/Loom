#pragma once

#include "LoomAgentJson.h"
#include <Engine/WeaverProcedura.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Loom::WeaverProceduraRecipe{

namespace Proc = Engine::WeaverProcedura;

struct Document{
    std::string name = "Untitled Recipe";
    Proc::Graph graph;
};

inline const AgentJsonValue& required(const AgentJsonValue& value, const char* key){
    const AgentJsonValue* result = value.get(key);
    if(!result) throw std::runtime_error(std::string("missing field: ") + key);
    return *result;
}

inline std::string readString(const AgentJsonValue& value, const char* field){
    if(value.kind != AgentJsonValue::Kind::String)
        throw std::runtime_error(std::string(field) + " must be a string");
    return value.string;
}

inline double readNumber(const AgentJsonValue& value, const char* field){
    if(value.kind != AgentJsonValue::Kind::Number || !std::isfinite(value.number))
        throw std::runtime_error(std::string(field) + " must be a finite number");
    return value.number;
}

inline uint64_t readUnsigned(const AgentJsonValue& value, const char* field){
    const double number = readNumber(value, field);
    if(number < 0.0 || number > 9007199254740991.0 || std::floor(number) != number)
        throw std::runtime_error(std::string(field) + " must be a safe non-negative integer");
    return uint64_t(number);
}

inline uint32_t readU32(const AgentJsonValue& value, const char* field){
    const uint64_t number = readUnsigned(value, field);
    if(number > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error(std::string(field) + " exceeds 32-bit range");
    return uint32_t(number);
}

inline float readFloat(const AgentJsonValue& value, const char* field){
    const double number = readNumber(value, field);
    if(std::abs(number) > double(std::numeric_limits<float>::max()))
        throw std::runtime_error(std::string(field) + " exceeds float range");
    return float(number);
}

inline bool readBool(const AgentJsonValue& value, const char* field){
    if(value.kind != AgentJsonValue::Kind::Boolean)
        throw std::runtime_error(std::string(field) + " must be a boolean");
    return value.boolean;
}

inline glm::vec3 readVec3(const AgentJsonValue& value, const char* field){
    if(value.kind != AgentJsonValue::Kind::Array || value.array.size() != 3)
        throw std::runtime_error(std::string(field) + " must contain three numbers");
    return {readFloat(value.array[0], field), readFloat(value.array[1], field),
            readFloat(value.array[2], field)};
}

inline const char* primitiveName(Proc::PrimitiveType primitive){
    switch(primitive){
        case Proc::PrimitiveType::Cube: return "cube";
        case Proc::PrimitiveType::Plane: return "plane";
        case Proc::PrimitiveType::Sphere: return "sphere";
        case Proc::PrimitiveType::Pyramid: return "pyramid";
        case Proc::PrimitiveType::Capsule: return "capsule";
        case Proc::PrimitiveType::Cylinder: return "cylinder";
        case Proc::PrimitiveType::Torus: return "torus";
    }
    throw std::runtime_error("unknown primitive type");
}

inline Proc::PrimitiveType readPrimitive(const AgentJsonValue& value){
    const std::string name = readString(value,"primitive");
    if(name == "cube") return Proc::PrimitiveType::Cube;
    if(name == "plane") return Proc::PrimitiveType::Plane;
    if(name == "sphere") return Proc::PrimitiveType::Sphere;
    if(name == "pyramid") return Proc::PrimitiveType::Pyramid;
    if(name == "capsule") return Proc::PrimitiveType::Capsule;
    if(name == "cylinder") return Proc::PrimitiveType::Cylinder;
    if(name == "torus") return Proc::PrimitiveType::Torus;
    throw std::runtime_error("unknown primitive type: " + name);
}

inline void writeFilter(std::ostream& out, const Proc::TriangleFilter& filter){
    out << "\"filter\":{\"semantic\":" << agentJsonEscape(filter.semantic)
        << ",\"use_direction\":" << (filter.useDirection ? "true" : "false")
        << ",\"direction\":[" << filter.direction.x << ',' << filter.direction.y << ',' << filter.direction.z
        << "],\"max_angle_degrees\":" << filter.maxAngleDegrees << '}';
}

inline Proc::TriangleFilter readFilter(const AgentJsonValue& parameters){
    Proc::TriangleFilter filter;
    const AgentJsonValue* encoded = parameters.get("filter");
    if(!encoded) return filter;
    if(encoded->kind != AgentJsonValue::Kind::Object) throw std::runtime_error("filter must be an object");
    filter.semantic = readString(required(*encoded, "semantic"), "filter.semantic");
    filter.useDirection = readBool(required(*encoded, "use_direction"), "filter.use_direction");
    filter.direction = readVec3(required(*encoded, "direction"), "filter.direction");
    filter.maxAngleDegrees = readFloat(required(*encoded, "max_angle_degrees"), "filter.max_angle_degrees");
    return filter;
}

inline std::string serialize(const Document& document){
    const Proc::ValidationResult validation = Proc::validate(document.graph);
    if(!validation) throw std::runtime_error(validation.error);
    if(document.name.empty() || document.name.size() > 120)
        throw std::runtime_error("recipe name must contain 1 to 120 characters");

    std::ostringstream out;
    out << std::setprecision(9)
        << "{\"format\":\"loom.weaverprocedura.recipe\",\"schema_version\":"
        << document.graph.schemaVersion << ",\"name\":" << agentJsonEscape(document.name)
        << ",\"seed\":" << document.graph.seed << ",\"nodes\":[";
    for(std::size_t i = 0; i < document.graph.nodes.size(); ++i){
        if(i) out << ',';
        const Proc::Node& node = document.graph.nodes[i];
        out << "{\"id\":" << node.id << ",\"position\":[" << node.editorX << ',' << node.editorY
            << "],\"parameters\":";
        if(const auto* curve = std::get_if<Proc::CurveNode>(&node.payload)){
            out << "{\"type\":\"curve\",\"closed\":" << (curve->curve.closed ? "true" : "false")
                << ",\"points\":[";
            for(std::size_t p = 0; p < curve->curve.points.size(); ++p){
                if(p) out << ',';
                const glm::vec3& point = curve->curve.points[p];
                out << '[' << point.x << ',' << point.y << ',' << point.z << ']';
            }
            out << "]}";
        }else if(const auto* profile = std::get_if<Proc::RectangleProfileNode>(&node.payload)){
            out << "{\"type\":\"rectangle_profile\",\"width\":" << profile->width
                << ",\"height\":" << profile->height << '}';
        }else if(const auto* sweep = std::get_if<Proc::SweepNode>(&node.payload)){
            out << "{\"type\":\"sweep\",\"sample_spacing\":" << sweep->settings.sampleSpacing
                << ",\"reference_up\":[" << sweep->settings.referenceUp.x << ','
                << sweep->settings.referenceUp.y << ',' << sweep->settings.referenceUp.z
                << "],\"cap_ends\":" << (sweep->settings.capEnds ? "true" : "false")
                << ",\"max_vertices\":" << sweep->settings.maxVertices << '}';
        }else if(const auto* grid = std::get_if<Proc::GridNode>(&node.payload)){
            out << "{\"type\":\"grid\",\"width\":" << grid->width << ",\"depth\":" << grid->depth
                << ",\"cells_x\":" << grid->cellsX << ",\"cells_z\":" << grid->cellsZ << '}';
        }else if(const auto* height = std::get_if<Proc::SetGridPointHeightNode>(&node.payload)){
            out << "{\"type\":\"set_grid_point_height\",\"column\":" << height->column
                << ",\"row\":" << height->row << ",\"height\":" << height->height << '}';
        }else if(std::holds_alternative<Proc::GridToMeshNode>(node.payload)){
            out << "{\"type\":\"grid_to_mesh\"}";
        }else if(const auto* interior = std::get_if<Proc::InteriorBlockoutNode>(&node.payload)){
            out << "{\"type\":\"interior_blockout\",\"rooms_per_side\":" << interior->roomsPerSide
                << ",\"room_width\":" << interior->roomWidth << ",\"room_depth\":" << interior->roomDepth
                << ",\"corridor_width\":" << interior->corridorWidth << ",\"wall_height\":" << interior->wallHeight
                << ",\"wall_thickness\":" << interior->wallThickness << ",\"floor_thickness\":"
                << interior->floorThickness << ",\"door_width\":" << interior->doorWidth << '}';
        }else if(const auto* primitive = std::get_if<Proc::AddPrimitiveNode>(&node.payload)){
            out << "{\"type\":\"add_primitive\",\"primitive\":" << agentJsonEscape(primitiveName(primitive->primitive))
                << ",\"size\":[" << primitive->size.x << ',' << primitive->size.y << ',' << primitive->size.z
                << "],\"tube_ratio\":" << primitive->tubeRatio << '}';
        }else if(const auto* move = std::get_if<Proc::MoveNode>(&node.payload)){
            out << "{\"type\":\"move\",\"offset\":[" << move->offset.x << ',' << move->offset.y << ',' << move->offset.z << "]}";
        }else if(const auto* rotate = std::get_if<Proc::RotateNode>(&node.payload)){
            out << "{\"type\":\"rotate\",\"degrees\":[" << rotate->degrees.x << ',' << rotate->degrees.y << ',' << rotate->degrees.z
                << "],\"pivot\":[" << rotate->pivot.x << ',' << rotate->pivot.y << ',' << rotate->pivot.z << "]}";
        }else if(const auto* scale = std::get_if<Proc::ScaleNode>(&node.payload)){
            out << "{\"type\":\"scale\",\"factor\":[" << scale->factor.x << ',' << scale->factor.y << ',' << scale->factor.z
                << "],\"pivot\":[" << scale->pivot.x << ',' << scale->pivot.y << ',' << scale->pivot.z << "]}";
        }else if(const auto* extrude = std::get_if<Proc::ExtrudeNode>(&node.payload)){
            out << "{\"type\":\"extrude\",\"face_index\":" << extrude->faceIndex << ",\"distance\":" << extrude->distance
                << ",\"use_filter\":" << (extrude->useFilter ? "true" : "false") << ',';
            writeFilter(out, extrude->filter);
            out << '}';
        }else if(const auto* bevel = std::get_if<Proc::BevelNode>(&node.payload)){
            out << "{\"type\":\"bevel\",\"amount\":" << bevel->amount << ",\"segments\":" << bevel->segments << '}';
        }else if(std::holds_alternative<Proc::MeshToPointNode>(node.payload)){
            out << "{\"type\":\"mesh_to_point\"}";
        }else if(const auto* sample = std::get_if<Proc::PointFromMeshNode>(&node.payload)){
            out << "{\"type\":\"point_from_mesh\",\"count\":" << sample->count << ",\"seed\":" << sample->seed << '}';
        }else if(const auto* circle = std::get_if<Proc::CircleProfileNode>(&node.payload)){
            out << "{\"type\":\"circle_profile\",\"radius\":" << circle->radius << ",\"sides\":" << circle->sides << '}';
        }else if(std::holds_alternative<Proc::MergeNode>(node.payload)){
            out << "{\"type\":\"merge\"}";
        }else if(const auto* tag = std::get_if<Proc::SetSemanticNode>(&node.payload)){
            out << "{\"type\":\"set_semantic\",\"semantic\":" << agentJsonEscape(tag->semantic) << ',';
            writeFilter(out, tag->filter);
            out << '}';
        }else if(const auto* paint = std::get_if<Proc::SetMaterialNode>(&node.payload)){
            out << "{\"type\":\"set_material\",\"material\":" << agentJsonEscape(paint->material) << ',';
            writeFilter(out, paint->filter);
            out << '}';
        }else if(const auto* smooth = std::get_if<Proc::SmoothNormalsNode>(&node.payload)){
            out << "{\"type\":\"smooth_normals\",\"angle_degrees\":" << smooth->angleDegrees << '}';
        }else if(const auto* uv = std::get_if<Proc::UVProjectNode>(&node.payload)){
            out << "{\"type\":\"uv_project\",\"tile_size\":" << uv->tileSize << '}';
        }else if(const auto* copy = std::get_if<Proc::CopyToPointsNode>(&node.payload)){
            out << "{\"type\":\"copy_to_points\",\"align_to_normal\":" << (copy->alignToNormal ? "true" : "false")
                << ",\"scale\":" << copy->scale << ",\"random_yaw_degrees\":" << copy->randomYawDegrees
                << ",\"random_scale\":" << copy->randomScale << ",\"seed\":" << copy->seed
                << ",\"max_copies\":" << copy->maxCopies << '}';
        }else if(const auto* smooth = std::get_if<Proc::CurveSmoothNode>(&node.payload)){
            out << "{\"type\":\"curve_smooth\",\"subdivisions\":" << smooth->subdivisions << '}';
        }else if(const auto* catenary = std::get_if<Proc::CatenaryCurveNode>(&node.payload)){
            out << "{\"type\":\"catenary_curve\",\"start\":[" << catenary->start.x << ',' << catenary->start.y << ','
                << catenary->start.z << "],\"end\":[" << catenary->end.x << ',' << catenary->end.y << ',' << catenary->end.z
                << "],\"sag\":" << catenary->sag << ",\"samples\":" << catenary->samples << '}';
        }else if(const auto* along = std::get_if<Proc::CopyAlongCurveNode>(&node.payload)){
            out << "{\"type\":\"copy_along_curve\",\"spacing\":" << along->spacing << ",\"start_offset\":" << along->startOffset
                << ",\"roll_degrees\":" << along->rollDegrees << ",\"alternate_roll_degrees\":" << along->alternateRollDegrees
                << ",\"reference_up\":[" << along->referenceUp.x << ',' << along->referenceUp.y << ',' << along->referenceUp.z
                << "],\"max_copies\":" << along->maxCopies << '}';
        }else{
            throw std::runtime_error("recipe contains an unsupported node payload");
        }
        out << '}';
    }
    out << "],\"links\":[";
    for(std::size_t i = 0; i < document.graph.links.size(); ++i){
        if(i) out << ',';
        const Proc::Link& link = document.graph.links[i];
        out << "{\"from\":" << link.from << ",\"from_port\":" << link.fromPort
            << ",\"to\":" << link.to << ",\"to_port\":" << link.toPort << '}';
    }
    out << "]}";
    const std::string encoded = out.str();
    if(encoded.size() > 1'000'000) throw std::runtime_error("serialized recipe exceeds the 1000000 byte limit");
    return encoded;
}

inline Document parse(const std::string& source){
    if(source.empty() || source.size() > 1'000'000)
        throw std::runtime_error("recipe file must contain 1 to 1000000 bytes");
    const AgentJsonValue root = AgentJsonParser(source).parse();
    if(root.kind != AgentJsonValue::Kind::Object ||
       readString(required(root, "format"), "format") != "loom.weaverprocedura.recipe")
        throw std::runtime_error("not a Loom WeaverProcedura recipe");

    Document document;
    document.name = readString(required(root, "name"), "name");
    if(document.name.empty() || document.name.size() > 120)
        throw std::runtime_error("recipe name must contain 1 to 120 characters");
    const uint64_t schema = readUnsigned(required(root, "schema_version"), "schema_version");
    if(schema < 3 || schema > Proc::graphSchemaVersion) throw std::runtime_error("unsupported recipe schema version");
    // Versions 4-6 add node types, per-triangle attributes, and optional fields with
    // defaults (tube_ratio, extrude use_filter/filter); older payloads keep their meaning.
    document.graph.schemaVersion = Proc::graphSchemaVersion;
    document.graph.seed = readUnsigned(required(root, "seed"), "seed");

    const AgentJsonValue& nodes = required(root, "nodes");
    if(nodes.kind != AgentJsonValue::Kind::Array || nodes.array.size() > 256)
        throw std::runtime_error("recipe nodes must be an array containing at most 256 entries");
    for(const AgentJsonValue& encodedNode : nodes.array){
        if(encodedNode.kind != AgentJsonValue::Kind::Object)
            throw std::runtime_error("recipe node must be an object");
        Proc::Node node;
        node.id = readUnsigned(required(encodedNode, "id"), "node.id");
        const AgentJsonValue& position = required(encodedNode, "position");
        if(position.kind != AgentJsonValue::Kind::Array || position.array.size() != 2)
            throw std::runtime_error("node position must contain two numbers");
        node.editorX = readFloat(position.array[0], "node.position.x");
        node.editorY = readFloat(position.array[1], "node.position.y");
        const AgentJsonValue& parameters = required(encodedNode, "parameters");
        const std::string type = readString(required(parameters, "type"), "node.parameters.type");
        if(type == "curve"){
            Proc::CurveNode curve;
            curve.curve.closed = readBool(required(parameters, "closed"), "curve.closed");
            const AgentJsonValue& points = required(parameters, "points");
            if(points.kind != AgentJsonValue::Kind::Array || points.array.size() > 4096)
                throw std::runtime_error("curve points must be an array containing at most 4096 entries");
            for(const AgentJsonValue& point : points.array)
                curve.curve.points.push_back(readVec3(point, "curve.point"));
            node.payload = std::move(curve);
        }else if(type == "rectangle_profile"){
            node.payload = Proc::RectangleProfileNode{readFloat(required(parameters, "width"), "profile.width"),
                readFloat(required(parameters, "height"), "profile.height")};
        }else if(type == "sweep"){
            Proc::SweepNode sweep;
            sweep.settings.sampleSpacing = readFloat(required(parameters, "sample_spacing"), "sweep.sample_spacing");
            sweep.settings.referenceUp = readVec3(required(parameters, "reference_up"), "sweep.reference_up");
            sweep.settings.capEnds = readBool(required(parameters, "cap_ends"), "sweep.cap_ends");
            sweep.settings.maxVertices = std::size_t(readUnsigned(required(parameters, "max_vertices"), "sweep.max_vertices"));
            node.payload = sweep;
        }else if(type == "grid"){
            Proc::GridNode grid;
            grid.width = readFloat(required(parameters, "width"), "grid.width");
            grid.depth = readFloat(required(parameters, "depth"), "grid.depth");
            grid.cellsX = readU32(required(parameters, "cells_x"), "grid.cells_x");
            grid.cellsZ = readU32(required(parameters, "cells_z"), "grid.cells_z");
            node.payload = grid;
        }else if(type == "set_grid_point_height"){
            Proc::SetGridPointHeightNode height;
            height.column = readU32(required(parameters, "column"), "height.column");
            height.row = readU32(required(parameters, "row"), "height.row");
            height.height = readFloat(required(parameters, "height"), "height.value");
            node.payload = height;
        }else if(type == "grid_to_mesh"){
            node.payload = Proc::GridToMeshNode{};
        }else if(type == "interior_blockout"){
            Proc::InteriorBlockoutNode room;
            room.roomsPerSide = readU32(required(parameters, "rooms_per_side"), "interior.rooms_per_side");
            room.roomWidth = readFloat(required(parameters, "room_width"), "interior.room_width");
            room.roomDepth = readFloat(required(parameters, "room_depth"), "interior.room_depth");
            room.corridorWidth = readFloat(required(parameters, "corridor_width"), "interior.corridor_width");
            room.wallHeight = readFloat(required(parameters, "wall_height"), "interior.wall_height");
            room.wallThickness = readFloat(required(parameters, "wall_thickness"), "interior.wall_thickness");
            room.floorThickness = readFloat(required(parameters, "floor_thickness"), "interior.floor_thickness");
            room.doorWidth = readFloat(required(parameters, "door_width"), "interior.door_width");
            node.payload = room;
        }else if(type == "add_primitive"){
            Proc::AddPrimitiveNode primitive;
            primitive.primitive = readPrimitive(required(parameters,"primitive"));
            primitive.size = readVec3(required(parameters,"size"),"primitive.size");
            if(const AgentJsonValue* tube = parameters.get("tube_ratio")) primitive.tubeRatio = readFloat(*tube,"primitive.tube_ratio");
            node.payload = primitive;
        }else if(type == "move"){
            node.payload = Proc::MoveNode{readVec3(required(parameters,"offset"),"move.offset")};
        }else if(type == "rotate"){
            Proc::RotateNode rotate;
            rotate.degrees = readVec3(required(parameters,"degrees"),"rotate.degrees");
            rotate.pivot = readVec3(required(parameters,"pivot"),"rotate.pivot");
            node.payload = rotate;
        }else if(type == "scale"){
            Proc::ScaleNode scale;
            scale.factor = readVec3(required(parameters,"factor"),"scale.factor");
            scale.pivot = readVec3(required(parameters,"pivot"),"scale.pivot");
            node.payload = scale;
        }else if(type == "extrude"){
            Proc::ExtrudeNode extrude;
            extrude.faceIndex = readU32(required(parameters,"face_index"),"extrude.face_index");
            extrude.distance = readFloat(required(parameters,"distance"),"extrude.distance");
            if(const AgentJsonValue* useFilter = parameters.get("use_filter")) extrude.useFilter = readBool(*useFilter,"extrude.use_filter");
            extrude.filter = readFilter(parameters);
            node.payload = extrude;
        }else if(type == "bevel"){
            Proc::BevelNode bevel;
            bevel.amount = readFloat(required(parameters,"amount"),"bevel.amount");
            bevel.segments = readU32(required(parameters,"segments"),"bevel.segments");
            node.payload = bevel;
        }else if(type == "mesh_to_point"){
            node.payload = Proc::MeshToPointNode{};
        }else if(type == "point_from_mesh"){
            Proc::PointFromMeshNode sample;
            sample.count = readU32(required(parameters,"count"),"point_from_mesh.count");
            sample.seed = readUnsigned(required(parameters,"seed"),"point_from_mesh.seed");
            node.payload = sample;
        }else if(type == "circle_profile"){
            Proc::CircleProfileNode circle;
            circle.radius = readFloat(required(parameters,"radius"),"circle_profile.radius");
            circle.sides = readU32(required(parameters,"sides"),"circle_profile.sides");
            node.payload = circle;
        }else if(type == "merge"){
            node.payload = Proc::MergeNode{};
        }else if(type == "set_semantic"){
            Proc::SetSemanticNode tag;
            tag.semantic = readString(required(parameters,"semantic"),"set_semantic.semantic");
            tag.filter = readFilter(parameters);
            node.payload = std::move(tag);
        }else if(type == "set_material"){
            Proc::SetMaterialNode paint;
            paint.material = readString(required(parameters,"material"),"set_material.material");
            paint.filter = readFilter(parameters);
            node.payload = std::move(paint);
        }else if(type == "smooth_normals"){
            node.payload = Proc::SmoothNormalsNode{readFloat(required(parameters,"angle_degrees"),"smooth_normals.angle_degrees")};
        }else if(type == "uv_project"){
            node.payload = Proc::UVProjectNode{readFloat(required(parameters,"tile_size"),"uv_project.tile_size")};
        }else if(type == "copy_to_points"){
            Proc::CopyToPointsNode copy;
            copy.alignToNormal = readBool(required(parameters,"align_to_normal"),"copy_to_points.align_to_normal");
            copy.scale = readFloat(required(parameters,"scale"),"copy_to_points.scale");
            copy.randomYawDegrees = readFloat(required(parameters,"random_yaw_degrees"),"copy_to_points.random_yaw_degrees");
            copy.randomScale = readFloat(required(parameters,"random_scale"),"copy_to_points.random_scale");
            copy.seed = readUnsigned(required(parameters,"seed"),"copy_to_points.seed");
            copy.maxCopies = readU32(required(parameters,"max_copies"),"copy_to_points.max_copies");
            node.payload = copy;
        }else if(type == "curve_smooth"){
            node.payload = Proc::CurveSmoothNode{readU32(required(parameters,"subdivisions"),"curve_smooth.subdivisions")};
        }else if(type == "catenary_curve"){
            Proc::CatenaryCurveNode catenary;
            catenary.start = readVec3(required(parameters,"start"),"catenary_curve.start");
            catenary.end = readVec3(required(parameters,"end"),"catenary_curve.end");
            catenary.sag = readFloat(required(parameters,"sag"),"catenary_curve.sag");
            catenary.samples = readU32(required(parameters,"samples"),"catenary_curve.samples");
            node.payload = catenary;
        }else if(type == "copy_along_curve"){
            Proc::CopyAlongCurveNode along;
            along.spacing = readFloat(required(parameters,"spacing"),"copy_along_curve.spacing");
            along.startOffset = readFloat(required(parameters,"start_offset"),"copy_along_curve.start_offset");
            along.rollDegrees = readFloat(required(parameters,"roll_degrees"),"copy_along_curve.roll_degrees");
            along.alternateRollDegrees = readFloat(required(parameters,"alternate_roll_degrees"),"copy_along_curve.alternate_roll_degrees");
            along.referenceUp = readVec3(required(parameters,"reference_up"),"copy_along_curve.reference_up");
            along.maxCopies = readU32(required(parameters,"max_copies"),"copy_along_curve.max_copies");
            node.payload = along;
        }else{
            throw std::runtime_error("unknown recipe node type: " + type);
        }
        document.graph.nodes.push_back(std::move(node));
    }

    const AgentJsonValue& links = required(root, "links");
    if(links.kind != AgentJsonValue::Kind::Array || links.array.size() > 1024)
        throw std::runtime_error("recipe links must be an array containing at most 1024 entries");
    for(const AgentJsonValue& encodedLink : links.array){
        if(encodedLink.kind != AgentJsonValue::Kind::Object)
            throw std::runtime_error("recipe link must be an object");
        Proc::Link link;
        link.from = readUnsigned(required(encodedLink, "from"), "link.from");
        link.fromPort = readU32(required(encodedLink, "from_port"), "link.from_port");
        link.to = readUnsigned(required(encodedLink, "to"), "link.to");
        link.toPort = readU32(required(encodedLink, "to_port"), "link.to_port");
        document.graph.links.push_back(link);
    }

    Proc::NodeId highest = 0;
    for(const Proc::Node& node : document.graph.nodes) highest = std::max(highest, node.id);
    if(highest == std::numeric_limits<Proc::NodeId>::max())
        document.graph.nextNodeId = 0;
    else document.graph.nextNodeId = highest + 1;
    const Proc::ValidationResult validation = Proc::validate(document.graph);
    if(!validation) throw std::runtime_error(validation.error);
    return document;
}

} // namespace Loom::WeaverProceduraRecipe
