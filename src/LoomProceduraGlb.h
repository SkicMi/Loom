#pragma once

// Procedura mesh -> .glb: one primitive per material (colour from the caller), and for a held
// asset its grips in the node's extras ("loom_tool": kind + grips, the fields of Warp's Grip), so a
// generated sword or hammer can be imported as a Tool and held where its recipe says.

#include "LoomAgentJson.h"
#include <Engine/WeaverProcedura.h>

#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace Loom::WeaverProceduraRecipe{

inline bool writeGlb(const Engine::WeaverProcedura::MeshData& mesh, const std::string& path,
                     const std::function<glm::vec3(uint16_t material)>& colour, const std::string& kind,
                     const std::vector<Engine::WeaverProcedura::AssetGrip>& grips, std::string& error){
    namespace Proc = Engine::WeaverProcedura;
    if(mesh.empty() || mesh.indices.size() % 3 != 0){ error = "nothing to export"; return false; }
    // Triangles by material; each group gets its own compact vertex list.
    std::map<uint16_t, std::vector<std::size_t>> groups;
    for(std::size_t t = 0; t < mesh.indices.size() / 3; ++t)
        groups[t < mesh.triangles.size() ? mesh.triangles[t].material : 0].push_back(t);

    std::string binary;
    auto append = [&](const void* data, std::size_t bytes){
        binary.append(static_cast<const char*>(data), bytes);
        while(binary.size() % 4) binary.push_back('\0');
    };
    std::ostringstream views, accessors, primitives, materials;
    accessors.precision(9);
    int view = 0, accessor = 0, material = 0;
    auto addView = [&](std::size_t offset, std::size_t bytes, int target){
        views << (view ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << offset << ",\"byteLength\":" << bytes
              << ",\"target\":" << target << '}';
        return view++;
    };
    for(const auto& [id, triangles] : groups){
        std::map<uint32_t, uint32_t> remap;
        std::vector<float> positions, normals;
        std::vector<uint32_t> indices;
        glm::vec3 low(1e30f), high(-1e30f);
        for(std::size_t t : triangles)
            for(int k = 0; k < 3; ++k){
                const uint32_t source = mesh.indices[t * 3 + std::size_t(k)];
                auto [found, added] = remap.emplace(source, uint32_t(remap.size()));
                if(added){
                    const Proc::MeshVertex& v = mesh.vertices[source];
                    positions.insert(positions.end(), {v.position.x, v.position.y, v.position.z});
                    const glm::vec3 n = glm::length(v.normal) > 1e-6f ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
                    normals.insert(normals.end(), {n.x, n.y, n.z});
                    low = glm::min(low, v.position); high = glm::max(high, v.position);
                }
                indices.push_back(found->second);
            }
        const std::size_t count = positions.size() / 3;
        std::size_t offset = binary.size();
        const int positionView = addView(offset, positions.size() * 4, 34962);
        append(positions.data(), positions.size() * 4);
        offset = binary.size();
        const int normalView = addView(offset, normals.size() * 4, 34962);
        append(normals.data(), normals.size() * 4);
        offset = binary.size();
        const int indexView = addView(offset, indices.size() * 4, 34963);
        append(indices.data(), indices.size() * 4);
        accessors << (accessor ? "," : "")
                  << "{\"bufferView\":" << positionView << ",\"componentType\":5126,\"count\":" << count
                  << ",\"type\":\"VEC3\",\"min\":[" << low.x << ',' << low.y << ',' << low.z << "],\"max\":["
                  << high.x << ',' << high.y << ',' << high.z << "]},"
                  << "{\"bufferView\":" << normalView << ",\"componentType\":5126,\"count\":" << count << ",\"type\":\"VEC3\"},"
                  << "{\"bufferView\":" << indexView << ",\"componentType\":5125,\"count\":" << indices.size() << ",\"type\":\"SCALAR\"}";
        primitives << (material ? "," : "") << "{\"attributes\":{\"POSITION\":" << accessor << ",\"NORMAL\":" << accessor + 1
                   << "},\"indices\":" << accessor + 2 << ",\"material\":" << material << '}';
        accessor += 3;
        const glm::vec3 c = colour(id);
        const std::string name = Proc::materialName(id);
        const bool metal = name == "metal" || name == "steel_chain";
        materials << (material ? "," : "") << "{\"name\":" << agentJsonEscape(name.empty() ? "default" : name)
                  << ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" << c.x << ',' << c.y << ',' << c.z
                  << ",1],\"metallicFactor\":" << (metal ? 1 : 0) << ",\"roughnessFactor\":" << (metal ? 0.35 : 0.7) << "}}";
        ++material;
    }

    std::ostringstream extras;
    extras.precision(9);
    if(!kind.empty() || !grips.empty()){
        extras << ",\"extras\":{\"loom_tool\":{\"kind\":" << agentJsonEscape(kind) << ",\"grips\":[";
        for(std::size_t g = 0; g < grips.size(); ++g){
            const Proc::AssetGrip& grip = grips[g];
            auto vec = [&](const glm::vec3& v){ extras << '[' << v.x << ',' << v.y << ',' << v.z << ']'; };
            extras << (g ? "," : "") << "{\"name\":" << agentJsonEscape(grip.name) << ",\"point\":"; vec(grip.point);
            extras << ",\"axis\":"; vec(grip.axis);
            extras << ",\"palm\":"; vec(grip.palm);
            extras << ",\"thickness\":" << grip.thickness << ",\"preset\":" << agentJsonEscape(grip.preset) << ",\"hand\":" << grip.hand << '}';
        }
        extras << "]}}";
    }
    std::ostringstream json;
    json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Loom WeaverProcedura\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
         << "\"nodes\":[{\"name\":\"Procedura\",\"mesh\":0" << extras.str() << "}],"
         << "\"meshes\":[{\"name\":\"Procedura\",\"primitives\":[" << primitives.str() << "]}],"
         << "\"materials\":[" << materials.str() << "],\"accessors\":[" << accessors.str() << "],"
         << "\"bufferViews\":[" << views.str() << "],\"buffers\":[{\"byteLength\":" << binary.size() << "}]}";
    std::string text = json.str();
    while(text.size() % 4) text.push_back(' ');

    std::ofstream out(path, std::ios::binary);
    if(!out){ error = "cannot write " + path; return false; }
    auto word = [&](uint32_t value){ out.write(reinterpret_cast<const char*>(&value), 4); };
    word(0x46546C67u); word(2u); word(uint32_t(12 + 8 + text.size() + 8 + binary.size()));
    word(uint32_t(text.size())); word(0x4E4F534Au); out.write(text.data(), std::streamsize(text.size()));
    word(uint32_t(binary.size())); word(0x004E4942u); out.write(binary.data(), std::streamsize(binary.size()));
    if(!out){ error = "writing " + path + " failed"; return false; }
    return true;
}

}  // namespace Loom::WeaverProceduraRecipe
