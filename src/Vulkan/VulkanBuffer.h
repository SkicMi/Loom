#pragma once
#include "VulkanDevice.h"
#include <cstring>

enum class MemoryUsage{
    GPU_ONLY,
    CPU_TO_GPU
};

class VulkanBuffer{
    public:
    VulkanBuffer(const VulkanDevice& device,
         vk::DeviceSize size, 
         vk::BufferUsageFlags usage, 
         MemoryUsage memoryUsage);

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) = default;

    void upload(const void* data, vk::DeviceSize uploadSize, vk::DeviceSize offset = 0);

    //getter
    const vk::raii::Buffer& getBuffer() const {return buffer;}
    const vk::raii::DeviceMemory& getMemory() const {return memory;}

    private:
    const VulkanDevice& device;
    vk::DeviceSize size;
    MemoryUsage memoryUsage;

    vk::raii::Buffer buffer = nullptr;
    vk::raii::DeviceMemory memory = nullptr;

    void createBuffer(vk::BufferUsageFlags usage);

};
