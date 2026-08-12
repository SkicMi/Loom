#pragma once
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"

struct CommandConfig{
    uint32_t framesInFlight = 2; //default to 2 frames in flight, can be changed by user
};


class VulkanCommand{
    public:
    VulkanCommand(const VulkanDevice& device, const CommandConfig& config = {});

    //getters
    const vk::raii::CommandPool& getCommandPool() const {return commandPool;}
    const std::vector<vk::raii::CommandBuffer>& getCommandBuffers() const {return commandBuffers;}
    void copyBuffer(const vk::raii::Buffer& src,
                    const vk::raii::Buffer& dst,
                    vk::DeviceSize size) const;


    private:
    const VulkanDevice& device;
    CommandConfig config;

    vk::raii::CommandPool commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    vk::raii::CommandPool transferPool = nullptr;


    void createCommandPool();
    void allocateCommandBuffers();
    void createTransferPool();


};