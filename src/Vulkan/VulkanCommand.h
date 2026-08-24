#pragma once
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanImage.h"
#include <functional>

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

    void transitionImageLayout(vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor, uint32_t layerCount = 1) const;
    void transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor, uint32_t layerCount = 1) const;

    //Same move, but the image is told where it ended up, so the next user does not guess
    void transitionImageLayout(const VulkanImage& image, vk::ImageLayout newLayout) const;
    void copyBufferToImage(const vk::raii::Buffer& src, const vk::raii::Image& dst, vk::Extent2D extent, vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor) const;
    void copyImageToBuffer(vk::Image src, const vk::raii::Buffer& dst, vk::Extent2D extent, vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor, uint32_t layer = 0) const;
    void copyImageToBuffer(const vk::raii::Image& src, const vk::raii::Buffer& dst, vk::Extent2D extent, vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor, uint32_t layer = 0) const;


    private:
    const VulkanDevice& device;
    CommandConfig config;

    vk::raii::CommandPool commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    vk::raii::CommandPool transferPool = nullptr;


    void createCommandPool();
    void allocateCommandBuffers();
    void createTransferPool();
    void oneTimeSubmit(const std::function<void(const vk::raii::CommandBuffer&)>& record) const;


};