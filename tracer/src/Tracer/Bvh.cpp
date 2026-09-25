#include "Tracer/Bvh.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tracer{

namespace{

struct Box{
    glm::vec3 min{std::numeric_limits<float>::infinity()};
    glm::vec3 max{-std::numeric_limits<float>::infinity()};
    void grow(const glm::vec3& p){ min = glm::min(min, p); max = glm::max(max, p); }
    void grow(const Box& b){ min = glm::min(min, b.min); max = glm::max(max, b.max); }
    float area() const{
        const glm::vec3 d = max - min;
        if(d.x < 0.0f) return 0.0f;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }
};

constexpr int Bins = 16;
constexpr int MaxDepth = 60;            //stog obilaska ima 96 mjesta
constexpr uint32_t MaxLeaf = 8;         //list veci od ovoga samo kad se trokuti ne daju razdvojiti

}

void Bvh::build(const std::vector<glm::vec3>& positions, const std::vector<Triangle>& triangles){
    nodes.clear();
    prepared.clear();
    order.clear();
    maxDepth = 0;
    const uint32_t count = uint32_t(triangles.size());
    if(count == 0) return;

    std::vector<Box> boxes(count);
    std::vector<glm::vec3> centres(count);
    for(uint32_t i = 0; i < count; ++i){
        for(int k = 0; k < 3; ++k) boxes[i].grow(positions[triangles[i].v[k]]);
        centres[i] = (boxes[i].min + boxes[i].max) * 0.5f;
    }
    order.resize(count);
    for(uint32_t i = 0; i < count; ++i) order[i] = i;

    nodes.reserve(size_t(count) * 2);
    nodes.push_back(Node{});
    struct Task{ uint32_t node, first, count; int depth; };
    std::vector<Task> tasks{{0, 0, count, 0}};

    while(!tasks.empty()){
        const Task task = tasks.back();
        tasks.pop_back();
        maxDepth = std::max(maxDepth, task.depth);

        Box bounds, centroidBounds;
        for(uint32_t i = task.first; i < task.first + task.count; ++i){
            bounds.grow(boxes[order[i]]);
            centroidBounds.grow(centres[order[i]]);
        }
        Node& node = nodes[task.node];
        node.min = bounds.min;
        node.max = bounds.max;
        auto makeLeaf = [&]{
            nodes[task.node].leftOrFirst = task.first;
            nodes[task.node].count = task.count;
        };
        if(task.count <= 2 || task.depth >= MaxDepth){ makeLeaf(); continue; }

        //SAH po binovima, sve tri osi
        float bestCost = std::numeric_limits<float>::infinity();
        int bestAxis = -1, bestSplit = -1;
        const glm::vec3 extent = centroidBounds.max - centroidBounds.min;
        for(int axis = 0; axis < 3; ++axis){
            if(extent[axis] <= 1e-12f) continue;
            Box binBox[Bins];
            uint32_t binCount[Bins] = {};
            const float scale = float(Bins) / extent[axis];
            for(uint32_t i = task.first; i < task.first + task.count; ++i){
                const uint32_t t = order[i];
                const int b = std::min(Bins - 1, int((centres[t][axis] - centroidBounds.min[axis]) * scale));
                binBox[b].grow(boxes[t]);
                ++binCount[b];
            }
            float leftArea[Bins - 1];
            uint32_t leftCount[Bins - 1];
            Box sweep;
            uint32_t sum = 0;
            for(int b = 0; b < Bins - 1; ++b){
                sweep.grow(binBox[b]);
                sum += binCount[b];
                leftArea[b] = sweep.area();
                leftCount[b] = sum;
            }
            sweep = Box{};
            sum = 0;
            for(int b = Bins - 1; b > 0; --b){
                sweep.grow(binBox[b]);
                sum += binCount[b];
                if(leftCount[b - 1] == 0 || sum == 0) continue;
                const float cost = leftArea[b - 1] * float(leftCount[b - 1]) + sweep.area() * float(sum);
                if(cost < bestCost){ bestCost = cost; bestAxis = axis; bestSplit = b; }
            }
        }
        //Trosak lista: svi trokuti. Trosak reza: obilazak (~1 trokut) plus ocekivani trokuti
        const float leafCost = bounds.area() * float(task.count);
        const float splitCost = bounds.area() * 1.0f + bestCost;
        if(bestAxis < 0 || (splitCost >= leafCost && task.count <= MaxLeaf)){ makeLeaf(); continue; }

        const float scale = float(Bins) / extent[bestAxis];
        const float minimum = centroidBounds.min[bestAxis];
        uint32_t* begin = order.data() + task.first;
        uint32_t* middle = std::partition(begin, begin + task.count, [&](uint32_t t){
            return std::min(Bins - 1, int((centres[t][bestAxis] - minimum) * scale)) < bestSplit;
        });
        uint32_t leftCount = uint32_t(middle - begin);
        if(leftCount == 0 || leftCount == task.count){
            //Ne bi se smjelo dogoditi (rez je izabran s oba djeteta neprazna), ali float zna
            //razbiti jednakost na rubu bina - onda pola-pola po redu
            leftCount = task.count / 2;
            std::nth_element(begin, begin + leftCount, begin + task.count, [&](uint32_t a, uint32_t b){
                return centres[a][bestAxis] < centres[b][bestAxis];
            });
        }
        const uint32_t left = uint32_t(nodes.size());
        nodes.push_back(Node{});
        nodes.push_back(Node{});
        nodes[task.node].leftOrFirst = left;
        nodes[task.node].count = 0;
        tasks.push_back({left, task.first, leftCount, task.depth + 1});
        tasks.push_back({left + 1, task.first + leftCount, task.count - leftCount, task.depth + 1});
    }

    prepared.resize(count);
    for(uint32_t i = 0; i < count; ++i){
        const Triangle& t = triangles[order[i]];
        const glm::vec3& v0 = positions[t.v[0]];
        prepared[i] = {v0, positions[t.v[1]] - v0, positions[t.v[2]] - v0};
    }
    nodes.shrink_to_fit();
}

}
