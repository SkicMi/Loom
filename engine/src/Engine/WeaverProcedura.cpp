#include "WeaverProcedura.h"

#include <cstddef>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>

namespace Engine::WeaverProcedura{

ValidationResult validate(const Graph& graph){
    if(graph.schemaVersion != graphSchemaVersion)
        return {false, "unsupported WeaverProcedura graph schema version"};

    std::unordered_map<NodeId, size_t> nodeIndex;
    nodeIndex.reserve(graph.nodes.size());
    for(size_t i = 0; i < graph.nodes.size(); ++i){
        const Node& node = graph.nodes[i];
        if(node.id == 0) return {false, "node ID must be non-zero"};
        if(node.typeId.empty()) return {false, "node type ID must not be empty"};
        if(!nodeIndex.emplace(node.id, i).second) return {false, "node IDs must be unique"};
    }

    std::vector<size_t> incoming(graph.nodes.size(), 0);
    std::vector<std::vector<size_t>> outgoing(graph.nodes.size());
    std::set<std::tuple<NodeId, uint32_t, NodeId, uint32_t>> uniqueLinks;
    for(const Link& link : graph.links){
        const auto from = nodeIndex.find(link.from);
        const auto to = nodeIndex.find(link.to);
        if(from == nodeIndex.end() || to == nodeIndex.end())
            return {false, "link references a missing node"};
        if(link.from == link.to) return {false, "a node cannot link to itself"};
        if(!uniqueLinks.emplace(link.from, link.fromPort, link.to, link.toPort).second)
            return {false, "duplicate link"};

        outgoing[from->second].push_back(to->second);
        ++incoming[to->second];
    }

    std::queue<size_t> ready;
    for(size_t i = 0; i < incoming.size(); ++i)
        if(incoming[i] == 0) ready.push(i);

    size_t visited = 0;
    while(!ready.empty()){
        const size_t node = ready.front();
        ready.pop();
        ++visited;
        for(size_t next : outgoing[node])
            if(--incoming[next] == 0) ready.push(next);
    }

    if(visited != graph.nodes.size()) return {false, "graph connections must be acyclic"};
    return {};
}

}
