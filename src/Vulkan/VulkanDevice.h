#pragma once
#include <vulkan/vulkan_raii.hpp>
#include "VulkanInstance.h"
#include <optional>
#include <set>


//Queue Family indices struct used for creation of Device Queue Info
struct QueueFamilyIndices{
    std::optional<uint32_t> graphicsFamilies;
    std::optional<uint32_t> presentFamilies;
    bool isComplete() const { return graphicsFamilies.has_value() && presentFamilies.has_value();}
};

 class VulkanDevice{
    public:
    VulkanDevice(const VulkanInstance& instance);

    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const;
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const ;






    //getters
    const vk::raii::PhysicalDevice& getPhysicalDevice() const {return physicalDevice;}
    const vk::raii::Device& getDevice() const {return device;}
    const vk::raii::Queue& getGraphicsQueue() const {return graphicsQueue;}
    const vk::raii::Queue& getPresentQueue() const {return presentQueue;}
    const QueueFamilyIndices& getQueueIndices() const {return queueIndices;}

    private:
    const VulkanInstance& instance;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    vk::raii::Queue graphicsQueue = nullptr;
    vk::raii::Queue presentQueue = nullptr;

    QueueFamilyIndices queueIndices;


    //helpers
    bool isDeviceSuitable(const vk::raii::PhysicalDevice& candidate);
    bool checkDeviceExtensionSupport(const vk::raii::PhysicalDevice& candidate);



    void pickPhysicalDevice(const std::vector<vk::raii::PhysicalDevice>& candidates);
    QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice& candidate);

    static inline const std::vector<const char*> deviceExtensions = {
        "VK_KHR_swapchain"};



    
    

   void createLogicalDevice();


 };