#pragma once
#include <vulkan/vulkan_raii.hpp>
#include "VulkanInstance.h"
#include "VulkanAllocator.h"
#include <optional>
#include <string>
#include <set>

enum class DevicePreference{
    Discrete,
    Integrated,
    Any
};

struct DeviceConfig{
    DevicePreference preference = DevicePreference::Discrete;
};

//Queue Family indices struct used for creation of Device Queue Info
struct QueueFamilyIndices{
    std::optional<uint32_t> graphicsFamilies;
    std::optional<uint32_t> presentFamilies;

    //A present queue is only required when there is a surface to present to. Headless needs
    //graphics and nothing else, and demanding a present family there would reject every
    //device on a machine with no display
    bool isComplete(bool needsPresent = true) const {
        return graphicsFamilies.has_value() && (!needsPresent || presentFamilies.has_value());
    }
};

 class VulkanDevice{
    public:
    VulkanDevice(const VulkanInstance& instance, const DeviceConfig& config);

    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const;
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const ;






    //getters
    const vk::raii::PhysicalDevice& getPhysicalDevice() const {return physicalDevice;}
    const vk::raii::Device& getDevice() const {return device;}
    const vk::raii::Queue& getGraphicsQueue() const {return graphicsQueue;}
    const vk::raii::Queue& getPresentQueue() const {return presentQueue;}
    const VulkanAllocator& getAllocator() const {return allocator;}
    bool isHeadless() const {return !needsPresent;}
    const QueueFamilyIndices& getQueueIndices() const {return queueIndices;}
    std::string getDeviceName() const {return std::string(physicalDevice.getProperties().deviceName.data());}
    vk::PhysicalDeviceType getDeviceType() const {return physicalDevice.getProperties().deviceType;}

    private:
    const VulkanInstance& instance;
    DeviceConfig config;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    vk::raii::Queue graphicsQueue = nullptr;
    vk::raii::Queue presentQueue = nullptr;

    //Declared after the device on purpose: it has to be torn down before the device it
    //allocates from, and members die in reverse declaration order
    VulkanAllocator allocator;

    //The required list plus whichever optional ones this card turned out to have
    std::vector<const char*> enabledExtensions;
    bool hasMemoryBudget = false;
    bool hasMemoryPriority = false;

    //Asked once at construction from the instance, because every suitability check and the
    //extension list both need the same answer
    bool needsPresent = true;

    QueueFamilyIndices queueIndices;


    //helpers
    bool isDeviceSuitable(const vk::raii::PhysicalDevice& candidate);
    bool checkDeviceExtensionSupport(const vk::raii::PhysicalDevice& candidate);



    void pickPhysicalDevice(const std::vector<vk::raii::PhysicalDevice>& candidates);
    QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice& candidate);

    //Only required when there is a surface. A headless device that asked for it would fail
    //on any driver that does not expose it without a display
    static inline const std::vector<const char*> surfaceDeviceExtensions = {
        "VK_KHR_swapchain"};

    //Nice to have, never required. Neither changes what Loom can draw - they only let the
    //allocator ask the driver how much memory is really left, and say which resources should
    //be the last ones evicted when it runs out. A card without them still runs everything
    static inline const std::vector<const char*> optionalDeviceExtensions = {
        "VK_EXT_memory_budget",
        "VK_EXT_memory_priority"};

    static bool supportsExtension(const vk::raii::PhysicalDevice& candidate, const char* name);
    void createAllocator();



    
    

   void createLogicalDevice();


 };