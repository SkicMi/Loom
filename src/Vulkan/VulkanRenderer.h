#pragma once
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanCommand.h"
#include "VulkanGraphicsPipeline.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include "RenderTarget.h"
#include "Core/Camera.h"
#include "Material.h"
#include "Mesh.h"
#include "Core/Light.h"
#include "Core/FrameData.h"
#include "Core/Environment.h"
#include <array>
#include <cstdint>

struct RendererConfig{
    std::array<float,4> clearColor = {0.0f,0.0f,0.0f,1.0f}; //clear color settings for renderer, default to black
    float clearDepth = 1.0f; //Maximum 1, with eLess compare and range 0,1 every first fragement passes, if it was 0.0, nothing would be drawn with zero errors in log
    uint32_t maxLights = 16; //storage buffer capacity, not a shader limit!
    uint32_t maxPassesPerFrame = 8; //how many FrameData blocks fir in one frame's buffer

};


class VulkanRenderer{

    public:
    VulkanRenderer(
    const VulkanDevice& device,
    VulkanSwapchain& swapchain,
    const VulkanCommand& command,
    const VulkanGraphicsPipeline& graphicsPipeline,
    VulkanImage* depthImage,
    const vk::raii::DescriptorPool& descriptorPool,
    const RendererConfig& rendererConfig = {}
    );

    VulkanImage* depthImage = nullptr;


    
    bool beginFrame();
    void beginPass(); //swapchain
    void beginPass(const RenderTarget& target); //offscreen target
    void endPass();
    void draw(const Mesh& mesh, const glm::mat4& model = glm::mat4(1.0f));
    void draw(const Mesh& mesh, const glm::mat4& model, const Material& material); //overload fuction for model with material
    void endFrame();

    void setCamera(const Camera& cam) { camera = &cam;}
    void setEnvironment(const Environment& newEnvironment) { environment = &newEnvironment;}
    void addLight(const Light& newLight) {lights.push_back(&newLight);}
    void clearLights() {lights.clear();}


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
    const Environment* environment = nullptr;
    std::vector<const Light*> lights;
    std::vector<VulkanBuffer> lightBuffers;
    std::vector<GpuLight> lightStaging;
    uint32_t currentImageIndex = 0;
    bool needsRecreate = false;
    bool frameActive = false;
    bool passActive = false;
    const RenderTarget* currentTarget = nullptr;
    const VulkanGraphicsPipeline* boundPipeline = nullptr;
    const vk::raii::DescriptorPool& descriptorPool;
    std::vector<VulkanBuffer> frameBuffers;
    vk::DeviceSize frameDataStride = 0;
    uint32_t passIndex = 0;
    std::vector<vk::raii::DescriptorSet> frameSets;
    FrameData frameData;


    void createSyncObjects();
    void createFrameResources();
    void startPass(vk::Image colorImage, vk::ImageView colorView, const VulkanImage* depth, vk::Extent2D extent);
    void recreateSwapchain();

};