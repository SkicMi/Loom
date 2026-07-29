#pragma once
#include <vulkan/vulkan_raii.hpp>
#include "VulkanInstance.h"

 class VulkanDevice{
    public:
    VulkanDevice(const VulkanInstance& instance);




    //getters
    const vk::raii::PhysicalDevice& getPhysicalDevice() const {return physicalDevice;}
    const vk::raii::Device& getDevice() const {return device;}

    private:
    const VulkanInstance& instance;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;


 };