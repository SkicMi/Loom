#pragma once
#include <vulkan/vulkan_raii.hpp>
#include "Window.h"
#include <cstring>
#include <iostream>


class VulkanInstance{

    public:
    VulkanInstance(const std::string appName, const std::string engineName, const Window& window);
 

    //getters
    const vk::raii::Context& getContext() const {return context;}
    const vk::raii::Instance& getInstance() const {return instance;}
    const vk::raii::SurfaceKHR& getSurface() const {return surface;}
    const vk::raii::DebugUtilsMessengerEXT& getMessenger() const {return debugMessenger;}
    
    private:
    const std::string appName;
    const std::string engineName;
    const Window& window;
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;


    
    

    std::vector<const char*> getExtensions();


    static inline const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"};
   

    //Static debug callback function 
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

 bool checkValidationLayerSupport();
    vk::DebugUtilsMessengerCreateInfoEXT populateDebugMessengerCreateInfo();

};