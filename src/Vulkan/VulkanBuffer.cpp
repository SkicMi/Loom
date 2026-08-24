#include "VulkanBuffer.h"
#include <vk_mem_alloc.h>

VulkanBuffer::VulkanBuffer(const VulkanDevice& device,
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    MemoryUsage memoryUsage,
    const BufferConfig& config) : device(device), size(size), memoryUsage(memoryUsage), config(config){
        //A sentence now instead of VUID-VkBufferCreateInfo-size-00912 later
        if(size == 0){
            throw std::runtime_error("VulkanBuffer: size is 0");
        }
        createBuffer(usage);
}

void VulkanBuffer::createBuffer(vk::BufferUsageFlags usage){
    VmaAllocator allocator = device.getAllocator().get();

    vk::BufferCreateInfo bufferInfo;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    //VMA_MEMORY_USAGE_AUTO* means "read the buffer usage flags and the host access flags and
    //pick the memory type yourself". That is strictly more informed than the old
    //findMemoryType could ever be: it knows the whole VkBufferCreateInfo, it knows the heap
    //budgets, and it will fall back to another heap instead of failing when one is full
    VmaAllocationCreateInfo allocationCreateInfo{};

    switch(memoryUsage){
        case MemoryUsage::GPU_ONLY:
            allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;

        case MemoryUsage::CPU_TO_GPU:
            //SEQUENTIAL_WRITE promises memcpy in, never a read back out. That promise is what
            //lets VMA hand out write combined or resizable BAR memory, which is uncached:
            //writing it is fast, reading it would be dreadfully slow
            allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            break;

        case MemoryUsage::GPU_TO_CPU:
            //RANDOM asks for cached memory instead, because this one is going to be read
            allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            break;
    }

    if(memoryUsage != MemoryUsage::GPU_ONLY && config.persistentlyMapped){
        allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    if(config.dedicated){
        allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    allocationCreateInfo.priority = config.priority;

    const VkBufferCreateInfo& rawBufferInfo = bufferInfo;

    VkBuffer rawBuffer = VK_NULL_HANDLE;
    VmaAllocation rawAllocation = nullptr;
    VmaAllocationInfo allocationInfo{};

    const VkResult result = config.minAlignment > 0
        ? vmaCreateBufferWithAlignment(allocator, &rawBufferInfo, &allocationCreateInfo,
                                       config.minAlignment, &rawBuffer, &rawAllocation, &allocationInfo)
        : vmaCreateBuffer(allocator, &rawBufferInfo, &allocationCreateInfo,
                          &rawBuffer, &rawAllocation, &allocationInfo);

    if(result != VK_SUCCESS){
        throw std::runtime_error("VulkanBuffer: vmaCreateBuffer failed");
    }

    //Ownership is split on purpose: VMA created both, but the handle goes into vk::raii::Buffer
    //so every getBuffer() caller keeps working, and the memory stays with MemoryAllocation.
    //Member order in the header guarantees the buffer dies first
    allocation = MemoryAllocation(allocator, rawAllocation);
    buffer = vk::raii::Buffer(device.getDevice(), rawBuffer);

    //What VMA actually chose. Nothing here was assumed before the allocation - that is the
    //difference from asking for eHostVisible | eHostCoherent and hoping
    VkMemoryPropertyFlags properties = 0;
    vmaGetAllocationMemoryProperties(allocator, rawAllocation, &properties);

    hostVisible = (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    hostCoherent = (properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    deviceLocal = (properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
    mappedData = allocationInfo.pMappedData;
}

void VulkanBuffer::upload(const void* data, vk::DeviceSize uploadSize, vk::DeviceSize offset){
    if(!hostVisible){
        throw std::runtime_error("upload() works only on a host visible buffer (CPU_TO_GPU or GPU_TO_CPU)");
    }
    if(offset + uploadSize > size){
        throw std::runtime_error("upload() data size bigger then buffer size");
    }

    //Does the mapping (or reuses the persistent one), the memcpy, and the vkFlushMappedMemoryRanges
    //if the memory type turned out not to be coherent. The old code was only correct because it
    //had demanded eHostCoherent up front; now the memory type is VMA's choice, so the flush is real
    if(vmaCopyMemoryToAllocation(allocation.getAllocator(), data, allocation.get(), offset, uploadSize) != VK_SUCCESS){
        throw std::runtime_error("upload() failed to copy into the allocation");
    }
}

void VulkanBuffer::download(void* destination, vk::DeviceSize downloadSize, vk::DeviceSize offset) const {
    if(!hostVisible){
        throw std::runtime_error{"download() works only on a host visible buffer (GPU_TO_CPU)"};
    }
    if(offset + downloadSize > size){
        throw std::runtime_error{"download() data size bigger then buffer size"};
    }

    //The mirror image: invalidates the range first when the memory is not coherent, so what
    //the CPU reads is what the GPU wrote and not a stale cache line
    if(vmaCopyAllocationToMemory(allocation.getAllocator(), allocation.get(), offset, destination, downloadSize) != VK_SUCCESS){
        throw std::runtime_error{"download() failed to copy out of the allocation"};
    }
}

void VulkanBuffer::setDebugName(const char* name){
    vmaSetAllocationName(allocation.getAllocator(), allocation.get(), name);
}
