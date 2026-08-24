#pragma once
#include "VulkanDevice.h"
#include "VulkanAllocator.h"
#include <cstring>

//What the CPU is going to do with this memory. VMA turns each of these into an actual
//memory type at runtime, which is the whole point - GPU_ONLY on a card with resizable BAR
//can land somewhere a hand written findMemoryType would never have looked
enum class MemoryUsage{
    GPU_ONLY,   //the CPU never touches it: vertex, index, storage buffers, attachments
    CPU_TO_GPU, //the CPU writes it and never reads it back: staging, per frame UBO and SSBO
    GPU_TO_CPU  //the CPU reads it: readback staging. Asks for cached memory, not write combined
};

//Knobs that only matter for a few buffers, so they have defaults that suit all the others
struct BufferConfig{
    //Force a whole VkDeviceMemory block of its own instead of a slice of a shared one.
    //Worth it for a handful of big, long lived resources; wasteful for anything small
    bool dedicated = false;

    //Host visible buffers stay mapped for their whole life. Vulkan allows one mapping per
    //VkDeviceMemory, and VMA keeps a block mapped for everyone in it, so this is the way a
    //per frame UBO avoids a map/unmap pair every single frame
    bool persistentlyMapped = true;

    //0..1, only listened to when VK_EXT_memory_priority is present. 1.0 means "evict this
    //last when memory runs out" - meant for attachments the frame cannot be drawn without
    float priority = 0.5f;

    //Extra alignment on top of what the driver requires. The dynamic UBO stride uses it
    vk::DeviceSize minAlignment = 0;
};

class VulkanBuffer{
    public:
    VulkanBuffer(const VulkanDevice& device,
         vk::DeviceSize size,
         vk::BufferUsageFlags usage,
         MemoryUsage memoryUsage,
         const BufferConfig& config = {});

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) = default;

    void upload(const void* data, vk::DeviceSize uploadSize, vk::DeviceSize offset = 0);
    void download(void* destination, vk::DeviceSize downloadSize, vk::DeviceSize offset = 0) const;

    //Shows up in the VMA JSON dump next to the allocation. Costs nothing in release
    void setDebugName(const char* name);

    //getter
    const vk::raii::Buffer& getBuffer() const {return buffer;}
    vk::DeviceSize getSize() const {return size;}
    MemoryUsage getMemoryUsage() const {return memoryUsage;}

    //Whether the CPU can touch this memory at all, and whether writes reach the GPU without
    //an explicit flush. Both are answers VMA gives after it picked the memory type, not
    //assumptions made before it
    bool isHostVisible() const {return hostVisible;}
    bool isHostCoherent() const {return hostCoherent;}
    bool isDeviceLocal() const {return deviceLocal;}

    //The persistent mapping, or nullptr. Writing through it directly skips upload()'s memcpy,
    //but then flushing is the caller's problem on non coherent memory
    void* getMappedData() const {return mappedData;}

    //True when VMA put a "GPU only" buffer in memory the CPU can also write - resizable BAR.
    //Nothing depends on it, but it is the one thing worth measuring after this change
    bool isDeviceLocalHostVisible() const {return deviceLocal && hostVisible;}

    private:
    const VulkanDevice& device;
    vk::DeviceSize size;
    MemoryUsage memoryUsage;
    BufferConfig config;

    bool hostVisible = false;
    bool hostCoherent = false;
    bool deviceLocal = false;
    void* mappedData = nullptr;

    //Declaration order is the destruction contract: allocation is declared first so it dies
    //last, which means vkDestroyBuffer always runs before the memory under it is freed
    MemoryAllocation allocation;
    vk::raii::Buffer buffer = nullptr;

    void createBuffer(vk::BufferUsageFlags usage);

};
