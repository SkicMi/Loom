#include "Mesh.h"

Mesh::Mesh(const VulkanDevice& device,
    const VulkanCommand& command,
    const std::vector<Vertex>& vertices,
    const std::vector<uint16_t>& indices) :
    vertexBuffer(device, 
    sizeof(Vertex) * vertices.size(), 
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, 
    MemoryUsage::CPU_TO_GPU), 
    vertexCount(static_cast<uint32_t>(vertices.size())){

        

        if(vertices.empty()){
            throw std::runtime_error("Mesh: Vertices empty");
        }

        vk::DeviceSize vertexBytes = sizeof(Vertex) * vertices.size();


        {
            VulkanBuffer staging(device, vertexBytes, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            staging.upload(vertices.data(),vertexBytes);
            command.copyBuffer(staging.getBuffer(), vertexBuffer.getBuffer(), vertexBytes);
        }

        if(!indices.empty()){
            vk::DeviceSize indexBytes = sizeof(uint16_t) * indices.size();

            indexBuffer.emplace(device,indexBytes, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::CPU_TO_GPU);
            VulkanBuffer staging(device, indexBytes, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            staging.upload(indices.data(), indexBytes);
            command.copyBuffer(staging.getBuffer(), indexBuffer->getBuffer(), indexBytes);
            indexCount = static_cast<uint32_t>(indices.size());
        }
    }

