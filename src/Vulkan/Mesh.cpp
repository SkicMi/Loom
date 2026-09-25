#include "Mesh.h"

Mesh::Mesh(const VulkanDevice& device,
    const VulkanCommand& command,
    const std::vector<Vertex>& vertices,
    const std::vector<uint16_t>& indices,
    bool dynamic) :
    vertexBuffer(device,
    sizeof(Vertex) * vertices.size(),
    vk::BufferUsageFlagBits::eVertexBuffer | (dynamic ? vk::BufferUsageFlags{} : vk::BufferUsageFlags(vk::BufferUsageFlagBits::eTransferDst)),
    dynamic ? MemoryUsage::CPU_TO_GPU : MemoryUsage::GPU_ONLY),
    vertexCount(static_cast<uint32_t>(vertices.size())),
    dynamic(dynamic){

        

        if(vertices.empty()){
            throw std::runtime_error("Mesh: Vertices empty");
        }

        //The mesh buffers are GPU_ONLY: the staging buffer below is what the CPU writes, and
        //copyBuffer moves it into memory the GPU reads at full speed. They used to be
        //CPU_TO_GPU, which paid for the staging copy and then left the data in host memory
        //anyway - the vertex fetch read across the bus every frame
        vk::DeviceSize vertexBytes = sizeof(Vertex) * vertices.size();


        if(dynamic){
            vertexBuffer.upload(vertices.data(), vertexBytes);
        }else{
            VulkanBuffer staging(device, vertexBytes, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            staging.upload(vertices.data(), vertexBytes);
            command.copyBuffer(staging.getBuffer(), vertexBuffer.getBuffer(), vertexBytes);
        }

        if(!indices.empty()){
            vk::DeviceSize indexBytes = sizeof(uint16_t) * indices.size();

            indexBuffer.emplace(device,indexBytes, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_ONLY);
            VulkanBuffer staging(device, indexBytes, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            staging.upload(indices.data(), indexBytes);
            command.copyBuffer(staging.getBuffer(), indexBuffer->getBuffer(), indexBytes);
            indexCount = static_cast<uint32_t>(indices.size());
        }
    }


void Mesh::updateVertices(const std::vector<Vertex>& vertices){
    if(!dynamic) throw std::logic_error("Mesh::updateVertices requires a dynamic mesh");
    if(vertices.size() != vertexCount) throw std::invalid_argument("Mesh::updateVertices cannot change vertex count");
    vertexBuffer.upload(vertices.data(), sizeof(Vertex) * vertices.size());
}
