#pragma once
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanCommand.h"
#include "VulkanGraphicsPipeline.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include "Core/Camera.h"
#include "Mesh.h"
#include <array>
#include <cstdint>

struct RendererConfig{
    std::array<float,4> clearColor = {0.0f,0.0f,0.0f,1.0f}; //clear color settings for renderer, default to black
    float clearDepth = 1.0f; //Maximum 1, with eLess compare and range 0,1 every first fragement passes, if it was 0.0, nothing would be drawn with zero errors in log


};


class VulkanRenderer{

    public:
    VulkanRenderer(
    const VulkanDevice& device,
    VulkanSwapchain& swapchain,
    const VulkanCommand& command,
    const VulkanGraphicsPipeline& graphicsPipeline,
    VulkanImage* depthImage,
    const RendererConfig& rendererConfig = {}
    );

    VulkanImage* depthImage = nullptr;


    
    bool beginFrame();
    void draw(const Mesh& mesh, const glm::mat4& model = glm::mat4(1.0f));
    void endFrame();

    void setCamera(const Camera& cam) { camera = &cam;}


    private:
    const VulkanDevice& device;
    VulkanSwapchain& swapchain;
    const VulkanCommand& command;
    const VulkanGraphicsPipeline& graphicsPipeline;
    RendererConfig rendererConfig;

    std::vector<vk::raii::Semaphore> imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    size_t currentFrame = 0;
    const Camera* camera = nullptr;
    uint32_t currentImageIndex = 0;
    bool needsRecreate = false;
    bool frameActive = false;


    void createSyncObjects();
    void beginRecording(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);
    void endRecording(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);
    void recreateSwapchain();

};