#pragma once

#include <vulkan/vulkan_raii.hpp>
#include "VulkanInstance.h"
#include "Window.h"
#include "VulkanDevice.h"
#include <limits>
#include <algorithm>

struct SwapchainConfig {
    //Mailbox is preffered but not guaranteed on each device, so we will fallback to FiFo
    //eFifo is guaranteed to be on each GPU cause it is demanded in specs of each device
    vk::PresentModeKHR preferredPresentMode = vk::PresentModeKHR::eMailbox;
    
    vk::SurfaceFormatKHR preferredSurfaceFormat = {
        vk::Format::eB8G8R8A8Srgb,
        vk::ColorSpaceKHR::eSrgbNonlinear
    };
    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    //Lets the window be read back. Added only if the surface supports it, and it is what
    //makes a window-level regression test possible at all
    bool allowReadback = true;
    uint32_t preferredImageCount = 0; //0 = use minImageCount + 1 heuristics
};


class VulkanSwapchain{
    public:
    VulkanSwapchain(const VulkanInstance& instance,
         const Window& window, 
         const VulkanDevice& device,
         const SwapchainConfig& config = {});

        

    //getters
    const vk::raii::SwapchainKHR& getSwapchain() const {return swapchain;}
    const std::vector<vk::raii::ImageView>& getImageViews() const {return imageViews;}
    const vk::SurfaceFormatKHR& getSurfaceFormat() const {return surfaceFormat;}
    const vk::Extent2D& getExtent() const {return extent;}
    const std::vector<vk::Image>& getImages() const {return images;}
    bool canReadback() const {return readbackAvailable;}


    void recreateSwapchain();




    private:
    bool readbackAvailable = false;

    const VulkanInstance& instance;
    const Window& window;
    const VulkanDevice& device;
    SwapchainConfig config;
    vk::raii::SwapchainKHR swapchain = nullptr;

    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
    vk::SurfaceFormatKHR surfaceFormat;
    vk::PresentModeKHR presentMode;
    vk::Extent2D extent;
    uint32_t imageCount = 0;
    std::vector<vk::Image>images;
    std::vector<vk::raii::ImageView> imageViews;
    


    void queryCapabilities();
    vk::SurfaceFormatKHR chooseSurfaceFormat();
    vk::PresentModeKHR choosePresentMode();
    vk::Extent2D chooseExtent();
    uint32_t chooseImageCount();
    void createSwapchain();
    void createImageViews();
    void build();
};
