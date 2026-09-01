#pragma once
#include <atomic>
#include <vulkan/vulkan_raii.hpp>
#include <cstdint>
#include <string>
#include <vector>

//VMA's handles are opaque pointers. They are declared here exactly the way vk_mem_alloc.h
//declares them, so no Loom header has to pull in VMA's 16000 lines - only the .cpp files
//that really allocate include it. Repeating an identical typedef is legal C++, so the two
//declarations agree instead of clashing
VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

//Which optional pieces the allocator was allowed to switch on. VulkanDevice fills this in,
//because it is the one that knows what the physical device actually offered
struct AllocatorConfig{
    bool useMemoryBudget = false;   //VK_EXT_memory_budget: real usage/budget straight from the driver
    bool useMemoryPriority = false; //VK_EXT_memory_priority: attachments get evicted last under pressure
    uint32_t apiVersion = VK_API_VERSION_1_3;
};

//One heap the way VMA sees it. blockBytes is what Vulkan handed out, allocationBytes what
//Loom is really using - the difference is the suballocator's slack
struct HeapStats{
    uint32_t heapIndex = 0;
    bool deviceLocal = false;
    uint64_t size = 0;            //what the heap physically is
    uint64_t budget = 0;          //what this process may use right now (needs memory budget)
    uint64_t usage = 0;           //what this process is using right now (needs memory budget)
    uint64_t blockBytes = 0;
    uint64_t allocationBytes = 0;
    uint32_t blockCount = 0;
    uint32_t allocationCount = 0;
};

//The number that matters for scaling is blockCount: those are the VkDeviceMemory objects,
//and maxMemoryAllocationCount (4096 on a lot of drivers) counts exactly those. Before VMA
//blockCount and allocationCount were always equal - one allocation per buffer
struct MemoryStats{
    uint32_t blockCount = 0;
    uint32_t allocationCount = 0;
    uint64_t blockBytes = 0;
    uint64_t allocationBytes = 0;
    uint32_t deviceAllocationLimit = 0; //maxMemoryAllocationCount, so blockCount has something to be compared to
    std::vector<HeapStats> heaps;
};

class VulkanAllocator{
    public:
    //A null allocator, so VulkanDevice can hold one as a member before the logical device exists
    VulkanAllocator() = default;

    VulkanAllocator(const vk::raii::Instance& instance,
                    const vk::raii::PhysicalDevice& physicalDevice,
                    const vk::raii::Device& device,
                    const AllocatorConfig& config);

    ~VulkanAllocator();

    VulkanAllocator(const VulkanAllocator&) = delete;
    VulkanAllocator& operator=(const VulkanAllocator&) = delete;
    VulkanAllocator(VulkanAllocator&& other) noexcept;
    VulkanAllocator& operator=(VulkanAllocator&& other) noexcept;

    //getters
    VmaAllocator get() const {return allocator;}
    const AllocatorConfig& getConfig() const {return config;}
    bool isValid() const {return allocator != nullptr;}

    //Cheap - a summary VMA keeps up to date anyway. Safe to call every frame
    MemoryStats getStats() const;

    //KOLIKO JE ALOKACIJA IKAD NAPRAVLJENO, a ne koliko ih trenutno stoji.
    //
    //getStats() daje trenutno stanje, pa alokacija koja se u istom kadru napravi i oslobodi
    //kroz njega prolazi nevidljivo - a bas je to najskuplji obrazac koji se u petlji kadra
    //moze pojaviti. Ovaj brojac se ne smanjuje, pa se test smije pitati je li se kroz sto
    //kadrova dogodila ijedna
    static void noteAllocation() {allocationsMade.fetch_add(1);}
    static uint64_t getAllocationsMade() {return allocationsMade.load();}
    static void resetAllocationsMade() {allocationsMade.store(0);}

    //A one line summary for a log or a test
    std::string summary() const;

    //VMA's own JSON dump, the format VmaDumpVis draws. Slow: a debugging tool, not a frame call
    std::string statsJson(bool detailedMap = true) const;
    void writeStatsJson(const std::string& path, bool detailedMap = true) const;

    //Tells VMA how old an allocation is, which is what its budget heuristics work on
    void setCurrentFrameIndex(uint32_t frameIndex) const;

    private:
    static std::atomic<uint64_t> allocationsMade;

    VmaAllocator allocator = nullptr;
    AllocatorConfig config;
    vk::PhysicalDeviceMemoryProperties memoryProperties;
    uint32_t maxMemoryAllocationCount = 0;
};

//One VMA suballocation, owned. Declare it BEFORE the vk::raii::Buffer or Image it belongs
//to: members die in reverse declaration order, so the handle gets destroyed first and the
//memory is freed after it, never the other way around
class MemoryAllocation{
    public:
    MemoryAllocation() = default;
    MemoryAllocation(VmaAllocator allocator, VmaAllocation allocation) : allocator(allocator), allocation(allocation){}
    ~MemoryAllocation();

    MemoryAllocation(const MemoryAllocation&) = delete;
    MemoryAllocation& operator=(const MemoryAllocation&) = delete;
    MemoryAllocation(MemoryAllocation&& other) noexcept;
    MemoryAllocation& operator=(MemoryAllocation&& other) noexcept;

    void reset();

    //getters
    VmaAllocation get() const {return allocation;}
    VmaAllocator getAllocator() const {return allocator;}
    explicit operator bool() const {return allocation != nullptr;}

    private:
    VmaAllocator allocator = nullptr;
    VmaAllocation allocation = nullptr;
};
