#pragma once
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanCommand.h"
#include <array>
#include <cstdint>

struct RendererConfig{
    std::array<float,4> clearColor = {0.0f,0.0f,0.0f,1.0f}; //clear color settings for renderer, default to black

};


class VulkanRenderer{

    public:
    VulkanRenderer(
    const VulkanDevice& device,
    VulkanSwapchain& swapchain,
    const VulkanCommand& command,
    const RendererConfig& rendererConfig = {}
    );


    void drawFrame();

    //getters


    private:
    const VulkanDevice& device;
    VulkanSwapchain& swapchain;
    const VulkanCommand& command;
    RendererConfig rendererConfig;

    std::vector<vk::raii::Semaphore> imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    size_t currentFrame = 0;

    void createSyncObjects();
    void recordCommandBuffer(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);
    void recreateSwapchain();

};