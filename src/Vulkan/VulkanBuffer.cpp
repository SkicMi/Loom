#include "VulkanBuffer.h"

VulkanBuffer::VulkanBuffer(const VulkanDevice& device,
    vk::DeviceSize size, 
    vk::BufferUsageFlags usage, 
    MemoryUsage memoryUsage) : device(device), size(size), memoryUsage(memoryUsage){
        createBuffer(usage);
}

void VulkanBuffer::createBuffer(vk::BufferUsageFlags usage){
    vk::BufferCreateInfo bufferInfo;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    buffer = vk::raii::Buffer(device.getDevice(),bufferInfo);

    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();

    vk::MemoryPropertyFlags properties;
    if(memoryUsage == MemoryUsage::GPU_ONLY){
        properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    }
    else if(memoryUsage == MemoryUsage::CPU_TO_GPU){
        properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    }

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, properties);

    memory = vk::raii::DeviceMemory(device.getDevice(),allocInfo);

    buffer.bindMemory(*memory,0);
}


void VulkanBuffer::upload(const void* data, vk::DeviceSize uploadSize, vk::DeviceSize offset){
    if(memoryUsage != MemoryUsage::CPU_TO_GPU){
        throw std::runtime_error("upload() works only on CPU_TO_GPU BUFFER");
    }
    if(offset + uploadSize > size){
        throw std::runtime_error("upload() data size bigger then buffer size");
    }

    void* mapped = memory.mapMemory(offset,uploadSize);
    std::memcpy(mapped, data, static_cast<size_t>(uploadSize));
    memory.unmapMemory();
}

void VulkanBuffer::download(void* destination, vk::DeviceSize downloadSize, vk::DeviceSize offset) const {
    if(memoryUsage != MemoryUsage::CPU_TO_GPU){
        throw std::runtime_error{"download() works only on CPU_TO_GPU BUFFER"};
    }
    if(offset + downloadSize > size){
        throw std::runtime_error{"download() data size bigger then buffer size"};
    }

    const void* mapped = memory.mapMemory(offset, downloadSize);
    std::memcpy(destination, mapped, static_cast<size_t>(downloadSize));
    memory.unmapMemory();
}

