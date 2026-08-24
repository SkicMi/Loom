#include "VulkanAllocator.h"
#include <vk_mem_alloc.h>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>

VulkanAllocator::VulkanAllocator(const vk::raii::Instance& instance,
                                const vk::raii::PhysicalDevice& physicalDevice,
                                const vk::raii::Device& device,
                                const AllocatorConfig& config) : config(config){

    //vk::raii carries its own function pointer tables - it opens the loader itself and Loom
    //never links libvulkan. So VMA is built with VMA_STATIC_VULKAN_FUNCTIONS 0 (see CMake)
    //and would find no symbols on its own. It gets these two entry points and fetches
    //every other function it needs through them
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = instance.getDispatcher()->vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = instance.getDispatcher()->vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo createInfo{};
    createInfo.instance = *instance;
    createInfo.physicalDevice = *physicalDevice;
    createInfo.device = *device;
    createInfo.pVulkanFunctions = &functions;

    //Telling VMA the version unlocks the core paths: dedicated allocations,
    //vkBindBufferMemory2 and vkGetBufferMemoryRequirements2 are all core since 1.1,
    //vkGetDeviceBufferMemoryRequirements since 1.3 (that one is why maintenance4 is enabled
    //in VulkanDevice - the function exists in 1.3, but the feature has to be on to call it)
    createInfo.vulkanApiVersion = config.apiVersion;

    if(config.useMemoryBudget){
        createInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    if(config.useMemoryPriority){
        createInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
    }

    //VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT is deliberately NOT set. It would drop
    //VMA's internal mutexes and Loom is single threaded today, so it would even be correct -
    //but the first worker thread that ever allocates would corrupt the allocator silently.
    //The mutexes are uncontended anyway, so this costs nothing measurable

    if(vmaCreateAllocator(&createInfo, &allocator) != VK_SUCCESS){
        throw std::runtime_error("VulkanAllocator: vmaCreateAllocator failed");
    }

    memoryProperties = physicalDevice.getMemoryProperties();
    maxMemoryAllocationCount = physicalDevice.getProperties().limits.maxMemoryAllocationCount;
}

VulkanAllocator::~VulkanAllocator(){
    if(allocator){
        vmaDestroyAllocator(allocator);
        allocator = nullptr;
    }
}

VulkanAllocator::VulkanAllocator(VulkanAllocator&& other) noexcept :
allocator(other.allocator),
config(other.config),
memoryProperties(other.memoryProperties),
maxMemoryAllocationCount(other.maxMemoryAllocationCount){
    other.allocator = nullptr;
}

VulkanAllocator& VulkanAllocator::operator=(VulkanAllocator&& other) noexcept{
    if(this != &other){
        if(allocator){
            vmaDestroyAllocator(allocator);
        }
        allocator = other.allocator;
        config = other.config;
        memoryProperties = other.memoryProperties;
        maxMemoryAllocationCount = other.maxMemoryAllocationCount;
        other.allocator = nullptr;
    }
    return *this;
}

MemoryStats VulkanAllocator::getStats() const{
    MemoryStats out;
    if(!allocator) return out;

    out.deviceAllocationLimit = maxMemoryAllocationCount;

    //One VmaBudget per heap. This is the cheap query - VMA keeps these counters current as
    //it allocates, it does not walk the block lists like vmaCalculateStatistics does
    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
    vmaGetHeapBudgets(allocator, budgets.data());

    const uint32_t heapCount = memoryProperties.memoryHeapCount;
    out.heaps.reserve(heapCount);

    for(uint32_t i = 0; i < heapCount; ++i){
        HeapStats heap;
        heap.heapIndex = i;
        heap.deviceLocal = static_cast<bool>(memoryProperties.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal);
        heap.size = memoryProperties.memoryHeaps[i].size;
        heap.budget = budgets[i].budget;
        heap.usage = budgets[i].usage;
        heap.blockBytes = budgets[i].statistics.blockBytes;
        heap.allocationBytes = budgets[i].statistics.allocationBytes;
        heap.blockCount = budgets[i].statistics.blockCount;
        heap.allocationCount = budgets[i].statistics.allocationCount;

        out.blockCount += heap.blockCount;
        out.allocationCount += heap.allocationCount;
        out.blockBytes += heap.blockBytes;
        out.allocationBytes += heap.allocationBytes;

        out.heaps.push_back(heap);
    }

    return out;
}

std::string VulkanAllocator::summary() const{
    const MemoryStats stats = getStats();

    //Kept in KiB so the number stays readable without pulling in <iomanip>
    std::ostringstream out;
    out << "VMA: " << stats.allocationCount << " allocations in "
        << stats.blockCount << " device memory blocks (limit " << stats.deviceAllocationLimit << "), "
        << (stats.allocationBytes / 1024) << " KiB used of "
        << (stats.blockBytes / 1024) << " KiB reserved";
    return out.str();
}

std::string VulkanAllocator::statsJson(bool detailedMap) const{
    if(!allocator) return "{}";

    char* json = nullptr;
    vmaBuildStatsString(allocator, &json, detailedMap ? VK_TRUE : VK_FALSE);
    std::string out = json ? json : "{}";
    vmaFreeStatsString(allocator, json);
    return out;
}

void VulkanAllocator::writeStatsJson(const std::string& path, bool detailedMap) const{
    std::ofstream file(path);
    if(!file){
        throw std::runtime_error("VulkanAllocator: cannot open " + path + " for the stats dump");
    }
    file << statsJson(detailedMap);
}

void VulkanAllocator::setCurrentFrameIndex(uint32_t frameIndex) const{
    if(allocator){
        vmaSetCurrentFrameIndex(allocator, frameIndex);
    }
}

MemoryAllocation::~MemoryAllocation(){
    reset();
}

MemoryAllocation::MemoryAllocation(MemoryAllocation&& other) noexcept :
allocator(other.allocator), allocation(other.allocation){
    other.allocator = nullptr;
    other.allocation = nullptr;
}

MemoryAllocation& MemoryAllocation::operator=(MemoryAllocation&& other) noexcept{
    if(this != &other){
        reset();
        allocator = other.allocator;
        allocation = other.allocation;
        other.allocator = nullptr;
        other.allocation = nullptr;
    }
    return *this;
}

void MemoryAllocation::reset(){
    if(allocator && allocation){
        vmaFreeMemory(allocator, allocation);
    }
    allocator = nullptr;
    allocation = nullptr;
}
