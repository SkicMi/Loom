#pragma once
#include "VulkanBuffer.h"
#include "VulkanCommand.h"
#include "Vertex.h"
#include <optional>
#include <vector>

class Mesh{
    public:
    Mesh(const VulkanDevice& device,
    const VulkanCommand& command,
    const std::vector<Vertex>& vertices,
    const std::vector<uint16_t>& indices = {});

    Mesh(const Mesh&) = delete;
    Mesh& operator = (const Mesh&) = delete;
    Mesh(Mesh&&) = default;
    

    //getters
    const VulkanBuffer& getVertexBuffer() const {return vertexBuffer;}
    const VulkanBuffer& getIndexBuffer() const {return *indexBuffer;}
    uint32_t getVertexCount() const {return vertexCount;}
    uint32_t getIndexCount() const {return indexCount;}
    bool hasIndices() const {return indexBuffer.has_value();}

    private:
    VulkanBuffer vertexBuffer;
    std::optional <VulkanBuffer> indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;



};